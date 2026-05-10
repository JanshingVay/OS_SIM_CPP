#include "filesystem.h"
#include <iostream>
#include <cassert>
#include <string>

using namespace std;

// 辅助函数：打印分隔线
void print_separator(const string &title) {
    cout << "\n========================================\n";
    cout << title << "\n";
    cout << "========================================\n";
}

// 测试1: 创建普通文件（类似 touch）
void test_create_file() {
    print_separator("测试1: 创建普通文件 (touch file1.txt)");
    int ino = fs.create_file("file1.txt");
    assert(ino != -1);
    fs.ls();  // 应列出 file1.txt
}

// 测试2: 写入并读取文件（类似 echo + cat）
void test_write_and_read() {
    print_separator("测试2: 写入并读取文件 (echo 'Hello' > file1.txt && cat file1.txt)");
    string data = "Hello, File System!";
    int written = fs.write_file("file1.txt", data, 0);
    assert(written == (int)data.size());

    string content = fs.read_file("file1.txt", 0, -1);
    assert(content == data);
    cout << "读取内容: " << content << endl;
}

// 测试3: 覆盖写入和追加
void test_overwrite_and_append() {
    print_separator("测试3: 覆盖写入和追加");
    fs.write_file("file1.txt", "NEW CONTENT", 0);
    string content = fs.read_file("file1.txt", 0, -1);
    assert(content == "NEW CONTENT");

    fs.write_file("file1.txt", " + APPEND", 12);  // 从偏移12开始追加
    content = fs.read_file("file1.txt", 0, -1);
    assert(content == "NEW CONTENT + APPEND");
    cout << "追加后内容: " << content << endl;
}

// 测试4: 创建目录并切换（类似 mkdir + cd）
void test_create_dir_and_cd() {
    print_separator("测试4: 创建目录并切换 (mkdir mydir && cd mydir)");
    int dir_ino = fs.create_file("mydir", true);
    assert(dir_ino != -1);
    fs.ls();  // 根目录应包含 mydir

    fs.cd("mydir");
    fs.pwd();  // 应显示 /mydir

    // 在子目录中创建文件
    int file_ino = fs.create_file("inside.txt");
    assert(file_ino != -1);
    fs.write_file("inside.txt", "Subdirectory content", 0);
    string sub_content = fs.read_file("inside.txt", 0, -1);
    cout << "子目录文件内容: " << sub_content << endl;

    fs.ls();  // 列出子目录内容
    fs.cd("..");
    fs.pwd();  // 回到根目录
}

// 测试5: 重命名文件（类似 mv old new）
void test_rename() {
    print_separator("测试5: 重命名文件 (mv file1.txt renamed.txt)");
    fs.rename("file1.txt", "renamed.txt");
    fs.ls();  // 应显示 renamed.txt，无 file1.txt
    string content = fs.read_file("renamed.txt", 0, -1);
    assert(!content.empty());
}

// 测试6: 设置只读权限（类似 chmod）
void test_permission() {
    print_separator("测试6: 设置只读权限 (chmod 444 renamed.txt)");
    fs.set_file_permission("renamed.txt", true);
    int ret = fs.write_file("renamed.txt", "Should fail", 0);
    assert(ret == -1);  // 只读文件写入应失败

    string content = fs.read_file("renamed.txt", 0, -1);
    assert(!content.empty());
    cout << "只读状态下仍可读取，内容: " << content << endl;

    fs.set_file_permission("renamed.txt", false);
    ret = fs.write_file("renamed.txt", "Now writable", 0);
    assert(ret == 11);  // "Now writable" 长度为11
    content = fs.read_file("renamed.txt", 0, -1);
    assert(content == "Now writable");
}

// 测试7: 删除文件（类似 rm）
void test_delete_file() {
    print_separator("测试7: 删除文件 (rm renamed.txt)");
    fs.delete_file("renamed.txt");
    fs.ls();  // 应不再显示 renamed.txt
    string content = fs.read_file("renamed.txt", 0, -1);
    assert(content.empty());
}

// 测试8: 删除目录（需先清空，类似 rmdir）
void test_delete_dir() {
    print_separator("测试8: 删除目录 (cd mydir && rm inside.txt && cd .. && rmdir mydir)");
    fs.cd("mydir");
    fs.delete_file("inside.txt");
    fs.cd("..");
    fs.delete_file("mydir");
    fs.ls();  // 根目录应为空（或只剩下其他临时文件）
}

// 测试9: 边界情况测试
void test_edge_cases() {
    print_separator("测试9: 边界情况测试");
    // 创建文件名为空
    int ret = fs.create_file("");
    assert(ret == -1);
    // 写入不存在的文件
    ret = fs.write_file("notexist.txt", "data", 0);
    assert(ret == -1);
    // 读取不存在的文件
    string content = fs.read_file("notexist.txt");
    assert(content.empty());
    // 删除不存在的文件
    bool ok = fs.delete_file("notexist.txt");
    assert(!ok);
    // 重命名不存在的文件
    ok = fs.rename("notexist.txt", "newname.txt");
    assert(!ok);
    // 进入不存在的目录
    ok = fs.cd("notexist");
    assert(!ok);
    cout << "所有边界情况测试通过" << endl;
}

// 测试10: 多块文件写入（超过一个块，测试间接块链）
void test_multiblock_file() {
    print_separator("测试10: 多块文件写入 (写入超过 1KB 数据)");
    fs.create_file("large.txt");
    string large_data(3000, 'A');  // 3KB 数据
    int written = fs.write_file("large.txt", large_data, 0);
    assert(written == 3000);
    string read_data = fs.read_file("large.txt", 0, -1);
    assert(read_data.size() == 3000);
    // 验证部分内容
    assert(read_data[0] == 'A' && read_data[2999] == 'A');
    // 验证文件大小
    iNode inode;
    fs.get_file_info("large.txt", inode);
    assert(inode.i_size == 3000);
    // 清理
    fs.delete_file("large.txt");
    cout << "多块文件测试通过" << endl;
}

int main() {
    cout << "开始文件系统终端模拟测试...\n";
    // 注意：FileSystem 构造函数会自动初始化磁盘和根目录
    test_create_file();
    test_write_and_read();
    test_overwrite_and_append();
    test_create_dir_and_cd();
    test_rename();
    test_permission();
    test_delete_file();
    test_delete_dir();
    test_edge_cases();
    test_multiblock_file();

    print_separator("所有测试通过！文件系统工作正常");
    return 0;
}