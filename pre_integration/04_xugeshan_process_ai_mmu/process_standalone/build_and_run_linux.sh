#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
if command -v clang++ >/dev/null 2>&1; then DEFAULT_CXX=clang++; else DEFAULT_CXX=g++; fi
CXX=${CXX:-$DEFAULT_CXX}
rm -rf build
mkdir -p build
"$CXX" -std=gnu++17 -Wall -Wextra -O0 demo_process.cpp process/program.cpp process/device.cpp memory/memory.cpp disk.cpp -o build/demo_process -pthread
exec ./build/demo_process
