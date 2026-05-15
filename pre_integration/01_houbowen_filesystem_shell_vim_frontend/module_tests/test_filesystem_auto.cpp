#include "filesystem.h"
#include <iostream>
#include <string>

static int pass_count = 0;
static int fail_count = 0;

void check(bool condition, const std::string& name) {
    if (condition) {
        ++pass_count;
        std::cout << "[PASS] " << name << "\n";
    } else {
        ++fail_count;
        std::cout << "[FAIL] " << name << "\n";
    }
}

int main() {
    std::cout << "=== 侯博文：文件系统模块自动测试 ===\n";
    fs.format();

    check(fs.create_file("demo", true) >= 0, "创建目录 demo");
    check(fs.cd("demo"), "进入目录 demo");
    check(current_path == "/demo", "当前路径更新为 /demo");

    check(fs.create_file("note.txt", false) >= 0, "创建普通文件 note.txt");
    check(fs.write_file("note.txt", "hello", 0) == 5, "普通写入 hello");
    check(fs.read_file("note.txt") == "hello", "读取完整文件内容");
    check(fs.read_file("note.txt", 1, 3) == "ell", "按偏移读取子串");

    check(fs.write_file("note.txt", "YY", 2) == 2, "偏移写入 write_at");
    check(fs.read_file("note.txt") == "heYYo", "偏移写入后保留原文件尾部");

    iNode info;
    check(fs.get_file_info("note.txt", info), "读取 inode 元数据");
    check(info.i_mode == 1 && info.i_size == 5, "inode 类型与大小正确");
    check(fs.set_file_permission("note.txt", true), "设置只读权限");
    check(fs.write_file("note.txt", "X", 0) == -1, "只读文件拒绝写入");
    check(fs.set_file_permission("note.txt", false), "恢复可写权限");
    check(fs.rename("note.txt", "report.txt"), "重命名文件");
    check(fs.read_file("report.txt") == "heYYo", "重命名后内容保持不变");
    check(fs.delete_file("report.txt"), "删除文件");
    check(fs.cd(".."), "返回上级目录");
    check(fs.delete_file("demo"), "删除空目录 demo");

    std::cout << "文件系统自动测试完成：" << pass_count << " PASS / " << fail_count << " FAIL\n";
    return fail_count == 0 ? 0 : 1;
}
