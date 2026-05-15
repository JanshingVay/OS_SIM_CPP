#!/usr/bin/env python3
from pathlib import Path
import sys

BASE = Path(__file__).resolve().parents[1]
SNAP = BASE / "frontend"
index = (SNAP / "index.html").read_text(encoding="utf-8", errors="ignore")
vimplus = (SNAP / "vimplus.html").read_text(encoding="utf-8", errors="ignore")
low = (index + "\n" + vimplus).lower()
checks = []

def check(name, condition):
    checks.append((name, bool(condition)))
    print(("[PASS] " if condition else "[FAIL] ") + name)

print("=== 杨皓哲：前端模块静态自动测试 ===")
check("index.html 包含命令输入/终端输出", "command" in low and ("terminal" in low or "终端" in index))
check("前端 fetch 调用命令接口", "fetch(" in index and "/command" in index)
check("前端提交 JSON 字段 command", "command: cmd" in index or "JSON.stringify({ command: cmd })" in index)
check("前端包含文件/状态 API 调用", "/status" in index and "/files" in index)
check("vimplus.html 保留 Vim 编辑页面", "vim" in vimplus.lower() and "editor" in vimplus.lower())
check("静态资源文件齐全", all((SNAP / f).exists() for f in ["arch.png", "bg.png", "cute.png", "macos.png"]))
failed = sum(1 for _, ok in checks if not ok)
print(f"前端静态自动测试完成：{len(checks)-failed} PASS / {failed} FAIL")
sys.exit(0 if failed == 0 else 1)
