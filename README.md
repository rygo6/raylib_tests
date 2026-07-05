# raylib_tests — deterministic image-equivalence tests

Reproducible, cycle-based screenshot capture and comparison for raylib examples. Renders a set
of examples to fixed frames (0, 10, 30) and diffs a candidate set against a committed baseline,
so rendering regressions (or differences between graphics backends) are caught deterministically.

## Layout

```
src/                                  C99 tools (link raylib) + rini.h (vendored config lib)
  image_comparison_capture.c          run examples, capture frames + record environment.rini
  image_comparison_diff.c             compare candidate vs baseline, print PASS/FAIL, write report.html
  Makefile
image_equivalence/
  image_comparison.ini                rini config (paths, frames, tolerance, exclusions)
  rlgl_baseline/<example>/frame_00NN.png  committed GL reference frames (+ environment.rini provenance)
  rlgl/<example>/frame_00NN.png        candidate frames captured for a comparison run
```

PNGs are stored via **git LFS** (see `.gitattributes`).

## Requirements

This repo expects the **raylib repo as a sibling** (`../raylib`) built with the deterministic
capture flag, which makes rendering a pure function of the frame counter (fixed 60 fps virtual
clock, fixed RNG seed, neutralized mouse input, in-engine screenshot+exit hook):

```sh
# In the raylib repo:
cd ../raylib/src
make CC='gcc -pipe -DDETERMINISTIC_IMAGE_COMPARISON_CAPTURE'          # deterministic libraylib.a
cd ../examples && make CC='gcc -pipe'                                 # build examples against it
```

## Build the tools

```sh
cd src && make            # -> image_comparison_capture, image_comparison_diff
```

## Run

```sh
cd image_equivalence
../src/image_comparison_capture baseline    # (re)generate the baseline set + environment.rini
../src/image_comparison_capture rlgl        # capture a candidate set
../src/image_comparison_diff                # compare -> PASS/FAIL per scene + report.html
```

`image_comparison_diff` exits 0 when everything matches, else 2. Open `report.html` for a visual
diff (reference / candidate / amplified difference per mismatch).

## Notes

- A small per-channel **tolerance** (config `tolerance`) absorbs unavoidable 1-ulp GPU rounding.
- A few examples are **excluded** (config `exclude`) because they render real-world state no
  frame clock can fix: `shapes_digital_clock` / `shapes_clock_of_clocks` (system wall-clock) and
  `audio_raw_stream` / `audio_stream_callback` (live audio thread).
- All paths in the config are relative and resolved at run time — nothing absolute is hardcoded.
