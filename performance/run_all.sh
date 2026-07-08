#!/usr/bin/env bash
cd /c/Developer/raylib_tests/performance
echo "===== [$(date +%H:%M:%S)] BUILD rlgl ====="
bash build_backend.sh rlgl 2>&1 | grep -vE "^gcc |^ar "
echo "===== [$(date +%H:%M:%S)] CAPTURE rlgl ====="
./src/performance_capture.exe rlgl performance_rlgl.ini 2>&1 | grep -vE "^INFO:|^WARNING:"
echo "===== [$(date +%H:%M:%S)] BUILD rlsw ====="
bash build_backend.sh rlsw 2>&1 | grep -vE "^gcc |^ar "
echo "===== [$(date +%H:%M:%S)] CAPTURE rlsw ====="
./src/performance_capture.exe rlsw performance_rlsw.ini 2>&1 | grep -vE "^INFO:|^WARNING:"
echo "===== [$(date +%H:%M:%S)] BUILD rlvk ====="
bash build_backend.sh rlvk 2>&1 | grep -vE "^gcc |^ar "
echo "===== [$(date +%H:%M:%S)] CAPTURE rlvk ====="
./src/performance_capture.exe rlvk performance_rlvk.ini 2>&1 | grep -vE "^INFO:|^WARNING:"
echo "===== [$(date +%H:%M:%S)] REPORTS ====="
./src/performance_report.exe 2>&1
echo "===== [$(date +%H:%M:%S)] ALL DONE ====="
