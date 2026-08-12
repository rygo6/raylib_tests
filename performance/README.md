# raylib_tests — performance

Full-speed frame-time and resource benchmarking for the three raylib graphics backends:

| Backend | Build define | What it is |
|---------|--------------|------------|
| **rlgl** | `GRAPHICS_API_OPENGL_33` | OpenGL 3.3 (the default hardware backend) |
| **rlsw** | `GRAPHICS_API_OPENGL_SOFTWARE` | CPU software rasterizer (`external/rlsw.h`) |
| **rlvk** | `GRAPHICS_API_VULKAN_14` | Vulkan 1.3 (`rlvk.h`; the selector keeps its historical _14 name) |

Each curated example is opened and run **at full speed** (frame cap / vsync / present-sync all
neutralized) for `duration_ms` (default 10 s), repeated `runs` times (default 3). Every run the
example self-measures and writes:

- **frame time** — min / max / median / average (+ p95 / p99), and sustained FPS
- **CPU** — average and peak process utilization (% of the whole machine)
- **RAM** — average and peak working set
- **GPU VRAM** — average and peak per-process video memory

The result is one HTML report per backend plus a collated cross-backend comparison.

## How it works

Measurement lives inside raylib behind an opt-in compile flag, `PERFORMANCE_CAPTURE` (the
performance sibling of `DETERMINISTIC_IMAGE_COMPARISON_CAPTURE`):

- `../raylib/src/rcore_performance_capture.{h,c}` — the hook. Compiled as its own translation
  unit so it can use `<windows.h>`/`<dxgi1_4.h>` without colliding with raylib's own symbol
  redefinitions in `rcore.c`. `PerfCapture_Tick()` is called once per frame from `EndDrawing()`.
- `rcore.c` — under `PERFORMANCE_CAPTURE`: skips the frame wait, strips `FLAG_VSYNC_HINT`,
  unfocuses the window, and calls `PerfCapture_Tick()`.
- `rlvk.h` — under `PERFORMANCE_CAPTURE`: selects an uncapped present mode (IMMEDIATE, else
  MAILBOX) instead of the vsync-locked FIFO default.

Every metric source is backend-agnostic — OpenGL, Vulkan and the software renderer are measured
identically (software naturally reports ~0 VRAM):

| | Windows | Linux | macOS |
|---|---|---|---|
| clock | `QueryPerformanceCounter` | `clock_gettime(CLOCK_MONOTONIC)` | `clock_gettime(CLOCK_MONOTONIC)` |
| CPU | `GetProcessTimes` | `/proc/self/stat` utime+stime | `getrusage(RUSAGE_SELF)` |
| RAM | psapi working set | `/proc/self/statm` resident pages | mach `task_info` resident set |
| VRAM | DXGI `QueryVideoMemoryInfo` (local segment) | DRM `fdinfo` `drm-resident-vram`, summed over the process's DRM clients | 0 by design (unified memory has no per-process VRAM metric; both backends report the same 0) |

All Windows measurement entry points are resolved dynamically and the Linux ones are plain `/proc`
reads, so example link lines are unchanged on both.

## Layout

```
src/
  performance_capture.c   run each example RUNS times, collect run_<n>.rini (no raylib dependency)
  performance_report.c    aggregate captures -> per-backend + comparison HTML
  Makefile, rini.h
performance_rlgl.ini      per-backend config: example list, duration, runs, output dir
performance_rlsw.ini
performance_rlvk.ini
performance_rlvk_regression.ini  6-scene rlvk regression subset (see "Regression subset" below)
build_backend.sh          build raylib (a backend) + the curated examples with PERFORMANCE_CAPTURE
run_all.sh                build + capture all three backends, then generate every report
run_regression_rlvk.sh    build rlvk + capture the regression subset + compare vs the last local campaign
regression_compare.sh     flag frame-time/RAM/VRAM regressions between two same-backend captures
rlgl/ rlsw/ rlvk/         per-run captures: <example>/run_<n>.rini + environment.rini  (not committed)
report_rlgl.html          per-backend reports (not committed)
report_rlsw.html
report_rlvk.html
report_comparison.html    collated cross-backend comparison (not committed)
```

## Requirements

The **raylib repo as a sibling** (`../raylib`) and gcc — MinGW-w64 on Windows, the system gcc on
Linux, Apple clang on macOS. For rlvk: the Vulkan loader plus headers (Windows: the SDK with
`VULKAN_SDK` set; Linux: `vulkan-headers` and a driver ICD; macOS: Homebrew `molten-vk` +
`vulkan-loader` + `vulkan-headers`), and `shaderc` at run time for custom-shader scenes. Backends
share one `libraylib.a`, so each is built and captured fully before the next.

## macOS

Runs with the stock scripts: `bash run_all.sh rlgl rlvk` (rlsw untested there). Prerequisites are
Homebrew `molten-vk`, `vulkan-loader`, `vulkan-headers` and `shaderc`; the label auto-resolves to
`macos_apple` through a Vulkan probe (the probe enables `VK_KHR_portability_enumeration`, or the
device would be invisible). Because macOS can host more than one Vulkan-on-Metal ICD (MoltenVK,
Mesa's KosmicKrisp), **rlvk captures pin the ICD in the label**: `performance_rlvk_moltenvk.ini`
and `performance_rlvk_kosmickrisp.ini` set `label macos_moltenvk` / `macos_kosmickrisp`, and the
ICD is selected at run time with `VK_DRIVER_FILES=<icd.json>` (MoltenVK:
`/opt/homebrew/etc/vulkan/icd.d/MoltenVK_icd.json`; KosmicKrisp:
`/opt/homebrew/opt/mesa/share/vulkan/icd.d/kosmickrisp_mesa_icd.aarch64.json`). rlgl does not go
through a Vulkan ICD, so its captures keep the machine label `macos_apple`. Builds run at plain `-O2` — Apple clang has no gcda-flow PGO, so
`build_backend.sh` forces the no-PGO path and both backends stay at identical flags. Keep the
display awake (`caffeinate -dimsu`) for unattended runs: display sleep fails GLFW window creation.

### The ~1.8 ms Metal present floor — read this before interpreting macOS numbers

On a composited macOS window, presenting through Metal is paced by CoreAnimation handing out
drawables: the wait surfaces in MoltenVK as **"Retrieve a CAMetalDrawable"** and measures ~1.8 ms
per frame at uncapped rates, no matter how little the frame draws. This is *not* MoltenVK being
conservative — its own instrumentation shows it already defers the drawable request maximally
(it happens inside the submit's encode, exactly when the present blit needs the drawable's
texture), and every native Metal app pays the same pacing. macOS **GL never pays it**: it
presents by flushing an IOSurface with no drawable handshake, which is why rlgl can report
7000+ FPS on scenes that draw almost nothing.

Diagnosis is reproducible with MoltenVK's built-in tracking:

```sh
MVK_CONFIG_PERFORMANCE_TRACKING=1 MVK_CONFIG_PERFORMANCE_LOGGING_FRAME_COUNT=256 MVK_CONFIG_LOG_LEVEL=3 ./bench_idle    # look for "Retrieve a CAMetalDrawable"
```

Config surface exhausted (all A/B'd on bench_idle, none moved the floor): 3 swapchain images
(measured WORSE — 2.08 vs 1.81 ms), `MVK_CONFIG_FAST_MATH_ENABLED`,
`MVK_CONFIG_PRESENT_WITH_COMMAND_BUFFER`, `MVK_CONFIG_SYNCHRONOUS_QUEUE_SUBMITS` (both values),
`MVK_CONFIG_MAX_ACTIVE_METAL_COMMAND_BUFFERS_PER_QUEUE`. Only not presenting at all
(offscreen/present-skip) escapes it.

rlvk's mitigation: the backend acquires its swapchain image in the **present chain**, not at
frame begin (the frame renders into an intermediate; only the final flip blit needs the
swapchain image), so the drawable wait overlaps the frame's CPU recording instead of adding to
it — that change alone took bench_instanced from 3.40 to 1.85 ms. What remains is the floor
itself: **any scene whose real work is under ~1.8 ms measures present pacing, not backend
cost** — the macOS analogue of the Linux µs-class CHECK-us policy, with a 10× higher bar.

### KosmicKrisp: the same floor is driver-specific — and Mesa has the knob

Mesa's KosmicKrisp (conformant Vulkan 1.4 on Metal 4, Homebrew `mesa` on macOS 26+) advertises
IMMEDIATE but its IMMEDIATE still paces on drawable acquire — **~3.3 ms** at bench_idle, worse
than MoltenVK's 1.8. The fix is Mesa's official WSI override:

```sh
MESA_VK_WSI_PRESENT_MODE=mailbox    # REQUIRED for full-speed capture on KosmicKrisp
```

Mesa's mailbox path presents through its own thread, and the floor collapses to **~1.60 ms —
below MoltenVK's** (bench_idle 3.13 → 1.60 ms, bench_drawcalls 3.41 → 1.62 ms). MoltenVK
advertises no MAILBOX and has no equivalent override, so its ~1.8 ms floor stands. The mode is
not advertised by the driver but is the same uncapped-presentation semantics class the capture
hook already targets (its own fallback order is IMMEDIATE, else MAILBOX).

### Results (2026-08-11/12, Apple M5, MoltenVK 1.4.2 vs KosmicKrisp Mesa 26.2.0, one machine-state window)

Sustained-FPS medians; KosmicKrisp captured with the mailbox knob. Scenes at ~1.60 ms on
KosmicKrisp / ~1.8 ms on MoltenVK sit on the respective present floor.

| scene | rlgl | rlvk (MoltenVK) | rlvk (KosmicKrisp) | verdict |
|---|---|---|---|---|
| bench_drawcalls (8000 draws) | 120 fps | 474 fps | **581 fps** | rlvk 4–4.8× over GL; both ICDs floor-bound |
| performance_stress_test | 84 fps | **195 fps** | 149 fps | rlvk wins; **MoltenVK 1.3× over KK** (the one KK loss) |
| models_waving_cubes | 378 fps | 544 fps | **595 fps** | rlvk 1.4–1.6× over GL |
| shaders_raymarching_rendering | 319 fps | 156 fps | **260 fps** | KK 1.7× over MoltenVK (fragment-ALU class) |
| performance_stress_test_direct | 211.8 ms | 325.5 ms | **239.4 ms** | KK 1.36× over MoltenVK, near-closes the GL gap |
| bench_instanced | 2344 fps | 362 fps | **497 fps** | KK 1.41× over MoltenVK |
| models_loading / skybox / maze / camera_free | (sub-floor GL) | 289–353 fps | **524–564 fps** | KK ~1.8–2.1× — MoltenVK pays extra present cost here, KK mailbox flattens all to the 1.60 floor |
| remaining light scenes | (sub-floor) | ~460–544 fps | ~460–590 fps | both at their floors — present pacing, not backend cost |

Net: with the mailbox knob, **KosmicKrisp ≥ MoltenVK on 17 of 19 scenes**. The fragment-ALU
class (`stress_test_direct`, raymarching) is where the ICDs genuinely differ in code
generation: Mesa's NIR→MSL compiler beats the glslang→SPIRV-Cross→MSL double translation
(325.5 → 239.4 ms; Apple's direct GL compiler still holds 211.8). The one KK loss is
`performance_stress_test` (mixed heavy batch volume, 6.71 vs 5.10 ms). Costs: KosmicKrisp
carries ~8–9 MB more fixed process RAM (210 vs 160 MB peak on stress_test). KosmicKrisp lacks
`fillModeNonSolid` (wire/point polygon modes render filled — documented rlvk fallback); its
image-equivalence gate is otherwise GREEN and slightly *more* bit-exact than MoltenVK
(459 vs 457 of 637 frames).

## Build the tools

```sh
cd src && make              # -> performance_capture, performance_report  (mingw32-make on Windows)
```

## Run everything

```sh
bash run_all.sh             # builds + captures rlgl, rlsw, rlvk, then writes all reports
bash run_all.sh rlgl rlvk   # or just the backends you name, still one machine-state window
```

## Run one backend

```sh
bash build_backend.sh rlvk                          # build raylib(rlvk) + curated examples
./src/performance_capture rlvk performance_rlvk.ini  # 3 x 10 s per example -> rlvk_<label>/
./src/performance_report                             # (re)generate all reports found
```

(Add `.exe` to the tool names on Windows.)

`performance_report` with no arguments reads the three standard configs and skips any backend
that has no captures, so you can report on whatever subset you have run.

## Regression subset vs full suite

Two ways to answer "did my rlvk change cost performance?":

- **Full suite** (the reference record): `run_all.sh` — all 19 curated scenes on all three
  backends, back-to-back in one machine-state window, producing the labelled capture trees
  (local artifacts; only the HTML reports are committed) and the cross-backend reports. Use it
  for milestone numbers and any rlgl-vs-rlvk claim.
- **Regression subset** (the inner development loop, ~5 min):

  ```sh
  bash run_regression_rlvk.sh
  ```

  Builds rlvk, captures the 6 scenes of `performance_rlvk_regression.ini` (each guarding a
  distinct cost center: idle overhead, draw-call/pipeline-bind cost, instancing, 2D batch
  volume, fragment-bound present cost, a real mixed 3D scene; same 3 x 10 s methodology), then
  runs `regression_compare.sh` against this machine's last full-suite rlvk capture
  (`rlvk_<label>/`). Per scene it compares the representative median frame time (median of run
  medians, matching `performance_report`) plus peak RAM/VRAM: > +10% median frame time or
  > +15% memory = WARN, > +25% frame time = FAIL (non-zero exit).

**Interpret with care:** the baseline was captured in a *different machine-state
window* than your candidate, so WARN-level deltas are noise candidates — re-measure the flagged
scene back-to-back against a fresh build of the pre-change commit before concluding anything.
Microsecond-scale scenes (baseline median < 0.15 ms) drift by tens of percent *between windows*
(+38% was measured on an unmodified build), so their frame-time verdicts are capped at a
non-fatal `CHECK-us` — only a same-window A/B can pass judgment on them. Heavier scenes'
FAIL-level deltas have so far always been real. Regression captures
(`rlvk_regression_<label>/`) are gitignored; never commit them or mix them into cross-backend
reports.

## Build flags: what the numbers are measured at

The lib builds at `-O2` (raylib upstream ships `-O1` on desktop; this fork changed it after a
measured A/B - see the src/Makefile comment). Two further rungs are measured and available as
an opt-in campaign configuration, but are NOT the committed-report default:

| config (RX 7900 XTX / RADV, medians) | drawcalls | stress | cubes | notes |
|---|---|---|---|---|
| `-O1` (old upstream default) | 1.294 | 8.53 | 1.042 | |
| `-O2` (the shipped default)  | 0.842 | 7.65 | 0.891 | bit-identical output, full-suite verified |
| `-O2 -flto`                  | 0.623 | 7.32 | 0.926 | cross-TU inlining; +4% on batch fill |
| `-O2 -flto` + PGO            | 0.640 | **7.16** | **0.884** | PGO fixes LTO's batch regression; THE CAMPAIGN CONFIG since 2026-08-04 |

Recipe for the LTO+PGO build (train on the curated scenes, then rebuild with the profile):

```sh
# 1. instrumented:  CUSTOM_CFLAGS='-O2 -flto -ffat-lto-objects -fprofile-generate -fprofile-update=atomic'
# 2. run the curated scenes once (profiles land as src/*.gcda)
# 3. optimized:     CUSTOM_CFLAGS='-O2 -flto -ffat-lto-objects -fprofile-use -fprofile-correction'
#    (link examples with -flto both times; plain ar works thanks to -ffat-lto-objects)
```

The committed reports use the LTO+PGO config since 2026-08-04 (user policy decision;
build_backend.sh implements the two-pass build for every backend uniformly, and
RAYLIB_PERF_NO_PGO=1 falls back to plain -O2). The default -O2 remains what a plain
`make` user gets; the reports measure the tuned campaign configuration.
CAVEAT: changing CUSTOM_CFLAGS does NOT invalidate make's object cache - always `rm -f *.o
libraylib.a *.gcda` in src/ when flags change, or the archive silently mixes codegen.

## Multi-machine data (platform x vendor labelling)

Outputs are labelled `<os>_<vendor>` so results from different machines coexist and compare:
`rlvk_windows_nvidia/`, `report_rlgl_linux_amd.html`, `report_comparison_windows_amd.html`, etc.

The label resolves as (both tools agree, so one machine is self-consistent):
1. `RAYLIB_PERF_LABEL` env var (explicit override)
2. `label` key in the backend `.ini`
3. auto — `<os>` from the build target, `<vendor>` from the GPU name (NVIDIA/AMD/Intel).
   GPU detection is via DXGI on Windows and a `dlopen`'d Vulkan probe on Linux, so both platforms
   auto-resolve; the probe also supplies the adapter name, driver version and VRAM total that the
   reports print as provenance.

The target combinations are `windows_nvidia`, `windows_amd`, `linux_nvidia`, `linux_amd`, and
`macos_apple` (the probe opts into portability enumeration, so a portability device is visible).
On macOS, rlvk results additionally pin the Vulkan-on-Metal ICD in the label —
`macos_moltenvk` / `macos_kosmickrisp` (see the macOS section above). On each machine just run
`run_all.sh`; the labelled trees and reports can be committed side by side for
cross-platform/vendor comparison.

## Notes

- The example set is **curated** (`examples` in each `.ini`): heavy, input-independent scenes
  that render real work every frame so a full-speed window is representative. Edit the `examples`
  line (and the matching array in `build_backend.sh`) to change it.
- Frame time is measured `EndDrawing`-to-`EndDrawing` (the whole update + draw + present loop) on
  the CPU. The first `warmup_ms` (default 500) is excluded so shader compiles / texture uploads
  don't skew the numbers.
- Shader-heavy examples may not execute their custom GLSL on the **software** backend; treat
  those rows as "does it still run / at what cost", not as identical GPU work across backends.
- Captures and reports are generated artifacts and are gitignored.
