#include "disk.h"
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>
#include <algorithm>

static void print_help() {
    std::cout << "\n可用命令：\n"
              << "  help                         显示帮助\n"
              << "  format                       格式化虚拟磁盘\n"
              << "  alloc_inode                  分配一个 inode\n"
              << "  free_inode <inode>           释放 inode\n"
              << "  read_inode <inode>           查看 inode 元数据\n"
              << "  write_inode <inode> <mode> <size> <block>   写入 inode 元数据\n"
              << "  alloc_block                  分配一个数据块\n"
              << "  free_block <block>           释放数据块\n"
              << "  write_block <block> <text>   向数据块写入文本\n"
              << "  read_block <block> [len]     读取数据块内容\n"
              << "  quick_demo                   快速演示：分配/写入/读取/释放\n"
              << "  exit                         退出\n\n";
}

static std::string rest_text(std::istringstream& iss) {
    std::string s; std::getline(iss, s); if (!s.empty() && s[0] == ' ') s.erase(0, 1); return s;
}

int main() {
    std::cout << "=== 虚拟磁盘模块演示 ===\n";
    std::cout << "输入 help 查看命令。本程序仅依赖 disk.cpp。\n";
    DiskManager& dm = get_disk_manager();
    print_help();
    std::string line;
    while (true) {
        std::cout << "磁盘> ";
        if (!std::getline(std::cin, line)) break;
        if (line.empty()) continue;
        std::istringstream iss(line);
        std::string cmd; iss >> cmd;
        if (cmd == "exit" || cmd == "quit") break;
        else if (cmd == "help") print_help();
        else if (cmd == "format") { dm.format_disk(); std::cout << "[成功] 磁盘已格式化\n"; }
        else if (cmd == "alloc_inode") { std::cout << "inode=" << dm.allocate_inode() << "\n"; }
        else if (cmd == "free_inode") { int id; iss >> id; std::cout << (dm.free_inode(id) ? "[成功]\n" : "[失败]\n"); }
        else if (cmd == "read_inode") {
            int id; iss >> id; iNode node;
            if (dm.read_inode(id, node)) {
                std::cout << "inode=" << node.i_num << " 模式=" << node.i_mode << " 大小=" << node.i_size
                          << " 只读=" << node.is_readonly << " 数据块=";
                for (int b : node.direct_blocks) if (b != -1) std::cout << b << ' ';
                std::cout << "\n";
            } else std::cout << "[失败]\n";
        }
        else if (cmd == "write_inode") {
            int id, mode, size, block; iss >> id >> mode >> size >> block;
            iNode node; node.i_num = id; node.i_mode = mode; node.i_size = size; node.direct_blocks[0] = block;
            std::cout << (dm.write_inode(id, node) ? "[成功]\n" : "[失败]\n");
        }
        else if (cmd == "alloc_block") { std::cout << "block=" << dm.allocate_block() << "\n"; }
        else if (cmd == "free_block") { int id; iss >> id; std::cout << (dm.free_block(id) ? "[成功]\n" : "[失败]\n"); }
        else if (cmd == "write_block") {
            int id; iss >> id; std::string text = rest_text(iss);
            char buf[BLOCK_SIZE] = {0}; std::strncpy(buf, text.c_str(), BLOCK_SIZE - 1);
            std::cout << (dm.write_data_block(id, buf) ? "[成功] 已写入数据块\n" : "[失败]\n");
        }
        else if (cmd == "read_block") {
            int id, len = 80; iss >> id; if (!(iss >> len)) len = 80;
            len = std::max(0, std::min(len, BLOCK_SIZE));
            char buf[BLOCK_SIZE + 1] = {0};
            if (dm.read_data_block(id, buf)) { buf[len] = 0; std::cout << buf << "\n"; }
            else std::cout << "[失败]\n";
        }
        else if (cmd == "quick_demo") {
            int inode = dm.allocate_inode(); int block = dm.allocate_block();
            std::cout << "已分配 inode=" << inode << " block=" << block << "\n";
            iNode node; node.i_num = inode; node.i_mode = 1; node.i_size = 12; node.direct_blocks[0] = block;
            dm.write_inode(inode, node);
            char w[BLOCK_SIZE] = {0}; std::strcpy(w, "disk_demo_ok"); dm.write_data_block(block, w);
            iNode r; char rb[BLOCK_SIZE] = {0}; dm.read_inode(inode, r); dm.read_data_block(block, rb);
            std::cout << "读回 inode=" << r.i_num << " 大小=" << r.i_size << " 数据=" << rb << "\n";
            dm.free_block(block); dm.free_inode(inode); std::cout << "已释放 inode 和数据块\n";
        }
        else std::cout << "未知命令，请输入 help 查看帮助。\n";
    }
    std::cout << "已退出。\n";
    return 0;
}
