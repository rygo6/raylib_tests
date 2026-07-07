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
OS, GPU, and driver provenance (`report_*_windows_nvidia.html` are committed).

Current state: rlvk leads rlgl on 17 of 19 scenes (1.5×–7.5×); the two fragment-ALU-saturated
scenes measure at parity (a ~2% NVIDIA driver-codegen residual, smaller than run noise).

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
