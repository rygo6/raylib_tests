/*******************************************************************************************
*
*   perf_label.h - platform x vendor label for output naming (shared by capture + report)
*
*   Produces a "<os>_<vendor>" slug (e.g. windows_nvidia, linux_amd) used to suffix capture
*   directories and report filenames so results from different machines coexist and compare.
*
*   Resolution order (both tools compute it identically, so the same machine always agrees):
*       1. RAYLIB_PERF_LABEL environment variable   (explicit override; use on Linux/CI)
*       2. 'label' key in the backend .ini          (explicit override)
*       3. auto: "<os>_<vendor>"  - os from the build target, vendor from the GPU name
*          (GPU name via DXGI on Windows; on other platforms auto falls back to "<os>_unknown",
*           so pass RAYLIB_PERF_LABEL there until native detection is added)
*
********************************************************************************************/

#ifndef PERF_LABEL_H
#define PERF_LABEL_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#if defined(_WIN32)
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef COBJMACROS
        #define COBJMACROS
    #endif
    #include <windows.h>
    #include <dxgi1_4.h>
    static const GUID PL_IID_IDXGIFactory1 = { 0x770aae78, 0xf26f, 0x4dba, { 0xa8, 0x29, 0x25, 0x3c, 0x83, 0xd1, 0xb3, 0x87 } };
    typedef HRESULT (WINAPI *PFN_PL_CreateDXGIFactory1)(REFIID, void **);
#endif

static const char *PerfDetectOS(void)
{
#if defined(_WIN32)
    return "windows";
#elif defined(__linux__)
    return "linux";
#elif defined(__APPLE__)
    return "macos";
#else
    return "os";
#endif
}

// Classify a GPU description string into a short vendor slug
static const char *PerfVendorOf(const char *gpu)
{
    if (gpu == NULL) return "unknown";
    // Case-insensitive substring checks
    char up[512]; int i = 0;
    for (; gpu[i] && i < 511; i++) up[i] = (char)toupper((unsigned char)gpu[i]);
    up[i] = '\0';
    if (strstr(up, "NVIDIA") || strstr(up, "GEFORCE") || strstr(up, "QUADRO") || strstr(up, "RTX") || strstr(up, "GTX")) return "nvidia";
    if (strstr(up, "AMD") || strstr(up, "RADEON") || strstr(up, "ATI"))    return "amd";
    if (strstr(up, "INTEL") || strstr(up, "ARC") || strstr(up, "UHD"))     return "intel";
    return "unknown";
}

// Best-effort GPU name (Windows: highest-VRAM DXGI adapter; other: "unknown")
static void PerfDetectGpuName(char *out, int outSize)
{
    snprintf(out, outSize, "unknown");
#if defined(_WIN32)
    HMODULE hDxgi = LoadLibraryA("dxgi.dll");
    if (hDxgi == NULL) return;
    PFN_PL_CreateDXGIFactory1 pCreate = (PFN_PL_CreateDXGIFactory1)(void *)GetProcAddress(hDxgi, "CreateDXGIFactory1");
    if (pCreate == NULL) return;
    IDXGIFactory1 *factory = NULL;
    if (FAILED(pCreate(&PL_IID_IDXGIFactory1, (void **)&factory)) || !factory) return;
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
            WideCharToMultiByte(CP_UTF8, 0, desc.Description, -1, out, outSize, NULL, NULL);
        }
        IDXGIAdapter1_Release(adapter);
    }
    IDXGIFactory1_Release(factory);
#endif
}

// Fill 'out' with the effective label (see resolution order above). cfgLabel may be NULL/empty.
static void PerfComputeLabel(const char *cfgLabel, char *out, int outSize)
{
    const char *env = getenv("RAYLIB_PERF_LABEL");
    if (env != NULL && env[0] != '\0') { snprintf(out, outSize, "%s", env); return; }
    if (cfgLabel != NULL && cfgLabel[0] != '\0') { snprintf(out, outSize, "%s", cfgLabel); return; }
    char gpu[512]; PerfDetectGpuName(gpu, sizeof(gpu));
    snprintf(out, outSize, "%s_%s", PerfDetectOS(), PerfVendorOf(gpu));
}

#endif // PERF_LABEL_H
