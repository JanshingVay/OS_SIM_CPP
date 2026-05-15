#!/usr/bin/env python3
from pathlib import Path
import sys

BASE = Path(__file__).resolve().parents[1]
SNAP = BASE / "frontend_snapshot"
index = (SNAP / "index.html").read_text(encoding="utf-8", errors="ignore")
low = index.lower()
checks = []

def check(name, condition):
    checks.append((name, bool(condition)))
    print(("[PASS] " if condition else "[FAIL] ") + name)

print("=== 崔敬哲：内存前端页面静态自动测试 ===")
check("页面包含内存状态展示入口", "mem" in low or "内存" in index)
check("页面包含命令输入与执行函数", "command" in low and "fetch(" in index)
check("页面提交字段为 command", "command: cmd" in index or "JSON.stringify({ command: cmd })" in index)
check("页面内置内存/调度相关命令提示", any(k in low for k in ["memstat", "setmem", "access", "translate"]))
check("页面静态资源齐全", all((SNAP / f).exists() for f in ["arch.png", "bg.png", "cute.png", "macos.png"]))
failed = sum(1 for _, ok in checks if not ok)
print(f"内存前端静态自动测试完成：{len(checks)-failed} PASS / {failed} FAIL")
sys.exit(0 if failed == 0 else 1)
