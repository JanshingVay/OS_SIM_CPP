#include "program.h"
#include <iostream>

std::map<int, PCB> proMap;
static int nextPid = 1;

int createProc(const std::string& name) {
    int pid = nextPid++;
    proMap[pid] = PCB{pid, name, READY, ""};
    std::cout << "[进程] 创建进程 P" << pid << " (" << name << ")\n";
    return pid;
}

int block(int PID, int, const std::string& reason) {
    auto it = proMap.find(PID);
    if (it == proMap.end()) return -1;
    it->second.state = BLOCK;
    it->second.blockReason = reason;
    std::cout << "[进程] 阻塞 P" << PID << ": " << reason << "\n";
    return 0;
}

int block(int PID) { return block(PID, 0, "手动阻塞"); }

int wakeup(int PID) {
    auto it = proMap.find(PID);
    if (it == proMap.end()) return -1;
    it->second.state = READY;
    it->second.blockReason.clear();
    std::cout << "[进程] 唤醒 P" << PID << "\n";
    return 0;
}

std::string stateToString(int state) {
    switch (state) {
        case READY: return "就绪";
        case RUN: return "运行";
        case BLOCK: return "阻塞";
        case END: return "结束";
        case SUSPEND: return "挂起";
        case ZOMBIE: return "僵尸";
        default: return "未知";
    }
}

void showProcessTable() {
    std::cout << "\nPID	NAME	STATE	REASON\n";
    for (const auto& kv : proMap) {
        const PCB& p = kv.second;
        std::cout << p.PID << "	" << p.name << "	" << stateToString(p.state) << "	" << p.blockReason << "\n";
    }
}
