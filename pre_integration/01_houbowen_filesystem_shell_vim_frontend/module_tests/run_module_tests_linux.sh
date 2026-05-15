#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
if command -v clang++ >/dev/null 2>&1; then DEFAULT_CXX=clang++; else DEFAULT_CXX=g++; fi
CXX=${CXX:-$DEFAULT_CXX}
rm -rf build
mkdir -p build logs
"$CXX" -std=gnu++17 -Wall -Wextra -O0 test_filesystem_auto.cpp ../filesystem_standalone/filesystem.cpp ../filesystem_standalone/disk.cpp -I../filesystem_standalone -o build/test_filesystem_auto
./build/test_filesystem_auto 2>&1 | tee logs/filesystem_auto_linux.log
python3 test_shell_vim_frontend_auto.py 2>&1 | tee logs/shell_vim_frontend_auto_linux.log
echo "[成功] 成员模块测试通过。"
