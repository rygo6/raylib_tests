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
#   rlvk  -> GRAPHICS_API_VULKAN_14        (Vulkan; needs VULKAN_SDK)

set -u
BACKEND="${1:-}"
RAYLIB="/c/Developer/raylib"
SRC="$RAYLIB/src"
EXDIR="$RAYLIB/examples"
MAKE=mingw32-make

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
)

case "$BACKEND" in
  rlgl) GRAPHICS=GRAPHICS_API_OPENGL_33;       EX_LDLIBS="" ;;
  rlsw) GRAPHICS=GRAPHICS_API_OPENGL_SOFTWARE; EX_LDLIBS="" ;;
  rlvk) GRAPHICS=GRAPHICS_API_VULKAN_14
        if [ -z "${VULKAN_SDK:-}" ]; then echo "ERROR: VULKAN_SDK not set"; exit 1; fi
        VKLIB=$(cygpath -m "$VULKAN_SDK" 2>/dev/null || echo "${VULKAN_SDK//\\//}")   # Windows path, forward slashes
        EX_LDLIBS="-L$VKLIB/Lib -lraylib -lgdi32 -lwinmm -luser32 -lkernel32 -lvulkan-1" ;;
  *) echo "usage: $0 <rlgl|rlsw|rlvk>"; exit 2 ;;
esac

echo "==================================================================="
echo " Building raylib ($BACKEND / $GRAPHICS) with PERFORMANCE_CAPTURE"
echo "==================================================================="

cd "$SRC" || exit 1
$MAKE clean >/dev/null 2>&1
if ! $MAKE PLATFORM=PLATFORM_DESKTOP GRAPHICS="$GRAPHICS" CC='gcc -pipe -DPERFORMANCE_CAPTURE' RAYLIB_LIBTYPE=STATIC 2>&1 | tail -3; then
  echo "ERROR: raylib lib build failed"; exit 1
fi
[ -f "$SRC/libraylib.a" ] || { echo "ERROR: libraylib.a not produced"; exit 1; }

echo "--- archiving measurement unit into libraylib.a ---"
gcc -pipe -c "$SRC/rcore_performance_capture.c" -o "$SRC/rcore_performance_capture.o" || exit 1
ar rcs "$SRC/libraylib.a" "$SRC/rcore_performance_capture.o" || exit 1

echo "--- building ${#EXAMPLES[@]} examples ---"
cd "$EXDIR" || exit 1
built=0; failed=0
for ex in "${EXAMPLES[@]}"; do
  rm -f "$EXDIR/$ex.exe"                      # force relink against the new backend lib
  if [ -n "$EX_LDLIBS" ]; then
    $MAKE "$ex" GRAPHICS="$GRAPHICS" LDLIBS="$EX_LDLIBS" >/dev/null 2>&1
  else
    $MAKE "$ex" GRAPHICS="$GRAPHICS" >/dev/null 2>&1
  fi
  if [ -f "$EXDIR/$ex.exe" ]; then built=$((built+1)); echo "  ok   $ex"; else failed=$((failed+1)); echo "  FAIL $ex"; fi
done

echo "-------------------------------------------------------------------"
echo " $BACKEND: built $built, failed $failed"
echo "-------------------------------------------------------------------"
[ "$failed" -eq 0 ]
