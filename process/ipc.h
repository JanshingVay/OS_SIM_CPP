#pragma once
#include <string>
#include <queue>
#include <map>
#include <vector>

struct Semaphore {
    std::string name;
    int value;
    std::queue<int> waitQueue; // 等待该信号量的进程PID队列
};

struct Message {
    int sender_pid;
    std::string text;
};

struct Mailbox {
    std::queue<Message> msgs;
};

class IPCManager {
public:
    std::map<std::string, Semaphore> semaphores;
    std::map<int, Mailbox> mailboxes; // 以接收方 PID 为键的信箱

    // 信号量机制
    bool create_semaphore(const std::string& name, int init_val);
    int sem_P(const std::string& name, int pid); // 返回 1 继续执行, 0 阻塞, -1 失败
    int sem_V(const std::string& name);          // 返回唤醒的 PID, 0 无需唤醒, -1 失败

    // 消息队列机制
    bool send_message(int sender_pid, int receiver_pid, const std::string& msg);
    std::string read_message(int receiver_pid);
};

extern IPCManager ipc_manager;