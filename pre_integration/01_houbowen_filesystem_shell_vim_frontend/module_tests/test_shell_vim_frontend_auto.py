#!/usr/bin/env python3
from pathlib import Path
import sys

BASE = Path(__file__).resolve().parents[1]
SNAP = BASE / "shell_and_vim_snapshot"
checks = []

def check(name, condition):
    checks.append((name, bool(condition)))
    print(("[PASS] " if condition else "[FAIL] ") + name)

index = (SNAP / "index.html").read_text(encoding="utf-8", errors="ignore")
vimplus = (SNAP / "vimplus.html").read_text(encoding="utf-8", errors="ignore")
shell = (SNAP / "shell.cpp").read_text(encoding="utf-8", errors="ignore")
combined = (index + "\n" + vimplus + "\n" + shell).lower()

print("=== 侯博文：Shell / Vim / 前端静态自动测试 ===")
check("index.html 存在且包含终端区域", "terminal" in index.lower() or "终端" in index)
check("前端使用 fetch 调用后端 API", "fetch(" in index and "/command" in index)
check("前端按 command 字段提交命令", "JSON.stringify({ command: cmd })" in index or "command: cmd" in index)
check("页面提供文件系统状态/文件详情接口", "/files" in index and "/file/" in index)
check("vimplus.html 存在编辑器入口", "editor" in vimplus.lower() and "vim" in vimplus.lower())
check("Shell 快照包含 mkdir/cd/ls 命令分发", "mkdir" in shell and "cd" in shell and "ls" in shell)
check("Shell 快照包含文件读写命令", "write" in shell and "cat" in shell)
check("静态资源文件齐全", all((SNAP / f).exists() for f in ["arch.png", "bg.png", "cute.png", "macos.png"]))

failed = sum(1 for _, ok in checks if not ok)
print(f"Shell/Vim/前端静态自动测试完成：{len(checks)-failed} PASS / {failed} FAIL")
sys.exit(0 if failed == 0 else 1)
