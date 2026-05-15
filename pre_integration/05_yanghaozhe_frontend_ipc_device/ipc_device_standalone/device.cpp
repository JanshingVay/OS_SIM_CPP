// device.cpp - 设备分配、排队、中断机制与银行家算法核心实现
#include "device.h"
#include "program.h"
#include <iostream>
#include <cstdlib>
#include <algorithm>

// 初始化系统的三大硬件 (3种资源类别)
std::vector<IODevice> sysDevices = {
    {0, "打印机 (Printer)", false, -1, {}},
    {1, "键盘 (Keyboard)", false, -1, {}},
    {2, "磁盘 (Disk)", false, -1, {}}
};

// 银行家算法矩阵初始化 (初始状态下，3个设备均可用)
std::vector<int> Available = { 1, 1, 1 };
std::map<int, std::vector<int>> Max;
std::map<int, std::vector<int>> Allocation;
std::map<int, std::vector<int>> Need;

// 为新进入系统或首次请求设备的进程随机生成最大的资源需求
void initProcessBanker(int pid) {
    if (Max.find(pid) != Max.end()) return; // 已经初始化过了

    // 随机生成该进程生命周期内可能需要的最大资源组合 (0 或 1)
    std::vector<int> max_need = { rand() % 2, rand() % 2, rand() % 2 };

    // 确保它至少需要点什么，不然没意义
    if (max_need[0] == 0 && max_need[1] == 0 && max_need[2] == 0) {
        max_need[rand() % 3] = 1;
    }

    Max[pid] = max_need;
    Allocation[pid] = { 0, 0, 0 };
    Need[pid] = max_need;

    std::cout << "[银行家算法] 进程 PID " << pid << " 初始化最大资源需求: "
        << "[" << max_need[0] << ", " << max_need[1] << ", " << max_need[2] << "]" << std::endl;
}

// 核心：安全性算法 (Safety Algorithm)
bool isSafeState() {
    int resource_types = Available.size();
    std::vector<int> Work = Available;
    std::map<int, bool> Finish;

    // 初始化所有已知进程的 Finish 为 false
    for (auto const& pair : Allocation) {
        Finish[pair.first] = false;
    }

    std::vector<int> safe_sequence;
    bool progress = true;

    while (progress) {
        progress = false;
        for (auto const& pair : Need) {
            int pid = pair.first;
            if (!Finish[pid]) {
                bool can_allocate = true;
                for (int j = 0; j < resource_types; ++j) {
                    if (Need[pid][j] > Work[j]) {
                        can_allocate = false;
                        break;
                    }
                }

                // 如果当前进程的需求可以被满足
                if (can_allocate) {
                    for (int j = 0; j < resource_types; ++j) {
                        Work[j] += Allocation[pid][j]; // 假设它执行完毕并归还资源
                    }
                    Finish[pid] = true;
                    safe_sequence.push_back(pid);
                    progress = true; // 找到了一个进程，继续下一轮扫描
                }
            }
        }
    }

    // 检查是否所有进程都能 Finish
    for (auto const& pair : Finish) {
        if (!pair.second) {
            std::cout << ">>> [安全拦截] 银行家算法检测到潜在【死锁风险】！无法找到安全序列！" << std::endl;
            return false;
        }
    }

    std::cout << ">>> [安全通过] 银行家算法检测完毕，系统安全！找到安全序列: ";
    for (int p : safe_sequence) std::cout << "P" << p << " -> ";
    std::cout << "END" << std::endl;

    return true;
}

// 基于银行家算法的设备请求入口
int requestDeviceBanker(int pid, int devId) {
    if (devId < 0 || static_cast<size_t>(devId) >= sysDevices.size()) return -1;

    auto it = proMap.find(pid);
    if (it == proMap.end()) return -1;

    PCB& pcb = it->second;
    if (pcb.state == END || pcb.state == SUSPEND || pcb.state == BLOCK) {
        return -1;
    }

    // 1. 初始化（如果未初始化过）并进行基本校验
    initProcessBanker(pid);
    std::vector<int> request = { 0, 0, 0 };
    request[devId] = 1;

    if (request[devId] > Need[pid][devId]) {
        std::cout << "[设备拒绝] PID " << pid << " 请求的设备超出了其声明的最大需求量(Max)！" << std::endl;
        return 0; // 错误：非法请求
    }

    if (request[devId] > Available[devId]) {
        // 资源不足，必须排队等待 (由于是单例设备，这就是正常的被别人占用了)
        sysDevices[devId].waitQueue.push_back(pid);
        block(pid, 0, "等待设备释放: " + sysDevices[devId].name);
        std::cout << "[硬件排队] 设备 [" << sysDevices[devId].name << "] 忙碌，进程 PID " << pid << " 进入等待队列。" << std::endl;
        return 2; // 返回 2 表示被阻塞排队
    }

    // 2. 试探性分配 (Pre-allocation)
    Available[devId] -= request[devId];
    Allocation[pid][devId] += request[devId];
    Need[pid][devId] -= request[devId];

    // 3. 执行安全性算法
    std::cout << "[银行家算法] 进程 PID " << pid << " 发起设备请求，正在进行安全性试探分析..." << std::endl;
    if (isSafeState()) {
        // 安全，正式分配！
        sysDevices[devId].isBusy = true;
        sysDevices[devId].currentPID = pid;
        block(pid, 0, "正在使用设备 I/O: " + sysDevices[devId].name); // 模拟I/O处理过程需要挂起CPU
        std::cout << "[硬件层] 分配成功！设备 [" << sysDevices[devId].name << "] 已独占分配给进程 PID: " << pid << std::endl;
        return 1; // 成功
    }
    else {
        // 不安全，必须撤销试探性分配（回滚）
        std::cout << "[设备拒绝] 驳回！若分配给 PID " << pid << " 会导致系统进入不安全状态（死锁）！" << std::endl;
        Available[devId] += request[devId];
        Allocation[pid][devId] -= request[devId];
        Need[pid][devId] += request[devId];
        return 0; // 返回 0 表示因死锁风险被彻底拒绝
    }
}

// 模拟底层硬件 I/O 随机中断并唤醒进程
void processIOInterrupts() {
    for (auto& dev : sysDevices) {
        if (dev.isBusy) {
            // 25% 的概率 I/O 处理完成（触发硬件中断）
            if (rand() % 100 < 25) {
                int finished_pid = dev.currentPID;
                std::cout << ">>> ⚡ [硬件中断] 设备 [" << dev.name << "] 完成了 PID: " << finished_pid << " 的 I/O 任务！" << std::endl;

                // I/O完成，归还资源给银行家系统
                Available[dev.id] += 1;
                Allocation[finished_pid][dev.id] -= 1;
                Need[finished_pid][dev.id] += 1; // 假设以后还可能需要

                wakeup(finished_pid);
                dev.isBusy = false;
                dev.currentPID = -1;

                // 自动调度等待队列（简化的 FCFS 唤醒尝试）
                if (!dev.waitQueue.empty()) {
                    int nextPid = dev.waitQueue.front();
                    dev.waitQueue.pop_front();
                    wakeup(nextPid); // 唤醒它，让它重新尝试发起 requestDeviceBanker 请求
                    std::cout << "[设备调度器] 已唤醒等待队列中的 PID " << nextPid << "，其将重新尝试请求设备。" << std::endl;
                }
            }
        }
    }
}

// 当某个进程被意外 kill 时，清理银行家矩阵并防死锁
void releaseProcessDevices(int pid) {
    // 归还它所持有的所有资源给 Available
    if (Allocation.find(pid) != Allocation.end()) {
        for (int j = 0; j < 3; ++j) {
            Available[j] += Allocation[pid][j];
        }
        Allocation.erase(pid);
        Max.erase(pid);
        Need.erase(pid);
    }

    // 从物理设备的独占状态和等待队列中清理
    for (auto& dev : sysDevices) {
        if (dev.currentPID == pid) {
            dev.isBusy = false;
            dev.currentPID = -1;
            std::cout << "[硬件层] 警告: PID " << pid << " 终止，自动回收被其独占的设备 [" << dev.name << "]" << std::endl;
        }
        auto q_it = std::find(dev.waitQueue.begin(), dev.waitQueue.end(), pid);
        if (q_it != dev.waitQueue.end()) dev.waitQueue.erase(q_it);
    }
}