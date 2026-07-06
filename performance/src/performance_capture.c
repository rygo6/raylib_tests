/*******************************************************************************************
*
*   raylib [tests] - performance capture orchestrator
*
*   Runs a curated set of raylib example executables (built against a raylib compiled with
*   -DPERFORMANCE_CAPTURE) at full speed, RUNS times each, for RAYLIB_PERF_DURATION_MS ms per
*   run. Each child self-measures frame time + CPU/RAM/VRAM and writes
*   <outputDir>/<example>/run_<n>.rini; this orchestrator just sets the environment, launches
*   each run with a kill-timeout guard, and records the render environment.
*
*   One config file per backend (performance_rlgl.ini / _rlsw.ini / _rlvk.ini) selects the
*   output directory and the example list, so each backend's captures land in their own tree.
*
*   USAGE:
*       performance_capture [outputDir] [configFile]
*
*       outputDir    per-run output root, overrides config    (config key: capture_output)
*       configFile   rini config file                         (default: performance_rlgl.ini)
*
*   Config keys (rini "key value"): examples_dir, examples (comma-separated cat/name list),
*   duration_ms, warmup_ms, runs, timeout_ms, capture_output.
*
*   Licensed under zlib/libpng (same as raylib).
*
********************************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>

#define RINI_MAX_LINE_SIZE 4096   // the curated example list exceeds rini's 512 default
#define RINI_IMPLEMENTATION
#include "rini.h"
#include "perf_label.h"    // PerfComputeLabel(): "<os>_<vendor>" slug for output naming

#if defined(_WIN32)
    #define WIN32_LEAN_AND_MEAN
    #define COBJMACROS
    #include <windows.h>
    #include <tlhelp32.h>
    #include <direct.h>         // _getcwd
    #include <dxgi1_4.h>        // GPU name + total VRAM (header-only; dynamic-loaded factory)

    static const GUID PC_IID_IDXGIFactory1 = { 0x770aae78, 0xf26f, 0x4dba, { 0xa8, 0x29, 0x25, 0x3c, 0x83, 0xd1, 0xb3, 0x87 } };
    typedef HRESULT (WINAPI *PFN_PC_CreateDXGIFactory1)(REFIID, void **);
#endif

#define MAX_PATH_LEN    2048
#define MAX_EXAMPLES    256

#if defined(_WIN32)
    #define EXE_SUFFIX  ".exe"
#else
    #define EXE_SUFFIX  ""
#endif

//----------------------------------------------------------------------------------
// Small filesystem / process helpers (no raylib dependency)
//----------------------------------------------------------------------------------
static bool PathExists(const char *p)
{
#if defined(_WIN32)
    return (GetFileAttributesA(p) != INVALID_FILE_ATTRIBUTES);
#else
    FILE *f = fopen(p, "rb"); if (f) { fclose(f); return true; } return false;
#endif
}

static void MakeDir(const char *p)
{
#if defined(_WIN32)
    CreateDirectoryA(p, NULL);
#else
    char cmd[MAX_PATH_LEN]; snprintf(cmd, sizeof(cmd), "mkdir -p '%s'", p); system(cmd);
#endif
}

static void GetCwd(char *out, int size)
{
#if defined(_WIN32)
    if (!_getcwd(out, size)) snprintf(out, size, ".");
#else
    if (!getcwd(out, size)) snprintf(out, size, ".");
#endif
}

static bool IsAbsPath(const char *p)
{
    if ((p == NULL) || (p[0] == '\0')) return false;
    if ((p[0] == '/') || (p[0] == '\\')) return true;
    if (isalpha((unsigned char)p[0]) && (p[1] == ':')) return true;
    return false;
}

static void ResolvePath(const char *rel, char *out, int outSize)
{
    if (IsAbsPath(rel)) snprintf(out, outSize, "%s", rel);
    else { char cwd[MAX_PATH_LEN]; GetCwd(cwd, sizeof(cwd)); snprintf(out, outSize, "%s/%s", cwd, rel); }
}

static void SetEnvVar(const char *name, const char *value)
{
#if defined(_WIN32)
    SetEnvironmentVariableA(name, value);
#else
    setenv(name, value, 1);
#endif
}

#if defined(_WIN32)
// One Job object shared by every launched test, created with KILL_ON_JOB_CLOSE: if this
// harness dies for ANY reason (crash, task kill, console close), the OS terminates every
// test process with it. Tests can never outlive the harness and accumulate.
static HANDLE GetTestJob(void)
{
    static HANDLE job = NULL;
    if (job == NULL)
    {
        job = CreateJobObjectA(NULL, NULL);
        if (job != NULL)
        {
            JOBOBJECT_EXTENDED_LIMIT_INFORMATION lim; ZeroMemory(&lim, sizeof(lim));
            lim.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
            SetInformationJobObject(job, JobObjectExtendedLimitInformation, &lim, sizeof(lim));
        }
    }
    return job;
}

// Kill every running process with this image name and wait until each is gone. Returns how
// many were found. Used before every launch: a prior test that failed to close must never
// overlap the next one (overlapping tests contend for GPU/CPU and corrupt all measurements).
static int KillLingering(const char *exeName)
{
    int found = 0;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;
    PROCESSENTRY32 pe; pe.dwSize = sizeof(pe);
    if (Process32First(snap, &pe))
    {
        do
        {
            if (_stricmp(pe.szExeFile, exeName) == 0)
            {
                HANDLE h = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, pe.th32ProcessID);
                if (h != NULL)
                {
                    TerminateProcess(h, 1);
                    WaitForSingleObject(h, 5000);
                    CloseHandle(h);
                    found++;
                }
            }
        } while (Process32Next(snap, &pe));
    }
    CloseHandle(snap);
    return found;
}
#endif

// Spawn absExe from workDir, killing it after timeoutMs. 0 = self-exited, 1 = timed out, -1 = spawn failed.
// The child joins the kill-on-close Job, any lingering instance of the same test is killed
// first, and termination is VERIFIED before returning - the next test never starts while a
// prior one is still alive.
static int RunWithTimeout(const char *absExe, const char *workDir, unsigned int timeoutMs)
{
#if defined(_WIN32)
    char exeWin[MAX_PATH_LEN], dirWin[MAX_PATH_LEN], cmd[MAX_PATH_LEN + 4];
    int i = 0;
    for (; (absExe[i] != '\0') && (i < MAX_PATH_LEN - 1); i++) exeWin[i] = (absExe[i] == '/') ? '\\' : absExe[i];
    exeWin[i] = '\0';
    for (i = 0; (workDir[i] != '\0') && (i < MAX_PATH_LEN - 1); i++) dirWin[i] = (workDir[i] == '/') ? '\\' : workDir[i];
    dirWin[i] = '\0';
    snprintf(cmd, sizeof(cmd), "\"%s\"", exeWin);

    // Guard: no stale instance of this test may be running from a previous (crashed) session
    const char *base = strrchr(exeWin, '\\'); base = base ? base + 1 : exeWin;
    int stale = KillLingering(base);
    if (stale > 0) printf("    [guard] killed %d lingering instance(s) of %s before launch\n", stale, base);

    STARTUPINFOA si; ZeroMemory(&si, sizeof(si)); si.cb = sizeof(si);
    PROCESS_INFORMATION pi; ZeroMemory(&pi, sizeof(pi));
    if (!CreateProcessA(NULL, cmd, NULL, NULL, FALSE, CREATE_SUSPENDED, NULL, dirWin, &si, &pi)) return -1;
    if (GetTestJob() != NULL) AssignProcessToJobObject(GetTestJob(), pi.hProcess);
    ResumeThread(pi.hThread);

    int rc = 0;
    if (WaitForSingleObject(pi.hProcess, timeoutMs) == WAIT_TIMEOUT)
    {
        TerminateProcess(pi.hProcess, 1);
        rc = 1;
    }
    // VERIFY the test is gone before the caller may start the next one
    if (WaitForSingleObject(pi.hProcess, 10000) == WAIT_TIMEOUT)
    {
        printf("    [guard] WARNING: %s did not die within 10 s of termination\n", base);
        rc = 1;
    }
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return rc;
#else
    (void)workDir; (void)timeoutMs;
    char cmd[MAX_PATH_LEN]; snprintf(cmd, sizeof(cmd), "cd '%s' && '%s'", workDir, absExe);
    return (system(cmd) == 0) ? 0 : -1;
#endif
}

// Record render environment (machine, OS, GPU, VRAM total) + capture settings
static void WriteEnvironment(const char *outDir, const char *backend, const char *label, int durationMs, int warmupMs, int runs)
{
    char gpu[512] = "unknown";
    double vramTotalMB = 0.0;

#if defined(_WIN32)
    char machine[256] = "unknown"; DWORD sz = sizeof(machine); GetComputerNameA(machine, &sz);
    const char *os = "Windows";

    HMODULE hDxgi = LoadLibraryA("dxgi.dll");
    if (hDxgi != NULL)
    {
        PFN_PC_CreateDXGIFactory1 pCreate = (PFN_PC_CreateDXGIFactory1)(void *)GetProcAddress(hDxgi, "CreateDXGIFactory1");
        if (pCreate != NULL)
        {
            IDXGIFactory1 *factory = NULL;
            if (SUCCEEDED(pCreate(&PC_IID_IDXGIFactory1, (void **)&factory)) && factory)
            {
                SIZE_T bestVram = 0;
                for (UINT i = 0; ; i++)
                {
                    IDXGIAdapter1 *adapter = NULL;
                    if (IDXGIFactory1_EnumAdapters1(factory, i, &adapter) != S_OK) break;
                    DXGI_ADAPTER_DESC1 desc; memset(&desc, 0, sizeof(desc));
                    IDXGIAdapter1_GetDesc1(adapter, &desc);
                    if (!(desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) && (desc.DedicatedVideoMemory >= bestVram))
                    {
                        bestVram = desc.DedicatedVideoMemory;
                        WideCharToMultiByte(CP_UTF8, 0, desc.Description, -1, gpu, sizeof(gpu), NULL, NULL);
                        vramTotalMB = (double)desc.DedicatedVideoMemory/(1024.0*1024.0);
                    }
                    IDXGIAdapter1_Release(adapter);
                }
                IDXGIFactory1_Release(factory);
            }
        }
    }
#else
    char machine[256] = "unknown"; const char *os = "unknown";
#endif

    rini_data md = rini_load_from_memory("");
    rini_set_value_text(&md, "backend", backend, "graphics backend under test");
    rini_set_value_text(&md, "label", label, "platform x vendor label (os_vendor)");
    rini_set_value_text(&md, "machine", machine, "host machine name");
    char osVersion[128]; PerfDetectOSVersion(osVersion, sizeof(osVersion));
    char gpuDriver[128]; PerfDetectGpuDriver(gpuDriver, sizeof(gpuDriver));
    rini_set_value_text(&md, "os", os, "operating system");
    rini_set_value_text(&md, "os_version", osVersion, "operating system version (RtlGetVersion)");
    rini_set_value_text(&md, "gpu_driver", gpuDriver, "GPU user-mode driver version (DXGI CheckInterfaceSupport)");
    rini_set_value_text(&md, "gpu", gpu, "graphics card (DXGI adapter description)");
    rini_set_value(&md, "gpu_vram_total_mb", (int)(vramTotalMB + 0.5), "dedicated GPU memory, MB");
    rini_set_value(&md, "duration_ms", durationMs, "per-run measurement window, ms");
    rini_set_value(&md, "warmup_ms", warmupMs, "per-run warm-up excluded from stats, ms");
    rini_set_value(&md, "runs", runs, "runs per example");

    char path[MAX_PATH_LEN]; snprintf(path, sizeof(path), "%s/environment.rini", outDir);
    rini_save(md, path);
    rini_unload(&md);

    printf("Environment: %s | %s | %s (%.0f MB VRAM)\n\n", backend, os, gpu, vramTotalMB);
}

//----------------------------------------------------------------------------------
// Program main entry point
//----------------------------------------------------------------------------------
int main(int argc, char **argv)
{
    const char *configFile = (argc > 2) ? argv[2] : "performance_rlgl.ini";

    char examplesDir[MAX_PATH_LEN], outDir[MAX_PATH_LEN], backend[128], examplesRaw[8192], label[128];
    int durationMs, warmupMs, runs, timeoutMs;
    {
        bool haveCfg = PathExists(configFile);
        rini_data cfg = { 0 };
        if (haveCfg) cfg = rini_load(configFile);
        snprintf(examplesDir, sizeof(examplesDir), "%s", rini_get_value_text_fallback(cfg, "examples_dir", "../../raylib/examples"));
        snprintf(examplesRaw, sizeof(examplesRaw), "%s", rini_get_value_text_fallback(cfg, "examples", ""));
        snprintf(backend, sizeof(backend), "%s", rini_get_value_text_fallback(cfg, "backend", "rlgl"));
        durationMs = rini_get_value_fallback(cfg, "duration_ms", 10000);
        warmupMs   = rini_get_value_fallback(cfg, "warmup_ms", 500);
        runs       = rini_get_value_fallback(cfg, "runs", 3);
        timeoutMs  = rini_get_value_fallback(cfg, "timeout_ms", 30000);
        const char *cfgOut = rini_get_value_text_fallback(cfg, "capture_output", "rlgl");
        // Platform x vendor label (e.g. windows_nvidia); suffixes the output dir so results from
        // different machines coexist. Override with RAYLIB_PERF_LABEL or the ini 'label' key.
        PerfComputeLabel(rini_get_value_text_fallback(cfg, "label", ""), label, sizeof(label));
        snprintf(outDir, sizeof(outDir), "%s_%s", (argc > 1) ? argv[1] : cfgOut, label);
        if (haveCfg) rini_unload(&cfg);
    }

    if (runs < 1) runs = 1;

    char absExamples[MAX_PATH_LEN], absOut[MAX_PATH_LEN];
    ResolvePath(examplesDir, absExamples, sizeof(absExamples));
    ResolvePath(outDir, absOut, sizeof(absOut));

    if (!PathExists(absExamples)) { printf("ERROR: examples directory not found: %s\n", absExamples); return 1; }
    MakeDir(absOut);

    // Parse the comma-separated example list ("cat/name, cat/name, ...")
    char *examples[MAX_EXAMPLES]; int exampleCount = 0;
    {
        char *tok = strtok(examplesRaw, ",");
        while ((tok != NULL) && (exampleCount < MAX_EXAMPLES))
        {
            while ((*tok == ' ') || (*tok == '\t')) tok++;              // ltrim
            int n = (int)strlen(tok); while ((n > 0) && ((tok[n-1] == ' ') || (tok[n-1] == '\t') || (tok[n-1] == '\r'))) tok[--n] = '\0'; // rtrim
            if (n > 0) examples[exampleCount++] = tok;
            tok = strtok(NULL, ",");
        }
    }

    printf("Performance capture\n  backend:  %s\n  examples: %s (%d)\n  output:   %s\n  runs:     %d x %d ms (+%d ms warmup)\n  timeout:  %d ms\n\n",
           backend, absExamples, exampleCount, absOut, runs, durationMs, warmupMs, timeoutMs);

    WriteEnvironment(absOut, backend, label, durationMs, warmupMs, runs);

    char durStr[32], wupStr[32]; snprintf(durStr, sizeof(durStr), "%d", durationMs); snprintf(wupStr, sizeof(wupStr), "%d", warmupMs);
    SetEnvVar("RAYLIB_PERF_DURATION_MS", durStr);
    SetEnvVar("RAYLIB_PERF_WARMUP_MS", wupStr);

    int ran = 0, ok = 0, fail = 0;

    for (int e = 0; e < exampleCount; e++)
    {
        const char *catName = examples[e];                              // "models/models_loading"
        const char *slash = strrchr(catName, '/');
        const char *name = (slash != NULL) ? (slash + 1) : catName;

        char category[MAX_PATH_LEN]; snprintf(category, sizeof(category), "%s", catName);
        char *cs = strrchr(category, '/'); if (cs) *cs = '\0'; else category[0] = '\0';

        char workDir[MAX_PATH_LEN], exePath[MAX_PATH_LEN];
        if (category[0] != '\0') snprintf(workDir, sizeof(workDir), "%s/%s", absExamples, category);
        else                     snprintf(workDir, sizeof(workDir), "%s", absExamples);
        snprintf(exePath, sizeof(exePath), "%s/%s%s", workDir, name, EXE_SUFFIX);

        printf("  %-42s", name); fflush(stdout);

        if (!PathExists(exePath)) { printf(" ...NOT BUILT (%s)\n", exePath); fail++; continue; }

        char outEx[MAX_PATH_LEN]; snprintf(outEx, sizeof(outEx), "%s/%s", absOut, name);
        MakeDir(outEx);
        SetEnvVar("RAYLIB_PERF_DIR", outEx);

        int gotRuns = 0;
        for (int r = 1; r <= runs; r++)
        {
            char runStr[16]; snprintf(runStr, sizeof(runStr), "%d", r);
            SetEnvVar("RAYLIB_PERF_RUN", runStr);

            int rc = RunWithTimeout(exePath, workDir, (unsigned int)timeoutMs);

            char runFile[MAX_PATH_LEN]; snprintf(runFile, sizeof(runFile), "%s/run_%d.rini", outEx, r);
            if (PathExists(runFile)) { gotRuns++; printf(" r%d", r); }
            else                     { printf(" r%d%s", r, (rc == 1) ? "!TO" : "!X"); }
            fflush(stdout);
        }

        ran++;
        if (gotRuns == runs) { ok++;  printf(" ...ok\n"); }
        else                 { fail++; printf(" ...INCOMPLETE (%d/%d)\n", gotRuns, runs); }
        fflush(stdout);
    }

    printf("\nCAPTURE_SUMMARY: backend=%s examples=%d ok=%d fail=%d\n", backend, ran, ok, fail);
    return (fail > 0) ? 1 : 0;
}
