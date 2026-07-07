#!/usr/bin/env bash
#
# regression_compare.sh <baselineDir> <candidateDir>
#
# Compare two same-backend capture trees (each <dir>/<example>/run_<n>.rini) and flag frame-time
# regressions. Per example the representative value is the MEDIAN of the run medians (the same
# aggregation performance_report.c uses). Peak RAM and VRAM are compared on their per-run maxima.
#
# Thresholds: median frame time > +10% = WARN, > +25% = FAIL (exit 1); RAM/VRAM peak > +15% = WARN.
# The baseline usually comes from a DIFFERENT machine-state window than the candidate, so treat
# WARN-level deltas as noise candidates and re-measure back-to-back before concluding anything;
# only large (FAIL-level) deltas are trustworthy across windows.

set -u
BASE="${1:-}"; CAND="${2:-}"
[ -d "$BASE" ] && [ -d "$CAND" ] || { echo "usage: $0 <baselineDir> <candidateDir>"; exit 2; }

get() { awk -v k="$2" '$1 == k { print $2 }' "$1"; }   # rini "key value" lookup

median_of() {   # median of the whitespace-separated numbers in $1
  echo "$1" | tr ' ' '\n' | sed '/^$/d' | sort -g | awk '{ v[NR] = $1 } END { print (NR % 2) ? v[(NR+1)/2] : (v[NR/2] + v[NR/2+1])/2 }'
}
max_of() { echo "$1" | tr ' ' '\n' | sed '/^$/d' | sort -g | tail -1; }

collect() {     # collect <exampleDir> <key> -> space-separated values across runs
  local vals=""
  for r in "$1"/run_*.rini; do [ -f "$r" ] && vals="$vals $(get "$r" "$2")"; done
  echo "$vals"
}

echo "Regression compare: candidate=$CAND vs baseline=$BASE"
echo "(representative = median of run medians; cross-window captures: trust only large deltas)"
printf "%-34s %12s %12s %8s %8s %8s  %s\n" "example" "base med ms" "cand med ms" "d.med%" "d.RAM%" "d.VRAM%" "verdict"

fail=0; warn=0; compared=0
for exDir in "$CAND"/*/; do
  ex=$(basename "$exDir")
  [ -d "$BASE/$ex" ] || { echo "  (no baseline for $ex - skipped)"; continue; }

  bMed=$(median_of "$(collect "$BASE/$ex" frame_median_ms)")
  cMed=$(median_of "$(collect "$exDir" frame_median_ms)")
  bRam=$(max_of "$(collect "$BASE/$ex" ram_peak_bytes)")
  cRam=$(max_of "$(collect "$exDir" ram_peak_bytes)")
  bVram=$(max_of "$(collect "$BASE/$ex" vram_peak_bytes)")
  cVram=$(max_of "$(collect "$exDir" vram_peak_bytes)")
  [ -n "$bMed" ] && [ -n "$cMed" ] || { echo "  (missing data for $ex - skipped)"; continue; }
  compared=$((compared+1))

  read -r dMed dRam dVram verdict <<EOF
$(awk -v bm="$bMed" -v cm="$cMed" -v br="$bRam" -v cr="$cRam" -v bv="$bVram" -v cv="$cVram" 'BEGIN {
    dm = (bm > 0) ? (cm - bm)/bm*100 : 0
    dr = (br > 0) ? (cr - br)/br*100 : 0
    dv = (bv > 0) ? (cv - bv)/bv*100 : 0
    v = "ok"
    if (dr > 15 || dv > 15) v = "WARN-mem"
    if (dm > 10) v = "WARN-time"
    if (dm > 25) v = "FAIL"
    printf "%+.1f %+.1f %+.1f %s", dm, dr, dv, v
}')
EOF
  printf "%-34s %12s %12s %8s %8s %8s  %s\n" "$ex" "$bMed" "$cMed" "$dMed" "$dRam" "$dVram" "$verdict"
  case "$verdict" in FAIL) fail=$((fail+1));; WARN*) warn=$((warn+1));; esac
done

echo
echo "REGRESSION_COMPARE: compared=$compared warn=$warn fail=$fail"
[ "$compared" -gt 0 ] || { echo "ERROR: nothing compared"; exit 2; }
exit $((fail > 0 ? 1 : 0))
