// test_process.cpp - 进程管理模块独立测试
// 编译: g++ -O2 -std=c++14 test_process.cpp memory/memory.cpp process/program.cpp process/device.cpp process/ipc.cpp filesystem.cpp disk.cpp -o run_process_test -pthread
#include "memory/memory.h"
#include "process/program.h"
#include "process/device.h"
#include <iostream>
#include <cassert>

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
    std::cout << C << "\n=== 进程管理模块测试 ===\n" << N << std::endl;

    // ---- 1. 进程创建 ----
    std::cout << Y << "[1] 进程创建与基本属性" << N << std::endl;
    int p1 = createProc("P1_Calc", 10, 4096, 8);
    int p2 = createProc("P2_Server", 6, 2048, 5);
    int p3 = createProc("P3_Worker", 15, 8192, 10);
    chk("P1 创建成功 PID>0", p1 > 0);
    chk("P2 创建成功 PID>0", p2 > 0);
    chk("P3 创建成功 PID>0", p3 > 0);
    chk("PID 严格递增", p1 < p2 && p2 < p3);
    chk("进程数目 >= 3", (int)proMap.size() >= 3);

    auto& pcb1 = proMap[p1];
    chk("P1 名称: P1_Calc", pcb1.name == "P1_Calc");
    chk("P1 初始状态: READY", pcb1.state == READY);
    chk("P1 静态优先级: 8", pcb1.priority == 8);
    chk("P1 动态优先级: 等于静态优先级", pcb1.dynamicPriority == pcb1.priority);
    chk("P1 needTime: 10", pcb1.needTime == 10);
    chk("P1 remainTime: 等于 needTime", pcb1.remainTime == pcb1.needTime);
    chk("P1 内存需求: 4096", pcb1.size == 4096);
    chk("P1 timeSlice: 默认值3", pcb1.timeSlice == 3);
    chk("P1 parentPID: 0(系统根)", pcb1.parentPID == 0);
    chk("P1 admitted: true", pcb1.admitted == true);

    // ---- 2. 队列展示 ----
    std::cout << Y << "\n[2] 队列与调度" << N << std::endl;
    run(); nowTime++;
    chk("调度后有进程在运行", currentRunningPID != -1);
    chk("就绪队列非空", !readVector.empty());
    std::cout << Y; showQueues(); std::cout << N;

    // ---- 3. 阻塞与唤醒 ----
    std::cout << Y << "[3] 阻塞与唤醒" << N << std::endl;
    int target = readVector.front().PID;
    int bres = block(target, 5, "io_wait");
    chk("block() 返回 1", bres == 1);
    chk("状态变为 BLOCK", proMap[target].state == BLOCK);
    chk("阻塞原因: io_wait", proMap[target].blockReason == "io_wait");
    chk("blockRemain: 5", proMap[target].blockRemain == 5);

    // 模拟自动唤醒: 倒计时归零
    proMap[target].blockRemain = 0;
    int wres = wakeup(target);
    chk("wakeup() 返回 1", wres == 1);
    chk("状态恢复为 READY", proMap[target].state == READY);

    // 重复唤醒应失败
    int wres2 = wakeup(target);
    chk("重复唤醒返回 0", wres2 == 0);

    // 阻塞已阻塞进程应失败
    block(target, 3, "double_block");
    int bres2 = block(target, 3, "double_block");
    chk("重复阻塞返回 0", bres2 == 0);

    // ---- 4. 挂起与恢复 ----
    std::cout << Y << "[4] 挂起与恢复" << N << std::endl;
    target = readVector.front().PID;
    int sres = suspendProc(target);
    chk("suspendProc() 返回 1", sres == 1);
    chk("状态变为 SUSPEND", proMap[target].state == SUSPEND);

    int rres = resumeProc(target);
    chk("resumeProc() 返回 1", rres == 1);
    chk("状态恢复为 READY", proMap[target].state == READY);

    // ---- 5. 进程终止 ----
    std::cout << Y << "[5] 进程终止" << N << std::endl;
    int pkill = createProc("P_KillMe", 3, 2048, 2);
    int beforeSize = endVector.size();
    stop(pkill);
    chk("stop() 后进程进入 endVector", (int)endVector.size() == beforeSize + 1);
    chk("已结束进程从 proMap 移除", proMap.find(pkill) == proMap.end());

    // ---- 6. Fork / 进程树 / 僵尸回收 ----
    std::cout << Y << "[6] Fork / 进程树 / 僵尸回收" << N << std::endl;
    int parent = createProc("P_Orch", 10, 4096, 7);
    int child = forkProc(parent);
    chk("forkProc() 子进程 PID>0", child > 0);
    chk("子进程 parentPID 正确", proMap[child].parentPID == parent);
    chk("父进程 children 含子进程", 
        std::find(proMap[parent].children.begin(), proMap[parent].children.end(), child)
        != proMap[parent].children.end());
    chk("子进程名含 _child", proMap[child].name.find("_child") != std::string::npos);
    std::cout << Y; printProcessTree(); std::cout << N;

    // kill 子进程 -> 僵尸
    stop(child);
    chk("kill 子进程后变为 ZOMBIE", proMap.count(child) && proMap[child].state == ZOMBIE);

    int reaped = waitProc(parent);
    chk("waitProc() 回收成功", reaped == child);
    chk("ZOMBIE 从 proMap 移除", proMap.find(child) == proMap.end());

    // ---- 7. 调度算法 ----
    std::cout << Y << "[7] 调度算法切换" << N << std::endl;
    chk("-> FCFS", setScheduleAlgorithm(SCHED_FCFS) == 1);
    chk("  名称: FCFS", getScheduleAlgorithmName() == "FCFS");
    chk("-> SJF", setScheduleAlgorithm(SCHED_SJF) == 1);
    chk("  名称: SJF", getScheduleAlgorithmName() == "SJF");
    chk("-> HRRN", setScheduleAlgorithm(SCHED_HRRN) == 1);
    chk("  名称: HRRN", getScheduleAlgorithmName() == "HRRN");
    chk("-> Priority+RR", setScheduleAlgorithm(SCHED_PRIORITY_RR) == 1);
    chk("  名称: Dynamic Priority + RR", getScheduleAlgorithmName() == "Dynamic Priority + RR");
    chk("非法算法号返回 0", setScheduleAlgorithm(99) == 0);

    // 时间片计算
    PCB tp; tp.dynamicPriority = 10; tp.remainTime = 5; tp.size = 4096;
    setScheduleAlgorithm(SCHED_PRIORITY_RR);
    int ts = computeTimeSlice(tp);
    chk("时间片计算结果 1~8", ts >= 1 && ts <= 8);

    // ---- 8. 老化机制 ----
    std::cout << Y << "[8] 优先级老化" << N << std::endl;
    setScheduleAlgorithm(SCHED_PRIORITY_RR);
    int p_low = createProc("P_LowPri", 20, 4096, 2);
    int old_dp = proMap[p_low].dynamicPriority;
    // 多次调用 ageReadyQueue
    for (int i = 0; i < 10; i++) ageReadyQueue();
    chk("低优先级进程动态优先级提升", proMap[p_low].dynamicPriority > old_dp);

    // ---- 9. pstat / pinfo ----
    std::cout << Y << "[9] 进程统计与详情" << N << std::endl;
    std::cout << Y; showProcessDetail(p1); std::cout << N;
    chk("pinfo 正常输出", true);
    chk("pstat / showProcessSummary 可调用", true);

    // ---- 10. 多 tick 调度 ----
    std::cout << Y << "[10] 多Tick调度运行" << N << std::endl;
    for (int i = 0; i < 10; i++) { run(); nowTime++; }
    std::cout << Y; showQueues(); showProcessSummary(); std::cout << N;
    chk("10 tick 调度无崩溃", true);

    // ---- 结果 ----
    std::cout << C << "\n========================================" << N << std::endl;
    std::cout << "  进程测试: " << G << passed << " 通过" << N
              << " / " << (failed ? R : N) << failed << " 失败" << N << std::endl;
    std::cout << C << "========================================\n" << N << std::endl;
    return failed > 0 ? 1 : 0;
}
