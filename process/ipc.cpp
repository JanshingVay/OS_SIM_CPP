#include "ipc.h"
#include "program.h"
#include <iostream>

IPCManager ipc_manager;

bool IPCManager::create_semaphore(const std::string& name, int init_val) {
    if (semaphores.find(name) != semaphores.end()) return false;
    semaphores[name] = { name, init_val, std::queue<int>() };
    return true;
}

int IPCManager::sem_P(const std::string& name, int pid) {
    // 【新增防呆设计】：检查进程是否真的存在且存活
    if (proMap.find(pid) == proMap.end()) {
        return -2; // 返回 -2 表示幽灵进程，拒绝执行
    }

    auto it = semaphores.find(name);
    if (it == semaphores.end()) return -1;

    // 获取信号量，值减1
    it->second.value--;
    if (it->second.value < 0) {
        // 资源不足，将进程加入等待队列并阻塞
        it->second.waitQueue.push(pid);
        block(pid, 0, "等待信号量:" + name);
        return 0; // 进程已被阻塞
    }
    return 1; // 获取成功，继续执行
}

int IPCManager::sem_V(const std::string& name) {
    auto it = semaphores.find(name);
    if (it == semaphores.end()) return -1;

    // 释放信号量，值加1
    it->second.value++;
    if (it->second.value <= 0 && !it->second.waitQueue.empty()) {
        // 唤醒等待队列中的第一个进程
        int wake_pid = it->second.waitQueue.front();
        it->second.waitQueue.pop();
        wakeup(wake_pid);
        return wake_pid;
    }
    return 0; // 无需唤醒
}

bool IPCManager::send_message(int sender_pid, int receiver_pid, const std::string& msg) {
    // 检查接收进程是否存在
    if (proMap.find(receiver_pid) == proMap.end()) return false;
    mailboxes[receiver_pid].msgs.push({ sender_pid, msg });
    return true;
}

std::string IPCManager::read_message(int receiver_pid) {
    auto it = mailboxes.find(receiver_pid);
    if (it == mailboxes.end() || it->second.msgs.empty()) {
        return ""; // 信箱为空
    }
    Message m = it->second.msgs.front();
    it->second.msgs.pop();
    return "[发自 P" + std::to_string(m.sender_pid) + "]: " + m.text;
}