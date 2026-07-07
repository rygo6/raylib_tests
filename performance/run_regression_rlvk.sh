#!/usr/bin/env bash
#
# run_regression_rlvk.sh — fast rlvk performance regression check (~5 min).
#
# Builds the rlvk backend with PERFORMANCE_CAPTURE (via build_backend.sh), captures the 6-scene
# regression subset (performance_rlvk_regression.ini, 3 x 10 s per scene), and compares it
# against the committed full-suite rlvk capture for this machine (rlvk_<label>/) with
# regression_compare.sh.
#
# The committed baseline was captured in a different machine-state window, so WARN-level deltas
# are noise candidates (re-measure back-to-back before concluding); FAIL-level deltas (>25%
# median frame time) are real enough to investigate. The full cross-backend story stays with
# run_all.sh; this is the inner-loop check for backend development.
#
# Exit code: 0 = no FAIL-level regression, non-zero otherwise.

set -u
cd "$(dirname "$0")" || exit 1

# rlvk compiles GLSL at runtime through shaderc_shared.dll; without it on PATH every custom
# shader silently falls back to the default shader and shader scenes measure the wrong work
if [ -n "${VULKAN_SDK:-}" ]; then
  VKLIB=$(cygpath -m "$VULKAN_SDK" 2>/dev/null || echo "${VULKAN_SDK//\\//}")
  export PATH="$(cygpath -u "$VKLIB" 2>/dev/null || echo "$VKLIB")/Bin:$PATH"
fi

echo "=== build rlvk backend + examples (PERFORMANCE_CAPTURE) ==="
bash build_backend.sh rlvk || exit 1

echo "=== capture regression subset ==="
rm -rf rlvk_regression_*                    # a fresh window per run; captures are cheap
./src/performance_capture.exe rlvk_regression performance_rlvk_regression.ini || exit 1

CAND=$(ls -d rlvk_regression_*/ 2>/dev/null | head -1)
CAND=${CAND%/}
[ -n "$CAND" ] || { echo "ERROR: no capture produced"; exit 1; }
LABEL=${CAND#rlvk_regression_}
BASE="rlvk_$LABEL"
[ -d "$BASE" ] || { echo "ERROR: no committed baseline $BASE for this machine"; exit 2; }

echo "=== compare against committed baseline ==="
bash regression_compare.sh "$BASE" "$CAND"
