#include "program.h"
#include "device.h" 
#include <iomanip>
#include <numeric>
#include <cstdlib>

std::map<int, PCB> proMap;
std::vector<PCB> endVector;
std::vector<PCB> readVector;
std::vector<PCB> readQueues[MLFQ_LEVELS]; // [MLFQ] 0/1/2 三个优先级就绪队列
std::vector<PCB> blockVector;
std::vector<PCB> suspendVector;
std::vector<PCB> zombieVector; // 僵尸队列

// [新功能: SMP] 多核处理器初始化
int cpuCores[MAX_CORES] = { -1, -1 };
int currentRunningPID = -1; // 保持兼容，映射到 cpuCores[0]

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

    int clampQueueLevel(int level) {
        return std::max(0, std::min(MLFQ_LEVELS - 1, level));
    }

    void eraseReadyNoSync(int PID) {
        readVector.erase(std::remove_if(readVector.begin(), readVector.end(), [&](const PCB& p) { return p.PID == PID; }), readVector.end());
        for (int i = 0; i < MLFQ_LEVELS; ++i) {
            readQueues[i].erase(std::remove_if(readQueues[i].begin(), readQueues[i].end(), [&](const PCB& p) { return p.PID == PID; }), readQueues[i].end());
        }
    }

    void rebuildMLFQQueuesFromReady() {
        for (int i = 0; i < MLFQ_LEVELS; ++i) readQueues[i].clear();
        for (auto& p : readVector) {
            auto it = proMap.find(p.PID);
            if (it != proMap.end()) p = it->second;
            int level = clampQueueLevel(p.queueLevel);
            readQueues[level].push_back(p);
        }
    }

    void pushReady(const PCB& pcb) {
        PCB copy = pcb;
        copy.queueLevel = clampQueueLevel(copy.queueLevel);
        eraseReadyNoSync(copy.PID);
        readVector.push_back(copy);
        readQueues[copy.queueLevel].push_back(copy);
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
    case SCHED_FCFS: return "FCFS"; case SCHED_HRRN: return "HRRN"; 
    case SCHED_MLFQ: return "MLFQ (Multi-Level Feedback)"; // [新功能: MLFQ]
    default: return "Unknown";
    }
}

int setScheduleAlgorithm(int algo) {
    if (algo < SCHED_PRIORITY_RR || algo > SCHED_MLFQ) return 0;
    currentScheduleAlgo = algo;
    rebuildMLFQQueuesFromReady();
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
    else if (currentScheduleAlgo == SCHED_MLFQ) {
        // [新功能: MLFQ] 队列层级越低(数字越大)，时间片越大。0级:2, 1级:4, 2级:8
        slice = 2 << std::min(2, pcb.queueLevel); 
    }
    return std::max(1, std::min(8, slice));
}

void eraseRead(int PID) {
    eraseReadyNoSync(PID);
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

    // [新增] 高级特性初始化
    newPCB.pgid = newPCB.PID;          // 默认自己是一个新进程组
    newPCB.maxChildrenLimit = 999;     // 默认无子进程限制
    newPCB.queueLevel = 0;             // MLFQ 默认进入最高级队列
    newPCB.pendingSignals.clear();

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

    // [新功能: ulimit] 检查子进程数量限制
    if (static_cast<int>(parent.children.size()) >= parent.maxChildrenLimit) {
        std::cout << "[资源受限] Fork失败: PID " << parentPID << " 已达到最大子进程限制 (" << parent.maxChildrenLimit << ")。" << std::endl;
        return -1;
    }
    
    PCB child = parent; // 深拷贝
    child.PID = ++PID_COUNTER;
    child.name = parent.name + "_child";
    child.parentPID = parentPID;
    child.children.clear();
    
    // [新功能: Job Control & MLFQ]
    child.pgid = parent.pgid;           // 继承父进程组
    child.pendingSignals.clear();       // 清空信号
    child.queueLevel = 0;               // 新进程重新进入最高级队列

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

    // [新功能: SMP 核心剥离]
    for (int i = 0; i < MAX_CORES; ++i) {
        if (cpuCores[i] == PID) { cpuCores[i] = -1; if (i == 0) currentRunningPID = -1; }
    }
    if (pcb.state == READY) eraseRead(PID);

    pcb.state = BLOCK;
    // autoWakeTicks > 0 表示定时阻塞；<=0 表示手动阻塞，不能被系统 tick 自动唤醒。
    pcb.blockRemain = (autoWakeTicks > 0) ? autoWakeTicks : -1;
    pcb.blockReason = reason;
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
    
    // [新功能: MLFQ] I/O 阻塞唤醒后，优先级提升(层级减小)
    if (currentScheduleAlgo == SCHED_MLFQ) {
        pcb.queueLevel = std::max(0, pcb.queueLevel - 1);
    }

    eraseBlock(PID); pushReady(pcb);
    preemptIfNeeded();
    return 1;
}

int suspendProc(int PID) {
    auto it = proMap.find(PID);
    if (it == proMap.end()) return 0;
    PCB& pcb = it->second;
    if (pcb.state == END || pcb.state == SUSPEND || pcb.state == ZOMBIE) return 0;

    // [新功能: SMP 核心剥离]
    for (int i = 0; i < MAX_CORES; ++i) {
        if (cpuCores[i] == PID) { cpuCores[i] = -1; if (i == 0) currentRunningPID = -1; }
    }
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
    
    // [新功能: SMP 核心剥离]
    for (int i = 0; i < MAX_CORES; ++i) {
        if (cpuCores[i] == PID) { cpuCores[i] = -1; if (i == 0) currentRunningPID = -1; }
    }

    releaseProcessDevices(PID);
    mem_manager.free_mem(PID); 
    pcb.admitted = false;

    pcb.finishTime = nowTime;
    pcb.turnaroundTime = pcb.finishTime - pcb.arriveTime;
    pcb.weightedTurnaround = pcb.needTime == 0 ? 0.0 : (pcb.turnaroundTime * 1.0 / pcb.needTime);

    for (int child_pid : pcb.children) {
        if (proMap.find(child_pid) != proMap.end()) {
            proMap[child_pid].parentPID = 1;
            if (proMap.find(1) != proMap.end()) {
                proMap[1].children.push_back(child_pid);
            }
        }
    }
    pcb.children.clear(); 

    auto parentIt = proMap.find(pcb.parentPID);
    if (parentIt != proMap.end()) {
        if (parentIt->second.state == BLOCK && parentIt->second.blockReason == "等待子进程") {
            wakeup(pcb.parentPID);
        }
    }

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
        // [新功能: MLFQ 老化防止饥饿]
        if (currentScheduleAlgo == SCHED_MLFQ && pcb.ageTicks >= AGING_THRESHOLD * 3) {
            pcb.queueLevel = std::max(0, pcb.queueLevel - 1); pcb.ageTicks = 0;
        }
        readyPCB = pcb;
    }
    rebuildMLFQQueuesFromReady();
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

    auto is_running = [](int pid) {
        for (int i = 0; i < MAX_CORES; ++i) if (cpuCores[i] == pid) return true;
        return false;
    };

    // [MLFQ] 真正使用三层就绪队列：0级最高，1级次之，2级最低。
    if (currentScheduleAlgo == SCHED_MLFQ) {
        rebuildMLFQQueuesFromReady();
        for (int level = 0; level < MLFQ_LEVELS; ++level) {
            auto bestIt = readQueues[level].end();
            for (auto it = readQueues[level].begin(); it != readQueues[level].end(); ++it) {
                if (is_running(it->PID)) continue;
                if (bestIt == readQueues[level].end() || it->lastReadyTime < bestIt->lastReadyTime ||
                    (it->lastReadyTime == bestIt->lastReadyTime && it->PID < bestIt->PID)) {
                    bestIt = it;
                }
            }
            if (bestIt != readQueues[level].end()) return bestIt->PID;
        }
        return -1;
    }

    auto bestIt = readVector.end();
    for (auto it = readVector.begin(); it != readVector.end(); ++it) {
        if (is_running(it->PID)) continue;
        if (bestIt == readVector.end()) { bestIt = it; continue; }

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
        default:
            break;
        }
    }
    return (bestIt == readVector.end()) ? -1 : bestIt->PID;
}

// [新功能: SMP 核心分发器]
void dispatch() {
    for (int i = 0; i < MAX_CORES; ++i) {
        if (cpuCores[i] == -1 && !readVector.empty()) {
            int nextPID = selectNextProcessPID();
            if (nextPID == -1) break;

            eraseRead(nextPID); 
            PCB& pcb = proMap[nextPID]; 
            cpuCores[i] = nextPID;
            if (i == 0) currentRunningPID = nextPID; // 映射给主线程单核视角

            pcb.state = RUN; pcb.timeSlice = computeTimeSlice(pcb); pcb.currentSlice = pcb.timeSlice;
            pcb.contextSwitches++; pcb.cpuBursts++; pcb.lastScheduleTime = nowTime;
            if (pcb.firstRunTime == -1) pcb.firstRunTime = nowTime;
        }
    }
}

// [新功能: SMP 全局抢占检查]
void preemptIfNeeded() {
    if (readVector.empty()) return;
    for (int i = 0; i < MAX_CORES; ++i) {
        if (cpuCores[i] == -1) continue; 
        PCB& running = proMap[cpuCores[i]];
        int candidatePID = selectNextProcessPID();
        if (candidatePID == -1 || candidatePID == cpuCores[i]) continue;
        PCB& cand = proMap[candidatePID];

        bool shouldPreempt = false;
        if (currentScheduleAlgo == SCHED_PRIORITY_RR) shouldPreempt = cand.dynamicPriority > running.dynamicPriority;
        else if (currentScheduleAlgo == SCHED_SJF) shouldPreempt = cand.remainTime < running.remainTime;
        else if (currentScheduleAlgo == SCHED_MLFQ) shouldPreempt = cand.queueLevel < running.queueLevel;

        if (shouldPreempt) {
            running.state = READY; running.lastReadyTime = nowTime;
            if (currentScheduleAlgo == SCHED_PRIORITY_RR) running.dynamicPriority = std::max(MIN_PRIORITY, running.dynamicPriority - 1);
            pushReady(running); 
            cpuCores[i] = -1; 
            if (i == 0) currentRunningPID = -1;
        }
    }
    dispatch();
}

bool dynamic_resize_memory(int pid, int delta_bytes) {
    if (proMap.find(pid) == proMap.end()) return false;
    if (!mem_manager.dynamic_alloc(pid, delta_bytes)) return false;
    proMap[pid].size += delta_bytes;
    for (auto& p : readVector) if (p.PID == pid) p.size += delta_bytes;
    for (auto& p : blockVector) if (p.PID == pid) p.size += delta_bytes;
    return true;
}

int sendSignal(int pid, int signum) {
    auto it = proMap.find(pid);
    if (it == proMap.end() || it->second.state == END || it->second.state == ZOMBIE) return 0;
    it->second.pendingSignals.push_back(signum);
    syncPCBToQueue(readVector, pid);
    syncPCBToQueue(blockVector, pid);
    syncPCBToQueue(suspendVector, pid);
    rebuildMLFQQueuesFromReady();
    return 1;
}

int setMaxChildrenLimit(int pid, int limit) {
    auto it = proMap.find(pid);
    if (it == proMap.end() || limit < 0) return 0;
    it->second.maxChildrenLimit = limit;
    syncPCBToQueue(readVector, pid);
    syncPCBToQueue(blockVector, pid);
    syncPCBToQueue(suspendVector, pid);
    rebuildMLFQQueuesFromReady();
    return 1;
}

int setProcessGroup(int pid, int pgid) {
    auto it = proMap.find(pid);
    if (it == proMap.end()) return 0;
    it->second.pgid = pgid;
    syncPCBToQueue(readVector, pid);
    syncPCBToQueue(blockVector, pid);
    syncPCBToQueue(suspendVector, pid);
    rebuildMLFQQueuesFromReady();
    return 1;
}

int killGroup(int pgid) {
    std::vector<int> victims;
    for (const auto& kv : proMap) {
        if (kv.second.pgid == pgid) victims.push_back(kv.first);
    }
    for (int pid : victims) stop(pid);
    return static_cast<int>(victims.size());
}

// [重构: 支持多核(SMP)、信号(Signals)处理和MLFQ降级的主时钟推进]
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

    // 遍历所有 CPU 核心，推进每个在跑进程的时间
    for (int i = 0; i < MAX_CORES; ++i) {
        int rpid = cpuCores[i];
        if (rpid != -1) {
            PCB& runPCB = proMap[rpid];

            // --- [新功能: 软中断信号处理] ---
            bool killed_or_suspended = false;
            while (!runPCB.pendingSignals.empty()) {
                int sig = runPCB.pendingSignals.front();
                runPCB.pendingSignals.pop_front();
                if (sig == 9) { // SIGKILL
                    std::cout << "\n[Signal] PID " << rpid << " 收到 SIGKILL(9), 强制终止。" << std::endl;
                    stop(rpid); killed_or_suspended = true; break;
                } else if (sig == 19) { // SIGSTOP
                    std::cout << "\n[Signal] PID " << rpid << " 收到 SIGSTOP(19), 已挂起。" << std::endl;
                    suspendProc(rpid); killed_or_suspended = true; break;
                }
            }
            if (killed_or_suspended) continue; // 如果进程死了，跳过本核心后续处理
            // --------------------------------

            runPCB.remainTime--; runPCB.runTime++; runPCB.currentSlice--;

            // 内存访问模拟
            int logical_pages_count = (runPCB.size + PAGE_SIZE - 1) / PAGE_SIZE;
            if (logical_pages_count > 0) {
                uint32_t random_page = rand() % logical_pages_count;
                uint32_t random_offset = rand() % PAGE_SIZE;
                uint32_t virt_addr = (random_page << 12) | random_offset;
                mem_manager.access_addr(rpid, virt_addr); 
            }

            if (runPCB.remainTime <= 0) {
                stop(rpid);
            }
            else {
                bool sliceExpired = (runPCB.currentSlice <= 0);
                if (sliceExpired || currentScheduleAlgo == SCHED_SJF) {
                    runPCB.state = READY; runPCB.lastReadyTime = nowTime;
                    
                    if (currentScheduleAlgo == SCHED_PRIORITY_RR && sliceExpired) {
                        runPCB.dynamicPriority = std::max(MIN_PRIORITY, runPCB.dynamicPriority - 1);
                    } else if (currentScheduleAlgo == SCHED_MLFQ && sliceExpired) {
                        // [新功能: MLFQ 降级惩罚]
                        runPCB.queueLevel = std::min(2, runPCB.queueLevel + 1); 
                    }

                    pushReady(runPCB); 
                    cpuCores[i] = -1; 
                    if (i == 0) currentRunningPID = -1;
                }
            }
        }
    }
    // 时钟周期末尾再次尝试分发空闲核心
    dispatch();
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
    std::cout << "进程组(PGID): " << p.pgid << " | 资源限额(子进程): " << p.maxChildrenLimit << std::endl;
    std::cout << "状态: " << stateToString(p.state) << " | 静态优先级: " << p.priority
        << " | 动态优先级: " << p.dynamicPriority << " | MLFQ层级: " << p.queueLevel << std::endl;
    std::cout << "need/remain/run: " << p.needTime << "/" << p.remainTime << "/" << p.runTime << std::endl;
    std::cout << "wait/block: " << p.waitTime << "/" << p.blockTime << std::endl;
    std::cout << "timeSlice/currentSlice: " << p.timeSlice << "/" << p.currentSlice << std::endl;
    std::cout << "arrive/firstRun/finish: " << p.arriveTime << "/" << p.firstRunTime << "/" << p.finishTime << std::endl;
    std::cout << "contextSwitches/cpuBursts: " << p.contextSwitches << "/" << p.cpuBursts << std::endl;
    
    if (!p.blockReason.empty()) std::cout << "blockReason: " << p.blockReason << std::endl;
    if (!p.pendingSignals.empty()) std::cout << "待处理信号数量: " << p.pendingSignals.size() << std::endl;
    
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

// [新功能: SMP] 改进了多核心的排队显示
void showQueues() {
    rebuildSpecialQueues();

    std::cout << "\n================ 进程队列状态监控 ================" << std::endl;
    std::cout << "当前时间: " << nowTime << " | 调度算法: " << getScheduleAlgorithmName() << std::endl;

    std::cout << "[运行中 (RUNNING) - " << MAX_CORES << " Cores]" << std::endl;
    for (int i = 0; i < MAX_CORES; ++i) {
        std::cout << "  -> [Core " << i << "] ";
        if (cpuCores[i] != -1 && proMap.find(cpuCores[i]) != proMap.end()) {
            PCB& p = proMap[cpuCores[i]];
            std::cout << "PID: " << p.PID << " | 名称: " << p.name
                << " | 剩余: " << p.remainTime << " | 时间片: " << p.currentSlice;
            if (currentScheduleAlgo == SCHED_MLFQ) std::cout << " | MLFQ层级: " << p.queueLevel;
            std::cout << std::endl;
        }
        else {
            std::cout << "CPU 空闲" << std::endl;
        }
    }

    std::cout << "[就绪队列 (READY)]" << std::endl;
    if (readVector.empty()) std::cout << "  -> (空)" << std::endl;
    else {
        for (const auto& p : readVector) {
            std::cout << "  -> PID: " << p.PID << " | 名称: " << p.name
                << " | 动/静优先: " << p.dynamicPriority << "/" << p.priority;
            if (currentScheduleAlgo == SCHED_MLFQ) std::cout << " | MLFQ层级: " << p.queueLevel;
            std::cout << " | 剩余时间: " << p.remainTime << " | 等待: " << p.waitTime << std::endl;
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

