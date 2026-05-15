#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
if command -v clang++ >/dev/null 2>&1; then DEFAULT_CXX=clang++; else DEFAULT_CXX=g++; fi
CXX=${CXX:-$DEFAULT_CXX}
rm -rf build
mkdir -p build logs
"$CXX" -std=gnu++17 -Wall -Wextra -O0 test_disk_auto.cpp ../disk_standalone/disk.cpp -I../disk_standalone -o build/test_disk_auto
./build/test_disk_auto 2>&1 | tee logs/disk_auto_linux.log
echo "[成功] 成员模块测试通过。"
