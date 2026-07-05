#!/usr/bin/env bash
set -u
RS=/c/Developer/raylib/src; EX=/c/Developer/raylib/examples; IE=/c/Developer/raylib_tests/image_equivalence; MK=mingw32-make
VKLIB=$(cygpath -m "$VULKAN_SDK")
echo "[$(date +%H:%M:%S)] build deterministic rlvk lib"
cd "$RS" && $MK clean >/dev/null 2>&1
$MK GRAPHICS=GRAPHICS_API_VULKAN_14 CC='gcc -pipe -DDETERMINISTIC_IMAGE_COMPARISON_CAPTURE' RAYLIB_LIBTYPE=STATIC >/dev/null 2>&1 && echo "  lib ok"
echo "[$(date +%H:%M:%S)] build all examples (rlvk)"
cd "$EX" && $MK GRAPHICS=GRAPHICS_API_VULKAN_14 LDLIBS="-L$VKLIB/Lib -lraylib -lgdi32 -lwinmm -luser32 -lkernel32 -lvulkan-1" >/dev/null 2>&1
echo "  built $(find "$EX" -maxdepth 2 -name '*.exe' | wc -l) example exes"
echo "[$(date +%H:%M:%S)] capture rlvk frames"
cd "$IE" && ../src/image_comparison_capture.exe rlvk image_comparison_rlvk.ini 2>&1 | grep -E "CAPTURE_SUMMARY"
echo "[$(date +%H:%M:%S)] diff vs rlgl baseline"
../src/image_comparison_diff.exe image_comparison_rlvk.ini 2>&1 | grep -E "SUMMARY|PASS|FAIL" | tail -20
echo "[$(date +%H:%M:%S)] DONE"
