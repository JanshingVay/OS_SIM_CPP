#include "memory/memory.h"
#include <iostream>

int main() {
    std::cout << "=== Memory standalone demo before final integration ===\n";
    mem_manager.reset_memory_statistics();
    mem_manager.set_replacement_policy(ReplacementPolicy::LRU);

    int pid1 = 100;
    int pid2 = 101;
    mem_manager.alloc_mem(pid1, PAGE_SIZE * 3);
    mem_manager.alloc_mem(pid2, PAGE_SIZE * 2);

    mem_manager.access_addr(pid1, 0x0000);
    mem_manager.access_addr(pid1, 0x1000);
    mem_manager.access_addr(pid1, 0x2000);

    int frame = -1;
    size_t offset = 0;
    if (mem_manager.translate(pid1, 0x1000, frame, offset)) {
        std::cout << "translate pid=" << pid1 << " logical=0x1000 -> frame=" << frame << " offset=" << offset << "\n";
    }

    mem_manager.dynamic_alloc(pid1, PAGE_SIZE);
    mem_manager.share_mem(pid1, pid2, 1);
    mem_manager.print_memory_statistics();
    mem_manager.free_mem(pid1);
    mem_manager.free_mem(pid2);
    std::cout << "Demo finished.\n";
    return 0;
}
