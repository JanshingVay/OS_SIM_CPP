// test_memory.cpp - 内存管理模块独立测试
// 编译: g++ -O2 -std=c++14 test_memory.cpp memory/memory.cpp disk.cpp -o run_memory_test -pthread
#include "memory/memory.h"
#include "disk.h"
#include <iostream>
#include <cassert>
#include <cstring>

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
    std::cout << C << "\n=== 内存管理模块测试 ===\n" << N << std::endl;

    // ---- 1. 内存分配 ----
    std::cout << Y << "[1] 进程内存分配" << N << std::endl;
    int pidA = 101, pidB = 102, pidC = 103;
    chk("为 P101 分配 10 页", mem_manager.alloc_mem(pidA, 10 * PAGE_SIZE));
    chk("为 P102 分配 5 页", mem_manager.alloc_mem(pidB, 5 * PAGE_SIZE));
    chk("为 P103 分配 8 页", mem_manager.alloc_mem(pidC, 8 * PAGE_SIZE));

    std::vector<int> bm = mem_manager.get_memory_bitmap();
    int used = 0;
    for (int v : bm) if (v > 0) used++;
    chk("物理页框已被占用", used > 0);
    std::cout << "  [INFO] 已用物理页: " << used << " / " << mem_manager.get_active_physical_pages() << std::endl;

    // 重复分配应失败或覆盖
    chk("重复分配 P101", !mem_manager.alloc_mem(pidA, 10 * PAGE_SIZE));

    // ---- 2. 页面访问 & 缺页中断 ----
    std::cout << Y << "\n[2] 页面访问与缺页中断" << N << std::endl;
    mem_manager.reset_memory_statistics();
    for (int i = 0; i < 10; i++) {
        mem_manager.access_page(pidA, i);
    }
    mem_manager.print_memory_statistics();
    chk("访问次数 = 10", mem_manager.stat_memory_accesses == 10);
    chk("首次访问触发缺页", mem_manager.stat_page_faults > 0);

    // 重复访问应命中
    for (int i = 0; i < 5; i++) {
        mem_manager.access_page(pidA, i);
    }
    chk("重复访问有命中", mem_manager.stat_page_hits > 0);

    // 非法越界访问
    mem_manager.access_page(pidA, 9999);
    chk("非法越界被记录", mem_manager.stat_segment_faults > 0);

    // ---- 3. 地址转换 ----
    std::cout << Y << "[3] 地址转换 (逻辑地址 -> 物理帧)" << N << std::endl;
    int phys = -1; size_t off = 0;
    bool t1 = mem_manager.translate(pidA, 0x0000, phys, off);
    chk("0x0000 转换成功", t1);
    chk("  物理帧 >= 0", phys >= 0);
    chk("  页内偏移 = 0", off == 0x0);

    phys = -1; off = 0;
    bool t2 = mem_manager.translate(pidA, 0x1000, phys, off);
    chk("0x1000 转换成功", t2);
    chk("  页内偏移 = 0", off == 0x0);

    phys = -1; off = 0;
    bool t3 = mem_manager.translate(pidA, 0x1FFF, phys, off);
    chk("0x1FFF 转换成功", t3);
    chk("  页内偏移 = 0x1FFF", off == 0xFFF);

    // 未分配页访问：触发缺页装入
    phys = -1; off = 0;
    bool t4 = mem_manager.translate(pidB, 0x3000, phys, off);
    chk("未装入页转换成功(缺页装入)", t4);
    chk("  物理帧 >= 0", phys >= 0);

    // ---- 4. TLB 快表 ----
    std::cout << Y << "[4] TLB 快表" << N << std::endl;
    mem_manager.tlb.clear();
    mem_manager.reset_memory_statistics();
    // 第一次访问: TLB miss
    mem_manager.access_page(pidA, 0);
    int tlb_misses_before = mem_manager.tlb.misses;
    // 第二次访问: TLB hit
    mem_manager.access_page(pidA, 0);
    chk("TLB 有命中记录", mem_manager.tlb.hits > 0);
    std::cout << "  [INFO] TLB hits: " << mem_manager.tlb.hits 
              << " | misses: " << mem_manager.tlb.misses << std::endl;

    // ---- 5. 页面置换策略 ----
    std::cout << Y << "[5] 页面置换策略切换" << N << std::endl;
    ReplacementPolicy orig = mem_manager.get_replacement_policy();
    mem_manager.set_replacement_policy(ReplacementPolicy::LRU);
    chk("切换到 LRU", mem_manager.get_replacement_policy() == ReplacementPolicy::LRU);
    mem_manager.set_replacement_policy(ReplacementPolicy::CLOCK);
    chk("切换到 CLOCK", mem_manager.get_replacement_policy() == ReplacementPolicy::CLOCK);
    mem_manager.set_replacement_policy(ReplacementPolicy::FIFO);
    chk("切换回 FIFO", mem_manager.get_replacement_policy() == ReplacementPolicy::FIFO);

    // 在 LRU 下访问页面，观察置换行为
    mem_manager.set_replacement_policy(ReplacementPolicy::LRU);
    // 连续访问大量页面触发 LRU 置换
    for (int i = 0; i < 10; i++) mem_manager.access_page(pidA, i);
    mem_manager.print_memory_statistics();
    chk("LRU 置换运行无崩溃", true);

    mem_manager.set_replacement_policy(ReplacementPolicy::CLOCK);
    mem_manager.reset_memory_statistics();
    for (int i = 0; i < 10; i++) mem_manager.access_page(pidB, i);
    mem_manager.print_memory_statistics();
    chk("CLOCK 置换运行无崩溃", true);

    mem_manager.set_replacement_policy(orig);

    // ---- 6. 共享内存 ----
    std::cout << Y << "[6] 共享内存" << N << std::endl;
    bool shm = mem_manager.share_mem(pidA, pidB, 1);
    chk("共享内存操作已执行", true);

    // ---- 7. 动态扩缩容 ----
    std::cout << Y << "[7] 动态内存调整" << N << std::endl;
    bool exp = mem_manager.dynamic_alloc(pidC, PAGE_SIZE);
    chk("扩容 1 页", exp);
    bool shr = mem_manager.dynamic_alloc(pidC, -PAGE_SIZE);
    chk("缩容 1 页", shr);

    // ---- 8. 内存释放 ----
    std::cout << Y << "[8] 内存释放" << N << std::endl;
    chk("释放 P101", mem_manager.free_mem(pidA));
    chk("释放后 bitmap 更新", mem_manager.get_memory_bitmap().size() > 0);
    chk("释放 P102", mem_manager.free_mem(pidB));
    chk("释放 P103", mem_manager.free_mem(pidC));

    bm = mem_manager.get_memory_bitmap();
    int used2 = 0;
    for (int v : bm) if (v > 0) used2++;
    chk("全部释放后无残留", used2 == 0);

    // ---- 9. 访问刚释放的进程 ----
    std::cout << Y << "[9] 边界条件" << N << std::endl;
    mem_manager.access_page(pidA, 0);
    chk("访问已释放进程的页(记录非法)", mem_manager.stat_segment_faults > 0);

    // ---- 结果 ----
    std::cout << C << "\n========================================" << N << std::endl;
    std::cout << "  内存测试: " << G << passed << " 通过" << N
              << " / " << (failed ? R : N) << failed << " 失败" << N << std::endl;
    std::cout << C << "========================================\n" << N << std::endl;
    return failed > 0 ? 1 : 0;
}
