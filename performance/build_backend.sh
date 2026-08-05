#!/usr/bin/env bash
#
# build_backend.sh <rlgl|rlsw|rlvk>
#
# Builds raylib as a static lib with -DPERFORMANCE_CAPTURE for the given backend, archives the
# isolated measurement translation unit into it, then (re)builds the curated performance example
# set against it. Backends share one libraylib.a path, so build + capture one backend fully
# before moving to the next.
#
#   rlgl  -> GRAPHICS_API_OPENGL_33       (default OpenGL)
#   rlsw  -> GRAPHICS_API_OPENGL_SOFTWARE  (software rasterizer)
#   rlvk  -> GRAPHICS_API_VULKAN_14        (Vulkan; needs the Vulkan loader + headers)
#
# Runs on Windows (MSYS/MinGW) and Linux; the raylib tree, make binary, executable suffix and
# link libraries all resolve from the platform rather than being hardcoded.

set -u
BACKEND="${1:-}"

# Repo layout: this script lives in <tests>/performance, raylib is a sibling of <tests>
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RAYLIB="${RAYLIB_DIR:-$(cd "$HERE/../.." && pwd)/raylib}"
SRC="$RAYLIB/src"
EXDIR="$RAYLIB/examples"

case "$(uname -s)" in
  MINGW*|MSYS*|CYGWIN*) HOST_OS=windows; EXT=".exe"; MAKE=${MAKE:-mingw32-make} ;;
  *)                    HOST_OS=linux;   EXT="";     MAKE=${MAKE:-make} ;;
esac

[ -d "$SRC" ] || { echo "ERROR: raylib source not found at $SRC (set RAYLIB_DIR)"; exit 1; }

# Curated example set (keep in sync with performance_*.ini)
EXAMPLES=(
  shapes/shapes_basic_shapes
  textures/textures_tiled_drawing
  textures/textures_particles_blending
  shapes/shapes_starfield_effect
  models/models_loading
  models/models_heightmap_rendering
  models/models_skybox_rendering
  models/models_waving_cubes
  models/models_first_person_maze
  core/core_3d_camera_free
  shaders/shaders_basic_lighting
  shaders/shaders_basic_pbr
  shaders/shaders_mandelbrot_set
  shaders/shaders_raymarching_rendering
  others/performance_stress_test
  others/performance_stress_test_direct
  others/bench_idle
  others/bench_drawcalls
  others/bench_instanced
)

case "$BACKEND" in
  rlgl) GRAPHICS=GRAPHICS_API_OPENGL_33
        # Explicit libs so the PGO link flags can append (the Makefile default is otherwise used)
        [ "$HOST_OS" = linux ] && EX_LDLIBS="-lraylib -lGL -lm -lpthread -ldl -lrt -lX11" || EX_LDLIBS="" ;;
  rlsw) GRAPHICS=GRAPHICS_API_OPENGL_SOFTWARE; EX_LDLIBS="" ;;
  rlvk) GRAPHICS=GRAPHICS_API_VULKAN_14
        if [ "$HOST_OS" = windows ]; then
          if [ -z "${VULKAN_SDK:-}" ]; then echo "ERROR: VULKAN_SDK not set"; exit 1; fi
          VKLIB=$(cygpath -m "$VULKAN_SDK" 2>/dev/null || echo "${VULKAN_SDK//\\//}")   # Windows path, forward slashes
          EX_LDLIBS="-L$VKLIB/Lib -lraylib -lgdi32 -lwinmm -luser32 -lkernel32 -lvulkan-1"
        else
          # The Vulkan loader replaces -lGL. -lX11 must be explicit: rcore.c's clipboard path
          # calls Xlib directly, and without -lGL nothing else pulls libX11 in transitively
          EX_LDLIBS="-lraylib -lvulkan -lX11 -lm -lpthread -ldl -lrt"
        fi ;;
  *) echo "usage: $0 <rlgl|rlsw|rlvk>"; exit 2 ;;
esac

echo "==================================================================="
echo " Building raylib ($BACKEND / $GRAPHICS) with PERFORMANCE_CAPTURE"
echo " raylib: $RAYLIB   host: $HOST_OS   make: $MAKE"
echo "==================================================================="

# CAMPAIGN CONFIG (2026-08-04, policy switch): every backend builds -O2 -flto + PGO, the
# measured best-balanced configuration (vs plain -O2: drawcalls -24%, stress -8%, and PGO
# cures LTO's +4% batch-fill regression). Two passes: instrumented build + a short training
# run over five scenes, then a profile-guided rebuild. Applied uniformly to every backend so
# cross-backend ratios stay fair. RAYLIB_PERF_NO_PGO=1 falls back to plain -O2.
TRAIN=(others/bench_drawcalls others/performance_stress_test models/models_waving_cubes others/bench_instanced models/models_loading)
LTO="-flto -ffat-lto-objects"

build_lib(){ # $1 = extra CUSTOM_CFLAGS
  cd "$SRC" || exit 1
  rm -f ./*.o libraylib.a   # flags change between passes: never trust make's object cache
  if ! $MAKE PLATFORM=PLATFORM_DESKTOP GRAPHICS="$GRAPHICS" CC='gcc -pipe -DPERFORMANCE_CAPTURE' \
       CUSTOM_CFLAGS="$1" RAYLIB_LIBTYPE=STATIC 2>&1 | tail -2; then
    echo "ERROR: raylib lib build failed"; exit 1
  fi
  [ -f "$SRC/libraylib.a" ] || { echo "ERROR: libraylib.a not produced"; exit 1; }
  gcc -pipe $1 -c "$SRC/rcore_performance_capture.c" -o "$SRC/rcore_performance_capture.o" || exit 1
  ar rcs "$SRC/libraylib.a" "$SRC/rcore_performance_capture.o" || exit 1
}

build_examples(){ # $1 = extra link flags   $2 = quiet
  cd "$EXDIR" || exit 1
  built=0; failed=0
  for ex in "${EXAMPLES[@]}"; do
    rm -f "$EXDIR/$ex$EXT"                      # force relink against the new backend lib
    if [ -n "$EX_LDLIBS" ]; then
      $MAKE "$ex" GRAPHICS="$GRAPHICS" LDLIBS="$EX_LDLIBS $1" >"/tmp/rlbuild_$$.log" 2>&1
    else
      $MAKE "$ex" GRAPHICS="$GRAPHICS" CUSTOM_LDLIBS="$1" >"/tmp/rlbuild_$$.log" 2>&1
    fi
    if [ -f "$EXDIR/$ex$EXT" ]; then
      built=$((built+1)); [ -n "${2:-}" ] || echo "  ok   $ex"
    else
      failed=$((failed+1)); echo "  FAIL $ex"; grep -m2 -E 'error|Error|undefined' "/tmp/rlbuild_$$.log" | sed 's/^/       /'
    fi
  done
  rm -f "/tmp/rlbuild_$$.log"
}

if [ -n "${RAYLIB_PERF_NO_PGO:-}" ]; then
  build_lib ""
  build_examples ""
else
  echo "--- PGO pass 1: instrumented build + training (5 scenes x 3 s) ---"
  find "$SRC" "$EXDIR" -maxdepth 2 -name '*.gcda' -delete 2>/dev/null
  mkdir -p /tmp/pgo_train
  build_lib "-O2 $LTO -fprofile-generate -fprofile-update=atomic"
  build_examples "-fprofile-generate" quiet
  for ex in "${TRAIN[@]}"; do
    d=$(dirname "$ex"); b=$(basename "$ex")
    [ -f "$EXDIR/$ex$EXT" ] || continue
    (cd "$EXDIR/$d" && RAYLIB_PERF_DIR=/tmp/pgo_train RAYLIB_PERF_RUN=9 RAYLIB_PERF_DURATION_MS=3000 \
       RAYLIB_PERF_WARMUP_MS=400 timeout 60 "./$b$EXT" >/dev/null 2>&1)
  done
  echo "--- PGO pass 2: profile-guided rebuild ---"
  build_lib "-O2 $LTO -fprofile-use -fprofile-correction -Wno-missing-profile"
  build_examples "-fprofile-use -fprofile-correction -Wno-missing-profile"
  find "$SRC" "$EXDIR" -maxdepth 2 -name '*.gcda' -delete 2>/dev/null
fi

echo "-------------------------------------------------------------------"
echo " $BACKEND: built $built, failed $failed"
echo "-------------------------------------------------------------------"
[ "$failed" -eq 0 ]
