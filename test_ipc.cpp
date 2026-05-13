// test_ipc.cpp - IPC 模块独立测试
// 编译: g++ -O2 -std=c++14 test_ipc.cpp memory/memory.cpp process/program.cpp process/ipc.cpp filesystem.cpp disk.cpp -o run_ipc_test -pthread
#include "memory/memory.h"
#include "process/program.h"
#include "process/ipc.h"
#include <iostream>

int nowTime = 0;

#define G "\033[1;32m"
#define R "\033[1;31m"
#define Y "\033[1;33m"
#define C "\033[1;36m"
#define N "\033[0m"

static int passed = 0, failed = 0;
void chk(const std::string& n, bool c) {
    if (c) { std::cout << G << "  [PASS] " << N << n << std::endl; passed++; }
    else   { std::cout << R << "  [FAIL] " << N << n << std::endl; failed++; }
}

int main() {
    std::cout << C << "\n=== IPC 模块测试 ===\n" << N << std::endl;

    // 创建测试进程
    int s1 = createProc("IPC_User1", 10, 4096, 5);
    int s2 = createProc("IPC_User2", 10, 4096, 5);
    int s3 = createProc("IPC_User3", 10, 4096, 5);
    chk("测试进程创建: s1", s1 > 0);
    chk("测试进程创建: s2", s2 > 0);
    chk("测试进程创建: s3", s3 > 0);

    // ---- 1. 信号量创建 ----
    std::cout << Y << "[1] 信号量创建" << N << std::endl;
    bool cr1 = ipc_manager.create_semaphore("mutex", 1);
    chk("创建 mutex 初值=1", cr1);

    bool cr2 = ipc_manager.create_semaphore("empty", 5);
    chk("创建 empty 初值=5", cr2);

    bool cr0 = ipc_manager.create_semaphore("zero", 0);
    chk("创建 zero 初值=0", cr0);

    bool dup = ipc_manager.create_semaphore("mutex", 1);
    chk("重复创建 mutex 失败", !dup);

    // ---- 2. P 操作 (获取) ----
    std::cout << Y << "\n[2] P 操作" << N << std::endl;
    
    // mutex=1 -> P -> 0, 成功获取
    int p_ok = ipc_manager.sem_P("mutex", s1);
    chk("s1 P(mutex): 成功获取(返回1)", p_ok == 1);

    // mutex=0 -> P -> -1, s2 被阻塞
    int p_block = ipc_manager.sem_P("mutex", s2);
    chk("s2 P(mutex): 被阻塞(返回0)", p_block == 0);
    chk("s2 状态变为 BLOCK", proMap[s2].state == BLOCK);
    chk("s2 阻塞原因含 mutex", proMap[s2].blockReason.find("mutex") != std::string::npos);

    // P 操作不存在的信号量
    int p_bad_sem = ipc_manager.sem_P("no_such_sem", s1);
    chk("P(不存在信号量): 返回-1", p_bad_sem == -1);

    // P 操作幽灵进程
    int p_ghost = ipc_manager.sem_P("mutex", 99999);
    chk("P(幽灵进程): 返回-2", p_ghost == -2);

    // ---- 3. V 操作 (释放) ----
    std::cout << Y << "\n[3] V 操作" << N << std::endl;

    int v1 = ipc_manager.sem_V("mutex");
    chk("V(mutex): 唤醒 s2", v1 == s2);
    chk("s2 恢复 READY", proMap[s2].state == READY);

    int v2 = ipc_manager.sem_V("mutex");
    chk("V(mutex): 无需唤醒(返回0)", v2 == 0);

    int v_bad = ipc_manager.sem_V("no_such_sem");
    chk("V(不存在信号量): 返回-1", v_bad == -1);

    // ---- 4. 初值为0的信号量 ----
    std::cout << Y << "\n[4] 初值=0 信号量测试" << N << std::endl;
    int p_zero = ipc_manager.sem_P("zero", s1);
    chk("P(zero=0): s1 立即阻塞", p_zero == 0);
    chk("s1 状态 BLOCK", proMap[s1].state == BLOCK);

    int v_zero = ipc_manager.sem_V("zero");
    chk("V(zero): 唤醒 s1", v_zero == s1);
    chk("s1 恢复 READY", proMap[s1].state == READY);

    // ---- 5. 多个进程等待同一信号量 ----
    std::cout << Y << "\n[5] 多进程排队等待" << N << std::endl;
    // mutex 当前值为... 让我重新创建一个
    ipc_manager.create_semaphore("lock", 1);
    ipc_manager.sem_P("lock", s1);  // s1 持有
    ipc_manager.sem_P("lock", s2);  // s2 等待
    ipc_manager.sem_P("lock", s3);  // s3 等待
    chk("s2 排队等待", proMap[s2].state == BLOCK);
    chk("s3 排队等待", proMap[s3].state == BLOCK);

    int v_s1 = ipc_manager.sem_V("lock");
    chk("V(lock): 唤醒第一个等待者 s2", v_s1 == s2);

    int v_s2 = ipc_manager.sem_V("lock");
    chk("V(lock): 唤醒第二个等待者 s3", v_s2 == s3);

    // ---- 6. 消息通信 ----
    std::cout << Y << "\n[6] 进程消息通信" << N << std::endl;

    bool send1 = ipc_manager.send_message(s1, s2, "Hello from s1!");
    chk("s1 -> s2: 发送成功", send1);

    bool send2 = ipc_manager.send_message(s3, s2, "Hello from s3!");
    chk("s3 -> s2: 发送成功", send2);

    // 按 FIFO 顺序读取
    std::string msg1 = ipc_manager.read_message(s2);
    chk("s2 读取第一条消息非空", !msg1.empty());
    chk("第一条来自 s1", msg1.find("from s1") != std::string::npos || msg1.find("P" + std::to_string(s1)) != std::string::npos);

    std::string msg2 = ipc_manager.read_message(s2);
    chk("s2 读取第二条消息非空", !msg2.empty());

    // 读空信箱
    std::string msg3 = ipc_manager.read_message(s2);
    chk("空信箱返回空串", msg3.empty());

    // 发给不存在的进程
    bool send_bad = ipc_manager.send_message(s1, 99999, "to ghost");
    chk("发送给幽灵进程: 失败", !send_bad);

    // ---- 7. 综合场景 ----
    std::cout << Y << "\n[7] 综合场景: 生产者-消费者信号量" << N << std::endl;
    ipc_manager.create_semaphore("prod_mutex", 1);
    ipc_manager.create_semaphore("full", 0);
    ipc_manager.create_semaphore("empty_slots", 3);

    // 消费者等待 full
    ipc_manager.sem_P("full", s3);
    chk("消费者 P(full=0): 阻塞", proMap[s3].state == BLOCK);

    // 生产者 P(empty_slots) -> 成功, V(full) -> 唤醒消费者
    ipc_manager.sem_P("empty_slots", s1);
    ipc_manager.sem_V("full");
    chk("生产者 V(full): 唤醒消费者 s3", proMap[s3].state == READY);

    // ---- 结果 ----
    std::cout << C << "\n========================================" << N << std::endl;
    std::cout << "  IPC测试: " << G << passed << " 通过" << N
              << " / " << (failed ? R : N) << failed << " 失败" << N << std::endl;
    std::cout << C << "========================================\n" << N << std::endl;
    return failed > 0 ? 1 : 0;
}
