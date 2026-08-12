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
or shader-ULP tie-break, catalogued per scene in `image_comparison_rlvk_macos_moltenvk.ini`.

The same gate on **rlmtl**, the native Metal backend (2026-08-12, clean-room fresh capture,
`image_comparison_rlmtl_macos.ini`): **461 bit-exact + 151 tolerated + 0 fail** — the most
bit-exact hardware backend of the three (rlmtl 461 > KosmicKrisp 459 > MoltenVK 457). One
rlmtl-specific allowance (a 1px DrawLineEx band tie-break scene) documented in the config.

The same gate on **Mesa's KosmicKrisp** ICD (2026-08-11, Mesa 26.2.0,
`image_comparison_rlvk_macos_kosmickrisp.ini`, same baseline and allowances): **459 bit-exact
+ 150 tolerated + 0 fail** — slightly *more* bit-exact than MoltenVK. Two documented
exclusions: `models_point_rendering` (KosmicKrisp lacks `fillModeNonSolid`; rlvk's documented
fallback renders point mode filled) and `core_directory_files` (stale-baseline drift, proven
by a fresh MoltenVK≡KosmicKrisp bit-identical control).

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
the macOS reports are committed; on macOS the rlvk label names the Vulkan-on-Metal ICD —
`report_rlvk_macos_moltenvk.html`, `report_comparison_macos_moltenvk.html` — while rlgl keeps
`report_rlgl_macos_apple.html`).

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

Current state, **macOS / Apple M5** (2026-08-11/12, MoltenVK 1.4.2 vs KosmicKrisp Mesa 26.2.0,
both backends at plain -O2 — Apple clang has no gcda-flow PGO — acquire-late present chain,
one machine-state window): the story is gated by **present floors** — on a composited window,
MoltenVK's image acquire is a blocking Metal drawable request that no MoltenVK knob removes
(~1.8 ms; 3 images measured worse; fast-math and present-mode configs are no-ops), while macOS
GL presents through an IOSurface flush with no floor at all. KosmicKrisp's IMMEDIATE paces even
worse (~3.3 ms), but Mesa ships the knob MoltenVK lacks: `MESA_VK_WSI_PRESENT_MODE=mailbox`
presents through Mesa's own thread and drops the floor to **~1.60 ms, below MoltenVK's**. With
that knob, **KosmicKrisp ≥ MoltenVK on 17 of 19 scenes**: the fragment-ALU class turns from
rlvk's one real macOS loss into near-parity with GL (raymarching stress 239 vs MoltenVK's 326
vs GL's 212 ms — Mesa's NIR→MSL beats the glslang→SPIRV-Cross→MSL double translation), and the
scenes where MoltenVK paid extra present cost (models_loading, skybox, maze, camera_free)
flatten to the floor for ~1.8–2.1× gains. MoltenVK still wins the mixed-batch stress scene
(5.10 vs 6.71 ms). Sub-floor scenes measure present semantics, not backend cost — the macOS
analogue of the Linux µs-class CHECK-us policy. Where real work exceeds the floor, rlvk leads
GL on either ICD: **4–4.8×** on 8000 draw calls, **1.8–2.3×** on the mixed stress scene,
**1.4–1.6×** on waving cubes. RAM is comparable (KosmicKrisp ~8–9 MB above MoltenVK);
per-process VRAM reports 0 on both backends by design (Apple Silicon unified memory has no
per-process VRAM metric).

The shader-language campaign (2026-08-12, round 2) closed the last loss:
`report_comparison_macos_rlmtl_languages.html` carries four columns — rlgl | rlmtl (GLSL) |
rlmtl_slang | rlmtl_msl. The GLSL column wins 17/19 outright plus raymarching at parity-win
(verified by cool isolated A/B; multi-leg campaigns thermally skew fragment-heavy scenes);
the mandelbulb falls to the language ladder: **Slang 83 ms and handwritten MSL 74 ms vs
rlgl's 245 ms** (the Slang variant is pixel-identical to the GLSL path — probed at two
camera positions; sources in `performance/shader_overrides/`, injected per shader via
`RLMTL_MSL_OVERRIDE`, keyed by SPIR-V hash). Net: **rlmtl ≥ rlgl on all 19 scenes.** The
GLSL translation loss mechanism is SPIRV-Cross's flattened-SSA MSL defeating Metal's
optimizer (a clean translation of identical rolled loops runs 1.5x faster); slangc's GLSL
mode cannot compile stock GLSL matrix ops to Metal, hence the Slang-language port.

Current state, **macOS / Apple M5 / rlmtl (native Metal)** (2026-08-12, same-window
three-leg campaign): **rlmtl beats rlgl on 18 of 19 scenes** — 6–26x on light scenes
(mailbox present thread + frame ring depth 3 remove every presentation and ring stall;
bench_idle 0.013 vs 0.136 ms), 19.8x on 8000 draw calls, 33.5x on instancing, 5.8x waving
cubes, 2.3x mixed stress. The one loss is the fragment-ALU mandelbulb (0.65x): SPIRV-Cross
keeps its multi-exit raymarch loop rolled where Mesa's NIR unrolls it (verified against
KosmicKrisp's MSL dump); measured not app-addressable through shaderc+SPIRV-Cross. RAM:
rlmtl wins the light half (86–88 vs 92 MB), carries a few MB extra on shader-heavy scenes.

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
