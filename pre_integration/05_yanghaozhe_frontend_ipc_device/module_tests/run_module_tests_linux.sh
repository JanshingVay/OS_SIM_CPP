#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
if command -v clang++ >/dev/null 2>&1; then DEFAULT_CXX=clang++; else DEFAULT_CXX=g++; fi
CXX=${CXX:-$DEFAULT_CXX}
rm -rf build
mkdir -p build logs
"$CXX" -std=gnu++17 -Wall -Wextra -O0 test_ipc_auto.cpp ../ipc_device_standalone/ipc.cpp ../ipc_device_standalone/program_stub.cpp -I../ipc_device_standalone -o build/test_ipc_auto
./build/test_ipc_auto 2>&1 | tee logs/ipc_auto_linux.log
"$CXX" -std=gnu++17 -Wall -Wextra -O0 test_device_auto.cpp ../ipc_device_standalone/device.cpp ../ipc_device_standalone/program_stub.cpp -I../ipc_device_standalone -o build/test_device_auto
./build/test_device_auto 2>&1 | tee logs/device_auto_linux.log
python3 test_frontend_auto.py 2>&1 | tee logs/frontend_auto_linux.log
echo "[成功] 成员模块测试通过。"
