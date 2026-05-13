#include "filesystem.h"
#include <iostream>
#include <cassert>
#include <string>

using namespace std;

// 辅助断言函数，带错误提示
void run_test(const string& test_name, bool condition) {
    if (condition) {
        cout << "  [✔] " << test_name << " 通过" << endl;
    } else {
        cerr << "  [✖] " << test_name << " 失败！" << endl;
        exit(1);
    }
}

// 智能环境清理：只清理测试产生的特定文件，绝不影响磁盘上原有的正常文件
void safe_cleanup() {
    // 确保我们在根目录
    fs.cd(".."); 
    fs.cd("..");

    string test_files[] = {"hello.txt", "os_core.txt", "large_data.bin", "test_dir2",
                           "single_indirect_test.bin", "double_indirect_test.bin"};
    
    for (const auto& fname : test_files) {
        iNode tmp;
        // 如果该测试文件存在，则清理它
        if (fs.get_file_info(fname, tmp)) {
            // 如果上次测试刚好把它设置成了只读就崩溃了，需要先解开权限
            if (tmp.is_readonly) {
                fs.set_file_permission(fname, false);
            }
            fs.delete_file(fname);
        }
    }
}

int main() {
    cout << "\n=========================================" << endl;
    cout << "       OS 课设: 文件系统与磁盘集成测试       " << endl;
    cout << "=========================================\n" << endl;

    cout << "--- 阶段 0: 环境预清理 (清除上次中断产生的垃圾) ---" << endl;
    safe_cleanup();
    cout << "  [✔] 测试环境已就绪" << endl;

    cout << "\n--- 阶段 1: 目录操作与环境测试 ---" << endl;
    fs.pwd();
    fs.ls();
    
    // 创建一个测试目录并进入
    run_test("创建目录 'test_dir2'", fs.create_file("test_dir2", true) != -1);
    run_test("进入目录 'test_dir2'", fs.cd("test_dir2"));
    fs.pwd();

    cout << "\n--- 阶段 2: 基本文件读写与重命名 ---" << endl;
    run_test("创建文件 'hello.txt'", fs.create_file("hello.txt", false) != -1);
    
    string text = "Hello, BUPT OS! 这是一个文件系统与底层磁盘的集成测试。";
    int written = fs.write_file("hello.txt", text);
    run_test("写入文件数据", written == text.size());
    
    string read_back = fs.read_file("hello.txt");
    run_test("读取数据一致性校验", read_back == text);

    run_test("重命名文件 'hello.txt' -> 'os_core.txt'", fs.rename("hello.txt", "os_core.txt"));
    
    // 确认旧文件已不存在
    run_test("验证旧文件名已失效", fs.read_file("hello.txt").empty());

    cout << "\n--- 阶段 3: 权限控制 (is_readonly) 测试 ---" << endl;
    run_test("设置文件为只读", fs.set_file_permission("os_core.txt", true));
    
    cout << "  -> 预期看到拒绝写入/删除的错误信息：" << endl;
    int write_res = fs.write_file("os_core.txt", "尝试破坏数据", 0);
    run_test("拦截只读文件写入", write_res == -1);
    
    bool del_res = fs.delete_file("os_core.txt");
    run_test("拦截只读文件删除", del_res == false);

    // 恢复权限
    run_test("恢复文件可写权限", fs.set_file_permission("os_core.txt", false));

    cout << "\n--- 阶段 4: 大文件跨块分配与截断测试 ---" << endl;
    run_test("创建大文件 'large_data.bin'", fs.create_file("large_data.bin", false) != -1);
    
    string large_text(2500, 'A'); 
    run_test("跨块写入 2500 字节", fs.write_file("large_data.bin", large_text) == 2500);
    
    string read_large = fs.read_file("large_data.bin");
    run_test("跨块读取数据完整性校验", read_large.size() == 2500 && read_large[2499] == 'A');

    // 测试组员写的“覆盖缩短”逻辑 (Truncate)
    string short_text = "Short";
    run_test("覆盖写入短数据并截断", fs.write_file("large_data.bin", short_text) == 5);
    
    iNode check_inode;
    fs.get_file_info("large_data.bin", check_inode);
    run_test("验证底层 iNode size 更新正确", check_inode.i_size == 5);

    cout << "\n--- 阶段 5: 一级间接索引测试 (Single Indirect) ---" << endl;
    run_test("创建单间接测试文件", fs.create_file("single_indirect_test.bin", false) != -1);
    
    // 10个直接块 = 10KB, 超过后使用一级间接
    // 写入 15KB (15个块)，触发一级间接索引
    string single_indirect_data(15 * 1024, 'S');
    for (int i = 0; i < 15 * 1024; ++i) {
        single_indirect_data[i] = 'S' + (i % 26);
    }
    run_test("写入 15KB 数据 (触发一级间接)", 
             fs.write_file("single_indirect_test.bin", single_indirect_data) == 15 * 1024);
    
    string read_single = fs.read_file("single_indirect_test.bin");
    run_test("读取 15KB 数据完整性", read_single == single_indirect_data);
    
    // 验证 inode 结构
    iNode single_inode;
    fs.get_file_info("single_indirect_test.bin", single_inode);
    run_test("验证单间接 inode 结构", 
             single_inode.single_indirect != -1 && single_inode.double_indirect == -1);
    cout << "  [ℹ] 单间接块号: " << single_inode.single_indirect << endl;

    cout << "\n--- 阶段 6: 二级间接索引测试 (Double Indirect) ---" << endl;
    run_test("创建双间接测试文件", fs.create_file("double_indirect_test.bin", false) != -1);
    
    // 10个直接 + 256个一级间接 = 266KB, 超过后使用二级间接
    // 写入 300KB，触发二级间接索引
    const int DOUBLE_TEST_SIZE = 300 * 1024;
    string double_indirect_data(DOUBLE_TEST_SIZE, 'D');
    for (int i = 0; i < DOUBLE_TEST_SIZE; ++i) {
        double_indirect_data[i] = 'D' + (i % 26);
    }
    run_test("写入 300KB 数据 (触发二级间接)", 
             fs.write_file("double_indirect_test.bin", double_indirect_data) == DOUBLE_TEST_SIZE);
    
    string read_double = fs.read_file("double_indirect_test.bin");
    run_test("读取 300KB 数据完整性", read_double == double_indirect_data);
    
    // 验证 inode 结构
    iNode double_inode;
    fs.get_file_info("double_indirect_test.bin", double_inode);
    run_test("验证双间接 inode 结构", 
             double_inode.single_indirect != -1 && double_inode.double_indirect != -1);
    cout << "  [ℹ] 单间接块号: " << double_inode.single_indirect << endl;
    cout << "  [ℹ] 双间接块号: " << double_inode.double_indirect << endl;

    cout << "\n--- 阶段 7: 间接索引块释放测试 ---" << endl;
    run_test("删除单间接测试文件", fs.delete_file("single_indirect_test.bin"));
    run_test("删除双间接测试文件", fs.delete_file("double_indirect_test.bin"));
    
    // 验证文件确实被删除
    iNode check_deleted;
    run_test("验证单间接文件已删除", !fs.get_file_info("single_indirect_test.bin", check_deleted));
    run_test("验证双间接文件已删除", !fs.get_file_info("double_indirect_test.bin", check_deleted));

    cout << "\n--- 阶段 8: 测试结束，安全清理中间产物 ---" << endl;
    run_test("返回上一级目录 (cd ..)", fs.cd(".."));
    safe_cleanup();
    cout << "  [✔] 中间文件已全部无痕清理" << endl;
    
    cout << "\n🎉 所有集成测试均已通过！文件系统与磁盘驱动完美融合！" << endl;
    return 0;
}