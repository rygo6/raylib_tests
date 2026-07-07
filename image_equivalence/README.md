# raylib_tests — deterministic image-equivalence tests

Reproducible, cycle-based screenshot capture and comparison for raylib examples. Renders a set
of examples to fixed frames (0, 10, 30) and diffs a candidate set against a committed baseline,
so rendering regressions (or differences between graphics backends) are caught deterministically.

## Layout

```
../src/                               C99 tools (link raylib) + rini.h (vendored config lib)
  image_comparison_capture.c          run examples, capture frames + record environment.rini
  image_comparison_diff.c             compare candidate vs baseline, print PASS/FAIL, write report.html
  Makefile
./ (this folder)
  image_comparison_rlvk.ini           Vulkan backend vs rlgl_baseline (allowances for expected variability)
  image_comparison_rlvk_regression.ini  ~37-scene regression subset of the rlvk suite (see below)
  run_regression_rlvk.sh              one-command build + capture + diff of the regression subset
  image_comparison_rlsw.ini           software renderer vs its own rlsw_baseline (bit-exact)
  rlgl_baseline/<example>/frame_00NN.png  committed GL reference frames (+ environment.rini provenance)
  rlsw_baseline/<example>/frame_00NN.png  committed software-renderer reference frames
  rlvk/, rlsw_candidate/               per-run candidate captures (not committed)
  report_rlvk.html, report_rlsw.html   per-backend HTML reports (not committed)
```

PNGs are stored via **git LFS** (see `.gitattributes`).

## Requirements

This repo expects the **raylib repo as a sibling of the repo root** (`../../raylib` from this
folder) built with the deterministic capture flag, which makes rendering a pure function of the
frame counter (fixed 60 fps virtual clock, fixed RNG seed, neutralized mouse input, in-engine
screenshot+exit hook):

```sh
# In the raylib repo:
cd ../../raylib/src
make CC='gcc -pipe -DDETERMINISTIC_IMAGE_COMPARISON_CAPTURE'          # deterministic libraylib.a
cd ../examples && make CC='gcc -pipe'                                 # build examples against it
```

## Build the tools

```sh
cd ../src && make         # -> image_comparison_capture, image_comparison_diff
```

## Run

```sh
# From this folder:
../src/image_comparison_capture baseline    # (re)generate the baseline set + environment.rini
../src/image_comparison_capture rlgl        # capture a candidate set
../src/image_comparison_diff                # compare -> PASS/FAIL per scene + report.html
```

`image_comparison_diff` exits 0 when everything matches, else 2. Open `report.html` for a visual
diff (reference / candidate / amplified difference per mismatch).

## Regression subset vs full suite

Two ways to run the rlvk comparison:

- **Full suite** (the merge gate): build **all** examples and run with `image_comparison_rlvk.ini`
  as shown above — every baseline scene is compared (~646 scenes, ~15 min including builds).
- **Regression subset** (the inner development loop): a curated ~37-scene subset chosen so every
  backend code path (batching, line raster, MSAA/sample locations, scissor, render textures, MRT,
  readback, blend modes, pixel formats, cubemaps, GPU skinning, CPU-anim buffer updates,
  instancing, multi-texture push descriptors, depth passes, stereo, uniform UBOs) is exercised at
  least once — a change that breaks a path fails here in ~2 minutes:

  ```sh
  bash run_regression_rlvk.sh     # build rlvk lib + subset examples, capture, diff
  ```

The subset lives in the `include` lines of `image_comparison_rlvk_regression.ini`, which drive
both tools: capture runs only the listed examples (others are skipped even if built) and diff
compares only them — a listed scene missing from the candidate is still a FAIL. Without any
`include` lines a config compares everything, which is what the full-suite configs do. Regression
artifacts are kept separate (`rlvk_regression/`, `diffs_rlvk_regression/`,
`report_rlvk_regression.html`, all gitignored).

A green subset does **not** replace the full suite — run the full comparison before committing
baseline-affecting changes or declaring a backend milestone. The regression config duplicates the
tolerance/allowance settings of the scenes it includes; when `image_comparison_rlvk.ini` changes,
mirror the change there.

## Notes

- A small per-channel **tolerance** (config `tolerance`) absorbs unavoidable 1-ulp GPU rounding.
- A few examples are **excluded** (config `exclude`) because they render real-world state no
  frame clock can fix: `shapes_digital_clock` / `shapes_clock_of_clocks` (system wall-clock) and
  `audio_raw_stream` / `audio_stream_callback` (live audio thread).
- All paths in the config are relative and resolved at run time — nothing absolute is hardcoded.
