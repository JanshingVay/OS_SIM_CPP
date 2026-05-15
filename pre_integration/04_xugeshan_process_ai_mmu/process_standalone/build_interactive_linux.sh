#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
if command -v clang++ >/dev/null 2>&1; then DEFAULT_CXX=clang++; else DEFAULT_CXX=g++; fi
CXX=${CXX:-$DEFAULT_CXX}
rm -rf build
mkdir -p build
"$CXX" -std=gnu++17 -Wall -Wextra -O0 interactive_process.cpp process/program.cpp process/device.cpp memory/memory.cpp disk.cpp -o build/interactive_process -pthread
printf '[完成] build/interactive_process\n'
