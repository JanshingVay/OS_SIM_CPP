#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
if [ ! -x bin/os_simulator_linux ]; then
  bash ./00_BUILD_ALL_LINUX.sh
fi
export OS_SIM_PORT=${OS_SIM_PORT:-8080}
echo "[运行] 浏览器打开 http://127.0.0.1:${OS_SIM_PORT}/"
echo "[运行] 按 Ctrl+C 停止系统。"
exec ./bin/os_simulator_linux
