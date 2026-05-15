#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
echo "正在启动进程与 MMU 模块演示..."
bash ./build_interactive_linux.sh
exec ./build/interactive_process
