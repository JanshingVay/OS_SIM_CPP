# test_os_pressure.py - OS 内核并发压力测试与死锁检测脚本

import requests
import time
import random
import threading

BASE_URL = "http://localhost:8080/api"
PROCESS_NAMES = ["Chrome", "WeChat", "Word", "VSCode", "MySQL", "Docker", "Game"]

# 统计数据
test_stats = {"created": 0, "io_requests": 0, "errors": 0}

def send_command(cmd):
    try:
        res = requests.post(f"{BASE_URL}/command", json={"command": cmd}, timeout=2)
        if res.status_code == 200:
            return True
        else:
            test_stats["errors"] += 1
    except:
        test_stats["errors"] += 1
    return False

def worker_create_processes(num):
    """模拟疯狂创建进程，压测内存管理器"""
    for _ in range(num):
        name = random.choice(PROCESS_NAMES) + str(random.randint(100, 999))
        time_need = random.randint(5, 20)
        mem_need = random.randint(1024, 8192) # 随机申请 1K ~ 8K 内存
        priority = random.randint(1, 10)
        
        cmd = f"create {name} {time_need} {mem_need} {priority}"
        if send_command(cmd):
            test_stats["created"] += 1
        time.sleep(random.uniform(0.1, 0.5))

def worker_random_io():
    """模拟疯狂请求 I/O 设备，压测设备排队与死锁预防"""
    for _ in range(30):
        pid = random.randint(1, 15) # 假设PID在1-15之间
        dev_id = random.randint(0, 2) # 0打印机, 1键盘, 2磁盘
        if send_command(f"io {pid} {dev_id}"):
            test_stats["io_requests"] += 1
        time.sleep(random.uniform(0.5, 1.5))

if __name__ == "__main__":
    print("🚀 开始 OS 内核极限并发压力测试...")
    print("正在启动并发测试线程...")
    
    threads = []
    # 启动 3 个线程疯狂创建进程
    for i in range(3):
        t = threading.Thread(target=worker_create_processes, args=(10,))
        threads.append(t)
        t.start()
        
    # 启动 2 个线程疯狂抢占设备
    for i in range(2):
        t = threading.Thread(target=worker_random_io)
        threads.append(t)
        t.start()
        
    for t in threads:
        t.join()
        
    print("\n✅ 压测结束！系统运行报告：")
    print(f" - 成功发送创建指令: {test_stats['created']} 次")
    print(f" - 成功发送 I/O 抢占指令: {test_stats['io_requests']} 次")
    print(f" - 网络/解析异常: {test_stats['errors']} 次")
    print("请前往前端大屏查看内核队列是否发生死锁，内存是否正确回收！")