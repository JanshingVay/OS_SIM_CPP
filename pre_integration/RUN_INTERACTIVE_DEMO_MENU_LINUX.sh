#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
echo "==============================="
echo "  成员独立模块交互式演示菜单"
echo "==============================="
echo "1. Houbowen    - 文件系统 / Shell / Vim 前端快照"
echo "2. Weijianxing - 磁盘 / inode / block bitmap"
echo "3. Cuijingzhe  - 内存 / 分页 / 页面置换"
echo "4. Xugeshan    - 进程 / 调度 / MMU 接口"
echo "5. Yanghaozhe  - 前端 / IPC / Device / 银行家算法"
echo
read -r -p "请输入 1-5：" choice
case "$choice" in
  1) bash 01_houbowen_filesystem_shell_vim_frontend/RUN_INTERACTIVE_DEMO_LINUX.sh ;;
  2) bash 02_weijianxing_disk/RUN_INTERACTIVE_DEMO_LINUX.sh ;;
  3) bash 03_cuijingzhe_memory_frontend/RUN_INTERACTIVE_DEMO_LINUX.sh ;;
  4) bash 04_xugeshan_process_ai_mmu/RUN_INTERACTIVE_DEMO_LINUX.sh ;;
  5) bash 05_yanghaozhe_frontend_ipc_device/RUN_INTERACTIVE_DEMO_LINUX.sh ;;
  *) echo "输入无效。"; exit 1 ;;
esac
