#pragma once
#include <iostream>
#include <string>
#include <stack>
#include <map>
#include <vector>
#include <algorithm>
#include <deque>
#include <climits>
#include <sstream>
#include "../memory/memory.h"

#define READY 0
#define RUN 1
#define BLOCK 2
#define END 3
#define SUSPEND 4
#define ZOMBIE 5 // 新增：僵尸状态

// 调度算法枚举
#define SCHED_PRIORITY_RR 0
#define SCHED_SJF 1
#define SCHED_FCFS 2
#define SCHED_HRRN 3

// 指令格式保持不变
typedef struct cmd {
    int time;
    int num;
    int num2;
    std::string path;
} cmd;

typedef struct PCB {
    int PID = -1;
    std::string name;     // 进程名
    int state = READY;    // 进程状态
    int priority = 1;     // 静态优先级 (数值越大优先级越高)
    int dynamicPriority = 1; // 动态优先级（用于老化/反馈调度）
    int timeSlice = 3;        // 分配的时间片大小
    int currentSlice = 0;     // 当前运行剩余的时间片

    int size = 0;             // 进程所需内存
    int arriveTime = 0;       // 进程到达时间
    int needTime = 0;         // 进程总共需要运行的时间
    int remainTime = 0;       // 进程还需运行的时间

    // 拓展字段：进程管理 / 调度统计
    int runTime = 0;              // 已运行时间
    int waitTime = 0;             // 累计等待时间
    int blockTime = 0;            // 累计阻塞时间
    int firstRunTime = -1;        // 第一次得到CPU时间
    int finishTime = -1;          // 完成时间
    int turnaroundTime = 0;       // 周转时间
    double weightedTurnaround = 0.0; // 带权周转时间
    int lastReadyTime = 0;        // 最近一次进入就绪队列时间
    int lastScheduleTime = -1;    // 最近一次被调度时间
    int contextSwitches = 0;      // 上下文切换次数
    int cpuBursts = 0;            // 获得CPU次数
    int ageTicks = 0;             // 就绪队列中的老化计数
    int blockRemain = 0;          // 自动唤醒倒计时
    bool admitted = false;        // 是否成功进入内存并投入运行
    std::string blockReason;      // 阻塞原因

    // ★ 新增：进程族谱与层级
    int parentPID = 0;            // 父进程PID (0表示系统根节点)
    std::vector<int> children;    // 子进程PID列表
    int exitCode = 0;             // 退出状态码

    std::stack<cmd> cmdStack; // 指令栈
} PCB;

extern std::map<int, PCB> proMap;              // 存储所有PCB信息
extern std::vector<PCB> endVector;             // 存储已经结束的PCB信息
extern std::vector<PCB> readVector;            // 就绪队列 (Ready Queue)
extern std::vector<PCB> blockVector;           // 阻塞队列快照
extern std::vector<PCB> suspendVector;         // 挂起/后备队列
extern std::vector<PCB> zombieVector;          // 僵尸队列快照
extern int currentRunningPID;                  // 当前正在运行的进程PID
extern int currentScheduleAlgo;                // 当前调度算法

// 核心接口
int createProc(std::string name, int needTime, int size, int priority);
int forkProc(int parentPID);                   // 创建子进程
int waitProc(int parentPID);                   // 等待并回收子进程
void printProcessTree();                       // 打印进程树
void eraseRead(int PID);
void eraseBlock(int PID);
void eraseSuspend(int PID);
int block(int PID);
int block(int PID, int autoWakeTicks, const std::string& reason);
int wakeup(int PID);
int suspendProc(int PID);
int resumeProc(int PID);
void stop(int PID);
void run();
void showQueues();

// 新增功能接口
bool dynamic_resize_memory(int pid, int delta_bytes);

// 拓展接口：可在后续自由接入 shell / GUI / 测试代码
int setScheduleAlgorithm(int algo);
std::string getScheduleAlgorithmName();
void dispatch();
void ageReadyQueue();
void tryAdmitSuspended();
void preemptIfNeeded();
int computeTimeSlice(const PCB& pcb);
int selectNextProcessPID();
void rebuildSpecialQueues();
std::string stateToString(int state);
void showProcessDetail(int PID);
void showProcessSummary();