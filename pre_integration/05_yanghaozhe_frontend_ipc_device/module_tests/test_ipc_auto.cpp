#include "program.h"
#include "ipc.h"
#include <iostream>
#include <string>

static int pass_count = 0;
static int fail_count = 0;

void check(bool condition, const std::string& name) {
    if (condition) { ++pass_count; std::cout << "[PASS] " << name << "\n"; }
    else { ++fail_count; std::cout << "[FAIL] " << name << "\n"; }
}

int main() {
    std::cout << "=== IPC 模块自动测试 ===\n";
    int p1 = createProc("ipc_sender");
    int p2 = createProc("ipc_receiver");
    check(p1 > 0 && p2 > 0, "创建 IPC 测试进程");
    check(ipc_manager.create_semaphore("mutex", 1), "创建互斥信号量");
    check(!ipc_manager.create_semaphore("mutex", 1), "拒绝重复创建同名信号量");
    check(ipc_manager.sem_P("mutex", p1) == 1, "P 操作成功获取资源");
    check(ipc_manager.sem_P("mutex", p2) == 0, "资源不足时 P 操作阻塞进程");
    check(proMap[p2].state == BLOCK, "等待信号量的进程进入 BLOCK");
    check(ipc_manager.sem_V("mutex") == p2, "V 操作唤醒等待队列首进程");
    check(proMap[p2].state == READY, "被唤醒进程回到 READY");
    check(ipc_manager.send_message(p1, p2, "hello_ipc"), "发送消息到接收方邮箱");
    check(ipc_manager.read_message(p2).find("hello_ipc") != std::string::npos, "接收方读取消息");
    check(ipc_manager.read_message(p2).empty(), "消息读取后邮箱为空");
    check(ipc_manager.sem_P("missing", p1) == -1, "不存在的信号量返回错误");
    check(ipc_manager.sem_P("mutex", 9999) == -2, "拒绝不存在的进程执行 P 操作");
    std::cout << "IPC 自动测试完成：" << pass_count << " PASS / " << fail_count << " FAIL\n";
    return fail_count == 0 ? 0 : 1;
}
