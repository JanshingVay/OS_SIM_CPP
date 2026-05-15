#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
echo "正在启动 IPC 与设备管理模块演示..."
bash ./build_interactive_linux.sh
echo
echo "进入程序后输入 help 查看命令，输入 exit 退出。"
echo
exec ./build/interactive_ipc_device
