#!/usr/bin/env bash
cd /c/Developer/raylib_tests/performance
RS="/c/Developer/raylib/src"; EX="/c/Developer/raylib/examples"; MK=mingw32-make

mk_ini() { cat > /tmp/stress_$1.ini <<EOF
backend $1
examples_dir ../../raylib/examples
capture_output $1
duration_ms 10000
warmup_ms 700
runs 3
timeout_ms 60000
examples others/performance_stress_test
EOF
}

build_stress() { # backend graphics ldlibs
  cd "$RS" && $MK clean >/dev/null 2>&1
  $MK GRAPHICS="$2" CC='gcc -pipe -DPERFORMANCE_CAPTURE' RAYLIB_LIBTYPE=STATIC >/dev/null 2>&1
  gcc -pipe -c rcore_performance_capture.c -o rcore_performance_capture.o && ar rcs libraylib.a rcore_performance_capture.o
  cd "$EX" && rm -f others/performance_stress_test.exe
  if [ -n "$3" ]; then $MK others/performance_stress_test GRAPHICS="$2" LDLIBS="$3" >/dev/null 2>&1
  else $MK others/performance_stress_test GRAPHICS="$2" >/dev/null 2>&1; fi
  [ -f others/performance_stress_test.exe ] && echo "  $1 stress exe: OK" || echo "  $1 stress exe: FAILED"
}

VKLIB=$(echo "${VULKAN_SDK:-}" | sed 's#\#/#g')

echo "===== [$(date +%H:%M:%S)] rlgl stress (lib already rlgl) ====="
mk_ini rlgl; ./src/performance_capture.exe rlgl /tmp/stress_rlgl.ini 2>&1 | grep -vE '^INFO:|^WARNING:'

echo "===== [$(date +%H:%M:%S)] build+capture rlsw stress ====="
build_stress rlsw GRAPHICS_API_OPENGL_SOFTWARE ""
mk_ini rlsw; ./src/performance_capture.exe rlsw /tmp/stress_rlsw.ini 2>&1 | grep -vE '^INFO:|^WARNING:'

echo "===== [$(date +%H:%M:%S)] build+capture rlvk stress ====="
build_stress rlvk GRAPHICS_API_VULKAN_14 "-L$VKLIB/Lib -lraylib -lgdi32 -lwinmm -luser32 -lkernel32 -lvulkan-1"
mk_ini rlvk; ./src/performance_capture.exe rlvk /tmp/stress_rlvk.ini 2>&1 | grep -vE '^INFO:|^WARNING:'

echo "===== [$(date +%H:%M:%S)] regenerate reports (all 15 examples) ====="
./src/performance_report.exe 2>&1
echo "===== [$(date +%H:%M:%S)] DONE ====="
