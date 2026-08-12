# raylib_tests

Test harnesses for raylib's interchangeable graphics backends — **rlgl** (OpenGL 3.3, the
default), **rlsw** (CPU software rasterizer), and **rlvk** (Vulkan 1.3). Two complementary
suites answer the two questions that matter for a backend: *does it render the same pixels?*
and *how fast does it do it?*

Both suites expect the **raylib repo as a sibling** (`../raylib`) and drive its stock examples
as test scenes; measurement hooks live inside raylib behind opt-in compile flags, so the
examples themselves are unmodified.

## [`image_equivalence/`](image_equivalence/README.md) — does it render the same pixels?

Deterministic screenshot capture and comparison. raylib is built with
`DETERMINISTIC_IMAGE_COMPARISON_CAPTURE`, which makes rendering a pure function of the frame
counter (fixed virtual clock, fixed RNG seed, neutralized input); each example is captured at
fixed frames (0, 10, 30) and diffed against a committed baseline — the GL backend's output for
rlvk, its own prior output for rlsw. Exact by default, with small documented per-scene
allowances where GPU rounding makes bit-exactness impossible. Output: per-scene PASS/FAIL and
an HTML visual diff report.

Current state: the Vulkan backend passes the full suite against the GL baseline
(539 bit-exact + 86 within allowances of 646 scenes, the rest excluded by design; aliased
rendering is bit-exact, MSAA scenes carry measured AA-edge allowances since rlvk uses
standard Vulkan MSAA rather than replicating GL's sample pattern and resolve).

macOS gate (2026-08-11, Apple M5 / MoltenVK 1.4.2, vs a same-machine Apple GL 4.1 baseline in
`rlgl_baseline_macos/` — cross-driver variance exceeds cross-backend variance, so committed
baselines from other machines are never compared against): **457 bit-exact + 158 within
measured allowances + 0 fail** of 637 frames; 22 skipped by design (wall-clock/live-audio
scenes plus compute, which has no GL reference on macOS — Apple GL tops out at 4.1). Both
backends are capture-deterministic on this machine (GL-vs-GL and rlvk-vs-rlvk controls
bit-exact at tolerance 0); every tolerated diff is a stable Apple-GL-vs-Metal rasterization
or shader-ULP tie-break, catalogued per scene in `image_comparison_rlvk_macos.ini`.

Two run modes: the **full suite** (all built examples — the merge gate) and a **regression
subset** (`bash image_equivalence/run_regression_rlvk.sh`, ~37 scenes covering every backend
code path, ~2 min) for the inner development loop — see the suite README.

## [`performance/`](performance/README.md) — how fast does it do it?

Full-speed frame-time and resource benchmarking. raylib is built with `PERFORMANCE_CAPTURE`,
which neutralizes the frame cap / vsync / present-sync and self-measures every frame: frame
time (min / median / p95 / p99 / max, sustained FPS), CPU utilization, RAM working set, and
per-process VRAM. A curated set of 19 scenes (real examples plus purpose-built benches:
idle overhead, 8000 draw calls, instancing, fragment-bound stress) runs 3×10 s per backend,
all backends captured back-to-back in one session so machine-state drift cannot bias the
comparison. Output: one HTML report per backend plus a cross-backend comparison, stamped with
OS, GPU, and driver provenance (`report_*_windows_nvidia.html`, `report_*_linux_amd.html` and
`report_*_macos_apple.html` are committed).

Current state, **Windows / RTX 4090 / NVIDIA 595.97**: rlvk leads rlgl on 17 of 19 scenes
(1.5×–7.5×); the two fragment-ALU-saturated scenes measure at parity (a ~2% NVIDIA
driver-codegen residual, smaller than run noise).

Current state, **Linux / RX 7900 XTX / Mesa RADV 26.1.6** (2026-08-04, includes the merged-draw
path, spirv-opt, the -O2 default and the static quad-index buffer; both backends measured at
the same flags): rlvk leads on 15 of 19 scenes, topping out at 6.4× (8000 draw calls — 2.8×
before draw merging and the -O2 ship), 2.3× (waving cubes) and 1.9× (mixed stress).
Raymarching is 1.00× — the fragment-ALU-saturated class that also ties on NVIDIA. The four
sub-parity scenes are all in the microsecond class (50–115 µs) where this suite caps verdicts
at CHECK-us: an identical-build bisect measured a 2× swing on skybox between capture windows
(0.049 vs 0.106 ms, same commit, same flags), so cross-campaign µs-scene movements — in either
direction — are window state, not code; only the same-window ratios are meaningful. Memory inverts the NVIDIA result: rlvk
uses ~33% less RAM (116 vs 172 MB typical) and less VRAM on nearly every scene, because Mesa's
Vulkan runtime is lighter than its GL one, whereas NVIDIA's is heavier. Same-window capture,
XWayland present path.

Current state, **macOS / Apple M5 / MoltenVK 1.4.2** (2026-08-11, both backends at plain -O2 —
Apple clang has no gcda-flow PGO — with the acquire-late present chain): the story is gated by
a **~1.8 ms Metal present floor** — on a composited window, MoltenVK's image acquire is a
blocking Metal drawable request that no knob removes (3 images measured worse; fast-math and
present-mode configs are no-ops), while macOS GL presents through an IOSurface flush with no
floor at all. 14 of 19 scenes complete their real work under that floor on one or both
backends, so their ratios measure present semantics, not backend cost — the macOS analogue of
the Linux µs-class CHECK-us policy, with a 10× higher bar. Where real work exceeds the floor,
rlvk leads: **4.6×** on 8000 draw calls, **2.3×** on the mixed stress scene, **1.7×** on waving
cubes. The fragment-ALU-saturated class loses ~1.6× (raymarching stress: 356 vs 223 ms):
the GLSL→SPIR-V→MSL double translation loses to Apple's direct GL compiler on branchy
raymarch loops — the same class that ties on NVIDIA/AMD, amplified by the extra translation
hop; shaderc's optimizer is already on and MoltenVK's fast-math is already default. RAM is
comparable (rlvk a few MB higher on texture-heavy scenes); per-process VRAM reports 0 on both
backends by design (Apple Silicon unified memory has no per-process VRAM metric).

Two run modes here as well: the **full suite** (`run_all.sh`, all scenes × all backends in one
machine-state window — the committed record) and a **regression subset**
(`bash performance/run_regression_rlvk.sh`, 6 scenes compared against the committed
same-machine rlvk capture with WARN/FAIL thresholds) — see the suite README for the
cross-window caveats.

## Layout

```
src/                image-equivalence tools (C99, link raylib)
image_equivalence/  configs, committed baselines (git LFS), candidate captures, reports
performance/        performance tools + configs, committed HTML reports, capture data
```
