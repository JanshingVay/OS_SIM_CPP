#!/usr/bin/env python3
from pathlib import Path
import sys
html = (Path('vimplus.html').read_text(encoding='utf-8', errors='ignore') + '\n' + Path('index.html').read_text(encoding='utf-8', errors='ignore'))
checks = {
    '进程管理': ['create','fork','wait','exit','ptree','block','wakeup','suspend','resume','setsched','signal','ulimit','setpgid','killgroup','ps','pinfo','pstat'],
    '内存管理': ['memstat','memreset','setmem','access','translate','resize','shm','FIFO','LRU','CLOCK'],
    '文件系统': ['touch','mkdir','rm','cat','read_at','write_at','write','chmod','stat','rename','format','tree','cp','grep','tar','vim'],
    '设备与 IPC': ['io','release','sem_create','P','V','msg_send','msg_read'],
    '分模块演示': ['demoDisk','demoFile','demoMemory','demoProcess','demoDevice','demoIPC','demoIntegration'],
}
passed = failed = 0
for section, tokens in checks.items():
    for token in tokens:
        ok = token in html
        print(('[PASS] ' if ok else '[FAIL] ') + f'{section}: {token}')
        passed += int(ok)
        failed += int(not ok)
print(f'SUMMARY passed={passed} failed={failed}')
sys.exit(0 if failed == 0 else 1)
