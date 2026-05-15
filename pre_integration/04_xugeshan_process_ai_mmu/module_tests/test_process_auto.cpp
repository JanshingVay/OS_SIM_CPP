#include "process/program.h"
#include <iostream>
#include <cstdlib>
#include <string>

int nowTime = 0;
static int pass_count = 0;
static int fail_count = 0;

void check(bool condition, const std::string& name) {
    if (condition) { ++pass_count; std::cout << "[PASS] " << name << "\n"; }
    else { ++fail_count; std::cout << "[FAIL] " << name << "\n"; }
}

int main() {
    std::cout << "=== 徐舸山：进程管理 / MMU 接口自动测试 ===\n";
    int p1 = createProc("P1", 5, 4096, 10);
    int p2 = createProc("P2", 3, 4096, 6);
    int p3 = createProc("P3", 4, 4096, 12);
    check(p1 > 0 && p2 > 0 && p3 > 0, "创建 3 个进程");
    check(proMap.count(p1) && proMap[p1].state == READY, "PCB 进入 READY 状态");
    check(readVector.size() >= 3, "就绪队列包含新进程");

    check(setScheduleAlgorithm(SCHED_PRIORITY_RR) == 1, "切换 Priority + RR 调度");
    run(); ++nowTime;
    check(currentRunningPID == -1 || proMap.count(currentRunningPID), "调度后运行 PID 合法");

    check(block(p2, 2, "auto test io wait") == 1, "阻塞 P2");
    check(proMap[p2].state == BLOCK, "P2 状态为 BLOCK");
    check(wakeup(p2) == 1, "唤醒 P2");
    check(proMap[p2].state == READY, "P2 回到 READY");

    int child = forkProc(p1);
    check(child > 0 && proMap[child].parentPID == p1, "fork 创建子进程并记录父 PID");
    check(!proMap[p1].children.empty(), "父进程 children 列表更新");

    check(dynamic_resize_memory(p1, PAGE_SIZE) == true, "通过进程接口动态扩展内存");
    check(setScheduleAlgorithm(SCHED_SJF) == 1, "切换 SJF 调度");
    check(setScheduleAlgorithm(SCHED_FCFS) == 1, "切换 FCFS 调度");
    check(setScheduleAlgorithm(SCHED_HRRN) == 1, "切换 HRRN 调度");

    stop(p3);
    check(proMap.find(p3) == proMap.end() || proMap[p3].state == ZOMBIE || proMap[p3].state == END, "结束进程后被回收或进入 ZOMBIE/END");
    std::cout << "进程管理/MMU 接口自动测试完成：" << pass_count << " PASS / " << fail_count << " FAIL\n";
    int code = (fail_count == 0 ? 0 : 1);
    std::cout.flush();
    std::cerr.flush();
    std::_Exit(code);
}
