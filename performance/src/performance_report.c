/*******************************************************************************************
*
*   raylib [tests] - performance report generator
*
*   Reads the per-run capture files written by the PERFORMANCE_CAPTURE hook
*   (<backendDir>/<example>/run_<n>.rini) plus <backendDir>/environment.rini, aggregates the
*   RUNS runs per example (representative = the run whose median frame time is the median run),
*   and emits:
*       report_<backend>.html   - one per backend, all metrics
*       report_comparison.html  - collated side-by-side across every backend supplied
*
*   USAGE:
*       performance_report [config1.ini config2.ini ...]
*
*   With no args it reads the three standard configs (performance_rlgl.ini, performance_rlsw.ini,
*   performance_rlvk.ini); any missing backend is skipped. Each config supplies the backend name,
*   its capture_output directory, and the example list/order.
*
*   Licensed under zlib/libpng (same as raylib).
*
********************************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#define RINI_MAX_LINE_SIZE 4096   // the curated example list exceeds rini's 512 default
#define RINI_IMPLEMENTATION
#include "rini.h"
#include "perf_label.h"    // PerfComputeLabel(): "<os>_<vendor>" slug for output naming

#define MAX_PATH_LEN   2048
#define MAX_EXAMPLES   256
#define MAX_RUNS       16
#define MAX_BACKENDS   8

typedef struct RunStats {
    bool   valid;
    int    frames;
    double seconds, fps;
    double fmin, fmax, fmed, favg, fp95, fp99;
    double cpuAvg, cpuPeak;
    double ramAvg, ramPeak;     // bytes
    double vramAvg, vramPeak;   // bytes
} RunStats;

typedef struct ExampleStats {
    char     name[128];
    RunStats runs[MAX_RUNS];
    int      runCount;
    RunStats agg;               // representative (median) run
} ExampleStats;

typedef struct BackendStats {
    char        backend[64];
    char        label[128];
    char        dir[MAX_PATH_LEN];
    char        gpu[256], os[64], osVersion[128], gpuDriver[128];
    int         durationMs, warmupMs, runs, vramTotalMB;
    ExampleStats ex[MAX_EXAMPLES];
    int         exCount;
} BackendStats;

//----------------------------------------------------------------------------------
// Helpers
//----------------------------------------------------------------------------------
static double GetD(rini_data d, const char *key)
{
    const char *t = rini_get_value_text(d, key);
    return (t != NULL) ? atof(t) : 0.0;
}

static int CmpDouble(const void *a, const void *b)
{
    double x = *(const double *)a, y = *(const double *)b;
    return (x < y) ? -1 : (x > y) ? 1 : 0;
}

static double MedianOf(double *v, int n)
{
    if (n <= 0) return 0.0;
    qsort(v, n, sizeof(double), CmpDouble);
    return (n & 1) ? v[n/2] : 0.5*(v[n/2 - 1] + v[n/2]);
}

// Load one run_<r>.rini
static bool LoadRun(const char *path, RunStats *rs)
{
    FILE *f = fopen(path, "rb"); if (!f) return false; fclose(f);
    rini_data d = rini_load(path);
    rs->frames  = (int)GetD(d, "frames");
    rs->seconds = GetD(d, "measured_seconds");
    rs->fps     = GetD(d, "fps");
    rs->fmin    = GetD(d, "frame_min_ms");
    rs->fmax    = GetD(d, "frame_max_ms");
    rs->fmed    = GetD(d, "frame_median_ms");
    rs->favg    = GetD(d, "frame_avg_ms");
    rs->fp95    = GetD(d, "frame_p95_ms");
    rs->fp99    = GetD(d, "frame_p99_ms");
    rs->cpuAvg  = GetD(d, "cpu_avg_pct");
    rs->cpuPeak = GetD(d, "cpu_peak_pct");
    rs->ramAvg  = GetD(d, "ram_avg_bytes");
    rs->ramPeak = GetD(d, "ram_peak_bytes");
    rs->vramAvg = GetD(d, "vram_avg_bytes");
    rs->vramPeak= GetD(d, "vram_peak_bytes");
    rs->valid   = (rs->frames > 0);
    rini_unload(&d);
    return rs->valid;
}

// Aggregate = arithmetic mean of every metric across the valid runs (2026-08-18 protocol:
// runs are interleaved capture cycles, so averaging distributes machine-state drift evenly;
// the old representative-median-run pick let one leg's thermal window own the whole cell)
static void Aggregate(ExampleStats *e)
{
    if (e->runCount <= 0) { e->agg.valid = false; return; }
    RunStats m = { 0 }; int n = 0;
    for (int i = 0; i < e->runCount; i++)
    {
        RunStats *r = &e->runs[i];
        if (!r->valid) continue;
        m.frames  += r->frames;
        m.seconds += r->seconds;  m.fps     += r->fps;
        m.fmin    += r->fmin;     m.fmax    += r->fmax;
        m.fmed    += r->fmed;     m.favg    += r->favg;
        m.fp95    += r->fp95;     m.fp99    += r->fp99;
        m.cpuAvg  += r->cpuAvg;   m.cpuPeak += r->cpuPeak;
        m.ramAvg  += r->ramAvg;   m.ramPeak += r->ramPeak;
        m.vramAvg += r->vramAvg;  m.vramPeak+= r->vramPeak;
        n++;
    }
    if (n == 0) { e->agg.valid = false; return; }
    m.frames /= n;
    m.seconds /= n;  m.fps     /= n;
    m.fmin    /= n;  m.fmax    /= n;
    m.fmed    /= n;  m.favg    /= n;
    m.fp95    /= n;  m.fp99    /= n;
    m.cpuAvg  /= n;  m.cpuPeak /= n;
    m.ramAvg  /= n;  m.ramPeak /= n;
    m.vramAvg /= n;  m.vramPeak/= n;
    m.valid = true;
    e->agg = m;
}

static void LoadBackend(const char *configFile, BackendStats *b)
{
    memset(b, 0, sizeof(*b));
    rini_data cfg = rini_load(configFile);
    snprintf(b->backend, sizeof(b->backend), "%s", rini_get_value_text_fallback(cfg, "backend", "rlgl"));
    PerfComputeLabel(rini_get_value_text_fallback(cfg, "label", ""), b->label, sizeof(b->label));
    snprintf(b->dir, sizeof(b->dir), "%s_%s", rini_get_value_text_fallback(cfg, "capture_output", b->backend), b->label);
    char examplesRaw[8192]; snprintf(examplesRaw, sizeof(examplesRaw), "%s", rini_get_value_text_fallback(cfg, "examples", ""));
    rini_unload(&cfg);

    // environment.rini
    char envPath[MAX_PATH_LEN]; snprintf(envPath, sizeof(envPath), "%s/environment.rini", b->dir);
    FILE *ef = fopen(envPath, "rb");
    if (ef) { fclose(ef);
        rini_data ed = rini_load(envPath);
        snprintf(b->gpu, sizeof(b->gpu), "%s", rini_get_value_text_fallback(ed, "gpu", "unknown"));
        snprintf(b->os, sizeof(b->os), "%s", rini_get_value_text_fallback(ed, "os", "unknown"));
        snprintf(b->osVersion, sizeof(b->osVersion), "%s", rini_get_value_text_fallback(ed, "os_version", "unknown"));
        snprintf(b->gpuDriver, sizeof(b->gpuDriver), "%s", rini_get_value_text_fallback(ed, "gpu_driver", "unknown"));
        b->durationMs  = rini_get_value_fallback(ed, "duration_ms", 0);
        b->warmupMs    = rini_get_value_fallback(ed, "warmup_ms", 0);
        b->runs        = rini_get_value_fallback(ed, "runs", 0);
        b->vramTotalMB = rini_get_value_fallback(ed, "gpu_vram_total_mb", 0);
        rini_unload(&ed);
    } else { snprintf(b->gpu, sizeof(b->gpu), "unknown"); snprintf(b->os, sizeof(b->os), "unknown"); snprintf(b->osVersion, sizeof(b->osVersion), "unknown"); snprintf(b->gpuDriver, sizeof(b->gpuDriver), "unknown"); }

    // Parse example list, load each example's runs
    char *tok = strtok(examplesRaw, ",");
    while ((tok != NULL) && (b->exCount < MAX_EXAMPLES))
    {
        while ((*tok == ' ') || (*tok == '\t')) tok++;
        int n = (int)strlen(tok); while ((n > 0) && ((tok[n-1]==' ')||(tok[n-1]=='\t')||(tok[n-1]=='\r'))) tok[--n] = '\0';
        if (n > 0)
        {
            const char *slash = strrchr(tok, '/');
            const char *name = slash ? slash+1 : tok;
            ExampleStats *e = &b->ex[b->exCount];
            snprintf(e->name, sizeof(e->name), "%s", name);
            for (int r = 1; r <= MAX_RUNS; r++)
            {
                char rf[MAX_PATH_LEN]; snprintf(rf, sizeof(rf), "%s/%s/run_%d.rini", b->dir, name, r);
                RunStats rs; memset(&rs, 0, sizeof(rs));
                if (LoadRun(rf, &rs)) e->runs[e->runCount++] = rs;
            }
            Aggregate(e);
            b->exCount++;
        }
        tok = strtok(NULL, ",");
    }
}

//----------------------------------------------------------------------------------
// HTML
//----------------------------------------------------------------------------------
static const char *CSS =
"<style>"
":root{--bg:#0f1116;--card:#171a21;--line:#262b36;--txt:#e6e9ef;--dim:#9aa4b2;--acc:#6ea8fe;--good:#54d18c;--bad:#ff6b6b;--warn:#ffcf5c}"
"*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--txt);font:14px/1.5 -apple-system,Segoe UI,Roboto,sans-serif;padding:28px}"
"h1{font-size:22px;margin:0 0 4px}h2{font-size:16px;margin:28px 0 10px;color:var(--acc)}"
".env{color:var(--dim);margin:0 0 8px;font-size:13px}.env b{color:var(--txt)}"
".wrap{overflow-x:auto;border:1px solid var(--line);border-radius:10px;background:var(--card);margin:10px 0}"
"table{border-collapse:collapse;width:100%;min-width:720px}"
"th,td{padding:8px 12px;text-align:right;border-bottom:1px solid var(--line);white-space:nowrap;font-variant-numeric:tabular-nums}"
"th{background:#1c2029;color:var(--dim);font-weight:600;position:sticky;top:0;text-align:right}"
"td.name,th.name{text-align:left;color:var(--txt);font-weight:600}"
"tr:last-child td{border-bottom:none}tr:hover td{background:#1b1f28}"
".best{color:var(--good);font-weight:700}.worst{color:var(--bad)}"
".sub{color:var(--dim);font-size:12px}.na{color:var(--dim)}"
".callout{border-left:4px solid var(--acc);background:rgba(128,128,128,0.08);padding:10px 14px;margin:14px 0;font-size:13px;line-height:1.5}.callout b{color:var(--acc)}"
"details{margin:6px 0}summary{cursor:pointer;color:var(--acc);font-size:13px}"
"caption{caption-side:top;text-align:left;color:var(--dim);padding:8px 12px;font-size:12px}"
"</style>";

static double MB(double bytes) { return bytes/(1024.0*1024.0); }

//----------------------------------------------------------------------------------
static const char* DisplayName(const BackendStats *b, char *buf, int bufSize);

// Per-backend report
//----------------------------------------------------------------------------------
// macOS present-floor callout: emitted at the top of every macOS report involving a Vulkan
// ICD so raw uncapped FPS is not misread as rendering speed. Evidence measured 2026-08-12
// on shapes_basic_shapes (Apple M5, MoltenVK performance tracking, uncapped)
static void WriteMacosFloorCallout(FILE *f)
{
    fprintf(f, "<div class=callout><b>Read this before comparing FPS on macOS.</b> "
        "Uncapped FPS on light scenes here measures <i>presentation</i>, not rendering. Apple GL presents by "
        "flushing an IOSurface &mdash; no per-frame handshake, essentially free &mdash; while Vulkan on macOS "
        "(MoltenVK or KosmicKrisp) can only present through Metal, which must acquire a CAMetalDrawable from "
        "CoreAnimation each frame, paced at ~1.6&ndash;1.8 ms on a composited window no matter how little the "
        "frame draws. Measured on shapes_basic_shapes, uncapped: frame interval avg 1.95 ms, of which "
        "<b>1.76 ms (90%%) is &quot;Retrieve a CAMetalDrawable&quot;</b> (MoltenVK's own performance tracking; "
        "the drawing itself costs ~0.12 ms and the GPU is nearly idle). Every native Metal app pays the same "
        "pacing; no driver knob removes it (KosmicKrisp's is lowered from ~3.3 to ~1.6 ms by "
        "<code>MESA_VK_WSI_PRESENT_MODE=mailbox</code>, MoltenVK's config surface is exhausted). "
        "Cells marked &#176; sit at that backend's floor: their cross-backend ratios compare macOS present "
        "plumbing. Rows <i>above</i> the floor compare real rendering &mdash; there the Vulkan backend leads "
        "(e.g. 8000 draw calls: 120 fps GL vs 581 fps rlvk). Under vsync all backends lock to the display "
        "rate and the floor is invisible.</div>");
}

static void WriteBackendReport(BackendStats *b)
{
    char path[MAX_PATH_LEN]; snprintf(path, sizeof(path), "report_%s_%s.html", b->backend, b->label);
    FILE *f = fopen(path, "w"); if (!f) { printf("ERROR: cannot write %s\n", path); return; }

    char nameBuf[128]; const char *dispName = DisplayName(b, nameBuf, sizeof(nameBuf));
    fprintf(f, "<!doctype html><html><head><meta charset=utf-8><meta name=viewport content=\"width=device-width,initial-scale=1\"><title>raylib perf - %s</title>%s</head><body>", dispName, CSS);
    fprintf(f, "<h1>raylib performance &mdash; <span style='color:var(--acc)'>%s</span></h1>", dispName);
    fprintf(f, "<p class=env><b>GPU</b> %s (%d MB) &nbsp;|&nbsp; <b>Driver</b> %s &nbsp;|&nbsp; <b>OS</b> %s &nbsp;|&nbsp; <b>%d</b> runs &times; <b>%d</b> ms (+%d ms warmup) at full speed</p>",
            b->gpu, b->vramTotalMB, b->gpuDriver, b->osVersion, b->runs, b->durationMs, b->warmupMs);
    if ((strstr(b->os, "macOS") != NULL) && (strcmp(b->backend, "rlvk") == 0)) WriteMacosFloorCallout(f);

    // Main aggregate table
    fprintf(f, "<h2>Summary (representative run per example)</h2><div class=wrap><table>");
    fprintf(f, "<caption>Frame time in milliseconds (lower is better); FPS at full speed; CPU %% of whole machine; RAM working set; VRAM per-process GPU memory.</caption>");
    fprintf(f, "<tr><th class=name>Example</th><th>FPS</th><th>min ms</th><th>median ms</th><th>avg ms</th><th>max ms</th><th>p95 ms</th><th>p99 ms</th><th>CPU %%</th><th>RAM MB</th><th>VRAM MB</th></tr>");
    for (int i = 0; i < b->exCount; i++)
    {
        ExampleStats *e = &b->ex[i];
        fprintf(f, "<tr><td class=name>%s</td>", e->name);
        if (e->agg.valid)
        {
            RunStats *a = &e->agg;
            fprintf(f, "<td>%.0f</td><td>%.3f</td><td>%.3f</td><td>%.3f</td><td>%.3f</td><td>%.3f</td><td>%.3f</td><td>%.1f</td><td>%.1f</td><td>%.1f</td>",
                    a->fps, a->fmin, a->fmed, a->favg, a->fmax, a->fp95, a->fp99, a->cpuAvg, MB(a->ramAvg), MB(a->vramAvg));
        }
        else fprintf(f, "<td class=na colspan=10>no data</td>");
        fprintf(f, "</tr>");
    }
    fprintf(f, "</table></div>");

    // Per-run detail
    fprintf(f, "<details><summary>Per-run detail (all %d runs)</summary><div class=wrap><table>", b->runs);
    fprintf(f, "<tr><th class=name>Example</th><th>run</th><th>frames</th><th>FPS</th><th>min ms</th><th>median ms</th><th>avg ms</th><th>max ms</th><th>CPU %%</th><th>RAM MB</th><th>VRAM MB</th></tr>");
    for (int i = 0; i < b->exCount; i++)
    {
        ExampleStats *e = &b->ex[i];
        for (int r = 0; r < e->runCount; r++)
        {
            RunStats *s = &e->runs[r];
            fprintf(f, "<tr><td class=name>%s</td><td>%d</td><td>%d</td><td>%.0f</td><td>%.3f</td><td>%.3f</td><td>%.3f</td><td>%.3f</td><td>%.1f</td><td>%.1f</td><td>%.1f</td></tr>",
                    (r==0)?e->name:"", r+1, s->frames, s->fps, s->fmin, s->fmed, s->favg, s->fmax, s->cpuAvg, MB(s->ramAvg), MB(s->vramAvg));
        }
    }
    fprintf(f, "</table></div></details>");

    fprintf(f, "</body></html>");
    fclose(f);
    printf("  wrote %s (%d examples)\n", path, b->exCount);
}

//----------------------------------------------------------------------------------
// Comparison report
//----------------------------------------------------------------------------------
// Metric selector for the comparison tables
typedef enum { M_FPS, M_MEDIAN, M_AVG, M_CPU, M_RAM, M_VRAM } Metric;

static double MetricVal(RunStats *a, Metric m)
{
    switch (m) {
        case M_FPS:    return a->fps;
        case M_MEDIAN: return a->fmed;
        case M_AVG:    return a->favg;
        case M_CPU:    return a->cpuAvg;
        case M_RAM:    return MB(a->ramAvg);
        case M_VRAM:   return MB(a->vramAvg);
    }
    return 0.0;
}
// true if higher is better for this metric
static bool HigherBetter(Metric m) { return (m == M_FPS); }

// Display name for a backend column. On macOS one rlvk binary can run on different
// Vulkan-on-Metal ICDs (MoltenVK, KosmicKrisp, and their threaded-present patches), so
// a bare "rlvk" is ambiguous: surface the ICD from the label, e.g. "rlvk (moltenvk
// threaded)". Other backends and platforms keep their plain name.
static const char* DisplayName(const BackendStats *b, char *buf, int bufSize)
{
    if ((strcmp(b->backend, "rlvk") == 0) && (strncmp(b->label, "macos_", 6) == 0))
    {
        snprintf(buf, bufSize, "%s (%s", b->backend, b->label + 6);
        for (char *c = buf; *c; c++) if (*c == '_') *c = ' ';
        size_t len = strlen(buf);
        if (len + 1 < (size_t)bufSize) { buf[len] = ')'; buf[len + 1] = 0; }
        return buf;
    }
    return b->backend;
}

// true if two compared backends share a name (e.g. rlvk under two Vulkan ICDs) so column
// headers must append the label to stay distinguishable
// Duplicate check runs on DISPLAY names: two rlvk columns on different macOS ICDs are
// already distinct as "rlvk (moltenvk)" vs "rlvk (kosmickrisp threaded)" and need no
// raw-label disambiguation.
static bool DuplicateBackendNames(BackendStats *bk, int nb)
{
    for (int i = 0; i < nb; i++) for (int j = i+1; j < nb; j++)
    {
        char bi[128], bj[128];
        if (strcmp(DisplayName(&bk[i], bi, sizeof(bi)), DisplayName(&bk[j], bj, sizeof(bj))) == 0) return true;
    }
    return false;
}

// Per-backend floor: the bench_idle representative median (0 when bench_idle is absent).
// A scene whose median sits at this floor measures present pacing / loop overhead, not
// rendering cost (macOS: Metal drawable acquire paces Vulkan presents while GL's IOSurface
// flush has no floor, so raw sub-floor ratios compare present plumbing, not backends)
static double BackendFloorMs(BackendStats *b)
{
    for (int j = 0; j < b->exCount; j++)
        if (strcmp(b->ex[j].name, "bench_idle") == 0 && b->ex[j].agg.valid) return b->ex[j].agg.fmed;
    return 0.0;
}

static void ComparisonTable(FILE *f, BackendStats *bk, int nb, const char *title, const char *caption, Metric m, const char *fmt)
{
    bool dup = DuplicateBackendNames(bk, nb);
    bool frameMetric = (m == M_FPS) || (m == M_MEDIAN) || (m == M_AVG);
    fprintf(f, "<h2>%s</h2><div class=wrap><table><caption>%s</caption>", title, caption);
    fprintf(f, "<tr><th class=name>Example</th>");
    for (int k = 0; k < nb; k++)
    {
        char nameBuf[128]; const char *dispName = DisplayName(&bk[k], nameBuf, sizeof(nameBuf));
        if (dup) fprintf(f, "<th>%s (%s)</th>", dispName, bk[k].label);
        else fprintf(f, "<th>%s</th>", dispName);
    }
    if (nb == 2)
    {
        char nameBuf[128]; const char *dispName = DisplayName(&bk[1], nameBuf, sizeof(nameBuf));
        if (dup) fprintf(f, "<th>%s (%s) speedup</th>", dispName, bk[1].label);
        else fprintf(f, "<th>%s speedup</th>", dispName);
    }
    fprintf(f, "</tr>");

    // Rows follow the first backend's example order
    for (int i = 0; i < bk[0].exCount; i++)
    {
        const char *name = bk[0].ex[i].name;
        fprintf(f, "<tr><td class=name>%s</td>", name);

        // Gather values across backends for best/worst highlight
        double vals[MAX_BACKENDS], meds[MAX_BACKENDS]; bool has[MAX_BACKENDS];
        double best = 0; int bestSet = 0;
        double worst = 0; int worstSet = 0;
        for (int k = 0; k < nb; k++)
        {
            has[k] = false; vals[k] = 0; meds[k] = 0;
            for (int j = 0; j < bk[k].exCount; j++)
            {
                if (strcmp(bk[k].ex[j].name, name) == 0 && bk[k].ex[j].agg.valid)
                { vals[k] = MetricVal(&bk[k].ex[j].agg, m); meds[k] = bk[k].ex[j].agg.fmed; has[k] = true; break; }
            }
            if (has[k])
            {
                if (!bestSet || (HigherBetter(m) ? (vals[k] > best) : (vals[k] < best))) { best = vals[k]; bestSet = 1; }
                if (!worstSet || (HigherBetter(m) ? (vals[k] < worst) : (vals[k] > worst))) { worst = vals[k]; worstSet = 1; }
            }
        }
        for (int k = 0; k < nb; k++)
        {
            if (!has[k]) { fprintf(f, "<td class=na>&mdash;</td>"); continue; }
            const char *cls = (nb > 1 && vals[k] == best) ? "best" : (nb > 1 && vals[k] == worst) ? "worst" : "";
            double floorMs = BackendFloorMs(&bk[k]);
            bool atFloor = frameMetric && (floorMs > 0) && (meds[k] <= floorMs*1.15);
            fprintf(f, "<td class='%s'>", cls);
            fprintf(f, fmt, vals[k]);
            if (atFloor) fprintf(f, "&#176;");
            fprintf(f, "</td>");
        }
        // Two-backend comparisons get a speedup column: second backend relative to the
        // first (>1x = second faster/lighter). Ratios where either side sits at its floor
        // keep the degree mark - they compare present plumbing, not rendering.
        if (nb == 2)
        {
            if (has[0] && has[1] && (vals[0] > 0) && (vals[1] > 0))
            {
                double ratio = HigherBetter(m) ? (vals[1]/vals[0]) : (vals[0]/vals[1]);
                bool anyFloor = false;
                for (int k = 0; k < 2; k++)
                {
                    double floorMs = BackendFloorMs(&bk[k]);
                    if (frameMetric && (floorMs > 0) && (meds[k] <= floorMs*1.15)) anyFloor = true;
                }
                const char *cls = (ratio >= 1.05) ? "best" : (ratio <= 0.95) ? "worst" : "";
                fprintf(f, "<td class='%s'><b>%.2fx</b>%s</td>", cls, ratio, anyFloor ? "&#176;" : "");
            }
            else fprintf(f, "<td class=na>&mdash;</td>");
        }
        fprintf(f, "</tr>");
    }
    fprintf(f, "</table></div>");
}

// Mesa NIR->MSL override callout: emitted whenever an rlmtl_* variant column carries the
// precompiled-shader override set, so the reader knows which translation produced the
// mandelbulb row. Evidence: the 2026-08-18 10-cycle interleaved protocol (every column
// once per cycle, order rotated, mean of 10 samples) - mesa 231.6 sd14 vs GL 247.4 sd9 ms,
// a 6%% win beyond noise; earlier single-sample sessions swung between +23%% and parity
// with machine load, which is what motivated the interleaved-mean protocol.
static void WriteMesaOverrideCallout(FILE *f)
{
    fprintf(f, "<div class=callout><b>rlmtl_mesa = the native Metal backend + Mesa's shader compiler.</b> "
        "This column runs rlmtl's stock pipeline (GLSL &rarr; shaderc &rarr; SPIR-V &rarr; SPIRV-Cross &rarr; MSL) on every "
        "scene except <code>performance_stress_test_direct</code>, whose fragment shader is precompiled offline "
        "by <code>raylib/tools/nir2msl</code> through <b>Mesa's NIR&rarr;MSL compiler</b> (the KosmicKrisp backend's "
        "<code>kosmicomp</code>, MIT) and injected via <code>RLMTL_MSL_OVERRIDE</code>. On that scene SPIRV-Cross's "
        "flattened-SSA output defeats Metal's optimizer; Mesa's NIR pipeline is the only automatic GLSL translation "
        "measured faster than Apple's own GL compiler &mdash; 231.6&#8201;&plusmn;14 vs 247.4&#8201;&plusmn;9 ms over "
        "10 interleaved cycles (2026-08-18), ahead of hand-written MSL (250), ANGLE (281), Slang (289), naga (322) "
        "and stock SPIRV-Cross (442). "
        "Pixel drift vs SPIRV-Cross is ULP-class only (84%% of differing channels off by 1, max 17/255; Mesa lowers "
        "sin/cos to conformant polynomials).</div>");
}

static void WriteComparisonReport(BackendStats *bk, int nb, const char *outPath)
{
    char cmpPath[MAX_PATH_LEN];
    if (outPath) snprintf(cmpPath, sizeof(cmpPath), "%s", outPath);
    else snprintf(cmpPath, sizeof(cmpPath), "report_comparison_%s.html", bk[0].label);
    FILE *f = fopen(cmpPath, "w");
    if (!f) { printf("ERROR: cannot write %s\n", cmpPath); return; }

    fprintf(f, "<!doctype html><html><head><meta charset=utf-8><meta name=viewport content=\"width=device-width,initial-scale=1\"><title>raylib perf - comparison</title>%s</head><body>", CSS);
    fprintf(f, "<h1>raylib performance &mdash; backend comparison</h1>");
    bool dup = DuplicateBackendNames(bk, nb);
    fprintf(f, "<p class=env>");
    for (int k = 0; k < nb; k++)
    {
        char nameBuf[128]; const char *dispName = DisplayName(&bk[k], nameBuf, sizeof(nameBuf));
        if (dup) fprintf(f, "%s<b>%s</b> (%s, %s)", (k?" &nbsp;vs&nbsp; ":""), dispName, bk[k].label, bk[k].gpuDriver);
        else fprintf(f, "%s<b>%s</b>", (k?" &nbsp;vs&nbsp; ":""), dispName);
    }
    fprintf(f, " &nbsp;|&nbsp; <b>GPU</b> %s (%d MB) &nbsp;|&nbsp; <b>Driver</b> %s &nbsp;|&nbsp; <b>OS</b> %s &nbsp;|&nbsp; %d runs &times; %d ms full speed</p>",
        bk[0].gpu, bk[0].vramTotalMB, bk[0].gpuDriver, bk[0].osVersion, bk[0].runs, bk[0].durationMs);
    fprintf(f, "<p class=sub>Green = best backend for that example/metric, red = worst. Frame time &amp; FPS are the representative run; software (rlsw) has no GPU so its VRAM is ~0. Shader-heavy examples may not execute custom shaders on the software backend.</p>");
    { bool macosVk = false; for (int k = 0; k < nb; k++) if ((strstr(bk[k].os, "macOS") != NULL) && (strncmp(bk[k].backend, "rlvk", 4) == 0)) macosVk = true;
    fprintf(f, "<p class=sub><b>&#176; = at that backend's floor</b> (median within 15%% of its own bench_idle median): the scene finishes its real work faster than one present/loop turnaround, so the number measures presentation pacing or loop overhead, not rendering cost, and cross-backend ratios there compare present plumbing.%s</p>",
        macosVk ? " On macOS, Metal-backed Vulkan presents pace on drawable acquire (~1.6-1.8 ms) while GL's IOSurface flush has no floor - only rows without &#176; compare backend rendering cost." : ""); }
    // The drawable-acquire floor is a Vulkan-on-Metal property: rlgl presents via IOSurface
    // flush and rlmtl via its mailbox present thread, so the callout only applies when an
    // rlvk column is present.
    { bool macosVk = false; for (int k = 0; k < nb; k++) if ((strstr(bk[k].os, "macOS") != NULL) && (strncmp(bk[k].backend, "rlvk", 4) == 0)) macosVk = true; if (macosVk) WriteMacosFloorCallout(f); }
    { for (int k = 0; k < nb; k++) if (strcmp(bk[k].backend, "rlmtl_mesa") == 0) { WriteMesaOverrideCallout(f); break; } }
    if (nb == 2) { char nb0[128], nb1[128];
    fprintf(f, "<p class=sub><b>speedup</b> = %s relative to %s, per metric direction (&gt;1x favors %s); &#176; on a ratio means at least one side sits at its floor, so it compares present plumbing.</p>",
        DisplayName(&bk[1], nb1, sizeof(nb1)), DisplayName(&bk[0], nb0, sizeof(nb0)), nb1); }

    ComparisonTable(f, bk, nb, "Frames per second (higher is better)", "Sustained FPS at full speed (uncapped).", M_FPS, "%.0f");
    ComparisonTable(f, bk, nb, "Median frame time, ms (lower is better)", "Median per-frame CPU wall time.", M_MEDIAN, "%.3f");
    ComparisonTable(f, bk, nb, "Average frame time, ms (lower is better)", "Mean per-frame wall time.", M_AVG, "%.3f");
    ComparisonTable(f, bk, nb, "CPU utilization, %", "Average process CPU as percent of the whole machine.", M_CPU, "%.1f");
    ComparisonTable(f, bk, nb, "RAM, MB", "Average working-set memory.", M_RAM, "%.1f");
    ComparisonTable(f, bk, nb, "GPU VRAM, MB", "Average per-process video memory.", M_VRAM, "%.1f");

    fprintf(f, "</body></html>");
    fclose(f);
    printf("  wrote %s (%d backends)\n", cmpPath, nb);
}

//----------------------------------------------------------------------------------
int main(int argc, char **argv)
{
    const char *defaults[4] = { "performance_rlgl.ini", "performance_rlsw.ini", "performance_rlvk.ini", "performance_rldx12.ini" };
    const char **configs; int nCfg;
    const char *outPath = NULL;    // -o <file>: comparison report path (default report_comparison_<label>.html)
    if ((argc > 2) && (strcmp(argv[1], "-o") == 0)) { outPath = argv[2]; argv += 2; argc -= 2; }
    if (argc > 1) { configs = (const char **)&argv[1]; nCfg = argc - 1; }
    else          { configs = defaults; nCfg = 4; }

    static BackendStats bk[MAX_BACKENDS];
    int nb = 0;

    printf("Performance report\n");
    for (int i = 0; (i < nCfg) && (nb < MAX_BACKENDS); i++)
    {
        FILE *cf = fopen(configs[i], "rb");
        if (!cf) { printf("  skip %s (no config)\n", configs[i]); continue; }
        fclose(cf);
        LoadBackend(configs[i], &bk[nb]);
        // Require at least one example with data
        int withData = 0; for (int e = 0; e < bk[nb].exCount; e++) if (bk[nb].ex[e].agg.valid) withData++;
        if (withData == 0) { printf("  skip %s (no captures in %s)\n", bk[nb].backend, bk[nb].dir); continue; }
        WriteBackendReport(&bk[nb]);
        nb++;
    }

    if (nb == 0) { printf("No backend captures found. Run performance_capture first.\n"); return 1; }
    WriteComparisonReport(bk, nb, outPath);
    printf("Done.\n");
    return 0;
}
