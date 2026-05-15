// device.h - I/O 设备管理与中断子系统 (含银行家算法死锁避免)
#pragma once
#include <string>
#include <vector>
#include <deque>
#include <map>

// 硬件设备结构体定义
struct IODevice {
    int id;
    std::string name;
    bool isBusy = false;
    int currentPID = -1;
    std::deque<int> waitQueue; // 阻塞排队队列
};

// 全局暴露的系统硬件设备列表
extern std::vector<IODevice> sysDevices;

// ==========================================
// 银行家算法 (Banker's Algorithm) 数据结构
// ==========================================
extern std::vector<int> Available; // 系统当前可用资源向量 [打印机数, 键盘数, 磁盘数]
extern std::map<int, std::vector<int>> Max;        // 进程最大需求矩阵
extern std::map<int, std::vector<int>> Allocation; // 进程已分配矩阵
extern std::map<int, std::vector<int>> Need;       // 进程还需矩阵

// 银行家算法核心函数
void initProcessBanker(int pid);           // 初始化进程的资源需求
bool isSafeState();                        // 安全性算法：检查系统当前是否处于安全状态
int requestDeviceBanker(int pid, int devId); // 基于银行家算法的设备请求

// 对外暴露的基础子系统接口
void processIOInterrupts();               // 模拟底层硬件中断回调
void releaseProcessDevices(int pid);      // 进程终止时强制回收其占用的设备