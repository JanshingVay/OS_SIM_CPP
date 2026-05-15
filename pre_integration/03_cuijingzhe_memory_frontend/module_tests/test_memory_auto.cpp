#include "memory/memory.h"
#include <iostream>
#include <cstdlib>
#include <string>

static int pass_count = 0;
static int fail_count = 0;

void check(bool condition, const std::string& name) {
    if (condition) { ++pass_count; std::cout << "[PASS] " << name << "\n"; }
    else { ++fail_count; std::cout << "[FAIL] " << name << "\n"; }
}

int main() {
    std::cout << "=== 崔敬哲：内存管理模块自动测试 ===\n";
    mem_manager.reset_memory_statistics();
    mem_manager.set_replacement_policy(ReplacementPolicy::LRU);
    check(mem_manager.get_replacement_policy() == ReplacementPolicy::LRU, "切换 LRU 页面置换算法");

    int pid1 = 301;
    int pid2 = 302;
    check(mem_manager.alloc_mem(pid1, PAGE_SIZE * 3), "为 pid1 分配 3 页虚拟内存");
    check(!mem_manager.alloc_mem(pid1, PAGE_SIZE), "拒绝重复 PID 分配");
    check(mem_manager.alloc_mem(pid2, PAGE_SIZE * 2), "为 pid2 分配 2 页虚拟内存");

    check(mem_manager.access_addr(pid1, 0x0000), "访问第 0 页并触发装入");
    check(mem_manager.access_addr(pid1, 0x1000), "访问第 1 页并触发装入");
    int frame = -1;
    size_t offset = 0;
    check(mem_manager.translate(pid1, 0x1000, frame, offset), "逻辑地址翻译成功");
    check(frame >= 0 && frame < TOTAL_PAGES && offset == 0, "翻译结果的物理帧与页内偏移合法");

    check(mem_manager.dynamic_alloc(pid1, PAGE_SIZE), "动态扩展 pid1 内存");
    check(mem_manager.access_addr(pid1, 0x3000), "访问动态扩展后的新页");
    check(mem_manager.share_mem(pid1, pid2, 1), "共享 1 页内存");
    check(mem_manager.stat_memory_accesses >= 3, "统计访问次数已更新");
    check(mem_manager.stat_page_faults >= 1, "缺页统计已更新");

    check(mem_manager.free_mem(pid1), "释放 pid1 内存");
    check(mem_manager.free_mem(pid2), "释放 pid2 内存");
    std::cout << "内存管理自动测试完成：" << pass_count << " PASS / " << fail_count << " FAIL\n";
    int code = (fail_count == 0 ? 0 : 1);
    std::cout.flush();
    std::cerr.flush();
    std::_Exit(code);
}
