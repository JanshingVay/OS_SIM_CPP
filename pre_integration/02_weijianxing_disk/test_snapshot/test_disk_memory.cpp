#include "disk.h"
#include "memory/memory.h"
#include <iostream>
#include <vector>

int main() {
    std::cout << "========== [集成测试：磁盘 + 内存] ==========" << std::endl;

    // 1. 初始化内存（内部会通过 get_disk_manager() 挂载磁盘） [cite: 85]
    // 确保物理页容量限制在 4 页，以便快速触发置换 [cite: 74]
    
    // 2. 模拟进程 A 分配内存
    int pid = 101;
    // 10 * PAGE_SIZE = 10 个逻辑页（10*1024 字节在 4KB 页下只有约 3 页，会误触越界）
    mem_manager.alloc_mem(pid, 10 * PAGE_SIZE);

    // 3. 连续访问页面，触发缺页（物理页充足时未必发生 Swap Out）
    std::cout << "\n[测试] 开始连续访问逻辑页，观察缺页与装入..." << std::endl;
    for (int i = 0; i < 10; ++i) {
        // 当访问页数超过物理页限额时，会自动调用 get_disk_manager().write_data_block
        mem_manager.access_page(pid, i); 
    }

    // 4. 打印统计信息，验证是否有磁盘交换发生
    mem_manager.print_memory_statistics();

    std::cout << "\n[成功] 编译与基本集成测试完成！" << std::endl;
    return 0;
}