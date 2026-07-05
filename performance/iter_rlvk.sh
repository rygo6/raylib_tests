#!/usr/bin/env bash
# iter_rlvk.sh "<space-separated example paths>" [durationMs]
# Rebuild the rlvk lib (with current rlvk.h edits) + the given examples, capture ONE probe run
# each into /tmp/probe, and print fps/cpu/ram/vram next to the committed rlvk + rlgl baselines.
# Fast inner loop for optimization: probe with 1 run; once promising, verify with the full harness.
set -u
EXLIST="${1:?usage: iter_rlvk.sh \"cat/name cat/name\" [durationMs]}"
DUR="${2:-5000}"
RS=/c/Developer/raylib/src; EX=/c/Developer/raylib/examples; PERF=/c/Developer/raylib_tests/performance; MK=mingw32-make
VKLIB=$(cygpath -m "$VULKAN_SDK" 2>/dev/null || echo "${VULKAN_SDK//\\//}")

echo "--- rebuild rlvk lib ---"
cd "$RS" && $MK clean >/dev/null 2>&1
if ! $MK GRAPHICS=GRAPHICS_API_VULKAN_14 CC='gcc -pipe -DPERFORMANCE_CAPTURE' RAYLIB_LIBTYPE=STATIC 2>build_err.log >/dev/null; then
  echo "LIB BUILD FAILED:"; tail -8 build_err.log; exit 1
fi
gcc -pipe -c rcore_performance_capture.c -o rcore_performance_capture.o && ar rcs libraylib.a rcore_performance_capture.o

echo "--- build examples ---"
cd "$EX"
for ex in $EXLIST; do
  rm -f "$ex.exe"
  $MK "$ex" GRAPHICS=GRAPHICS_API_VULKAN_14 LDLIBS="-L$VKLIB/Lib -lraylib -lgdi32 -lwinmm -luser32 -lkernel32 -lvulkan-1" >/dev/null 2>&1
  [ -f "$ex.exe" ] || { echo "  EXAMPLE BUILD FAILED: $ex"; exit 1; }
done

echo "--- probe ($DUR ms, 1 run) ---"
printf "%-30s | %-24s | %-24s | %s\n" scene "PROBE fps/cpu/ram/vram" "base rlvk" "base rlgl"
for ex in $EXLIST; do
  name=$(basename "$ex")
  rm -rf "/tmp/probe/$name"; mkdir -p "/tmp/probe/$name"
  cat > /tmp/probe.ini <<EOF
backend rlvk
examples_dir ../../raylib/examples
capture_output /tmp/probe
duration_ms $DUR
warmup_ms 600
runs 1
timeout_ms 60000
examples $ex
EOF
  cd "$PERF" && ./src/performance_capture.exe /tmp/probe /tmp/probe.ini >/dev/null 2>&1
  read pf pc pr pv < <(grep -hE '^fps |^cpu_avg_pct|^ram_avg_bytes|^vram_avg_bytes' "/tmp/probe/$name/run_1.rini" 2>/dev/null | awk '{print $2}' | paste -sd' ' -)
  fmt(){ awk "BEGIN{printf \"%6.0f %4.1f %4.0f %4.0f\",$1,$2,$3/1048576,$4/1048576}"; }
  probe=$(fmt "${pf:-0}" "${pc:-0}" "${pr:-0}" "${pv:-0}")
  bv="$PERF/rlvk/$name/run_1.rini"; bg="$PERF/rlgl/$name/run_1.rini"
  brlvk=$( [ -f "$bv" ] && fmt "$(grep '^fps ' $bv|awk '{print $2}')" "$(grep '^cpu_avg_pct' $bv|awk '{print $2}')" "$(grep '^ram_avg_bytes' $bv|awk '{print $2}')" "$(grep '^vram_avg_bytes' $bv|awk '{print $2}')" || echo "n/a")
  brgl=$(  [ -f "$bg" ] && fmt "$(grep '^fps ' $bg|awk '{print $2}')" "$(grep '^cpu_avg_pct' $bg|awk '{print $2}')" "$(grep '^ram_avg_bytes' $bg|awk '{print $2}')" "$(grep '^vram_avg_bytes' $bg|awk '{print $2}')" || echo "n/a")
  cd "$EX"
  printf "%-30s | %-24s | %-24s | %s\n" "$name" "$probe" "$brlvk" "$brgl"
done
echo "(cols: fps cpu% ramMB vramMB)"
