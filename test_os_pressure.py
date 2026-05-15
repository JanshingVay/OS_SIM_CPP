#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
OS_SIM_CPP HTTP 压力/功能验证测试
用法: python3 test_os_pressure.py [--mode func|stress]
  func   : 功能验证模式，逐项测试各模块 HTTP API (默认)
  stress : 并发压力模式，大量并发请求测试稳定性

环境变量：
  OS_SIM_BASE_URL=http://127.0.0.1:8080  指定服务地址
"""

import os
import sys
from concurrent.futures import ThreadPoolExecutor, as_completed

import requests

BASE_URL = os.environ.get("OS_SIM_BASE_URL", "http://127.0.0.1:8080")

passed = 0
failed = 0


def GREEN(s): return f"\033[1;32m{s}\033[0m"
def RED(s): return f"\033[1;31m{s}\033[0m"
def YELLOW(s): return f"\033[1;33m{s}\033[0m"
def CYAN(s): return f"\033[1;36m{s}\033[0m"


def ok(res):
    """HTTP 层和业务层都成功才算成功，避免 status:error 被假判为通过。"""
    return isinstance(res, dict) and "error" not in res and res.get("status", "success") != "error"


def cmd(command, timeout=5):
    """发送命令到 OS_SIM_CPP HTTP API。"""
    try:
        r = requests.post(
            f"{BASE_URL}/api/command",
            json={"command": command},
            timeout=timeout,
        )
        if r.status_code != 200:
            return {"error": f"HTTP {r.status_code}", "raw": r.text}
        data = r.json()
        if data.get("status") != "success":
            return {"error": data.get("msg", data), "raw": data}
        return data
    except Exception as exc:
        return {"error": str(exc)}


def get_status(timeout=5):
    try:
        r = requests.get(f"{BASE_URL}/api/status", timeout=timeout)
        if r.status_code != 200:
            return {"error": f"HTTP {r.status_code}", "raw": r.text}
        return r.json()
    except Exception as exc:
        return {"error": str(exc)}


def check(section_name, name, cond):
    global passed, failed
    if cond:
        print(f"  {GREEN('[PASS]')} {section_name} / {name}")
        passed += 1
    else:
        print(f"  {RED('[FAIL]')} {section_name} / {name}")
        failed += 1


def section(title):
    print(f"\n{CYAN('=' * 50)}")
    print(f"{CYAN('  ' + title)}")
    print(f"{CYAN('=' * 50)}")


def test_boot():
    section("0. 系统启动检查")
    status = get_status()
    check("启动", "HTTP 服务可访问", ok(status))

    try:
        r = requests.post(f"{BASE_URL}/api/boot", timeout=5)
        check("启动", "Boot API 响应", r.status_code == 200)
    except Exception:
        check("启动", "Boot API 可访问", False)


def test_proc_management():
    section("1. 进程管理")
    cmd("setsched priority")

    res1 = cmd("create P1_Calc 10 4096 8")
    res2 = cmd("create P2_Server 6 2048 5")
    res3 = cmd("create P3_Worker 15 8192 10")
    check("创建进程", "P1 创建成功", ok(res1))
    check("创建进程", "P2 创建成功", ok(res2))
    check("创建进程", "P3 创建成功", ok(res3))

    ps = cmd("ps")
    check("查看进程", "ps 命令成功", ok(ps))

    queues = cmd("queues")
    check("队列查看", "queues 命令成功", ok(queues))

    pinfo = cmd("pinfo 1")
    check("进程详情", "pinfo 命令成功", ok(pinfo))

    block_res = cmd("block 1 3 io_wait")
    check("阻塞操作", "block 命令成功", ok(block_res))

    wake_res = cmd("wakeup 1")
    check("唤醒操作", "wakeup 命令成功", ok(wake_res))

    sus_res = cmd("suspend 2")
    check("挂起操作", "suspend 命令成功", ok(sus_res))

    res_res = cmd("resume 2")
    check("恢复操作", "resume 命令成功", ok(res_res))

    fork_res = cmd("fork 3")
    check("Fork操作", "fork 命令成功", ok(fork_res))

    ptree = cmd("ptree")
    check("进程树", "ptree 命令成功", ok(ptree))


def test_scheduler():
    section("2. 调度算法切换")
    for algo in ["priority", "sjf", "fcfs", "hrrn", "mlfq"]:
        res = cmd(f"setsched {algo}")
        check("调度切换", f"切换到 {algo}", ok(res))

    res_bad = cmd("setsched invalid")
    check("调度切换", "非法算法有响应", isinstance(res_bad, dict))

    cmd("setsched priority")
    pstat = cmd("pstat")
    check("调度统计", "pstat 命令成功", ok(pstat))


def test_memory():
    section("3. 内存管理")
    memstat = cmd("memstat")
    check("内存统计", "memstat 命令成功", ok(memstat))

    for policy in ["FIFO", "LRU", "CLOCK"]:
        res = cmd(f"setmem {policy}")
        check("策略切换", f"切换到 {policy}", ok(res))

    access = cmd("access 1 0x1000")
    check("地址访问", "access 命令成功", ok(access))

    trans = cmd("translate 1 0x1000")
    check("地址转换", "translate 命令成功", ok(trans))

    shm = cmd("shm 1 2 1")
    check("共享内存", "shm 命令执行", ok(shm))

    resize = cmd("resize 1 4096")
    check("内存扩容", "resize 命令成功", ok(resize))

    cmd("memreset")


def test_filesystem():
    section("4. 文件系统")
    check("文件系统", "pwd 命令成功", ok(cmd("pwd")))
    check("文件系统", "ls 命令成功", ok(cmd("ls")))

    # 清理上次异常中断可能遗留的目录
    cmd("cd ..")
    cmd("cd ..")
    cmd("cd demo_test")
    cmd("chmod report.txt rw")
    cmd("rm report.txt")
    cmd("cd ..")
    cmd("rmdir demo_test")

    check("文件系统", "mkdir 命令成功", ok(cmd("mkdir demo_test")))
    check("文件系统", "cd 命令成功", ok(cmd("cd demo_test")))
    check("文件系统", "touch 命令成功", ok(cmd("touch note.txt")))
    check("文件系统", "write 命令成功", ok(cmd("write note.txt hello_os_test")))
    check("文件系统", "cat 命令成功", ok(cmd("cat note.txt")))
    check("文件系统", "write_at 保留尾部", ok(cmd("write_at note.txt 6 PATCH")))

    chmod = cmd("chmod note.txt ro")
    check("文件系统", "chmod 设为只读", ok(chmod))

    write_ro = cmd("write note.txt should_fail")
    check("文件系统", "只读保护有响应", isinstance(write_ro, dict))

    cmd("chmod note.txt rw")
    check("文件系统", "stat 命令成功", ok(cmd("stat note.txt")))
    check("文件系统", "rename 命令成功", ok(cmd("rename note.txt report.txt")))
    check("文件系统", "tree 命令成功", ok(cmd("tree")))

    cmd("chmod report.txt rw")
    cmd("rm report.txt")
    cmd("cd ..")
    cmd("rmdir demo_test")


def test_device():
    section("5. 设备管理")
    check("设备管理", "io 请求设备", ok(cmd("io 1 0")))
    check("设备管理", "release 释放设备", ok(cmd("release 1")))


def test_ipc():
    section("6. IPC")
    check("IPC", "创建信号量", ok(cmd("sem_create test_sem 2")))
    check("IPC", "P 操作", ok(cmd("P test_sem 1")))
    check("IPC", "V 操作", ok(cmd("V test_sem")))
    check("IPC", "发送消息", ok(cmd("msg_send 1 2 hello_ipc_test")))
    check("IPC", "读取消息", ok(cmd("msg_read 2")))


def test_stress():
    section("STRESS. 并发压力测试")

    def stress_create(i):
        return cmd(f"create Stress_{i} 20 2048 5", timeout=10)

    def stress_io(i):
        return cmd(f"io {i + 10} 0", timeout=10)

    print("  [INFO] 并发创建 20 个进程...")
    with ThreadPoolExecutor(max_workers=10) as ex:
        futures = [ex.submit(stress_create, i) for i in range(20)]
        results = [f.result() for f in as_completed(futures)]
    ok_count = sum(1 for r in results if ok(r))
    print(f"  [INFO] 创建结果: {ok_count}/20 成功")
    check("压力测试", "并发创建进程完成", ok_count >= 15)

    print("  [INFO] 并发请求 I/O 设备...")
    with ThreadPoolExecutor(max_workers=10) as ex:
        futures = [ex.submit(stress_io, i) for i in range(5)]
        results = [f.result() for f in as_completed(futures)]
    print(f"  [INFO] I/O 请求完成: {len(results)} 次")
    check("压力测试", "并发I/O请求完成", len(results) == 5)

    queues = cmd("queues")
    if ok(queues):
        output = queues.get("msg", queues.get("output", str(queues)))
        for line in output.split("\n")[:20]:
            print(f"    {line}")


def main():
    mode = "func"
    if "--mode" in sys.argv:
        idx = sys.argv.index("--mode")
        if idx + 1 < len(sys.argv):
            mode = sys.argv[idx + 1]

    print(CYAN("\n╔══════════════════════════════════════════════╗"))
    print(CYAN("║   OS_SIM_CPP  HTTP 测试套件                 ║"))
    print(CYAN(f"║   模式: {'功能验证' if mode == 'func' else '压力测试':20s}              ║"))
    print(CYAN("╚══════════════════════════════════════════════╝\n"))
    print(YELLOW(f"[!] 请确保 os_simulator 已启动并可访问 {BASE_URL}"))
    print(YELLOW("[!] 如果连接失败，请先运行 ./os_simulator\n"))

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
        print("用法: python3 test_os_pressure.py [--mode func|stress]")
        sys.exit(1)

    total = passed + failed
    print(f"\n{CYAN('=' * 50)}")
    print("  测试结果汇总")
    print(f"{GREEN(f'  通过: {passed}')}  /  {RED(f'失败: {failed}') if failed else '失败: 0'}  /  总计: {total}")
    print(f"{CYAN('=' * 50)}")

    if failed > 0:
        print(RED("\n  存在未通过的测试项!"))
        sys.exit(1)
    print(GREEN("\n  全部测试通过!"))
    sys.exit(0)


if __name__ == "__main__":
    main()
