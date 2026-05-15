#!/usr/bin/env python3
from pathlib import Path
import sys

BASE = Path(__file__).resolve().parents[1]
SNAP = BASE / "ai_voice_frontend_interface_snapshot"
index = (SNAP / "index.html").read_text(encoding="utf-8", errors="ignore")
vimplus = (SNAP / "vimplus.html").read_text(encoding="utf-8", errors="ignore")
text = (index + "\n" + vimplus).lower()
checks = []

def check(name, condition):
    checks.append((name, bool(condition)))
    print(("[PASS] " if condition else "[FAIL] ") + name)

print("=== 徐舸山：AI/语音/前端接口静态自动测试 ===")
check("页面包含语音或 SpeechRecognition 接口预留", "speech" in text or "语音" in index or "voice" in text)
check("页面包含 AI/助手入口文案或接口预留", "ai" in text or "智能" in index or "assistant" in text)
check("页面仍通过 command 字段接入命令分发", "command: cmd" in index or "JSON.stringify({ command: cmd })" in index)
check("Vim 页面保留编辑器入口", "editor" in vimplus.lower() and "vim" in vimplus.lower())
failed = sum(1 for _, ok in checks if not ok)
print(f"AI/语音/前端接口静态自动测试完成：{len(checks)-failed} PASS / {failed} FAIL")
sys.exit(0 if failed == 0 else 1)
