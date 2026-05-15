#include "program.h"
#include "ipc.h"
#include "device.h"

#include <algorithm>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

static std::string trim_left(std::string s) {
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](unsigned char ch) { return !std::isspace(ch); }));
    return s;
}

static void print_help() {
    std::cout << "\n可用命令：\n"
              << "  help                         显示帮助\n"
              << "  demo                         运行 IPC + Device 示例\n"
              << "  create <name>                创建一个桩进程\n"
              << "  ps                           查看进程表\n"
              << "  sem_create <name> <value>    创建信号量\n"
              << "  P <name> <pid>               执行信号量 P 操作\n"
              << "  V <name>                     执行信号量 V 操作\n"
              << "  sems                         查看信号量表\n"
              << "  send <from> <to> <message>   发送邮箱消息\n"
              << "  read <pid>                   读取指定 PID 邮箱中的一条消息\n"
              << "  io <pid> <device_id>         按银行家算法申请设备\n"
              << "  finish <device_id>           确定性地完成一次设备 I/O\n"
              << "  irq [times]                  模拟随机硬件中断\n"
              << "  release <pid>                回收某进程占用的设备\n"
              << "  wakeup <pid>                 手动唤醒被阻塞的桩进程\n"
              << "  devices                      查看设备状态和等待队列\n"
              << "  banker                       查看银行家算法矩阵\n"
              << "  exit                         退出\n\n"
              << "设备编号：0=打印机，1=键盘，2=磁盘\n";
}

static void show_semaphores() {
    std::cout << "\n信号量表\n";
    if (ipc_manager.semaphores.empty()) {
        std::cout << "  <空>\n";
        return;
    }
    std::cout << std::left << std::setw(16) << "名称" << std::setw(8) << "值" << "等待数\n";
    for (const auto& kv : ipc_manager.semaphores) {
        std::cout << std::left << std::setw(16) << kv.first << std::setw(8) << kv.second.value
                  << kv.second.waitQueue.size() << "\n";
    }
}

static void show_devices() {
    std::cout << "\n设备状态\n";
    std::cout << std::left << std::setw(6) << "ID" << std::setw(24) << "名称" << std::setw(8)
              << "忙碌" << std::setw(8) << "占用者" << "等待队列\n";
    for (const auto& dev : sysDevices) {
        std::cout << std::left << std::setw(6) << dev.id << std::setw(24) << dev.name
                  << std::setw(8) << (dev.isBusy ? "是" : "否")
                  << std::setw(8) << (dev.currentPID >= 0 ? std::to_string(dev.currentPID) : "-");
        for (int pid : dev.waitQueue) std::cout << "P" << pid << " ";
        std::cout << "\n";
    }
}

static void show_banker() {
    std::cout << "\n银行家算法状态\n";
    std::cout << "Available = [";
    for (size_t i = 0; i < Available.size(); ++i) {
        std::cout << Available[i] << (i + 1 == Available.size() ? "" : ", ");
    }
    std::cout << "]\n";
    std::cout << std::left << std::setw(8) << "PID" << std::setw(16) << "Max"
              << std::setw(16) << "Allocation" << "Need\n";
    for (const auto& kv : Max) {
        int pid = kv.first;
        auto vec_to_string = [](const std::vector<int>& v) {
            std::ostringstream os;
            os << "[";
            for (size_t i = 0; i < v.size(); ++i) os << v[i] << (i + 1 == v.size() ? "" : ",");
            os << "]";
            return os.str();
        };
        std::cout << std::left << std::setw(8) << pid << std::setw(16) << vec_to_string(Max[pid])
                  << std::setw(16) << vec_to_string(Allocation[pid]) << vec_to_string(Need[pid]) << "\n";
    }
}

static void describe_p_result(int result) {
    if (result == 1) std::cout << "[成功] 进程已获得信号量，可继续执行。\n";
    else if (result == 0) std::cout << "[阻塞] 信号量不可用，进程进入等待队列。\n";
    else if (result == -2) std::cout << "[失败] PID 不存在。\n";
    else std::cout << "[失败] 信号量不存在。\n";
}

static void describe_v_result(int result) {
    if (result > 0) std::cout << "[成功] 已释放信号量，并唤醒 P" << result << "。\n";
    else if (result == 0) std::cout << "[成功] 已释放信号量，无需唤醒其他进程。\n";
    else std::cout << "[失败] 信号量不存在。\n";
}

static void describe_io_result(int result) {
    if (result == 1) std::cout << "[成功] 已分配设备；进程因模拟 I/O 而阻塞。\n";
    else if (result == 2) std::cout << "[等待] 设备忙碌；进程进入设备等待队列。\n";
    else if (result == 0) std::cout << "[拒绝] 请求非法，或银行家算法判定为不安全。\n";
    else std::cout << "[失败] PID / 设备编号 / 进程状态无效。\n";
}

static void deterministic_finish_device(int devId) {
    if (devId < 0 || devId >= static_cast<int>(sysDevices.size())) {
        std::cout << "[失败] 设备编号无效。\n";
        return;
    }
    IODevice& dev = sysDevices[devId];
    if (!dev.isBusy || dev.currentPID < 0) {
        std::cout << "设备当前未忙，无需完成。\n";
        if (!dev.waitQueue.empty()) {
            int nextPid = dev.waitQueue.front();
            dev.waitQueue.pop_front();
            wakeup(nextPid);
            std::cout << "已唤醒等待中的 P" << nextPid << "；如需重新申请，请输入：io "
                      << nextPid << " " << devId << "\n";
        }
        return;
    }

    int finishedPid = dev.currentPID;
    std::cout << "设备 " << dev.id << " 已为 P" << finishedPid << " 完成一次 I/O。\n";
    if (dev.id >= 0 && dev.id < static_cast<int>(Available.size())) {
        Available[dev.id] += 1;
        if (Allocation.count(finishedPid) && Allocation[finishedPid][dev.id] > 0) {
            Allocation[finishedPid][dev.id] -= 1;
            Need[finishedPid][dev.id] += 1;
        }
    }
    wakeup(finishedPid);
    dev.isBusy = false;
    dev.currentPID = -1;

    if (!dev.waitQueue.empty()) {
        int nextPid = dev.waitQueue.front();
        dev.waitQueue.pop_front();
        wakeup(nextPid);
        std::cout << "已唤醒等待中的 P" << nextPid << "；如需重新申请，请输入：io "
                  << nextPid << " " << devId << "\n";
    }
}

static void run_demo() {
    std::cout << "\n=== 示例演示 ===\n";
    int p1 = createProc("frontend_client");
    int p2 = createProc("ipc_worker");
    int p3 = createProc("device_worker");

    std::cout << "\n[IPC - 信号量]\n";
    std::cout << "sem_create mutex 1 -> " << ipc_manager.create_semaphore("mutex", 1) << "\n";
    std::cout << "P mutex " << p1 << " -> "; describe_p_result(ipc_manager.sem_P("mutex", p1));
    std::cout << "P mutex " << p2 << " -> "; describe_p_result(ipc_manager.sem_P("mutex", p2));
    std::cout << "V mutex -> "; describe_v_result(ipc_manager.sem_V("mutex"));

    std::cout << "\n[IPC - 邮箱]\n";
    std::cout << "send " << p1 << " " << p2 << " hello -> "
              << ipc_manager.send_message(p1, p2, "hello from interactive demo") << "\n";
    std::cout << "read " << p2 << " -> " << ipc_manager.read_message(p2) << "\n";

    std::cout << "\n[Device - 设备管理]\n";
    int r1 = requestDeviceBanker(p1, 0);
    std::cout << "io " << p1 << " 0 -> "; describe_io_result(r1);
    int r2 = requestDeviceBanker(p3, 0);
    std::cout << "io " << p3 << " 0 -> "; describe_io_result(r2);
    deterministic_finish_device(0);

    showProcessTable();
    show_semaphores();
    show_devices();
    show_banker();
}

int main() {
    std::srand(1);
    std::cout << "=== IPC + Device 模块演示 ===\n";
    std::cout << "本程序用于展示 IPC 与设备管理功能。\n";
    print_help();

    std::string line;
    while (true) {
        std::cout << "ipc-device> " << std::flush;
        if (!std::getline(std::cin, line)) break;
        line = trim_left(line);
        if (line.empty()) continue;

        std::istringstream iss(line);
        std::string cmd;
        iss >> cmd;

        if (cmd == "exit" || cmd == "quit") break;
        else if (cmd == "help") print_help();
        else if (cmd == "demo") run_demo();
        else if (cmd == "create") {
            std::string name;
            iss >> name;
            if (name.empty()) name = "process";
            int pid = createProc(name);
            std::cout << "已创建 P" << pid << "。\n";
        }
        else if (cmd == "ps") showProcessTable();
        else if (cmd == "sem_create") {
            std::string name;
            int val;
            if (!(iss >> name >> val)) {
                std::cout << "用法：sem_create <name> <value>\n";
                continue;
            }
            std::cout << (ipc_manager.create_semaphore(name, val) ? "[成功]" : "[失败] 信号量重名") << "\n";
        }
        else if (cmd == "P" || cmd == "p") {
            std::string name;
            int pid;
            if (!(iss >> name >> pid)) {
                std::cout << "用法：P <name> <pid>\n";
                continue;
            }
            describe_p_result(ipc_manager.sem_P(name, pid));
        }
        else if (cmd == "V" || cmd == "v") {
            std::string name;
            if (!(iss >> name)) {
                std::cout << "用法：V <name>\n";
                continue;
            }
            describe_v_result(ipc_manager.sem_V(name));
        }
        else if (cmd == "sems") show_semaphores();
        else if (cmd == "send") {
            int from, to;
            if (!(iss >> from >> to)) {
                std::cout << "用法：send <from> <to> <message>\n";
                continue;
            }
            std::string msg;
            std::getline(iss, msg);
            msg = trim_left(msg);
            if (msg.empty()) msg = "<空消息>";
            std::cout << (ipc_manager.send_message(from, to, msg) ? "[成功]" : "[失败] 接收方 PID 不存在") << "\n";
        }
        else if (cmd == "read") {
            int pid;
            if (!(iss >> pid)) {
                std::cout << "用法：read <pid>\n";
                continue;
            }
            std::string msg = ipc_manager.read_message(pid);
            std::cout << (msg.empty() ? "<邮箱为空>" : msg) << "\n";
        }
        else if (cmd == "io") {
            int pid, devId;
            if (!(iss >> pid >> devId)) {
                std::cout << "用法：io <pid> <device_id>\n";
                continue;
            }
            describe_io_result(requestDeviceBanker(pid, devId));
        }
        else if (cmd == "finish") {
            int devId;
            if (!(iss >> devId)) {
                std::cout << "用法：finish <device_id>\n";
                continue;
            }
            deterministic_finish_device(devId);
        }
        else if (cmd == "irq") {
            int times = 1;
            iss >> times;
            if (times < 1) times = 1;
            for (int i = 0; i < times; ++i) processIOInterrupts();
            std::cout << "已完成 IRQ 模拟，共执行 " << times << " 次。\n";
        }
        else if (cmd == "release") {
            int pid;
            if (!(iss >> pid)) {
                std::cout << "用法：release <pid>\n";
                continue;
            }
            releaseProcessDevices(pid);
            std::cout << "已完成 P" << pid << " 的设备释放/回收。\n";
        }
        else if (cmd == "wakeup") {
            int pid;
            if (!(iss >> pid)) {
                std::cout << "用法：wakeup <pid>\n";
                continue;
            }
            std::cout << (wakeup(pid) == 0 ? "[成功]" : "[失败] PID 不存在") << "\n";
        }
        else if (cmd == "devices") show_devices();
        else if (cmd == "banker") show_banker();
        else {
            std::cout << "未知命令，请输入 help 查看命令列表。\n";
        }
    }

    std::cout << "已退出。\n";
    return 0;
}
