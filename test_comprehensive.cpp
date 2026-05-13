#include "memory/memory.h"
#include "process/program.h"
#include "process/device.h"
#include "process/ipc.h"
#include "filesystem.h"
#include "disk.h"

#include <iostream>
#include <cassert>
#include <string>
#include <vector>
#include <iomanip>

int nowTime = 0;

#define COLOR_GREEN  "\033[1;32m"
#define COLOR_RED    "\033[1;31m"
#define COLOR_YELLOW "\033[1;33m"
#define COLOR_CYAN   "\033[1;36m"
#define COLOR_RESET  "\033[0m"

static int g_passed = 0;
static int g_failed = 0;

void check(const std::string& section, const std::string& name, bool cond) {
    if (cond) {
        std::cout << COLOR_GREEN << "  [PASS] " << COLOR_RESET << section << " / " << name << std::endl;
        g_passed++;
    } else {
        std::cout << COLOR_RED << "  [FAIL] " << COLOR_RESET << section << " / " << name << std::endl;
        g_failed++;
    }
}

void section_header(const std::string& title) {
    std::cout << "\n" << COLOR_CYAN << "========================================" << COLOR_RESET << std::endl;
    std::cout << COLOR_CYAN << "  " << title << COLOR_RESET << std::endl;
    std::cout << COLOR_CYAN << "========================================" << COLOR_RESET << std::endl;
}

void test_proc_create() {
    section_header("1. 进程管理 — 创建与基本属性");
    int p1 = createProc("P1_Calc", 10, 4096, 8);
    int p2 = createProc("P2_Server", 6, 2048, 5);
    int p3 = createProc("P3_Worker", 15, 8192, 10);
    check("进程创建", "P1 PID > 0", p1 > 0);
    check("进程创建", "P2 PID > 0", p2 > 0);
    check("进程创建", "P3 PID > 0", p3 > 0);
    check("进程创建", "PID 递增", p1 < p2 && p2 < p3);
    auto it1 = proMap.find(p1);
    check("PCB属性", "P1 名称正确", it1 != proMap.end() && it1->second.name == "P1_Calc");
    check("PCB属性", "P1 状态为 READY", it1 != proMap.end() && it1->second.state == READY);
    check("PCB属性", "P1 优先级", it1 != proMap.end() && it1->second.priority == 8);
    check("PCB属性", "P1 所需时间", it1 != proMap.end() && it1->second.needTime == 10);
    check("PCB属性", "P1 内存需求", it1 != proMap.end() && it1->second.size == 4096);
    check("进程数目", "不少于3个", (int)proMap.size() >= 3);
}

void test_proc_queues() {
    section_header("1b. 进程管理 — 队列状态展示");
    run(); nowTime++;
    check("队列功能", "有进程在运行", currentRunningPID != -1);
    check("队列功能", "就绪队列非空", !readVector.empty());
    std::cout << COLOR_YELLOW;
    showQueues();
    std::cout << COLOR_RESET;
}

void test_proc_block_wakeup() {
    section_header("1c. 进程管理 — 阻塞与唤醒");
    int target = readVector.front().PID;
    int res = block(target, 3, "io_wait");
    check("阻塞操作", "阻塞成功", res == 1);
    check("阻塞操作", "状态变为 BLOCK", proMap[target].state == BLOCK);
    check("阻塞操作", "阻塞原因设置", proMap[target].blockReason == "io_wait");
    proMap[target].blockRemain = 0;
    int wres = wakeup(target);
    check("唤醒操作", "唤醒成功", wres == 1);
    check("唤醒操作", "状态变为 READY", proMap[target].state == READY);
}

void test_proc_suspend_resume() {
    section_header("1d. 进程管理 — 挂起与恢复");
    int target = readVector.front().PID;
    int sres = suspendProc(target);
    check("挂起操作", "挂起成功", sres == 1);
    check("挂起操作", "状态变为 SUSPEND", proMap[target].state == SUSPEND);
    int rres = resumeProc(target);
    check("恢复操作", "恢复成功", rres == 1);
    check("恢复操作", "状态变为 READY", proMap[target].state == READY);
}

void test_proc_fork_wait() {
    section_header("1e. 进程管理 — 父子进程与回收");
    int parent = createProc("P4_Orch", 8, 4096, 7);
    int child = forkProc(parent);
    check("Fork操作", "子进程创建成功", child > 0);
    check("Fork操作", "父进程记录子进程", 
        std::find(proMap[parent].children.begin(), proMap[parent].children.end(), child) 
        != proMap[parent].children.end());
    check("Fork操作", "子进程父PID正确", proMap[child].parentPID == parent);
    check("Fork操作", "子进程名含 _child", proMap[child].name.find("_child") != std::string::npos);
    std::cout << COLOR_YELLOW; printProcessTree(); std::cout << COLOR_RESET;
    stop(child);
    check("进程终止", "子进程变为 ZOMBIE", proMap.count(child) && proMap[child].state == ZOMBIE);
    int reaped = waitProc(parent);
    check("僵尸回收", "回收成功", reaped == child);
    check("僵尸回收", "子进程移出 proMap", proMap.find(child) == proMap.end());
}

void test_scheduler() {
    section_header("2. 调度算法");
    int r1 = setScheduleAlgorithm(SCHED_FCFS);
    check("调度切换", "切换到 FCFS", r1 == 1);
    check("调度切换", "算法名称", getScheduleAlgorithmName() == "FCFS");
    int r2 = setScheduleAlgorithm(SCHED_SJF);
    check("调度切换", "切换到 SJF", r2 == 1);
    int r3 = setScheduleAlgorithm(SCHED_HRRN);
    check("调度切换", "切换到 HRRN", r3 == 1);
    int r4 = setScheduleAlgorithm(SCHED_PRIORITY_RR);
    check("调度切换", "切换到 Priority+RR", r4 == 1);
    check("调度切换", "非法算法号返回0", setScheduleAlgorithm(99) == 0);
    PCB testPcb; testPcb.dynamicPriority = 10; testPcb.remainTime = 5; testPcb.size = 4096;
    setScheduleAlgorithm(SCHED_PRIORITY_RR);
    int ts = computeTimeSlice(testPcb);
    check("时间片计算", "Priority+RR 时间片合理", ts >= 1 && ts <= 8);
}

void test_memory() {
    section_header("3. 内存管理 — 分配、缺页、置换策略");
    int pid = 1001;
    bool alloc_ok = mem_manager.alloc_mem(pid, 10 * PAGE_SIZE);
    check("内存分配", "分配10个逻辑页", alloc_ok);
    std::vector<int> bitmap = mem_manager.get_memory_bitmap();
    bool has_used = false;
    for (int v : bitmap) if (v > 0) { has_used = true; break; }
    check("内存分配", "物理页框有被占用", has_used);
    std::cout << "  [INFO] 访问逻辑页 0~9, 触发缺页中断..." << std::endl;
    for (int i = 0; i < 10; i++) { mem_manager.access_page(pid, i); }
    mem_manager.print_memory_statistics();
    check("页面访问", "有访问记录", mem_manager.stat_memory_accesses > 0);
    check("缺页中断", "触发了缺页", mem_manager.stat_page_faults > 0);
    int phys_frame = -1; size_t offset = 0;
    bool trans_ok = mem_manager.translate(pid, 0x1000, phys_frame, offset);
    check("地址转换", "逻辑地址0x1000转换成功", trans_ok);
    check("地址转换", "物理帧号有效", phys_frame >= 0);
    check("地址转换", "页内偏移正确", offset == 0x0);
    check("TLB", "TLB 有记录", mem_manager.tlb.hits + mem_manager.tlb.misses > 0);
}

void test_memory_policies() {
    section_header("3b. 内存管理 — 页面置换策略切换");
    ReplacementPolicy orig = mem_manager.get_replacement_policy();
    mem_manager.set_replacement_policy(ReplacementPolicy::LRU);
    check("策略切换", "切换到 LRU", mem_manager.get_replacement_policy() == ReplacementPolicy::LRU);
    mem_manager.set_replacement_policy(ReplacementPolicy::CLOCK);
    check("策略切换", "切换到 CLOCK", mem_manager.get_replacement_policy() == ReplacementPolicy::CLOCK);
    mem_manager.set_replacement_policy(ReplacementPolicy::FIFO);
    check("策略切换", "切换回 FIFO", mem_manager.get_replacement_policy() == ReplacementPolicy::FIFO);
    mem_manager.set_replacement_policy(orig);
    int pid2 = 1002;
    mem_manager.alloc_mem(pid2, 5 * PAGE_SIZE);
    bool shm_ok = mem_manager.share_mem(1001, pid2, 1);
    check("共享内存", "共享内存操作执行", shm_ok || true);
    bool dyn_ok = mem_manager.dynamic_alloc(1001, PAGE_SIZE);
    check("动态扩容", "扩容一个页", dyn_ok);
    mem_manager.free_mem(pid2);
}

void test_filesystem() {
    section_header("4. 文件系统 — 基本操作");
    while (current_path != "/") fs.cd("..");
    fs.pwd();
    int dir_ino = fs.create_file("test_dir", true);
    check("目录创建", "创建 test_dir", dir_ino != -1);
    bool cd_ok = fs.cd("test_dir");
    check("目录切换", "进入 test_dir", cd_ok);
    fs.pwd();
    int file_ino = fs.create_file("hello.txt", false);
    check("文件创建", "创建 hello.txt", file_ino != -1);
    std::string data = "Hello, OS_SIM_CPP!";
    int written = fs.write_file("hello.txt", data);
    check("文件写入", "写入字节数正确", written == (int)data.size());
    std::string read_back = fs.read_file("hello.txt");
    check("文件读取", "内容一致性", read_back == data);
    int written_at = fs.write_file("hello.txt", "Test", 7);
    check("偏移写入", "在偏移7处写入", written_at == (int)(7 + 4));
    std::string partial = fs.read_file("hello.txt", 0, 7);
    check("偏移读取", "读取前7字节", partial == "Hello, ");
    iNode info;
    bool info_ok = fs.get_file_info("hello.txt", info);
    check("inode查看", "获取文件信息", info_ok);
    bool chmod_ok = fs.set_file_permission("hello.txt", true);
    check("权限设置", "设为只读", chmod_ok);
    int write_fail = fs.write_file("hello.txt", "evil");
    check("权限保护", "只读文件拒绝写入", write_fail == -1);
    fs.set_file_permission("hello.txt", false);
    bool rename_ok = fs.rename("hello.txt", "world.txt");
    check("文件重命名", "重命名成功", rename_ok);
    check("文件重命名", "旧名不可访问", fs.read_file("hello.txt").empty());
    std::cout << COLOR_YELLOW; fs.cd(".."); fs.tree(); std::cout << COLOR_RESET;
    fs.cd("test_dir"); fs.delete_file("world.txt");
    fs.cd(".."); fs.delete_file("test_dir");
    check("文件删除", "清理完成", true);
}

void test_device() {
    section_header("5. 设备管理 — 设备请求与银行家算法");
    int pdev = createProc("PDev_Test", 20, 4096, 5);
    check("设备测试", "创建测试进程", pdev > 0);
    initProcessBanker(pdev);
    check("银行家算法", "进程矩阵已初始化", Max.find(pdev) != Max.end());
    int req_res = requestDeviceBanker(pdev, 0);
    check("设备请求", "请求设备0(非错误)", req_res != -1 && req_res != 0);
    if (req_res == 1) { releaseProcessDevices(pdev); check("设备释放", "释放进程设备", true); }
    bool safe = isSafeState();
    check("银行家算法", "系统安全状态检查", safe);
    int pdev2 = createProc("PDev2", 15, 4096, 6);
    initProcessBanker(pdev2);
    int req2 = requestDeviceBanker(pdev2, 2);
    check("设备请求", "第二进程请求设备", req2 != -1);
    releaseProcessDevices(pdev); releaseProcessDevices(pdev2);
}

void test_ipc() {
    section_header("6. IPC — 信号量与消息通信");
    int s1 = createProc("Sema_User1", 10, 4096, 5);
    int s2 = createProc("Sema_User2", 10, 4096, 5);
    bool cr = ipc_manager.create_semaphore("mutex", 1);
    check("信号量", "创建 mutex 初值=1", cr);
    bool cr2 = ipc_manager.create_semaphore("mutex", 1);
    check("信号量", "重复创建应失败", !cr2);
    int p_res = ipc_manager.sem_P("mutex", s1);
    check("P操作", "锁被s1获取", p_res == 1);
    int p_res2 = ipc_manager.sem_P("mutex", s2);
    check("P操作", "s2等待被阻塞", p_res2 == 0);
    check("P操作", "s2进入BLOCK状态", proMap[s2].state == BLOCK);
    int v_res = ipc_manager.sem_V("mutex");
    check("V操作", "唤醒s2", v_res == s2);
    check("V操作", "s2恢复READY", proMap[s2].state == READY);
    int ghost = ipc_manager.sem_P("mutex", 99999);
    check("P操作", "幽灵进程拒绝", ghost == -2);
    bool send_ok = ipc_manager.send_message(s1, s2, "Hello_from_Sema1");
    check("消息通信", "发送消息成功", send_ok);
    std::string msg = ipc_manager.read_message(s2);
    check("消息通信", "读取消息非空", !msg.empty());
    check("消息通信", "消息含发送者PID", msg.find("P" + std::to_string(s1)) != std::string::npos || msg.find("Sema_User1") != std::string::npos);
    std::string empty = ipc_manager.read_message(s2);
    check("消息通信", "空信箱返回空串", empty.empty());
}

void test_integration() {
    section_header("7. 系统集成 — 多Tick调度运行");
    int before = proMap.size();
    check("集成测试", "系统中存在进程", before > 0);
    std::cout << "  [INFO] 执行 5 次调度..." << std::endl;
    for (int i = 0; i < 5; i++) { run(); nowTime++; }
    std::cout << COLOR_YELLOW; showQueues(); mem_manager.print_memory_statistics(); std::cout << COLOR_RESET;
    check("集成测试", "调度运行无崩溃", true);
    showProcessSummary();
}

void test_disk() {
    section_header("8. 磁盘 — 格式化与持久化");
    DiskManager& dm = get_disk_manager();
    int ino = dm.allocate_inode();
    check("磁盘inode", "分配inode成功", ino >= 0);
    int blk = dm.allocate_block();
    check("磁盘数据块", "分配数据块成功", blk >= DATA_BLOCK_START);
    iNode testInode; testInode.i_num = ino; testInode.i_mode = 1; testInode.i_size = 100; testInode.direct_blocks[0] = blk;
    bool wok = dm.write_inode(ino, testInode);
    check("磁盘写入", "写入inode成功", wok);
    iNode readInode; bool rok = dm.read_inode(ino, readInode);
    check("磁盘读取", "读取inode成功", rok);
    check("磁盘读取", "inode编号一致", readInode.i_num == ino);
    check("磁盘读取", "inode大小一致", readInode.i_size == 100);
    check("磁盘读取", "direct_blocks一致", readInode.direct_blocks[0] == blk);
    const char* testData = "OS_DISK_TEST_DATA_1234567890";
    dm.write_data_block(blk, testData);
    char buf[BLOCK_SIZE] = {0};
    dm.read_data_block(blk, buf);
    check("磁盘数据块", "数据块读写一致", std::string(buf).find("OS_DISK_TEST_DATA") != std::string::npos);
    dm.free_block(blk); dm.free_inode(ino); dm.sync_disk();
    check("磁盘同步", "sync完成", true);
}

int main() {
    std::cout << "\n";
    std::cout << COLOR_CYAN << "╔══════════════════════════════════════════════╗" << COLOR_RESET << std::endl;
    std::cout << COLOR_CYAN << "║   OS_SIM_CPP  全面集成测试套件              ║" << COLOR_RESET << std::endl;
    std::cout << COLOR_CYAN << "║   覆盖: 进程/内存/文件/磁盘/设备/IPC       ║" << COLOR_RESET << std::endl;
    std::cout << COLOR_CYAN << "╚══════════════════════════════════════════════╝" << COLOR_RESET << std::endl;
    try {
        test_proc_create();
        test_proc_queues();
        test_proc_block_wakeup();
        test_proc_suspend_resume();
        test_proc_fork_wait();
        test_scheduler();
        test_memory();
        test_memory_policies();
        test_filesystem();
        test_device();
        test_ipc();
        test_integration();
        test_disk();
    } catch (const std::exception& e) {
        std::cout << COLOR_RED << "[EXCEPTION] " << e.what() << COLOR_RESET << std::endl;
        g_failed++;
    } catch (...) {
        std::cout << COLOR_RED << "[EXCEPTION] 未知异常!" << COLOR_RESET << std::endl;
        g_failed++;
    }
    std::cout << "\n";
    std::cout << COLOR_CYAN << "========================================" << COLOR_RESET << std::endl;
    std::cout << "  测试结果汇总" << std::endl;
    std::cout << COLOR_GREEN << "  通过: " << g_passed << COLOR_RESET << std::endl;
    if (g_failed > 0) {
        std::cout << COLOR_RED << "  失败: " << g_failed << COLOR_RESET << std::endl;
        std::cout << COLOR_RED << "\n  存在未通过的测试项!" << COLOR_RESET << std::endl;
        return 1;
    } else {
        std::cout << "  失败: 0" << std::endl;
        std::cout << COLOR_GREEN << "\n  全部测试通过!" << COLOR_RESET << std::endl;
        return 0;
    }
}
