#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
echo "正在启动磁盘模块演示..."
bash ./build_interactive_linux.sh
exec ./build/interactive_disk
