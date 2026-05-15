#include "filesystem.h"
#include <iostream>
#include <sstream>
#include <string>

static void print_help() {
    std::cout << "\n可用命令：\n"
              << "  help                              显示帮助\n"
              << "  format                            格式化虚拟文件系统\n"
              << "  pwd | ls | tree                   显示当前路径 / 列表 / 树形目录\n"
              << "  mkdir <name>                      创建目录\n"
              << "  touch <name>                      创建文件\n"
              << "  cd <dir|..>                       切换目录\n"
              << "  write <file> <text>               从偏移 0 覆盖写入\n"
              << "  write_at <file> <offset> <text>   从指定偏移写入\n"
              << "  cat <file>                        读取整个文件\n"
              << "  read <file> [offset] [len]        读取文件片段\n"
              << "  chmod <file> <ro|rw>              修改只读属性\n"
              << "  stat <file>                       查看 inode 信息\n"
              << "  mv <old> <new>                    重命名文件/目录\n"
              << "  rm <name>                         删除文件或空目录\n"
              << "  exit                              退出\n\n";
}

static std::string trim_one_space(std::string s) {
    if (!s.empty() && s[0] == ' ') s.erase(0, 1);
    return s;
}

int main() {
    std::cout << "=== 文件系统模块演示 ===\n";
    std::cout << "输入 help 查看命令。本程序仅依赖 filesystem.cpp + disk.cpp。\n";
    print_help();
    std::string line;
    while (true) {
        std::cout << "文件系统> ";
        if (!std::getline(std::cin, line)) break;
        if (line.empty()) continue;
        std::istringstream iss(line);
        std::string cmd;
        iss >> cmd;
        if (cmd == "exit" || cmd == "quit") break;
        else if (cmd == "help") print_help();
        else if (cmd == "format") { fs.format(); std::cout << "[成功] 已格式化\n"; }
        else if (cmd == "pwd") fs.pwd();
        else if (cmd == "ls") fs.ls();
        else if (cmd == "tree") fs.tree();
        else if (cmd == "mkdir") { std::string name; iss >> name; std::cout << (fs.create_file(name, true) >= 0 ? "[成功]\n" : "[失败]\n"); }
        else if (cmd == "touch") { std::string name; iss >> name; std::cout << (fs.create_file(name, false) >= 0 ? "[成功]\n" : "[失败]\n"); }
        else if (cmd == "cd") { std::string name; iss >> name; std::cout << (fs.cd(name) ? "[成功]\n" : "[失败]\n"); }
        else if (cmd == "write") {
            std::string name, text; iss >> name; std::getline(iss, text); text = trim_one_space(text);
            int n = fs.write_file(name, text, 0);
            std::cout << (n >= 0 ? "[成功] 写入字节数=" : "[失败] 返回值=") << n << "\n";
        }
        else if (cmd == "write_at") {
            std::string name, text; int off = 0; iss >> name >> off; std::getline(iss, text); text = trim_one_space(text);
            int n = fs.write_file(name, text, off);
            std::cout << (n >= 0 ? "[成功] 写入字节数=" : "[失败] 返回值=") << n << "\n";
        }
        else if (cmd == "cat") { std::string name; iss >> name; std::cout << fs.read_file(name) << "\n"; }
        else if (cmd == "read") {
            std::string name; int off = 0, len = -1; iss >> name; if (iss >> off) { if (!(iss >> len)) len = -1; }
            std::cout << fs.read_file(name, off, len) << "\n";
        }
        else if (cmd == "chmod") {
            std::string name, mode; iss >> name >> mode;
            bool ro = (mode == "ro" || mode == "readonly");
            std::cout << (fs.set_file_permission(name, ro) ? "[成功]\n" : "[失败]\n");
        }
        else if (cmd == "stat") {
            std::string name; iss >> name; iNode info;
            if (fs.get_file_info(name, info)) {
                std::cout << "inode=" << info.i_num << " 类型=" << (info.i_mode == 0 ? "目录" : "文件")
                          << " 大小=" << info.i_size << " 只读=" << info.is_readonly << " 数据块=";
                for (int b : info.direct_blocks) if (b != -1) std::cout << b << ' ';
                std::cout << "\n";
            } else std::cout << "[失败] 未找到\n";
        }
        else if (cmd == "mv") { std::string a,b; iss >> a >> b; std::cout << (fs.rename(a,b) ? "[成功]\n" : "[失败]\n"); }
        else if (cmd == "rm") { std::string name; iss >> name; std::cout << (fs.delete_file(name) ? "[成功]\n" : "[失败]\n"); }
        else std::cout << "未知命令，请输入 help 查看帮助。\n";
    }
    std::cout << "已退出。\n";
    return 0;
}
