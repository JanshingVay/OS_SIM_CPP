#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
mkdir -p test_logs
bash ./00_BUILD_ALL_LINUX.sh
run_test() {
  local exe="$1"
  local name="$2"
  local workdir="/tmp/os_sim_${name}_$$"
  mkdir -p "$workdir"
  cp "$exe" "$workdir/"
  echo "=================================================="
  echo "[运行] $name"
  (cd "$workdir" && timeout 60s "./$(basename "$exe")") 2>&1 | tee "test_logs/${name}_linux.log"
}
run_test bin/test_process_linux test_process
run_test bin/test_process_extensions_linux test_process_extensions
run_test bin/test_memory_linux test_memory
run_test bin/test_fs_linux test_fs
run_test bin/test_disk_memory_linux test_disk_memory
run_test bin/test_device_linux test_device
run_test bin/test_ipc_linux test_ipc
run_test bin/test_comprehensive_linux test_comprehensive
python3 test_frontend_coverage.py 2>&1 | tee test_logs/test_frontend_coverage_linux.log
echo "[完成] Linux 自动化测试全部完成，日志位于 test_logs/。"
