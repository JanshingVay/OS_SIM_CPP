#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
if command -v clang++ >/dev/null 2>&1; then DEFAULT_CXX=clang++; else DEFAULT_CXX=g++; fi
CXX=${CXX:-$DEFAULT_CXX}
rm -rf build
mkdir -p build
"$CXX" -std=gnu++17 -Wall -Wextra -O0 demo_filesystem.cpp filesystem.cpp disk.cpp -o build/demo_filesystem
exec ./build/demo_filesystem
