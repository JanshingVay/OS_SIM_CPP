#include "process/program.h"
#include <iostream>
#include <sstream>
#include <string>

int nowTime = 0;

static void print_help() {
    std::cout << "\n可用命令：\n"
              << "  help                                  显示帮助\n"
              << "  create <name> <need> <mem> <prio>     创建进程\n"
              << "  ps                                    查看进程摘要\n"
              << "  queues                                查看就绪/阻塞/挂起/僵尸队列\n"
              << "  cores                                 查看 SMP 多核心运行状态\n"
              << "  run [n]                               运行调度器 n 个时钟周期\n"
              << "  sched <priority|sjf|fcfs|hrrn|mlfq>   切换调度算法\n"
              << "  block <pid> [ticks] [reason]          阻塞进程，ticks<=0 为手动阻塞\n"
              << "  wake <pid>                            唤醒阻塞进程\n"
              << "  suspend <pid> | resume <pid>          挂起/恢复进程\n"
              << "  fork <pid>                            创建子进程\n"
              << "  wait <pid>                            等待/回收子进程\n"
              << "  kill <pid>                            结束进程\n"
              << "  signal <pid> <9|19>                   投递软中断信号：9=SIGKILL, 19=SIGSTOP\n"
              << "  ulimit <pid> <max_children>           设置最大子进程数量\n"
              << "  setpgid <pid> <pgid>                  设置进程组 ID\n"
              << "  killgroup <pgid>                      终止整个进程组\n"
              << "  tree                                  打印进程树\n"
              << "  detail <pid>                          查看详细 PCB\n"
              << "  resize <pid> <delta_bytes>            通过 MMU 接口动态调整内存\n"
              << "  time                                  查看当前模拟时间\n"
              << "  exit                                  退出\n\n";
}

static bool set_sched_by_name(const std::string& name) {
    if (name == "priority" || name == "rr") return setScheduleAlgorithm(SCHED_PRIORITY_RR) == 1;
    if (name == "sjf") return setScheduleAlgorithm(SCHED_SJF) == 1;
    if (name == "fcfs") return setScheduleAlgorithm(SCHED_FCFS) == 1;
    if (name == "hrrn") return setScheduleAlgorithm(SCHED_HRRN) == 1;
    if (name == "mlfq") return setScheduleAlgorithm(SCHED_MLFQ) == 1;
    return false;
}
static void show_cores() {
    std::cout << "\nSMP 核心状态：\n";
    for (int i = 0; i < MAX_CORES; ++i) {
        std::cout << "Core " << i << ": ";
        if (cpuCores[i] != -1 && proMap.find(cpuCores[i]) != proMap.end()) {
            const PCB& p = proMap[cpuCores[i]];
            std::cout << "P" << p.PID << " " << p.name << " state=" << stateToString(p.state) << " Q" << p.queueLevel << " remain=" << p.remainTime;
        } else {
            std::cout << "空闲";
        }
        std::cout << "\n";
    }
}

int main() {
    std::cout << "=== 进程管理模块演示 ===\n";
    std::cout << "输入 help 查看命令。本程序依赖 process.cpp + memory.cpp + disk.cpp。\n";
    print_help();
    std::string line;
    while (true) {
        std::cout << "进程> ";
        if (!std::getline(std::cin, line)) break;
        if (line.empty()) continue;
        std::istringstream iss(line);
        std::string cmd; iss >> cmd;
        if (cmd == "exit" || cmd == "quit") break;
        else if (cmd == "help") print_help();
        else if (cmd == "create") {
            std::string name; int need, mem, prio; iss >> name >> need >> mem >> prio;
            int pid = createProc(name, need, mem, prio);
            std::cout << "PID=" << pid << "\n";
        }
        else if (cmd == "ps") showProcessSummary();
        else if (cmd == "queues") showQueues();
        else if (cmd == "cores") show_cores();
        else if (cmd == "run") {
            int n = 1; if (!(iss >> n)) n = 1;
            for (int i = 0; i < n; ++i) { run(); nowTime++; }
            std::cout << "[成功] 当前时间 nowTime=" << nowTime << "\n";
        }
        else if (cmd == "sched") { std::string s; iss >> s; std::cout << (set_sched_by_name(s) ? "[成功] " : "[失败] ") << getScheduleAlgorithmName() << "\n"; }
        else if (cmd == "block") {
            int pid, ticks = 0; std::string reason; iss >> pid; if (iss >> ticks) { std::getline(iss, reason); if (!reason.empty() && reason[0]==' ') reason.erase(0,1); }
            if (reason.empty()) reason = "交互式阻塞";
            int ok = ticks > 0 ? block(pid, ticks, reason) : block(pid, 0, reason);
            std::cout << (ok ? "[成功]\n" : "[失败]\n");
        }
        else if (cmd == "wake") { int pid; iss >> pid; std::cout << (wakeup(pid) ? "[成功]\n" : "[失败]\n"); }
        else if (cmd == "suspend") { int pid; iss >> pid; std::cout << (suspendProc(pid) ? "[成功]\n" : "[失败]\n"); }
        else if (cmd == "resume") { int pid; iss >> pid; std::cout << (resumeProc(pid) ? "[成功]\n" : "[失败]\n"); }
        else if (cmd == "fork") { int pid; iss >> pid; std::cout << "子进程 PID=" << forkProc(pid) << "\n"; }
        else if (cmd == "wait") { int pid; iss >> pid; std::cout << "wait 返回值=" << waitProc(pid) << "\n"; }
        else if (cmd == "kill") { int pid; iss >> pid; stop(pid); std::cout << "[成功]\n"; }
        else if (cmd == "signal") { int pid, sig; iss >> pid >> sig; std::cout << (sendSignal(pid, sig) ? "[成功] 已投递信号\n" : "[失败] PID 不存在\n"); }
        else if (cmd == "ulimit") { int pid, limit; iss >> pid >> limit; std::cout << (setMaxChildrenLimit(pid, limit) ? "[成功] 已设置子进程配额\n" : "[失败] PID 不存在或参数无效\n"); }
        else if (cmd == "setpgid") { int pid, pgid; iss >> pid >> pgid; std::cout << (setProcessGroup(pid, pgid) ? "[成功] 已设置进程组\n" : "[失败] PID 不存在\n"); }
        else if (cmd == "killgroup") { int pgid; iss >> pgid; std::cout << "[成功] 已终止进程数=" << killGroup(pgid) << "\n"; }
        else if (cmd == "tree") printProcessTree();
        else if (cmd == "detail") { int pid; iss >> pid; showProcessDetail(pid); }
        else if (cmd == "resize") { int pid, delta; iss >> pid >> delta; std::cout << (dynamic_resize_memory(pid, delta) ? "[成功]\n" : "[失败]\n"); }
        else if (cmd == "time") std::cout << "当前模拟时间 nowTime=" << nowTime << "\n";
        else std::cout << "未知命令，请输入 help 查看帮助。\n";
    }
    std::cout << "已退出。\n";
    return 0;
}
