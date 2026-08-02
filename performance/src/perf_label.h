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
*          (GPU name via DXGI on Windows, via a dlopen'd Vulkan probe on Linux; elsewhere auto
*           falls back to "<os>_unknown", so pass RAYLIB_PERF_LABEL there)
*
*   The Linux probe loads libvulkan.so.1 at run time (no link dependency) purely to read the
*   adapter's reported name, driver name/version and device-local heap size - the same three
*   provenance fields DXGI supplies on Windows. It is backend-agnostic: the harness itself
*   renders nothing, and the probe runs once before any test is launched.
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
    static const GUID PL_IID_IDXGIDevice   = { 0x54ec77fa, 0x1377, 0x44e6, { 0x8c, 0x32, 0x88, 0xfd, 0x5f, 0x44, 0xc8, 0x4c } };
    typedef HRESULT (WINAPI *PFN_PL_CreateDXGIFactory1)(REFIID, void **);
#elif defined(__linux__)
    #include <dlfcn.h>                  // Required for: dlopen(), dlsym() - runtime Vulkan probe
    #include <vulkan/vulkan.h>          // Types and enums only; every function is dlsym'd

// GPU provenance from Vulkan, probed once and cached: adapter name, driver name/version and
// device-local heap size (the Linux counterparts of the DXGI adapter description fields).
// The highest-VRAM device wins, matching the Windows adapter choice.
typedef struct PerfLinuxGpuInfo {
    char    name[512];
    char    driver[256];
    double  vramTotalMB;
    int     probed;
} PerfLinuxGpuInfo;

static const PerfLinuxGpuInfo *PerfLinuxProbeGpu(void)
{
    static PerfLinuxGpuInfo info = { "unknown", "unknown", 0.0, 0 };
    if (info.probed) return &info;
    info.probed = 1;

    void *lib = dlopen("libvulkan.so.1", RTLD_NOW | RTLD_LOCAL);
    if (lib == NULL) lib = dlopen("libvulkan.so", RTLD_NOW | RTLD_LOCAL);
    if (lib == NULL) return &info;

    PFN_vkCreateInstance pCreateInstance = (PFN_vkCreateInstance)dlsym(lib, "vkCreateInstance");
    PFN_vkDestroyInstance pDestroyInstance = (PFN_vkDestroyInstance)dlsym(lib, "vkDestroyInstance");
    PFN_vkEnumeratePhysicalDevices pEnumerate = (PFN_vkEnumeratePhysicalDevices)dlsym(lib, "vkEnumeratePhysicalDevices");
    PFN_vkGetPhysicalDeviceProperties2 pGetProps2 = (PFN_vkGetPhysicalDeviceProperties2)dlsym(lib, "vkGetPhysicalDeviceProperties2");
    PFN_vkGetPhysicalDeviceMemoryProperties pGetMem = (PFN_vkGetPhysicalDeviceMemoryProperties)dlsym(lib, "vkGetPhysicalDeviceMemoryProperties");
    if (!pCreateInstance || !pDestroyInstance || !pEnumerate || !pGetProps2 || !pGetMem) return &info;

    VkApplicationInfo app = { VK_STRUCTURE_TYPE_APPLICATION_INFO };
    app.pApplicationName = "raylib_tests";
    app.apiVersion = VK_API_VERSION_1_1;                    // Properties2 is 1.1 core
    VkInstanceCreateInfo ici = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
    ici.pApplicationInfo = &app;

    VkInstance instance = VK_NULL_HANDLE;
    if (pCreateInstance(&ici, NULL, &instance) != VK_SUCCESS) return &info;

    uint32_t count = 0;
    pEnumerate(instance, &count, NULL);
    if (count > 0)
    {
        if (count > 16) count = 16;
        VkPhysicalDevice devices[16];
        pEnumerate(instance, &count, devices);

        double bestVram = -1.0;
        for (uint32_t i = 0; i < count; i++)
        {
            VkPhysicalDeviceDriverProperties driverProps = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES };
            VkPhysicalDeviceProperties2 props = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, &driverProps };
            pGetProps2(devices[i], &props);

            VkPhysicalDeviceMemoryProperties mem;
            pGetMem(devices[i], &mem);
            double localMB = 0.0;
            for (uint32_t h = 0; h < mem.memoryHeapCount; h++)
                if (mem.memoryHeaps[h].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT)
                {
                    double mb = (double)mem.memoryHeaps[h].size/(1024.0*1024.0);
                    if (mb > localMB) localMB = mb;
                }

            if (localMB > bestVram)
            {
                bestVram = localMB;
                info.vramTotalMB = localMB;
                snprintf(info.name, sizeof(info.name), "%s", props.properties.deviceName);
                snprintf(info.driver, sizeof(info.driver), "%s %s", driverProps.driverName, driverProps.driverInfo);
            }
        }
    }
    pDestroyInstance(instance, NULL);
    return &info;
}
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

// Best-effort GPU name (Windows: highest-VRAM DXGI adapter; Linux: Vulkan probe; other: "unknown")
static void PerfDetectGpuName(char *out, int outSize)
{
    snprintf(out, outSize, "unknown");
#if defined(__linux__)
    snprintf(out, outSize, "%s", PerfLinuxProbeGpu()->name);
    return;
#endif
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


// OS version string, e.g. "Windows 11 (10.0.26100)". Uses RtlGetVersion: GetVersionEx lies
// without an application manifest, the kernel entry point does not.
static void PerfDetectOSVersion(char *out, int outSize)
{
    snprintf(out, outSize, "%s", PerfDetectOS());
#if defined(__linux__)
    // Distribution name from /etc/os-release plus the running kernel release
    char distro[128] = "Linux";
    FILE *f = fopen("/etc/os-release", "rb");
    if (f != NULL)
    {
        char line[256];
        while (fgets(line, sizeof(line), f) != NULL)
        {
            if (strncmp(line, "PRETTY_NAME=", 12) != 0) continue;
            char *value = line + 12;
            if (*value == '"') value++;
            char *end = value + strlen(value);
            while ((end > value) && ((end[-1] == '\n') || (end[-1] == '"') || (end[-1] == '\r'))) *(--end) = '\0';
            snprintf(distro, sizeof(distro), "%s", value);
            break;
        }
        fclose(f);
    }
    char kernel[128] = "";
    f = fopen("/proc/sys/kernel/osrelease", "rb");
    if (f != NULL)
    {
        if (fgets(kernel, sizeof(kernel), f) != NULL)
        {
            char *end = kernel + strlen(kernel);
            while ((end > kernel) && ((end[-1] == '\n') || (end[-1] == '\r'))) *(--end) = '\0';
        }
        fclose(f);
    }
    if (kernel[0] != '\0') snprintf(out, outSize, "%s (kernel %s)", distro, kernel);
    else snprintf(out, outSize, "%s", distro);
    return;
#endif
#if defined(_WIN32)
    typedef LONG (WINAPI *PFN_PL_RtlGetVersion)(PRTL_OSVERSIONINFOW);
    HMODULE hNt = GetModuleHandleA("ntdll.dll");
    PFN_PL_RtlGetVersion pRtl = hNt? (PFN_PL_RtlGetVersion)(void *)GetProcAddress(hNt, "RtlGetVersion") : NULL;
    if (pRtl != NULL)
    {
        RTL_OSVERSIONINFOW vi; memset(&vi, 0, sizeof(vi)); vi.dwOSVersionInfoSize = sizeof(vi);
        if (pRtl(&vi) == 0)
        {
            // Build >= 22000 is the Windows 11 line despite the 10.0 version prefix
            const char *name = (vi.dwMajorVersion == 10 && vi.dwBuildNumber >= 22000)? "Windows 11" :
                               (vi.dwMajorVersion == 10)? "Windows 10" : "Windows";
            snprintf(out, outSize, "%s (%lu.%lu.%lu)", name,
                (unsigned long)vi.dwMajorVersion, (unsigned long)vi.dwMinorVersion, (unsigned long)vi.dwBuildNumber);
        }
    }
#endif
}

// GPU driver version of the highest-VRAM adapter, e.g. "32.0.15.9597 (NVIDIA 595.97)".
// The raw value is the DXGI user-mode-driver version; the NVIDIA marketing number is decoded
// from its last five digits when the vendor matches.
static void PerfDetectGpuDriver(char *out, int outSize)
{
    snprintf(out, outSize, "unknown");
#if defined(__linux__)
    // Vulkan reports the driver by name and version directly (e.g. "radv Mesa 26.1.6"),
    // so no vendor-specific decoding is needed
    snprintf(out, outSize, "%s", PerfLinuxProbeGpu()->driver);
    return;
#endif
#if defined(_WIN32)
    HMODULE hDxgi = LoadLibraryA("dxgi.dll");
    if (hDxgi == NULL) return;
    PFN_PL_CreateDXGIFactory1 pCreate = (PFN_PL_CreateDXGIFactory1)(void *)GetProcAddress(hDxgi, "CreateDXGIFactory1");
    if (pCreate == NULL) return;
    IDXGIFactory1 *factory = NULL;
    if (FAILED(pCreate(&PL_IID_IDXGIFactory1, (void **)&factory)) || !factory) return;
    SIZE_T bestVram = 0;
    char gpu[512] = "";
    LARGE_INTEGER best; best.QuadPart = 0;
    for (UINT i = 0; ; i++)
    {
        IDXGIAdapter1 *adapter = NULL;
        if (IDXGIFactory1_EnumAdapters1(factory, i, &adapter) != S_OK) break;
        DXGI_ADAPTER_DESC1 desc; memset(&desc, 0, sizeof(desc));
        IDXGIAdapter1_GetDesc1(adapter, &desc);
        if (!(desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) && (desc.DedicatedVideoMemory >= bestVram))
        {
            LARGE_INTEGER umd;
            if (SUCCEEDED(IDXGIAdapter1_CheckInterfaceSupport(adapter, &PL_IID_IDXGIDevice, &umd)))
            {
                bestVram = desc.DedicatedVideoMemory;
                best = umd;
                WideCharToMultiByte(CP_UTF8, 0, desc.Description, -1, gpu, sizeof(gpu), NULL, NULL);
            }
        }
        IDXGIAdapter1_Release(adapter);
    }
    IDXGIFactory1_Release(factory);
    if (best.QuadPart == 0) return;
    unsigned a = (unsigned)(best.QuadPart >> 48) & 0xFFFF, b = (unsigned)(best.QuadPart >> 32) & 0xFFFF;
    unsigned c = (unsigned)(best.QuadPart >> 16) & 0xFFFF, d = (unsigned)best.QuadPart & 0xFFFF;
    if (strcmp(PerfVendorOf(gpu), "nvidia") == 0)
        snprintf(out, outSize, "%u.%u.%u.%u (NVIDIA %u.%02u)", a, b, c, d, ((c%10)*100 + d/100), d%100);
    else
        snprintf(out, outSize, "%u.%u.%u.%u", a, b, c, d);
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
