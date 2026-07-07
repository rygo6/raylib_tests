/*******************************************************************************************
*
*   raylib [tests] - image comparison capture
*
*   Deterministic frame capture orchestrator. Runs raylib example executables built with
*   -DDETERMINISTIC_IMAGE_COMPARISON_CAPTURE and collects their per-cycle screenshots into
*   <outputDir>/<example>/frame_00NN.png, one directory per example.
*
*   Before capturing it records the render environment (raylib version, machine, OS, graphics
*   card and driver) into <outputDir>/environment.rini, so a baseline set always carries the
*   provenance of the hardware/driver that produced it.
*
*   The examples must already be built (with the deterministic flag) next to their source .c
*   files. This tool discovers them, runs each one from its own directory (so relative resource
*   loading works) with RAYLIB_SHOT_DIR / RAYLIB_SHOT_FRAMES set in the environment, guarded by
*   a per-process timeout so an example that never self-exits cannot stall the run.
*
*   Settings are read from a rini config file; every path is relative to the working directory
*   and resolved at run time, so nothing absolute is hardcoded.
*
*   USAGE:
*       image_comparison_capture [outputDir] [configFile]
*
*       outputDir    frames output root, overrides config       (config key: capture_output)
*       configFile   rini config file                           (default: image_comparison_rlvk.ini)
*
*   Config keys (rini, "key value" pairs, '#' comments): examples_dir, frames, timeout_ms,
*   include (comma-separated example names; when present, ONLY those examples are run —
*   the regression-subset mechanism, everything else is skipped even if built)
*
*   This tool is licensed under an unmodified zlib/libpng license, which is an OSI-certified,
*   BSD-like license that allows static linking with closed source software
*
*   Copyright (c) 2025-2026 Ramon Santamaria (@raysan5)
*
********************************************************************************************/

#include "raylib.h"

#include <stdio.h>              // Required for: printf(), snprintf()
#include <stdlib.h>            // Required for: atoi()
#include <string.h>           // Required for: strlen()
#include <ctype.h>          // Required for: isalpha()

#define RINI_IMPLEMENTATION
#include "rini.h"       // raysan5's ini-style config reader ("key value" pairs, '#' comments)

#if defined(_WIN32)
    #define WIN32_LEAN_AND_MEAN
    #define NOGDI               // Avoid raylib/wingdi.h symbol clashes (Rectangle, ...)
    #define NOUSER              // Avoid raylib/winuser.h symbol clashes (CloseWindow, LoadImage, ...)
    #include <windows.h>        // Required for: CreateProcess(), WaitForSingleObject(), GetComputerName()
#else
    #include <unistd.h>         // Required for: fork(), execl(), chdir(), gethostname()
    #include <sys/wait.h>       // Required for: waitpid()
    #include <signal.h>         // Required for: kill()
    #include <time.h>           // Required for: nanosleep()
#endif

// Minimal OpenGL entry point to query GPU/driver strings (needs an active GL context).
// Declared directly so no GL headers are required; resolved from opengl32 / libGL at link time.
#if defined(_WIN32)
    __declspec(dllimport) const unsigned char *__stdcall glGetString(unsigned int name);
#else
    extern const unsigned char *glGetString(unsigned int name);
#endif
#define GL_VENDOR       0x1F00
#define GL_RENDERER     0x1F01
#define GL_VERSION      0x1F02

//----------------------------------------------------------------------------------
// Defines and Macros
//----------------------------------------------------------------------------------
#define MAX_PATH_LEN    1200

#if defined(_WIN32)
    #define EXE_SUFFIX  ".exe"
#else
    #define EXE_SUFFIX  ""
#endif

//----------------------------------------------------------------------------------
// Global Variables Definition
//----------------------------------------------------------------------------------
#define MAX_INCLUDES    256

static char includes[MAX_INCLUDES][192];    // Subset filter: when non-empty, only these examples are run
static int includeCount = 0;

//----------------------------------------------------------------------------------
// Module Functions Declaration
//----------------------------------------------------------------------------------
static void ParseNameList(const char *csv);                                 // Parse a comma-separated name list into includes[]
static bool IsIncluded(const char *example);                                // Check if an example passes the subset filter
static bool IsAbsPath(const char *p);                                       // Check if a path is absolute
static void ResolvePath(const char *rel, char *out, int outSize);           // Resolve a path against the working dir
static void SetEnvVar(const char *name, const char *value);                 // Set an environment variable (cross-platform)
static void WriteEnvironment(const char *outDir);                           // Record render environment to environment.rini
static int RunWithTimeout(const char *absExe, const char *workDir, unsigned int timeoutMs);   // Spawn a process with a kill timeout

//----------------------------------------------------------------------------------
// Program main entry point
//----------------------------------------------------------------------------------
int main(int argc, char **argv)
{
    // Initialization
    //------------------------------------------------------------------------------------
    // CLI: image_comparison_capture [outputDir] [configFile]. Settings come from the rini
    // config; the output dir (which changes per run, e.g. baseline vs rlgl) may be given as argv[1].
    const char *configFile = (argc > 2) ? argv[2] : "image_comparison_rlvk.ini";

    char examplesDir[MAX_PATH_LEN], outDir[MAX_PATH_LEN], frames[128];
    unsigned int timeoutMs;
    {
        bool haveCfg = FileExists(configFile);
        rini_data cfg = { 0 };
        if (haveCfg) cfg = rini_load(configFile);
        snprintf(examplesDir, sizeof(examplesDir), "%s", rini_get_value_text_fallback(cfg, "examples_dir", "../../raylib/examples"));
        snprintf(frames, sizeof(frames), "%s", rini_get_value_text_fallback(cfg, "frames", "0,10,30"));
        timeoutMs = (unsigned int)rini_get_value_fallback(cfg, "timeout_ms", 15000);
        const char *cfgOut = rini_get_value_text_fallback(cfg, "capture_output", "rlgl");
        snprintf(outDir, sizeof(outDir), "%s", (argc > 1) ? argv[1] : cfgOut);
        // Collect every 'include' entry (rini keeps duplicate keys), so the subset can be
        // listed one per line; each value may itself be a comma-separated list.
        for (unsigned int e = 0; e < cfg.count; e++)
            if (strcmp(cfg.entries[e].key, "include") == 0) ParseNameList(cfg.entries[e].text);
        if (haveCfg) rini_unload(&cfg);
    }

    SetTraceLogLevel(LOG_WARNING);

    char absExamples[MAX_PATH_LEN], absOut[MAX_PATH_LEN];
    ResolvePath(examplesDir, absExamples, sizeof(absExamples));
    ResolvePath(outDir, absOut, sizeof(absOut));

    if (!DirectoryExists(absExamples))
    {
        printf("ERROR: examples directory not found: %s\n", absExamples);
        return 1;
    }
    MakeDirectory(absOut);
    SetEnvVar("RAYLIB_SHOT_FRAMES", frames);

    printf("Capture\n  examples: %s\n  output:   %s\n  frames:   %s\n  timeout:  %u ms\n",
           absExamples, absOut, frames, timeoutMs);
    if (includeCount > 0) printf("  SUBSET:   only the %d example(s) listed by 'include' (regression subset)\n", includeCount);
    printf("\n");

    // Record the render environment (version, machine, GPU, OS, driver) alongside the frames
    WriteEnvironment(absOut);
    //------------------------------------------------------------------------------------

    // Capture: run every built deterministic example and collect its frames
    //------------------------------------------------------------------------------------
    int ran = 0, ok = 0, fail = 0, killed = 0;

    FilePathList categories = LoadDirectoryFiles(absExamples);
    for (unsigned int c = 0; c < categories.count; c++)
    {
        if (!DirectoryExists(categories.paths[c])) continue;    // only descend into category dirs

        FilePathList files = LoadDirectoryFiles(categories.paths[c]);
        for (unsigned int f = 0; f < files.count; f++)
        {
            if (!IsFileExtension(files.paths[f], ".c")) continue;   // one example per .c file

            char name[256];
            snprintf(name, sizeof(name), "%s", GetFileNameWithoutExt(files.paths[f]));
            if ((includeCount > 0) && !IsIncluded(name)) continue;  // subset run: not selected, skip

            char exePath[MAX_PATH_LEN];
            snprintf(exePath, sizeof(exePath), "%s/%s%s", categories.paths[c], name, EXE_SUFFIX);
            if (!FileExists(exePath)) continue;     // example not built -> skip silently

            ran++;
            char outEx[MAX_PATH_LEN];
            snprintf(outEx, sizeof(outEx), "%s/%s", absOut, name);
            MakeDirectory(outEx);
            SetEnvVar("RAYLIB_SHOT_DIR", outEx);

            // Print the scene name BEFORE running and flush, so the current example is visibly
            // named (not silent) while it runs / the timeout guard counts down
            printf("  %-45s", name);
            fflush(stdout);

            int rc = RunWithTimeout(exePath, categories.paths[c], timeoutMs);
            if (rc == 1) killed++;

            // Report the captured frames for this scene: "...0 ...10 ...30 ...done"
            int nums[64], n = 0;
            FilePathList produced = LoadDirectoryFiles(outEx);
            for (unsigned int k = 0; k < produced.count; k++)
            {
                if (!IsFileExtension(produced.paths[k], ".png")) continue;
                const char *fn = GetFileName(produced.paths[k]);
                const char *p = strstr(fn, "frame_");
                if ((p != NULL) && (n < 64)) nums[n++] = atoi(p + 6);
            }
            UnloadDirectoryFiles(produced);
            for (int a = 0; a < n; a++) for (int b = a + 1; b < n; b++) if (nums[b] < nums[a]) { int t = nums[a]; nums[a] = nums[b]; nums[b] = t; }

            for (int a = 0; a < n; a++) printf(" ...%d", nums[a]);
            if (n >= 1) { ok++;  printf(" ...done%s\n", (rc == 1) ? " [timed out]" : ""); }
            else        { fail++; printf(" ...NO FRAMES%s\n", (rc == 1) ? " [timed out]" : (rc < 0) ? " [spawn failed]" : ""); }
            fflush(stdout);
        }
        UnloadDirectoryFiles(files);
    }
    UnloadDirectoryFiles(categories);

    printf("\nCAPTURE_SUMMARY: ran=%d ok=%d fail=%d killed=%d\n", ran, ok, fail, killed);
    printf("(a scene marked [timed out] was killed by the timeout after capturing at least one frame)\n");
    //------------------------------------------------------------------------------------

    return (fail > 0) ? 1 : 0;
}

//----------------------------------------------------------------------------------
// Module Functions Definition
//----------------------------------------------------------------------------------

// Parse a comma-separated list of example names (a rini "include" value) into includes[]
static void ParseNameList(const char *csv)
{
    if ((csv == NULL) || (csv[0] == '\0')) return;

    char buffer[1024];
    snprintf(buffer, sizeof(buffer), "%s", csv);
    char *token = strtok(buffer, ",");
    while ((token != NULL) && (includeCount < MAX_INCLUDES))
    {
        while ((*token == ' ') || (*token == '\t')) token++;    // trim leading space
        int len = (int)strlen(token);
        while ((len > 0) && ((token[len-1] == ' ') || (token[len-1] == '\t'))) token[--len] = '\0';
        if (len > 0) snprintf(includes[includeCount++], 192, "%s", token);
        token = strtok(NULL, ",");
    }
}

// Check if an example name passes the subset filter (empty include list = everything passes)
static bool IsIncluded(const char *example)
{
    for (int i = 0; i < includeCount; i++) if (strcmp(includes[i], example) == 0) return true;
    return false;
}

// Check if p is an absolute path (POSIX "/...", or Windows "X:\..." / "\...")
static bool IsAbsPath(const char *p)
{
    if ((p == NULL) || (p[0] == '\0')) return false;
    if ((p[0] == '/') || (p[0] == '\\')) return true;
    if (isalpha((unsigned char)p[0]) && (p[1] == ':')) return true;
    return false;
}

// Resolve a (possibly relative) path against the current working directory at run time
// NOTE: Nothing absolute is hardcoded, the base comes from GetWorkingDirectory()
static void ResolvePath(const char *rel, char *out, int outSize)
{
    if (IsAbsPath(rel)) snprintf(out, outSize, "%s", rel);
    else snprintf(out, outSize, "%s/%s", GetWorkingDirectory(), rel);
}

// Set an environment variable (cross-platform)
static void SetEnvVar(const char *name, const char *value)
{
#if defined(_WIN32)
    SetEnvironmentVariableA(name, value);
#else
    setenv(name, value, 1);
#endif
}

// Record the render environment to <outDir>/environment.rini
// NOTE: GPU/driver strings require an active GL context, so a hidden window is opened briefly
static void WriteEnvironment(const char *outDir)
{
    SetConfigFlags(FLAG_WINDOW_HIDDEN);
    InitWindow(64, 64, "image_comparison_capture");

    char vendor[256], renderer[256], glVersion[256];
    const unsigned char *v = glGetString(GL_VENDOR);
    const unsigned char *r = glGetString(GL_RENDERER);
    const unsigned char *g = glGetString(GL_VERSION);
    snprintf(vendor,    sizeof(vendor),    "%s", (v != NULL) ? (const char *)v : "unknown");
    snprintf(renderer,  sizeof(renderer),  "%s", (r != NULL) ? (const char *)r : "unknown");
    snprintf(glVersion, sizeof(glVersion), "%s", (g != NULL) ? (const char *)g : "unknown");

    CloseWindow();

    char machine[256] = "unknown";
#if defined(_WIN32)
    DWORD sz = sizeof(machine);
    GetComputerNameA(machine, &sz);
    const char *os = "Windows";
#elif defined(__APPLE__)
    gethostname(machine, sizeof(machine));
    const char *os = "macOS";
#else
    gethostname(machine, sizeof(machine));
    const char *os = "Linux";
#endif

    rini_data md = rini_load_from_memory("");
    rini_set_value_text(&md, "raylib_version",  RAYLIB_VERSION, "raylib version used to render");
    rini_set_value_text(&md, "machine",         machine,        "host machine name");
    rini_set_value_text(&md, "os",              os,             "operating system");
    rini_set_value_text(&md, "gpu",             renderer,       "graphics card (GL_RENDERER)");
    rini_set_value_text(&md, "gpu_vendor",      vendor,         "graphics vendor (GL_VENDOR)");
    rini_set_value_text(&md, "graphics_driver", glVersion,      "GL_VERSION string (includes driver)");

    char path[MAX_PATH_LEN];
    snprintf(path, sizeof(path), "%s/environment.rini", outDir);
    rini_save(md, path);
    rini_unload(&md);

    printf("Environment: raylib %s | %s | %s | %s\n\n", RAYLIB_VERSION, os, renderer, glVersion);
}

// Spawn absExe with working directory workDir, killing it after timeoutMs
// Returns: 0 = exited on its own, 1 = timed out and was killed, -1 = failed to spawn
static int RunWithTimeout(const char *absExe, const char *workDir, unsigned int timeoutMs)
{
#if defined(_WIN32)
    // Windows CreateProcess prefers backslashes for the program path and current directory
    char exeWin[MAX_PATH_LEN], dirWin[MAX_PATH_LEN], cmd[MAX_PATH_LEN + 4];
    int i = 0;
    for (; (absExe[i] != '\0') && (i < MAX_PATH_LEN - 1); i++) exeWin[i] = (absExe[i] == '/') ? '\\' : absExe[i];
    exeWin[i] = '\0';
    for (i = 0; (workDir[i] != '\0') && (i < MAX_PATH_LEN - 1); i++) dirWin[i] = (workDir[i] == '/') ? '\\' : workDir[i];
    dirWin[i] = '\0';
    snprintf(cmd, sizeof(cmd), "\"%s\"", exeWin);

    STARTUPINFOA si; ZeroMemory(&si, sizeof(si)); si.cb = sizeof(si);
    PROCESS_INFORMATION pi; ZeroMemory(&pi, sizeof(pi));
    if (!CreateProcessA(NULL, cmd, NULL, NULL, FALSE, 0, NULL, dirWin, &si, &pi)) return -1;

    int rc = 0;
    if (WaitForSingleObject(pi.hProcess, timeoutMs) == WAIT_TIMEOUT)
    {
        TerminateProcess(pi.hProcess, 1);
        WaitForSingleObject(pi.hProcess, 3000);
        rc = 1;
    }
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return rc;
#else
    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid == 0)
    {
        if ((workDir != NULL) && (workDir[0] != '\0')) { if (chdir(workDir) != 0) _exit(127); }
        execl(absExe, absExe, (char *)NULL);
        _exit(127);
    }

    unsigned int waited = 0;
    const unsigned int step = 50;
    int status;
    while (waited < timeoutMs)
    {
        pid_t r = waitpid(pid, &status, WNOHANG);
        if (r == pid) return 0;
        if (r < 0) return 0;
        struct timespec ts; ts.tv_sec = 0; ts.tv_nsec = (long)step*1000000L;
        nanosleep(&ts, NULL);
        waited += step;
    }
    kill(pid, SIGKILL);
    waitpid(pid, &status, 0);
    return 1;
#endif
}
