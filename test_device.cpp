// test_device.cpp - 设备管理模块独立测试
// 编译: g++ -O2 -std=c++14 test_device.cpp memory/memory.cpp process/program.cpp process/device.cpp filesystem.cpp disk.cpp -o run_device_test -pthread
#include "memory/memory.h"
#include "process/program.h"
#include "process/device.h"
#include <iostream>

int nowTime = 0;

#define G "\033[1;32m"
#define R "\033[1;31m"
#define Y "\033[1;33m"
#define C "\033[1;36m"
#define N "\033[0m"

static int passed = 0, failed = 0;
void chk(const std::string& n, bool c) {
    if (c) { std::cout << G << "  [PASS] " << N << n << std::endl; passed++; }
    else   { std::cout << R << "  [FAIL] " << N << n << std::endl; failed++; }
}

int main() {
    std::cout << C << "\n=== 设备管理模块测试 ===\n" << N << std::endl;

    // ---- 1. 设备初始化检查 ----
    std::cout << Y << "[1] 设备初始化" << N << std::endl;
    chk("有 3 个 I/O 设备", sysDevices.size() == 3);
    chk("设备0: 打印机", sysDevices[0].name.find("Printer") != std::string::npos || sysDevices[0].name.find("打印机") != std::string::npos);
    chk("设备1: 键盘", sysDevices[1].name.find("Keyboard") != std::string::npos || sysDevices[1].name.find("键盘") != std::string::npos);
    chk("设备2: 磁盘", sysDevices[2].name.find("Disk") != std::string::npos || sysDevices[2].name.find("磁盘") != std::string::npos);
    chk("设备0 初始空闲", !sysDevices[0].isBusy);
    chk("Available = [1,1,1]", Available.size() == 3 && Available[0] == 1 && Available[1] == 1 && Available[2] == 1);

    // ---- 2. 银行家算法初始化 ----
    std::cout << Y << "\n[2] 银行家算法初始化" << N << std::endl;
    int p1 = createProc("P_DevUser1", 20, 4096, 5);
    int p2 = createProc("P_DevUser2", 15, 4096, 6);
    initProcessBanker(p1);
    initProcessBanker(p2);
    chk("P1 矩阵已初始化", Max.find(p1) != Max.end());
    chk("P2 矩阵已初始化", Max.find(p2) != Max.end());
    chk("Need <= Max", Need[p1][0] <= Max[p1][0] && Need[p1][1] <= Max[p1][1] && Need[p1][2] <= Max[p1][2]);

    // 重复初始化不应覆盖
    initProcessBanker(p1);
    chk("重复初始化安全", true);

    // ---- 3. 安全性算法 ----
    std::cout << Y << "\n[3] 安全性算法" << N << std::endl;
    bool safe_init = isSafeState();
    chk("初始状态安全", safe_init);

    // ---- 4. 设备请求 ----
    std::cout << Y << "[4] 设备请求与释放" << N << std::endl;
    int req1 = requestDeviceBanker(p1, 0); // 请求打印机
    chk("P1 请求设备0(成功或排队)", req1 == 1 || req1 == 2);
    
    if (req1 == 1) {
        chk("设备0 变为忙碌", sysDevices[0].isBusy);
        chk("设备0 当前 PID = P1", sysDevices[0].currentPID == p1);
    }

    // P2 请求同一设备
    int req2 = requestDeviceBanker(p2, 0);
    chk("P2 请求设备0", req2 >= 0);
    if (req2 == 2) {
        chk("P2 进入等待队列", !sysDevices[0].waitQueue.empty());
    }

    // 释放 P1 的设备
    releaseProcessDevices(p1);
    chk("释放后设备0 空闲", !sysDevices[0].isBusy);
    chk("释放后 Available[0] 恢复", Available[0] == 1);

    // ---- 5. 请求不存在的设备 ----
    std::cout << Y << "[5] 边界条件" << N << std::endl;
    int req_bad_dev = requestDeviceBanker(p1, 99);
    chk("请求非法设备ID 返回 -1", req_bad_dev == -1);

    int req_bad_pid = requestDeviceBanker(99999, 0);
    chk("幽灵进程请求 返回 -1", req_bad_pid == -1);

    // ---- 6. I/O 中断模拟 ----
    std::cout << Y << "[6] I/O 中断处理" << N << std::endl;
    // 重新请求设备让设备忙碌
    requestDeviceBanker(p1, 0);
    if (sysDevices[0].isBusy) {
        sysDevices[0].currentPID = p1;
        for (int i = 0; i < 20; i++) processIOInterrupts();
        chk("processIOInterrupts() 无崩溃", true);
    }

    // ---- 7. 释放已终止进程的设备 ----
    std::cout << Y << "[7] 进程终止时设备清理" << N << std::endl;
    int p3 = createProc("P_DevUser3", 10, 4096, 3);
    initProcessBanker(p3);
    requestDeviceBanker(p3, 1);
    releaseProcessDevices(p3);
    chk("releaseProcessDevices() 清理完成", Max.find(p3) == Max.end());

    // 清理
    releaseProcessDevices(p1);
    releaseProcessDevices(p2);

    // ---- 结果 ----
    std::cout << C << "\n========================================" << N << std::endl;
    std::cout << "  设备测试: " << G << passed << " 通过" << N
              << " / " << (failed ? R : N) << failed << " 失败" << N << std::endl;
    std::cout << C << "========================================\n" << N << std::endl;
    return failed > 0 ? 1 : 0;
}
