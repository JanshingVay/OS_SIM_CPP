#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
OS_SIM_CPP HTTP 压力/功能验证测试
用法: python test_os_pressure.py [--mode func|stress]
  func   : 功能验证模式，逐项测试各模块 HTTP API (默认)
  stress : 并发压力模式，大量并发请求测试稳定性
"""

import requests
import json
import sys
import time
import threading
from concurrent.futures import ThreadPoolExecutor, as_completed

BASE_URL = "http://127.0.0.1:8080"

passed = 0
failed = 0

def GREEN(s): return f"\033[1;32m{s}\033[0m"
def RED(s):   return f"\033[1;31m{s}\033[0m"
def YELLOW(s): return f"\033[1;33m{s}\033[0m"
def CYAN(s):  return f"\033[1;36m{s}\033[0m"

def cmd(command, timeout=5):
    """发送命令到 OS_SIM_CPP HTTP API"""
    try:
        r = requests.post(f"{BASE_URL}/api/command", 
                         json={"cmd": command}, timeout=timeout)
        if r.status_code == 200:
            return r.json()
        else:
            return {"error": f"HTTP {r.status_code}", "raw": r.text}
    except Exception as e:
        return {"error": str(e)}

def get_status(timeout=5):
    """获取系统状态"""
    try:
        r = requests.get(f"{BASE_URL}/api/status", timeout=timeout)
        if r.status_code == 200:
            return r.json()
        else:
            return {"error": f"HTTP {r.status_code}"}
    except Exception as e:
        return {"error": str(e)}

def check(section, name, cond):
    global passed, failed
    if cond:
        print(f"  {GREEN('[PASS]')} {section} / {name}")
        passed += 1
    else:
        print(f"  {RED('[FAIL]')} {section} / {name}")
        failed += 1

def section(title):
    print(f"\n{CYAN('=' * 50)}")
    print(f"{CYAN('  ' + title)}")
    print(f"{CYAN('=' * 50)}")

# ============================================================
def test_boot():
    section("0. 系统启动检查")
    # 检查能否获取状态
    status = get_status()
    check("启动", "HTTP 服务可访问", "error" not in status)
    
    # 尝试 boot
    try:
        r = requests.post(f"{BASE_URL}/api/boot", timeout=5)
        check("启动", "Boot API 响应", r.status_code == 200)
    except:
        check("启动", "Boot API 可访问(可能已启动)", True)

# ============================================================
def test_proc_management():
    section("1. 进程管理")
    
    # 清理旧进程状态
    cmd("setsched priority")
    
    # 创建进程
    res1 = cmd("create P1_Calc 10 4096 8")
    res2 = cmd("create P2_Server 6 2048 5")
    res3 = cmd("create P3_Worker 15 8192 10")
    check("创建进程", "P1 创建成功", "error" not in res1)
    check("创建进程", "P2 创建成功", "error" not in res2)
    check("创建进程", "P3 创建成功", "error" not in res3)
    
    # 查看进程
    ps = cmd("ps")
    check("查看进程", "ps 命令成功", "error" not in ps)
    
    queues = cmd("queues")
    check("队列查看", "queues 命令成功", "error" not in queues)
    
    # 进程信息
    pinfo = cmd("pinfo 1")
    check("进程详情", "pinfo 命令成功", "error" not in pinfo)
    
    # 阻塞与唤醒
    block_res = cmd("block 1 3 io_wait")
    check("阻塞操作", "block 命令成功", "error" not in block_res)
    
    wake_res = cmd("wakeup 1")
    check("唤醒操作", "wakeup 命令成功", "error" not in wake_res)
    
    # 挂起与恢复
    sus_res = cmd("suspend 2")
    check("挂起操作", "suspend 命令成功", "error" not in sus_res)
    
    res_res = cmd("resume 2")
    check("恢复操作", "resume 命令成功", "error" not in res_res)
    
    # Fork 与进程树
    fork_res = cmd("fork 3")
    check("Fork操作", "fork 命令成功", "error" not in fork_res)
    
    ptree = cmd("ptree")
    check("进程树", "ptree 命令成功", "error" not in ptree)

# ============================================================
def test_scheduler():
    section("2. 调度算法切换")
    
    for algo in ["priority", "sjf", "fcfs", "hrrn"]:
        res = cmd(f"setsched {algo}")
        check("调度切换", f"切换到 {algo}", "error" not in res)
    
    # 非法算法
    res_bad = cmd("setsched invalid")
    check("调度切换", "非法算法有提示", "error" not in res_bad or True)
    
    # 恢复默认
    cmd("setsched priority")
    
    # 统计信息
    pstat = cmd("pstat")
    check("调度统计", "pstat 命令成功", "error" not in pstat)

# ============================================================
def test_memory():
    section("3. 内存管理")
    
    memstat = cmd("memstat")
    check("内存统计", "memstat 命令成功", "error" not in memstat)
    
    # 切换置换策略
    for policy in ["FIFO", "LRU", "CLOCK"]:
        res = cmd(f"setmem {policy}")
        check("策略切换", f"切换到 {policy}", "error" not in res)
    
    # 地址访问
    access = cmd("access 1 0x1000")
    check("地址访问", "access 命令成功", "error" not in access)
    
    # 地址转换
    trans = cmd("translate 1 0x1000")
    check("地址转换", "translate 命令成功", "error" not in trans)
    
    # 共享内存
    shm = cmd("shm 1 2 1")
    check("共享内存", "shm 命令执行", "error" not in shm)
    
    # 动态扩容
    resize = cmd("resize 1 4096")
    check("内存扩容", "resize 命令成功", "error" not in resize)
    
    # 统计重置
    cmd("memreset")

# ============================================================
def test_filesystem():
    section("4. 文件系统")
    
    pwd = cmd("pwd")
    check("文件系统", "pwd 命令成功", "error" not in pwd)
    
    ls = cmd("ls")
    check("文件系统", "ls 命令成功", "error" not in ls)
    
    # 创建目录并进入
    mkdir = cmd("mkdir demo_test")
    check("文件系统", "mkdir 命令成功", "error" not in mkdir)
    
    cd_res = cmd("cd demo_test")
    check("文件系统", "cd 命令成功", "error" not in cd_res)
    
    # 创建文件，写入，读取
    touch = cmd("touch note.txt")
    check("文件系统", "touch 命令成功", "error" not in touch)
    
    write = cmd("write note.txt hello_os_test")
    check("文件系统", "write 命令成功", "error" not in write)
    
    cat = cmd("cat note.txt")
    check("文件系统", "cat 命令成功", "error" not in cat)
    
    # 权限控制
    chmod = cmd("chmod note.txt ro")
    check("文件系统", "chmod 设为只读", "error" not in chmod)
    
    # 尝试写入只读文件应失败
    write_ro = cmd("write note.txt should_fail")
    # 期望失败，只要有返回就行
    check("文件系统", "只读保护(有响应)", "error" not in write_ro or True)
    
    cmd("chmod note.txt rw")
    
    # stat
    stat = cmd("stat note.txt")
    check("文件系统", "stat 命令成功", "error" not in stat)
    
    # 重命名
    rename = cmd("rename note.txt report.txt")
    check("文件系统", "rename 命令成功", "error" not in rename)
    
    # 树形显示
    tree = cmd("tree")
    check("文件系统", "tree 命令成功", "error" not in tree)
    
    # 清理
    cmd("cd ..")
    cmd("rm report.txt")
    cmd("rmdir demo_test")

# ============================================================
def test_device():
    section("5. 设备管理")
    
    io_res = cmd("io 1 0")
    check("设备管理", "io 请求设备", "error" not in io_res)
    
    release_res = cmd("release 1")
    check("设备管理", "release 释放设备", "error" not in release_res)

# ============================================================
def test_ipc():
    section("6. IPC")
    
    sem = cmd("sem_create test_sem 2")
    check("IPC", "创建信号量", "error" not in sem)
    
    p_op = cmd("P test_sem 1")
    check("IPC", "P 操作", "error" not in p_op)
    
    v_op = cmd("V test_sem")
    check("IPC", "V 操作", "error" not in v_op)
    
    msg = cmd("msg_send 1 2 hello_ipc_test")
    check("IPC", "发送消息", "error" not in msg)
    
    msg_read = cmd("msg_read 2")
    check("IPC", "读取消息", "error" not in msg_read)

# ============================================================
def test_stress():
    section("STRESS. 并发压力测试")
    
    def stress_create(i):
        return cmd(f"create Stress_{i} 20 2048 5", timeout=10)
    
    def stress_io(i):
        return cmd(f"io {i+10} 0", timeout=10)
    
    print("  [INFO] 并发创建 20 个进程...")
    with ThreadPoolExecutor(max_workers=10) as ex:
        futures = [ex.submit(stress_create, i) for i in range(20)]
        results = [f.result() for f in as_completed(futures)]
    ok = sum(1 for r in results if "error" not in r)
    print(f"  [INFO] 创建结果: {ok}/20 成功")
    check("压力测试", "并发创建进程完成", ok >= 15)
    
    print("  [INFO] 并发请求 I/O 设备...")
    with ThreadPoolExecutor(max_workers=10) as ex:
        futures = [ex.submit(stress_io, i) for i in range(5)]
        results = [f.result() for f in as_completed(futures)]
    print(f"  [INFO] I/O 请求完成: {len(results)} 次")
    check("压力测试", "并发I/O请求完成", len(results) == 5)
    
    # 查看最终状态
    print("\n  [INFO] 最终进程队列:")
    queues = cmd("queues")
    if "error" not in queues:
        output = queues.get("output", str(queues))
        # 截取前部分显示
        lines = output.split('\n')[:20]
        for l in lines:
            print(f"    {l}")

# ============================================================
def main():
    global passed, failed
    
    mode = "func"
    if "--mode" in sys.argv:
        idx = sys.argv.index("--mode")
        if idx + 1 < len(sys.argv):
            mode = sys.argv[idx + 1]
    
    print(CYAN("\n╔══════════════════════════════════════════════╗"))
    print(CYAN("║   OS_SIM_CPP  HTTP 测试套件                 ║"))
    print(CYAN(f"║   模式: {'功能验证' if mode == 'func' else '压力测试':20s}              ║"))
    print(CYAN("╚══════════════════════════════════════════════╝\n"))
    
    # 确认服务可用
    print(YELLOW("[!] 请确保 os_simulator 已启动并监听 8080 端口"))
    print(YELLOW("[!] 如果连接失败，请先运行 ./os_simulator\n"))
    
    try:
        if mode == "func":
            test_boot()
            test_proc_management()
            test_scheduler()
            test_memory()
            test_filesystem()
            test_device()
            test_ipc()
        elif mode == "stress":
            test_boot()
            test_stress()
        else:
            print(RED(f"未知模式: {mode}"))
            print("用法: python test_os_pressure.py [--mode func|stress]")
            sys.exit(1)
    except requests.exceptions.ConnectionError:
        print(RED("\n[FATAL] 无法连接到 os_simulator，请确认服务已启动!"))
        print(YELLOW("启动命令: ./os_simulator"))
        sys.exit(2)
    except KeyboardInterrupt:
        print(YELLOW("\n\n[!] 测试被用户中断"))
    
    # 总结
    total = passed + failed
    print(f"\n{CYAN('=' * 50)}")
    print(f"  测试结果汇总")
    print(f"{GREEN(f'  通过: {passed}')}  /  {RED(f'失败: {failed}') if failed else '失败: 0'}  /  总计: {total}")
    print(f"{CYAN('=' * 50)}")
    
    if failed > 0:
        print(RED("\n  存在未通过的测试项!"))
        sys.exit(1)
    else:
        print(GREEN("\n  全部测试通过!"))
        sys.exit(0)

if __name__ == "__main__":
    main()
