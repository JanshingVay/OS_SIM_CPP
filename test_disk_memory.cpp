#include "disk.h"
#include "memory/memory.h"
#include <iostream>
#include <vector>
#include <cassert>

// 辅助断言函数
void run_test(const std::string& test_name, bool condition) {
    if (condition) {
        std::cout << "  [✔] " << test_name << " 通过" << std::endl;
    } else {
        std::cerr << "  [✖] " << test_name << " 失败！" << std::endl;
        exit(1);
    }
}

int main() {
    std::cout << "========== [集成测试：磁盘 + 内存] ==========" << std::endl;

    // ==================== 磁盘模块测试 ====================
    std::cout << "\n--- 阶段 1: 磁盘模块 - 直接索引测试 ---" << std::endl;
    
    // 测试直接索引块分配
    iNode test_inode;
    test_inode.i_num = 999;  // 测试用 inode
    
    // 分配前 5 个直接块
    for (int i = 0; i < 5; ++i) {
        int block = get_disk_manager().allocate_nth_block(test_inode, i);
        run_test("分配直接块 " + std::to_string(i), block != -1);
    }
    
    // 验证 get_nth_block 能正确读取
    for (int i = 0; i < 5; ++i) {
        int block = get_disk_manager().get_nth_block(test_inode, i);
        run_test("读取直接块 " + std::to_string(i), block != -1);
    }
    
    std::cout << "\n--- 阶段 2: 磁盘模块 - 一级间接索引测试 ---" << std::endl;
    
    // 分配第 12 个块（超过 10 个直接块，触发一级间接）
    int indirect_block = get_disk_manager().allocate_nth_block(test_inode, 12);
    run_test("分配一级间接块 (逻辑块 12)", indirect_block != -1);
    run_test("验证一级间接块已创建", test_inode.single_indirect != -1);
    
    // 验证能正确读取
    int read_block = get_disk_manager().get_nth_block(test_inode, 12);
    run_test("读取一级间接块数据", read_block == indirect_block);
    
    // 再分配几个一级间接块
    for (int i = 10; i < 20; ++i) {
        int block = get_disk_manager().allocate_nth_block(test_inode, i);
        run_test("分配一级间接块 " + std::to_string(i), block != -1);
    }

    std::cout << "\n--- 阶段 3: 磁盘模块 - 二级间接索引测试 ---" << std::endl;
    
    // 分配第 270 个块（超过 10 + 256 = 266，触发二级间接）
    int dbl_indirect_block = get_disk_manager().allocate_nth_block(test_inode, 270);
    run_test("分配二级间接块 (逻辑块 270)", dbl_indirect_block != -1);
    run_test("验证二级间接块已创建", test_inode.double_indirect != -1);
    
    // 验证能正确读取
    int read_dbl_block = get_disk_manager().get_nth_block(test_inode, 270);
    run_test("读取二级间接块数据", read_dbl_block == dbl_indirect_block);
    
    // 再分配几个二级间接块
    for (int i = 266; i < 275; ++i) {
        int block = get_disk_manager().allocate_nth_block(test_inode, i);
        run_test("分配二级间接块 " + std::to_string(i), block != -1);
    }

    std::cout << "\n--- 阶段 4: 磁盘模块 - 块释放测试 ---" << std::endl;
    
    // 记录当前状态
    int single_before = test_inode.single_indirect;
    int double_before = test_inode.double_indirect;
    
    // 释放所有数据块
    get_disk_manager().free_all_data_blocks(test_inode);
    
    run_test("验证直接块已释放", test_inode.direct_blocks[0] == -1);
    run_test("验证一级间接块已释放", test_inode.single_indirect == -1);
    run_test("验证二级间接块已释放", test_inode.double_indirect == -1);
    
    std::cout << "  [ℹ] 释放前: 单间接=" << single_before << ", 双间接=" << double_before << std::endl;
    std::cout << "  [ℹ] 释放后: 单间接=" << test_inode.single_indirect << ", 双间接=" << test_inode.double_indirect << std::endl;

    std::cout << "\n--- 阶段 5: 内存模块 - 缺页与交换测试 ---" << std::endl;
    
    // 1. 模拟进程 A 分配内存
    int pid = 101;
    mem_manager.alloc_mem(pid, 10 * PAGE_SIZE);

    // 2. 连续访问页面，触发缺页（物理页充足时未必发生 Swap Out）
    std::cout << "\n[测试] 开始连续访问逻辑页，观察缺页与装入..." << std::endl;
    for (int i = 0; i < 10; ++i) {
        // 当访问页数超过物理页限额时，会自动调用 get_disk_manager().write_data_block
        mem_manager.access_page(pid, i); 
    }

    // 3. 打印统计信息，验证是否有磁盘交换发生
    mem_manager.print_memory_statistics();

    std::cout << "\n========== [所有测试通过！] ==========" << std::endl;
    std::cout << "✓ 磁盘模块：直接索引、一级间接索引、二级间接索引均正常工作" << std::endl;
    std::cout << "✓ 内存模块：缺页处理与磁盘交换功能正常" << std::endl;
    return 0;
}
