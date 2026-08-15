#!/usr/bin/env bash
#
# run_regression_rldx12.sh — fast rldx12 image-equivalence regression check (~2 min).
#
# Builds raylib with the Direct3D 12 backend + DETERMINISTIC_IMAGE_COMPARISON_CAPTURE, builds
# ONLY the examples listed by the 'include' lines of image_comparison_rldx12_regression.ini
# (that config is the single source of truth for the subset; the build list is derived from
# it), captures the subset, and diffs it against the committed rlgl_baseline.
#
# The full suite (all built examples + image_comparison_rldx12.ini) remains the merge gate;
# this is the inner-loop check for backend development.
#
# Run from anywhere; needs the MSYS2 UCRT64 toolchain (gcc + mingw32-make) on PATH, and
# VULKAN_SDK set: the D3D12 backend's runtime GLSL path dlopens shaderc_shared.dll AND
# spirv-cross-c-shared.dll from the SDK's Bin - without them on PATH every custom shader
# silently falls back to the default shader and the whole shader subset fails.
# Exit code: 0 = subset passes, non-zero otherwise.

set -u
cd "$(dirname "$0")" || exit 1
HERE=$(pwd)

RAYLIB=$(cd ../../raylib && pwd)
CONFIG=image_comparison_rldx12_regression.ini
MAKE=mingw32-make

[ -n "${VULKAN_SDK:-}" ] || { echo "ERROR: VULKAN_SDK not set (shaderc + spirv-cross DLLs live in its Bin)"; exit 2; }
VKLIB=$(cygpath -m "$VULKAN_SDK" 2>/dev/null || echo "${VULKAN_SDK//\\//}")   # Windows path, forward slashes
EX_LDLIBS="-lraylib -ld3d12 -ldxgi -ldxguid -ld3dcompiler -lgdi32 -lwinmm -luser32 -lkernel32"

export PATH="$(cygpath -u "$VKLIB" 2>/dev/null || echo "$VKLIB")/Bin:$PATH"

# The subset comes from the config's 'include' lines; the example category is the name prefix
# (core_scissor_test -> core/core_scissor_test)
EXAMPLES=$(grep -E '^include[[:space:]]' "$CONFIG" | awk '{print $2}')
[ -n "$EXAMPLES" ] || { echo "ERROR: no include lines found in $CONFIG"; exit 2; }
COUNT=$(echo "$EXAMPLES" | wc -l)

echo "==================================================================="
echo " rldx12 image-equivalence REGRESSION SUBSET ($COUNT scenes)"
echo "==================================================================="

echo "--- building raylib (rldx12 + deterministic capture) ---"
cd "$RAYLIB/src" || exit 1
$MAKE clean >/dev/null 2>&1
if ! $MAKE PLATFORM=PLATFORM_DESKTOP GRAPHICS=GRAPHICS_API_DIRECTX_12 RAYLIB_LIBTYPE=STATIC \
        CC='gcc -pipe -DDETERMINISTIC_IMAGE_COMPARISON_CAPTURE' 2>&1 | tail -2; then
  echo "ERROR: raylib lib build failed"; exit 1
fi
[ -f "$RAYLIB/src/libraylib.a" ] || { echo "ERROR: libraylib.a not produced"; exit 1; }

echo "--- building $COUNT subset examples ---"
cd "$RAYLIB/examples" || exit 1
failed=0
for name in $EXAMPLES; do
  ex="${name%%_*}/$name"
  rm -f "$ex.exe"                               # force relink against the fresh backend lib
  $MAKE "$ex" GRAPHICS=GRAPHICS_API_DIRECTX_12 LDLIBS="$EX_LDLIBS" CC='gcc -pipe' >/dev/null 2>&1
  if [ -f "$ex.exe" ]; then echo "  ok   $ex"; else echo "  FAIL $ex"; failed=$((failed+1)); fi
done
[ "$failed" -eq 0 ] || { echo "ERROR: $failed example build(s) failed"; exit 1; }

cd "$HERE" || exit 1
echo "--- capturing subset ---"
../src/image_comparison_capture.exe rldx12_regression "$CONFIG"
captureRc=$?

echo "--- diffing against rlgl_baseline ---"
../src/image_comparison_diff.exe "$CONFIG"
diffRc=$?

[ $captureRc -eq 0 ] || echo "WARNING: capture reported failures (rc=$captureRc)"
exit $((diffRc != 0 ? diffRc : captureRc))
