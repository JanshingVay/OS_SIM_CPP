#include "program.h"
#include "device.h" 
#include <iomanip>
#include <numeric>
#include <cstdlib>

std::map<int, PCB> proMap;
std::vector<PCB> endVector;
std::vector<PCB> readVector;
std::vector<PCB> blockVector;
std::vector<PCB> suspendVector;
std::vector<PCB> zombieVector; // 僵尸队列
int currentRunningPID = -1;
int PID_COUNTER = 0;
int currentScheduleAlgo = SCHED_PRIORITY_RR;
extern int nowTime;

namespace {
    constexpr int DEFAULT_TIME_SLICE = 3;
    constexpr int MAX_PRIORITY = 20;
    constexpr int MIN_PRIORITY = 1;
    constexpr int AGING_THRESHOLD = 2;
    constexpr int BOOST_INTERVAL = 12;

    void syncPCBToQueue(std::vector<PCB>& q, int pid) {
        for (auto& item : q) {
            if (item.PID == pid) { item = proMap[pid]; return; }
        }
    }

    void pushReady(const PCB& pcb) {
        for (auto& p : readVector) {
            if (p.PID == pcb.PID) { p = pcb; return; }
        }
        readVector.push_back(pcb);
    }

    void pushBlock(const PCB& pcb) {
        for (auto& p : blockVector) {
            if (p.PID == pcb.PID) { p = pcb; return; }
        }
        blockVector.push_back(pcb);
    }

    void pushSuspend(const PCB& pcb) {
        for (auto& p : suspendVector) {
            if (p.PID == pcb.PID) { p = pcb; return; }
        }
        suspendVector.push_back(pcb);
    }

    bool betterHRRN(const PCB& a, const PCB& b) {
        double ra = (a.waitTime + std::max(1, a.remainTime)) * 1.0 / std::max(1, a.remainTime);
        double rb = (b.waitTime + std::max(1, b.remainTime)) * 1.0 / std::max(1, b.remainTime);
        if (ra != rb) return ra > rb;
        if (a.dynamicPriority != b.dynamicPriority) return a.dynamicPriority > b.dynamicPriority;
        return a.arriveTime < b.arriveTime;
    }
}

std::string stateToString(int state) {
    switch (state) {
    case READY: return "READY"; case RUN: return "RUN"; case BLOCK: return "BLOCK";
    case END: return "END"; case SUSPEND: return "SUSPEND"; 
    case ZOMBIE: return "ZOMBIE"; default: return "UNKNOWN";
    }
}

std::string getScheduleAlgorithmName() {
    switch (currentScheduleAlgo) {
    case SCHED_PRIORITY_RR: return "Dynamic Priority + RR"; case SCHED_SJF: return "SJF";
    case SCHED_FCFS: return "FCFS"; case SCHED_HRRN: return "HRRN"; default: return "Unknown";
    }
}

int setScheduleAlgorithm(int algo) {
    if (algo < SCHED_PRIORITY_RR || algo > SCHED_HRRN) return 0;
    currentScheduleAlgo = algo;
    std::cout << "[调度器] 已切换调度算法为: " << getScheduleAlgorithmName() << std::endl;
    return 1;
}

int computeTimeSlice(const PCB& pcb) {
    int slice = DEFAULT_TIME_SLICE;
    if (currentScheduleAlgo == SCHED_PRIORITY_RR) {
        slice += pcb.dynamicPriority / 4;
        if (pcb.remainTime <= 2) slice += 1;
        if (pcb.size >= 8) slice += 1;
    }
    else if (currentScheduleAlgo == SCHED_FCFS) {
        slice = std::max(1, pcb.remainTime);
    }
    else if (currentScheduleAlgo == SCHED_SJF) {
        slice = std::min(2, std::max(1, pcb.remainTime));
    }
    else if (currentScheduleAlgo == SCHED_HRRN) {
        slice = std::min(4, std::max(1, pcb.remainTime));
    }
    return std::max(1, std::min(8, slice));
}

void eraseRead(int PID) {
    for (auto it = readVector.begin(); it != readVector.end(); ++it) {
        if (it->PID == PID) { readVector.erase(it); return; }
    }
}

void eraseBlock(int PID) {
    for (auto it = blockVector.begin(); it != blockVector.end(); ++it) {
        if (it->PID == PID) { blockVector.erase(it); return; }
    }
}

void eraseSuspend(int PID) {
    for (auto it = suspendVector.begin(); it != suspendVector.end(); ++it) {
        if (it->PID == PID) { suspendVector.erase(it); return; }
    }
}

void rebuildSpecialQueues() {
    blockVector.clear(); suspendVector.clear(); zombieVector.clear();
    for (const auto& kv : proMap) {
        if (kv.second.state == BLOCK) blockVector.push_back(kv.second);
        else if (kv.second.state == SUSPEND) suspendVector.push_back(kv.second);
        else if (kv.second.state == ZOMBIE) zombieVector.push_back(kv.second);
    }
}

int createProc(std::string name, int needTime, int size, int priority) {
    PCB newPCB;
    newPCB.PID = ++PID_COUNTER; newPCB.name = name;
    newPCB.needTime = std::max(1, needTime); newPCB.remainTime = newPCB.needTime;
    newPCB.size = std::max(1, size);
    newPCB.priority = std::max(MIN_PRIORITY, std::min(MAX_PRIORITY, priority));
    newPCB.dynamicPriority = newPCB.priority;
    newPCB.timeSlice = DEFAULT_TIME_SLICE; newPCB.currentSlice = 0;
    newPCB.state = READY; newPCB.arriveTime = nowTime; newPCB.lastReadyTime = nowTime;
    newPCB.parentPID = 0; // 系统根节点创建的进程

    if (!mem_manager.alloc_mem(newPCB.PID, newPCB.size)) {
        newPCB.state = SUSPEND; newPCB.admitted = false; proMap[newPCB.PID] = newPCB; pushSuspend(newPCB);
        std::cout << "[后备队列] 内存不足，进程 PID: " << newPCB.PID << " 进入挂起队列。" << std::endl;
        return newPCB.PID;
    }
    newPCB.admitted = true; proMap[newPCB.PID] = newPCB; pushReady(newPCB);
    return newPCB.PID;
}

// Fork 进程
int forkProc(int parentPID) {
    auto it = proMap.find(parentPID);
    if (it == proMap.end()) return -1;
    
    PCB& parent = it->second;
    PCB child = parent; // 深拷贝
    
    child.PID = ++PID_COUNTER;
    child.name = parent.name + "_child";
    child.parentPID = parentPID;
    child.children.clear();
    
    child.state = READY;
    child.arriveTime = nowTime;
    child.lastReadyTime = nowTime;
    
    child.runTime = 0; child.waitTime = 0; child.blockTime = 0;
    child.firstRunTime = -1; child.finishTime = -1;
    child.turnaroundTime = 0; child.weightedTurnaround = 0.0;
    child.contextSwitches = 0; child.cpuBursts = 0; child.ageTicks = 0;
    
    parent.children.push_back(child.PID);

    if (!mem_manager.alloc_mem(child.PID, child.size)) {
        child.state = SUSPEND; child.admitted = false; 
        proMap[child.PID] = child; pushSuspend(child);
        std::cout << "[Fork] 内存不足，子进程 PID: " << child.PID << " 进入挂起队列。" << std::endl;
        return child.PID;
    }
    
    child.admitted = true; proMap[child.PID] = child; pushReady(child);
    return child.PID;
}

// 等待回收僵尸进程
int waitProc(int parentPID) {
    auto it = proMap.find(parentPID);
    if (it == proMap.end()) return -1;
    PCB& parent = it->second;

    if (parent.children.empty()) return -1; 

    int reapedChild = -1;
    for (auto cit = parent.children.begin(); cit != parent.children.end(); ++cit) {
        int childPID = *cit;
        if (proMap.find(childPID) != proMap.end() && proMap[childPID].state == ZOMBIE) {
            reapedChild = childPID;
            parent.children.erase(cit);
            break;
        }
    }

    if (reapedChild != -1) {
        PCB child = proMap[reapedChild];
        child.state = END;
        endVector.push_back(child);
        proMap.erase(reapedChild);
        rebuildSpecialQueues();
        return reapedChild; 
    }

    // 没有已死子进程，自我阻塞
    block(parentPID, 0, "等待子进程");
    return 0;
}

// 树状打印助手
void printProcessTreeHelper(int pid, int depth, std::ostringstream& oss) {
    if (proMap.find(pid) == proMap.end()) return;
    PCB& pcb = proMap[pid];
    for (int i = 0; i < depth; ++i) oss << "   ";
    oss << (depth > 0 ? "|-- " : "") << "P" << pcb.PID << " [" << pcb.name << "] (" << stateToString(pcb.state) << ")\n";
    for (int child_pid : pcb.children) {
        printProcessTreeHelper(child_pid, depth + 1, oss);
    }
}

void printProcessTree() {
    std::ostringstream oss;
    oss << "\n============ 进程树 (Process Tree) ============\n";
    for (const auto& kv : proMap) {
        if (kv.second.parentPID <= 0 || proMap.find(kv.second.parentPID) == proMap.end()) {
            printProcessTreeHelper(kv.first, 0, oss);
        }
    }
    oss << "===============================================\n";
    std::cout << oss.str();
}

int block(int PID, int autoWakeTicks, const std::string& reason) {
    auto it = proMap.find(PID);
    if (it == proMap.end()) return 0;
    PCB& pcb = it->second;
    if (pcb.state == END || pcb.state == SUSPEND || pcb.state == BLOCK || pcb.state == ZOMBIE) return 0;

    if (pcb.state == RUN) currentRunningPID = -1;
    else if (pcb.state == READY) eraseRead(PID);

    pcb.state = BLOCK; pcb.blockRemain = std::max(0, autoWakeTicks); pcb.blockReason = reason;
    pushBlock(pcb);
    return 1;
}

int block(int PID) { return block(PID, 0, "手动阻塞"); }

int wakeup(int PID) {
    auto it = proMap.find(PID);
    if (it == proMap.end() || it->second.state != BLOCK) return 0;

    PCB& pcb = it->second;
    pcb.state = READY; pcb.blockRemain = 0; pcb.blockReason.clear();
    pcb.lastReadyTime = nowTime; pcb.ageTicks = 0;
    pcb.dynamicPriority = std::min(MAX_PRIORITY, std::max(pcb.dynamicPriority, pcb.priority));
    eraseBlock(PID); pushReady(pcb);
    preemptIfNeeded();
    return 1;
}

int suspendProc(int PID) {
    auto it = proMap.find(PID);
    if (it == proMap.end()) return 0;
    PCB& pcb = it->second;
    if (pcb.state == END || pcb.state == SUSPEND || pcb.state == ZOMBIE) return 0;

    if (pcb.state == RUN) currentRunningPID = -1;
    if (pcb.state == READY) eraseRead(PID);
    if (pcb.state == BLOCK) eraseBlock(PID);

    mem_manager.free_mem(PID); pcb.admitted = false; pcb.state = SUSPEND; pushSuspend(pcb);
    return 1;
}

int resumeProc(int PID) {
    auto it = proMap.find(PID);
    if (it == proMap.end() || it->second.state != SUSPEND) return 0;
    PCB& pcb = it->second;

    if (!mem_manager.alloc_mem(PID, pcb.size)) return 0;

    pcb.admitted = true; pcb.state = READY; pcb.lastReadyTime = nowTime; pcb.ageTicks = 0;
    eraseSuspend(PID); pushReady(pcb);
    preemptIfNeeded();
    return 1;
}

void tryAdmitSuspended() {
    if (suspendVector.empty()) return;
    std::sort(suspendVector.begin(), suspendVector.end(), [](const PCB& a, const PCB& b) {
        if (a.priority != b.priority) return a.priority > b.priority;
        return a.arriveTime < b.arriveTime;
        });
    std::vector<int> candidates;
    for (const auto& p : suspendVector) candidates.push_back(p.PID);
    for (int pid : candidates) resumeProc(pid);
}

void stop(int PID) {
    auto it = proMap.find(PID);
    if (it == proMap.end()) return;

    PCB pcb = it->second;
    if (pcb.state == READY) eraseRead(PID);
    if (pcb.state == BLOCK) eraseBlock(PID);
    if (pcb.state == SUSPEND) eraseSuspend(PID);
    if (currentRunningPID == PID) currentRunningPID = -1;

    releaseProcessDevices(PID);
    mem_manager.free_mem(PID); 
    pcb.admitted = false;

    pcb.finishTime = nowTime;
    pcb.turnaroundTime = pcb.finishTime - pcb.arriveTime;
    pcb.weightedTurnaround = pcb.needTime == 0 ? 0.0 : (pcb.turnaroundTime * 1.0 / pcb.needTime);

    // 1. 孤儿进程过继给 PID 1 (Init 进程)
    for (int child_pid : pcb.children) {
        if (proMap.find(child_pid) != proMap.end()) {
            proMap[child_pid].parentPID = 1;
            if (proMap.find(1) != proMap.end()) {
                proMap[1].children.push_back(child_pid);
            }
        }
    }
    pcb.children.clear(); 

    // 2. 唤醒正在阻塞等待的父进程
    auto parentIt = proMap.find(pcb.parentPID);
    if (parentIt != proMap.end()) {
        if (parentIt->second.state == BLOCK && parentIt->second.blockReason == "等待子进程") {
            wakeup(pcb.parentPID);
        }
    }

    // 3. 处理僵尸状态
    if (pcb.parentPID <= 1) {
        pcb.state = END;
        endVector.push_back(pcb);
        proMap.erase(PID);
    } else {
        pcb.state = ZOMBIE;
        proMap[PID] = pcb; 
    }

    rebuildSpecialQueues();
    tryAdmitSuspended();
}

void ageReadyQueue() {
    for (auto& readyPCB : readVector) {
        auto it = proMap.find(readyPCB.PID);
        if (it == proMap.end()) continue;
        PCB& pcb = it->second; pcb.waitTime++; pcb.ageTicks++;
        if (currentScheduleAlgo == SCHED_PRIORITY_RR && pcb.ageTicks >= AGING_THRESHOLD) {
            pcb.dynamicPriority = std::min(MAX_PRIORITY, pcb.dynamicPriority + 1); pcb.ageTicks = 0;
        }
        readyPCB = pcb;
    }
    for (auto& blockedPCB : blockVector) {
        auto it = proMap.find(blockedPCB.PID);
        if (it == proMap.end()) continue;
        PCB& pcb = it->second; pcb.blockTime++;
        if (pcb.blockRemain > 0) pcb.blockRemain--;
        blockedPCB = pcb;
    }
}

int selectNextProcessPID() {
    if (readVector.empty()) return -1;
    auto bestIt = readVector.begin();
    for (auto it = readVector.begin(); it != readVector.end(); ++it) {
        switch (currentScheduleAlgo) {
        case SCHED_PRIORITY_RR:
            if (it->dynamicPriority > bestIt->dynamicPriority ||
                (it->dynamicPriority == bestIt->dynamicPriority && it->lastReadyTime < bestIt->lastReadyTime)) bestIt = it;
            break;
        case SCHED_SJF:
            if (it->remainTime < bestIt->remainTime ||
                (it->remainTime == bestIt->remainTime && it->arriveTime < bestIt->arriveTime)) bestIt = it;
            break;
        case SCHED_FCFS:
            if (it->arriveTime < bestIt->arriveTime ||
                (it->arriveTime == bestIt->arriveTime && it->PID < bestIt->PID)) bestIt = it;
            break;
        case SCHED_HRRN:
            if (betterHRRN(*it, *bestIt)) bestIt = it;
            break;
        }
    }
    return bestIt->PID;
}

void dispatch() {
    if (currentRunningPID != -1 || readVector.empty()) return;
    int nextPID = selectNextProcessPID();
    if (nextPID == -1) return;

    eraseRead(nextPID); PCB& pcb = proMap[nextPID]; currentRunningPID = nextPID;
    pcb.state = RUN; pcb.timeSlice = computeTimeSlice(pcb); pcb.currentSlice = pcb.timeSlice;
    pcb.contextSwitches++; pcb.cpuBursts++; pcb.lastScheduleTime = nowTime;
    if (pcb.firstRunTime == -1) pcb.firstRunTime = nowTime;
}

void preemptIfNeeded() {
    if (currentRunningPID == -1 || readVector.empty()) return;
    PCB& running = proMap[currentRunningPID];
    int candidatePID = selectNextProcessPID();
    if (candidatePID == -1 || candidatePID == currentRunningPID) return;
    PCB& cand = proMap[candidatePID];

    bool shouldPreempt = false;
    if (currentScheduleAlgo == SCHED_PRIORITY_RR) shouldPreempt = cand.dynamicPriority > running.dynamicPriority;
    else if (currentScheduleAlgo == SCHED_SJF) shouldPreempt = cand.remainTime < running.remainTime;

    if (!shouldPreempt) return;
    running.state = READY; running.lastReadyTime = nowTime;
    if (currentScheduleAlgo == SCHED_PRIORITY_RR) running.dynamicPriority = std::max(MIN_PRIORITY, running.dynamicPriority - 1);
    pushReady(running); currentRunningPID = -1; dispatch();
}

bool dynamic_resize_memory(int pid, int delta_bytes) {
    if (proMap.find(pid) == proMap.end()) return false;
    if (!mem_manager.dynamic_alloc(pid, delta_bytes)) return false;
    proMap[pid].size += delta_bytes;
    for (auto& p : readVector) if (p.PID == pid) p.size += delta_bytes;
    for (auto& p : blockVector) if (p.PID == pid) p.size += delta_bytes;
    return true;
}

void run() {
    ageReadyQueue();
    processIOInterrupts();

    std::vector<int> autoWakePIDs;
    for (const auto& p : blockVector) {
        if (p.blockRemain == 0 && !p.blockReason.empty() && p.blockReason.find("等待设备") == std::string::npos && p.blockReason.find("等待子进程") == std::string::npos)
            autoWakePIDs.push_back(p.PID);
    }
    for (int pid : autoWakePIDs) wakeup(pid);

    tryAdmitSuspended();
    preemptIfNeeded();
    dispatch();

    if (currentRunningPID != -1) {
        PCB& runPCB = proMap[currentRunningPID];
        runPCB.remainTime--; runPCB.runTime++; runPCB.currentSlice--;

        // 内存访问模拟
        int logical_pages_count = (runPCB.size + PAGE_SIZE - 1) / PAGE_SIZE;
        if (logical_pages_count > 0) {
            uint32_t random_page = rand() % logical_pages_count;
            uint32_t random_offset = rand() % PAGE_SIZE;
            uint32_t virt_addr = (random_page << 12) | random_offset;
            mem_manager.access_addr(currentRunningPID, virt_addr); 
        }

        if (runPCB.remainTime <= 0) {
            stop(currentRunningPID);
        }
        else {
            bool sliceExpired = (runPCB.currentSlice <= 0);
            if (sliceExpired || currentScheduleAlgo == SCHED_SJF) {
                runPCB.state = READY; runPCB.lastReadyTime = nowTime;
                if (currentScheduleAlgo == SCHED_PRIORITY_RR && sliceExpired)
                    runPCB.dynamicPriority = std::max(MIN_PRIORITY, runPCB.dynamicPriority - 1);
                pushReady(runPCB); currentRunningPID = -1; dispatch();
            }
        }
    }
}

void showProcessDetail(int PID) {
    auto it = proMap.find(PID);
    if (it == proMap.end()) {
        std::cout << "[查询失败] PID: " << PID << " 不存在。" << std::endl;
        return;
    }

    const PCB& p = it->second;
    std::cout << "\n---------------- 进程详细信息 ----------------" << std::endl;
    std::cout << "PID: " << p.PID << " | 名称: " << p.name << " | 父进程: " << p.parentPID << std::endl;
    std::cout << "状态: " << stateToString(p.state) << " | 静态优先级: " << p.priority
        << " | 动态优先级: " << p.dynamicPriority << std::endl;
    std::cout << "need/remain/run: " << p.needTime << "/" << p.remainTime << "/" << p.runTime << std::endl;
    std::cout << "wait/block: " << p.waitTime << "/" << p.blockTime << std::endl;
    std::cout << "timeSlice/currentSlice: " << p.timeSlice << "/" << p.currentSlice << std::endl;
    std::cout << "arrive/firstRun/finish: " << p.arriveTime << "/" << p.firstRunTime << "/" << p.finishTime << std::endl;
    std::cout << "contextSwitches/cpuBursts: " << p.contextSwitches << "/" << p.cpuBursts << std::endl;
    if (!p.blockReason.empty()) std::cout << "blockReason: " << p.blockReason << std::endl;
    
    std::cout << "子进程: ";
    if (p.children.empty()) std::cout << "无";
    else { for (int c : p.children) std::cout << "P" << c << " "; }
    
    std::cout << "\n----------------------------------------------\n" << std::endl;
}

void showProcessSummary() {
    if (endVector.empty()) {
        std::cout << "[统计信息] 当前尚无结束进程。" << std::endl;
        return;
    }

    double avgTurn = 0.0;
    double avgWTurn = 0.0;
    double avgResp = 0.0;
    for (const auto& p : endVector) {
        avgTurn += p.turnaroundTime;
        avgWTurn += p.weightedTurnaround;
        if (p.firstRunTime >= 0) avgResp += (p.firstRunTime - p.arriveTime);
    }
    avgTurn /= endVector.size();
    avgWTurn /= endVector.size();
    avgResp /= endVector.size();

    std::cout << "[系统统计] 已完成进程数: " << endVector.size()
        << " | 平均周转时间: " << std::fixed << std::setprecision(2) << avgTurn
        << " | 平均带权周转时间: " << avgWTurn
        << " | 平均响应时间: " << avgResp
        << std::defaultfloat << std::endl;
}

void showQueues() {
    rebuildSpecialQueues();

    std::cout << "\n================ 进程队列状态监控 ================" << std::endl;
    std::cout << "当前时间: " << nowTime << " | 调度算法: " << getScheduleAlgorithmName() << std::endl;

    std::cout << "[运行中 (RUNNING)]" << std::endl;
    if (currentRunningPID != -1 && proMap.find(currentRunningPID) != proMap.end()) {
        PCB& p = proMap[currentRunningPID];
        std::cout << "  -> PID: " << p.PID << " | 名称: " << p.name
            << " | 剩余时间: " << p.remainTime << " | 剩余时间片: " << p.currentSlice
            << " | 动态优先级: " << p.dynamicPriority << std::endl;
    }
    else {
        std::cout << "  -> CPU 空闲" << std::endl;
    }

    std::cout << "[就绪队列 (READY)]" << std::endl;
    if (readVector.empty()) std::cout << "  -> (空)" << std::endl;
    else {
        for (const auto& p : readVector) {
            std::cout << "  -> PID: " << p.PID << " | 名称: " << p.name
                << " | 静态/动态优先级: " << p.priority << "/" << p.dynamicPriority
                << " | 剩余时间: " << p.remainTime
                << " | 等待时间: " << p.waitTime << std::endl;
        }
    }

    std::cout << "[阻塞队列 (BLOCKED)]" << std::endl;
    if (blockVector.empty()) std::cout << "  -> (空)" << std::endl;
    else {
        for (const auto& p : blockVector) {
            std::cout << "  -> PID: " << p.PID << " | 名称: " << p.name
                << " | blockRemain: " << p.blockRemain;
            if (!p.blockReason.empty()) std::cout << " | 原因: " << p.blockReason;
            std::cout << std::endl;
        }
    }

    std::cout << "[僵尸队列 (ZOMBIE)]" << std::endl;
    if (zombieVector.empty()) std::cout << "  -> (空)" << std::endl;
    else {
        for (const auto& p : zombieVector) {
            std::cout << "  -> PID: " << p.PID << " | 名称: " << p.name
                << " | 等待父进程 P" << p.parentPID << " 回收" << std::endl;
        }
    }

    std::cout << "[挂起队列 (SUSPEND/BACKUP)]" << std::endl;
    if (suspendVector.empty()) std::cout << "  -> (空)" << std::endl;
    else {
        for (const auto& p : suspendVector) {
            std::cout << "  -> PID: " << p.PID << " | 名称: " << p.name
                << " | 需求内存: " << p.size << " | 优先级: " << p.priority << std::endl;
        }
    }

    std::cout << "[已完成队列 (FINISHED)]" << std::endl;
    if (endVector.empty()) std::cout << "  -> (空)" << std::endl;
    else {
        for (const auto& p : endVector) {
            std::cout << "  -> PID: " << p.PID << " | 名称: " << p.name
                << " | 周转: " << p.turnaroundTime
                << " | 带权周转: " << std::fixed << std::setprecision(2) << p.weightedTurnaround
                << std::defaultfloat << std::endl;
        }
    }

    std::cout << "==================================================" << std::endl;
    printProcessTree();
    showProcessSummary();
    std::cout << std::endl;
}
