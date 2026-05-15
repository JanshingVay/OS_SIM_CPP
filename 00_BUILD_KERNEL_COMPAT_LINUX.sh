#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
if command -v clang++ >/dev/null 2>&1; then DEFAULT_CXX=clang++; else DEFAULT_CXX=g++; fi
CXX=${CXX:-$DEFAULT_CXX}
if ! command -v "$CXX" >/dev/null 2>&1; then
  echo "[错误] 未找到 C++ 编译器。请安装 clang++ 或 g++，或设置 CXX。" >&2
  exit 1
fi
CXXFLAGS=${CXXFLAGS:-"-std=gnu++17 -Wall -Wextra -O0"}
THREADFLAGS=${THREADFLAGS:-"-pthread"}
echo "[编译] os_kernel_linux"
"$CXX" $CXXFLAGS os_sim_main.cpp filesystem.cpp disk.cpp memory/memory.cpp process/program.cpp process/device.cpp process/ipc.cpp -o os_kernel_linux $THREADFLAGS
echo "[完成] 已生成 os_kernel_linux。"
