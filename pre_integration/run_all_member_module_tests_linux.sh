#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
for d in \
  01_houbowen_filesystem_shell_vim_frontend \
  02_weijianxing_disk \
  03_cuijingzhe_memory_frontend \
  04_xugeshan_process_ai_mmu \
  05_yanghaozhe_frontend_ipc_device
  do
    echo "=================================================="
    echo "[运行] $d"
    bash "$d/module_tests/run_module_tests_linux.sh"
  done
echo "[完成] 全部成员模块测试通过。"
