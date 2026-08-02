#!/usr/bin/env bash
# iter_rlvk.sh "<space-separated example paths>" [durationMs]
#
# Rebuild the rlvk lib (with current rlvk.h edits) + the given examples, capture ONE probe run
# each into /tmp/probe, and print fps/cpu/ram/vram next to this machine's committed rlvk + rlgl
# captures. Fast inner loop for optimization: probe with 1 run, then verify anything promising
# with the full harness - a single run is NOT enough to judge a change (see the perf-bench
# methodology notes; micro-probes have motivated pivots that landed at parity).
#
# Runs on Windows (MSYS/MinGW) and Linux; paths, make binary, exe suffix and link libraries all
# resolve from the platform.
set -u
EXLIST="${1:?usage: iter_rlvk.sh \"cat/name cat/name\" [durationMs]}"
DUR="${2:-5000}"

PERF="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RAYLIB="${RAYLIB_DIR:-$(cd "$PERF/../.." && pwd)/raylib}"
RS="$RAYLIB/src"; EX="$RAYLIB/examples"

case "$(uname -s)" in
  MINGW*|MSYS*|CYGWIN*)
    HOST_OS=windows; EXT=".exe"; MK=${MAKE:-mingw32-make}
    VKLIB=$(cygpath -m "$VULKAN_SDK" 2>/dev/null || echo "${VULKAN_SDK//\\//}")
    EX_LDLIBS="-L$VKLIB/Lib -lraylib -lgdi32 -lwinmm -luser32 -lkernel32 -lvulkan-1" ;;
  *)
    HOST_OS=linux; EXT=""; MK=${MAKE:-make}
    EX_LDLIBS="-lraylib -lvulkan -lX11 -lm -lpthread -ldl -lrt" ;;
esac

# Captures are platform x vendor labelled (rlvk_windows_nvidia/, rlvk_linux_amd/, ...), so the
# comparison columns must resolve the same label the capture tool uses.
LABEL="${RAYLIB_PERF_LABEL:-}"
if [ -z "$LABEL" ]; then
  LABEL=$(sed -n 's/^label[[:space:]]\+//p' "$PERF/performance_rlvk.ini" 2>/dev/null | head -1)
fi
if [ -z "$LABEL" ]; then
  # Fall back to the newest capture tree present, so the columns work without configuration
  LABEL=$(ls -dt "$PERF"/rlvk_* 2>/dev/null | head -1 | sed 's|.*/rlvk_||')
fi
BASE_VK="$PERF/rlvk_${LABEL}"; BASE_GL="$PERF/rlgl_${LABEL}"

echo "--- rebuild rlvk lib ($HOST_OS) ---"
cd "$RS" || exit 1
$MK clean >/dev/null 2>&1
if ! $MK GRAPHICS=GRAPHICS_API_VULKAN_14 CC='gcc -pipe -DPERFORMANCE_CAPTURE' RAYLIB_LIBTYPE=STATIC 2>/tmp/iter_build.log >/dev/null; then
  echo "LIB BUILD FAILED:"; tail -8 /tmp/iter_build.log; exit 1
fi
gcc -pipe -c rcore_performance_capture.c -o rcore_performance_capture.o && ar rcs libraylib.a rcore_performance_capture.o

echo "--- build examples ---"
cd "$EX" || exit 1
for ex in $EXLIST; do
  rm -f "$ex$EXT"
  $MK "$ex" GRAPHICS=GRAPHICS_API_VULKAN_14 LDLIBS="$EX_LDLIBS" >/tmp/iter_ex.log 2>&1
  [ -f "$ex$EXT" ] || { echo "  EXAMPLE BUILD FAILED: $ex"; grep -m2 -E 'error|undefined' /tmp/iter_ex.log | sed 's/^/    /'; exit 1; }
done

echo "--- probe ($DUR ms, 1 run) vs ${LABEL:-<no capture found>} ---"
printf "%-30s | %-24s | %-24s | %s\n" scene "PROBE fps/cpu/ram/vram" "base rlvk" "base rlgl"
for ex in $EXLIST; do
  name=$(basename "$ex")
  rm -rf "/tmp/probe/$name"; mkdir -p "/tmp/probe/$name"
  cat > /tmp/probe.ini <<EOF
backend rlvk
examples_dir $EX
capture_output /tmp/probe
duration_ms $DUR
warmup_ms 600
runs 1
timeout_ms 60000
label probe
examples $ex
EOF
  (cd "$PERF" && "./src/performance_capture$EXT" /tmp/probe /tmp/probe.ini >/dev/null 2>&1)
  fmt(){ awk "BEGIN{printf \"%6.0f %4.1f %4.0f %4.0f\",$1,$2,$3/1048576,$4/1048576}"; }
  vals(){ # $1 = run_1.rini -> "fps cpu ram vram", zeros when absent
    [ -f "$1" ] || { echo "0 0 0 0"; return; }
    awk '/^fps /{f=$2} /^cpu_avg_pct/{c=$2} /^ram_avg_bytes/{r=$2} /^vram_avg_bytes/{v=$2} END{print f+0,c+0,r+0,v+0}' "$1"
  }
  probe=$(fmt $(vals "/tmp/probe_probe/$name/run_1.rini"))
  [ -f "/tmp/probe_probe/$name/run_1.rini" ] || probe=$(fmt $(vals "/tmp/probe/$name/run_1.rini"))
  bv="$BASE_VK/$name/run_1.rini"; bg="$BASE_GL/$name/run_1.rini"
  brlvk=$( [ -f "$bv" ] && fmt $(vals "$bv") || echo "n/a" )
  brgl=$(  [ -f "$bg" ] && fmt $(vals "$bg") || echo "n/a" )
  printf "%-30s | %-24s | %-24s | %s\n" "$name" "$probe" "$brlvk" "$brgl"
done
echo "(cols: fps cpu% ramMB vramMB; 1 run only - confirm with run_regression_rlvk.sh before acting)"
