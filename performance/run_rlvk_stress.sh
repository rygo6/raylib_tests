#!/usr/bin/env bash
set -u
RS=/c/Developer/raylib/src; EX=/c/Developer/raylib/examples; PERF=/c/Developer/raylib_tests/performance; MK=mingw32-make
VKLIB=$(cygpath -m "$VULKAN_SDK" 2>/dev/null || echo "${VULKAN_SDK//\//}")
echo "[$(date +%H:%M:%S)] build rlvk lib"
cd "$RS" && $MK clean >/dev/null 2>&1
$MK GRAPHICS=GRAPHICS_API_VULKAN_14 CC='gcc -pipe -DPERFORMANCE_CAPTURE' RAYLIB_LIBTYPE=STATIC >/dev/null 2>&1
gcc -pipe -c rcore_performance_capture.c -o rcore_performance_capture.o && ar rcs libraylib.a rcore_performance_capture.o && echo "  lib ok"
cd "$EX"
for ex in others/performance_stress_test others/performance_stress_test_direct; do
  rm -f "$ex.exe"
  $MK "$ex" GRAPHICS=GRAPHICS_API_VULKAN_14 LDLIBS="-L$VKLIB/Lib -lraylib -lgdi32 -lwinmm -luser32 -lkernel32 -lvulkan-1" >/dev/null 2>&1
  [ -f "$ex.exe" ] && echo "  built $ex" || echo "  FAILED $ex"
done
cat > /tmp/rlvk_stress.ini <<EOF
backend rlvk
examples_dir ../../raylib/examples
capture_output rlvk
duration_ms 10000
warmup_ms 700
runs 3
timeout_ms 60000
examples others/performance_stress_test, others/performance_stress_test_direct
EOF
cd "$PERF"
echo "[$(date +%H:%M:%S)] capture rlvk stress scenes"
./src/performance_capture.exe rlvk /tmp/rlvk_stress.ini 2>&1 | grep -E "CAPTURE_SUMMARY|ok"
./src/performance_report.exe >/dev/null 2>&1 && echo "  reports regenerated"
echo "[$(date +%H:%M:%S)] DONE"
