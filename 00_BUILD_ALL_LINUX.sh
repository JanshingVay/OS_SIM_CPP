#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
if command -v clang++ >/dev/null 2>&1; then DEFAULT_CXX=clang++; else DEFAULT_CXX=g++; fi
CXX=${CXX:-$DEFAULT_CXX}
if ! command -v "$CXX" >/dev/null 2>&1; then
  echo "[错误] 未找到 C++ 编译器。请安装 clang++ 或 g++，或设置 CXX。" >&2
  exit 1
fi
mkdir -p bin build/linux
CXXFLAGS=${CXXFLAGS:-"-std=gnu++17 -Wall -Wextra -O0"}
THREADFLAGS=${THREADFLAGS:-"-pthread"}
echo "[信息] 使用编译器: $CXX"
echo "[信息] 编译参数: $CXXFLAGS $THREADFLAGS"

compile_obj() {
  local src="$1"
  local out="$2"
  echo "[编译] $out"
  "$CXX" $CXXFLAGS -c "$src" -o "$out"
}

compile_obj filesystem.cpp build/linux/filesystem.o
compile_obj disk.cpp build/linux/disk.o
compile_obj memory/memory.cpp build/linux/memory.o
compile_obj process/program.cpp build/linux/program.o
compile_obj process/device.cpp build/linux/device.o
compile_obj process/ipc.cpp build/linux/ipc.o
COMMON_OBJS="build/linux/memory.o build/linux/program.o build/linux/device.o build/linux/ipc.o build/linux/filesystem.o build/linux/disk.o"

echo "[链接] bin/test_memory_linux"
"$CXX" $CXXFLAGS test_memory.cpp build/linux/memory.o build/linux/disk.o -o bin/test_memory_linux $THREADFLAGS

echo "[链接] bin/test_fs_linux"
"$CXX" $CXXFLAGS test_fs.cpp build/linux/filesystem.o build/linux/disk.o -o bin/test_fs_linux $THREADFLAGS

echo "[链接] bin/test_disk_memory_linux"
"$CXX" $CXXFLAGS test_disk_memory.cpp build/linux/memory.o build/linux/disk.o -o bin/test_disk_memory_linux $THREADFLAGS

echo "[链接] bin/test_device_linux"
"$CXX" $CXXFLAGS test_device.cpp $COMMON_OBJS -o bin/test_device_linux $THREADFLAGS

echo "[链接] bin/test_ipc_linux"
"$CXX" $CXXFLAGS test_ipc.cpp $COMMON_OBJS -o bin/test_ipc_linux $THREADFLAGS

if [ "${FORCE_FULL_REBUILD:-0}" = "1" ] || [ ! -x bin/test_process_linux ]; then
  echo "[链接] bin/test_process_linux"
  "$CXX" $CXXFLAGS test_process.cpp $COMMON_OBJS -o bin/test_process_linux $THREADFLAGS
else
  echo "[跳过] 已存在 bin/test_process_linux。"
fi

if [ "${FORCE_FULL_REBUILD:-0}" = "1" ] || [ ! -x bin/test_process_extensions_linux ]; then
  echo "[链接] bin/test_process_extensions_linux"
  "$CXX" $CXXFLAGS test_process_extensions.cpp $COMMON_OBJS -o bin/test_process_extensions_linux $THREADFLAGS
else
  echo "[跳过] 已存在 bin/test_process_extensions_linux。"
fi

if [ "${FORCE_FULL_REBUILD:-0}" = "1" ] || [ ! -x bin/test_comprehensive_linux ]; then
  echo "[链接] bin/test_comprehensive_linux"
  "$CXX" $CXXFLAGS test_comprehensive.cpp $COMMON_OBJS -o bin/test_comprehensive_linux $THREADFLAGS
else
  echo "[跳过] 已存在 bin/test_comprehensive_linux。"
  echo "[提示] 如需重新编译综合测试，可执行：FORCE_FULL_REBUILD=1 bash 00_BUILD_ALL_LINUX.sh"
fi

if [ "${FORCE_KERNEL_REBUILD:-0}" = "1" ] || [ ! -x bin/os_simulator_linux ]; then
  echo "[链接] bin/os_simulator_linux"
  rm -f bin/os_simulator_linux
  "$CXX" $CXXFLAGS os_sim_main.cpp $COMMON_OBJS -o bin/os_simulator_linux $THREADFLAGS
else
  echo "[跳过] 已存在 bin/os_simulator_linux。验收运行可直接使用该主程序。"
  echo "[提示] 如需在本机重新编译主服务，可执行：FORCE_KERNEL_REBUILD=1 bash 00_BUILD_ALL_LINUX.sh"
fi

echo "[完成] Linux 构建完成。可执行文件位于 bin/。"
