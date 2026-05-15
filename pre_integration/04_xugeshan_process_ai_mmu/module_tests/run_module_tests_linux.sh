#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
if command -v clang++ >/dev/null 2>&1; then DEFAULT_CXX=clang++; else DEFAULT_CXX=g++; fi
CXX=${CXX:-$DEFAULT_CXX}
rm -rf build
mkdir -p build logs
"$CXX" -std=gnu++17 -Wall -Wextra -O0 test_process_auto.cpp ../process_standalone/process/program.cpp ../process_standalone/process/device.cpp ../process_standalone/memory/memory.cpp ../process_standalone/disk.cpp -I../process_standalone -o build/test_process_auto -pthread
./build/test_process_auto 2>&1 | tee logs/process_auto_linux.log
python3 test_ai_voice_interface_auto.py 2>&1 | tee logs/ai_voice_interface_auto_linux.log
echo "[成功] 成员模块测试通过。"
