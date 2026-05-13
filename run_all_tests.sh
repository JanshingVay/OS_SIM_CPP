#!/usr/bin/env bash
set -euo pipefail

CXX="${CXX:-g++}"
PY="${PYTHON:-python3}"
if ! command -v "$PY" >/dev/null 2>&1 || ! "$PY" --version >/dev/null 2>&1; then
  PY="python"
fi
if ! command -v "$PY" >/dev/null 2>&1 || ! "$PY" --version >/dev/null 2>&1; then
  echo "Python is required for the HTTP API integration test." >&2
  exit 1
fi

echo "==> Resetting test virtual disk"
rm -f vdisk.bin

echo "==> Building file-system test"
"$CXX" -O2 -std=c++11 test_fs.cpp filesystem.cpp disk.cpp -o run_fs_test_check -pthread

echo "==> Running file-system test"
./run_fs_test_check

echo "==> Building disk-memory test"
"$CXX" -O2 -std=c++11 \
  disk.cpp \
  memory/memory.cpp \
  test_disk_memory.cpp \
  -o os_sim_test_disk_memory_check \
  -pthread

echo "==> Running disk-memory test"
./os_sim_test_disk_memory_check

echo "==> Building process/device/ipc unit test"
"$CXX" -O2 -std=c++14 -D_DEFAULT_SOURCE \
  test_process_ipc_device.cpp \
  memory/memory.cpp \
  process/program.cpp \
  process/device.cpp \
  process/ipc.cpp \
  disk.cpp \
  -o test_process_ipc_device_check \
  -pthread

echo "==> Running process/device/ipc unit test"
./test_process_ipc_device_check

echo "==> Building integrated OS simulator"
"$CXX" -O2 -std=c++14 -D_DEFAULT_SOURCE \
  os_sim_main.cpp \
  memory/memory.cpp \
  process/program.cpp \
  process/device.cpp \
  process/ipc.cpp \
  filesystem.cpp \
  disk.cpp \
  -o os_simulator_check \
  -pthread

echo "==> Starting integrated OS simulator"
./os_simulator_check > integration_server.log 2>&1 &
server_pid=$!

cleanup() {
  if kill -0 "$server_pid" >/dev/null 2>&1; then
    kill "$server_pid" >/dev/null 2>&1 || true
    wait "$server_pid" >/dev/null 2>&1 || true
  fi
}
trap cleanup EXIT

echo "==> Running HTTP API integration test"
"$PY" test_api_integration.py --base-url http://127.0.0.1:8080/api

echo "==> All tests passed"
