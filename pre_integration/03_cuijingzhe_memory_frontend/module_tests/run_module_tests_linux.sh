#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
if command -v clang++ >/dev/null 2>&1; then DEFAULT_CXX=clang++; else DEFAULT_CXX=g++; fi
CXX=${CXX:-$DEFAULT_CXX}
rm -rf build
mkdir -p build logs
"$CXX" -std=gnu++17 -Wall -Wextra -O0 test_memory_auto.cpp ../memory_standalone/memory/memory.cpp ../memory_standalone/disk.cpp -I../memory_standalone -o build/test_memory_auto -pthread
./build/test_memory_auto 2>&1 | tee logs/memory_auto_linux.log
python3 test_memory_frontend_auto.py 2>&1 | tee logs/memory_frontend_auto_linux.log
echo "[成功] 成员模块测试通过。"
