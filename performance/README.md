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

VRAM is read through DXGI `QueryVideoMemoryInfo`, which is backend-agnostic — OpenGL, Vulkan, and
the software renderer are all measured identically (software naturally reports ~0 VRAM). All
Windows measurement entry points are resolved dynamically, so example link lines are unchanged.

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

The **raylib repo as a sibling** (`../raylib`), MinGW-w64 gcc, and — for rlvk — the Vulkan SDK
with `VULKAN_SDK` set. Backends share one `libraylib.a`, so each is built and captured fully
before the next.

## Build the tools

```sh
cd src && mingw32-make      # -> performance_capture, performance_report
```

## Run everything

```sh
bash run_all.sh             # builds + captures rlgl, rlsw, rlvk, then writes all reports
```

## Run one backend

```sh
bash build_backend.sh rlvk                              # build raylib(rlvk) + curated examples
./src/performance_capture.exe rlvk performance_rlvk.ini  # 3 x 10 s per example -> rlvk/
./src/performance_report.exe                             # (re)generate all reports found
```

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

## Multi-machine data (platform x vendor labelling)

Outputs are labelled `<os>_<vendor>` so results from different machines coexist and compare:
`rlvk_windows_nvidia/`, `report_rlgl_linux_amd.html`, `report_comparison_windows_amd.html`, etc.

The label resolves as (both tools agree, so one machine is self-consistent):
1. `RAYLIB_PERF_LABEL` env var (explicit; **use this on Linux/CI**)
2. `label` key in the backend `.ini`
3. auto — `<os>` from the build target, `<vendor>` from the GPU name (NVIDIA/AMD/Intel).
   GPU detection is via DXGI on Windows; on Linux set `RAYLIB_PERF_LABEL` (e.g. `linux_amd`).

The four target combinations are `windows_nvidia`, `windows_amd`, `linux_nvidia`, `linux_amd`.
On each machine just run `run_all.sh` (or set `RAYLIB_PERF_LABEL` first on Linux); the labelled
trees and reports can be committed side by side for cross-platform/vendor comparison.

> NOTE: Linux resource sampling (CPU/RAM/VRAM) in `rcore_performance_capture.c` is currently
> Windows-only; frame-time still works everywhere. Linux VRAM/CPU/RAM sampling is TODO.

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
