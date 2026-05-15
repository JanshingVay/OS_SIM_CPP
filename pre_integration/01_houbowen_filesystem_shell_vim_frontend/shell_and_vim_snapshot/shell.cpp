// shell.cpp - 交互式文件系统终端模拟器
#include "filesystem.h"
#include <iostream>
#include <string>
#include <sstream>
#include <vector>
#include <cstring>
#include <algorithm>
#include <cctype>

using namespace std;

// 分割命令行字符串为参数列表
vector<string> split_args(const string &line) {
    vector<string> args;
    stringstream ss(line);
    string arg;
    while (ss >> arg) {
        args.push_back(arg);
    }
    return args;
}

// 去除字符串首尾空格
string trim(const string &str) {
    size_t first = str.find_first_not_of(" \t");
    if (first == string::npos) return "";
    size_t last = str.find_last_not_of(" \t");
    return str.substr(first, last - first + 1);
}

// 解析 echo 命令中的重定向 (支持 > 和 >>)
bool parse_echo_redirect(const string &line, string &content, string &filename, bool &append) {
    size_t redir_pos = line.find('>');
    if (redir_pos == string::npos) return false;
    
    // 检查是否追加
    if (redir_pos + 1 < line.size() && line[redir_pos + 1] == '>') {
        append = true;
        redir_pos++;
    } else {
        append = false;
    }
    
    string left = trim(line.substr(0, redir_pos));
    // 去掉 "echo" 前缀
    if (left.substr(0, 4) == "echo") {
        content = trim(left.substr(4));
    } else {
        return false;
    }
    
    // 去掉末尾可能的多余空格
    string right = trim(line.substr(redir_pos + 1));
    filename = right;
    return !filename.empty();
}

// 执行 touch 命令
void cmd_touch(const string &filename) {
    if (filename.empty()) {
        cout << "用法: touch <文件名>" << endl;
        return;
    }
    int ino = fs.create_file(filename, false);
    if (ino == -1) {
        // 创建失败可能因为文件已存在，此时什么也不做（touch 语义是更新时间戳，这里简化）
        // 但我们的文件系统没有时间戳，所以忽略
    }
}

// 执行 mkdir 命令
void cmd_mkdir(const string &dirname) {
    if (dirname.empty()) {
        cout << "用法: mkdir <目录名>" << endl;
        return;
    }
    fs.create_file(dirname, true);
}

// 执行 cat 命令
void cmd_cat(const string &filename) {
    if (filename.empty()) {
        cout << "用法: cat <文件名>" << endl;
        return;
    }
    string content = fs.read_file(filename, 0, -1);
    if (content.empty()) {
        // 可能是空文件或不存在，read_file 会打印错误，我们不用重复
    } else {
        cout << content << endl;
    }
}

// 执行 ls 命令
void cmd_ls() {
    fs.ls();
}

// 执行 pwd 命令
void cmd_pwd() {
    fs.pwd();
}

// 执行 cd 命令
void cmd_cd(const string &dirname) {
    if (dirname.empty()) {
        cout << "用法: cd <目录名>" << endl;
        return;
    }
    fs.cd(dirname);
}

// 执行 mv 命令
void cmd_mv(const string &oldname, const string &newname) {
    if (oldname.empty() || newname.empty()) {
        cout << "用法: mv <旧名称> <新名称>" << endl;
        return;
    }
    fs.rename(oldname, newname);
}

// 执行 chmod 命令
void cmd_chmod(const string &filename, const string &perm) {
    if (filename.empty() || perm.empty()) {
        cout << "用法: chmod <文件名> r|w" << endl;
        return;
    }
    if (perm == "r") {
        fs.set_file_permission(filename, true);
    } else if (perm == "w") {
        fs.set_file_permission(filename, false);
    } else {
        cout << "权限必须是 r (只读) 或 w (可写)" << endl;
    }
}

// 执行 rm 命令
void cmd_rm(const string &filename) {
    if (filename.empty()) {
        cout << "用法: rm <文件名>" << endl;
        return;
    }
    fs.delete_file(filename);
}

// 执行 rmdir 命令
void cmd_rmdir(const string &dirname) {
    if (dirname.empty()) {
        cout << "用法: rmdir <目录名>" << endl;
        return;
    }
    // 检查是否为目录且非空，我们的 delete_file 会做检查
    fs.delete_file(dirname);
}

// 简单的写入命令：write <文件名> <偏移量> <数据> （用于调试）
void cmd_write(const string &filename, int offset, const string &data) {
    if (filename.empty()) {
        cout << "用法: write <文件名> <偏移量> <字符串>" << endl;
        return;
    }
    fs.write_file(filename, data, offset);
}

// 简单读取命令：read <文件名> [偏移量] [长度]
void cmd_read(const string &filename, int offset, int len) {
    if (filename.empty()) {
        cout << "用法: read <文件名> [偏移量] [长度]" << endl;
        return;
    }
    string content = fs.read_file(filename, offset, len);
    cout << content;
    if (!content.empty() && content.back() != '\n') cout << endl;
}

// 显示帮助
void cmd_help() {
    cout << "可用命令:\n"
         << "  touch <文件名>               - 创建空文件\n"
         << "  mkdir <目录名>               - 创建目录\n"
         << "  echo <内容> > <文件名>       - 覆盖写入文件\n"
         << "  echo <内容> >> <文件名>      - 追加写入文件\n"
         << "  cat <文件名>                 - 显示文件内容\n"
         << "  ls                           - 列出当前目录\n"
         << "  pwd                          - 显示当前路径\n"
         << "  cd <目录名>                  - 切换目录\n"
         << "  mv <旧名> <新名>             - 重命名文件/目录\n"
         << "  chmod <文件名> r|w           - 设置只读/可写\n"
         << "  rm <文件名>                  - 删除文件\n"
         << "  rmdir <目录名>               - 删除空目录\n"
         << "  write <文件名> <偏移> <数据> - 写入数据到指定偏移\n"
         << "  read <文件名> [偏移] [长度]  - 读取数据\n"
         << "  help                         - 显示本帮助\n"
         << "  exit                         - 退出文件系统终端\n";
}

int main() {
    cout << "==================== 文件系统终端模拟器 ====================\n";
    cout << "输入 help 查看命令列表，exit 退出。\n\n";
    
    string line;
    while (true) {
        cout << "$ ";
        getline(cin, line);
        if (line.empty()) continue;
        
        // 尝试解析 echo 重定向命令
        string content, filename;
        bool append;
        if (parse_echo_redirect(line, content, filename, append)) {
            if (append) {
                // 追加：读取原文件大小，然后写入到末尾
                iNode inode;
                if (fs.get_file_info(filename, inode)) {
                    int offset = inode.i_size;
                    fs.write_file(filename, content, offset);
                } else {
                    // 文件不存在，直接创建并写入
                    if (fs.create_file(filename, false) != -1) {
                        fs.write_file(filename, content, 0);
                    }
                }
            } else {
                // 覆盖：从头写入
                // 先清空文件（写入空内容到偏移0，并设置大小为0）
                fs.write_file(filename, content, 0);
                // 注意：write_file 如果新内容长度小于原文件，会截断文件
            }
            continue;
        }
        
        // 普通命令解析
        vector<string> args = split_args(line);
        if (args.empty()) continue;
        
        string cmd = args[0];
        if (cmd == "exit" || cmd == "quit") {
            cout << "退出文件系统。再见！" << endl;
            break;
        } else if (cmd == "help") {
            cmd_help();
        } else if (cmd == "touch") {
            cmd_touch(args.size() > 1 ? args[1] : "");
        } else if (cmd == "mkdir") {
            cmd_mkdir(args.size() > 1 ? args[1] : "");
        } else if (cmd == "cat") {
            cmd_cat(args.size() > 1 ? args[1] : "");
        } else if (cmd == "ls") {
            cmd_ls();
        } else if (cmd == "pwd") {
            cmd_pwd();
        } else if (cmd == "cd") {
            cmd_cd(args.size() > 1 ? args[1] : "");
        } else if (cmd == "mv") {
            cmd_mv(args.size() > 1 ? args[1] : "", args.size() > 2 ? args[2] : "");
        } else if (cmd == "chmod") {
            cmd_chmod(args.size() > 1 ? args[1] : "", args.size() > 2 ? args[2] : "");
        } else if (cmd == "rm") {
            cmd_rm(args.size() > 1 ? args[1] : "");
        } else if (cmd == "rmdir") {
            cmd_rmdir(args.size() > 1 ? args[1] : "");
        } else if (cmd == "write") {
            if (args.size() < 4) {
                cout << "用法: write <文件名> <偏移量> <数据>" << endl;
            } else {
                int offset = stoi(args[2]);
                // 将剩余参数用空格连接作为数据（允许带空格）
                string data;
                for (size_t i = 3; i < args.size(); ++i) {
                    if (i > 3) data += " ";
                    data += args[i];
                }
                cmd_write(args[1], offset, data);
            }
        } else if (cmd == "read") {
            if (args.size() < 2) {
                cout << "用法: read <文件名> [偏移量] [长度]" << endl;
            } else {
                int offset = (args.size() > 2) ? stoi(args[2]) : 0;
                int len = (args.size() > 3) ? stoi(args[3]) : -1;
                cmd_read(args[1], offset, len);
            }
        } else {
            cout << "未知命令: " << cmd << "，输入 help 查看可用命令。" << endl;
        }
    }
    return 0;
}