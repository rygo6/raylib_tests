#!/usr/bin/env bash
#
# run_all.sh [backend ...]
#
# Builds and captures every requested backend BACK-TO-BACK in one machine-state window (the only
# way the cross-backend comparison is valid - absolute frame times drift between windows), then
# writes every report. With no arguments it does all three: rlgl, rlsw, rlvk.
#
# Paths and tooling resolve from the platform, so this runs on Windows (MSYS/MinGW) and Linux.

set -u
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$HERE" || exit 1

case "$(uname -s)" in
  MINGW*|MSYS*|CYGWIN*) EXT=".exe" ;;
  *)                    EXT="" ;;
esac

BACKENDS=("$@")
[ ${#BACKENDS[@]} -eq 0 ] && BACKENDS=(rlgl rlsw rlvk)

for backend in "${BACKENDS[@]}"; do
  echo "===== [$(date +%H:%M:%S)] BUILD $backend ====="
  bash build_backend.sh "$backend" 2>&1 | grep -vE "^gcc |^ar "
  echo "===== [$(date +%H:%M:%S)] CAPTURE $backend ====="
  "./src/performance_capture$EXT" "$backend" "performance_$backend.ini" 2>&1 | grep -vE "^INFO:|^WARNING:"
done

echo "===== [$(date +%H:%M:%S)] REPORTS ====="
"./src/performance_report$EXT" 2>&1
echo "===== [$(date +%H:%M:%S)] ALL DONE ====="
