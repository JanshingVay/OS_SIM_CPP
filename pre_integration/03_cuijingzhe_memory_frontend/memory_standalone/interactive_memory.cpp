#include "memory/memory.h"
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

static void print_help() {
    std::cout << "\n可用命令：\n"
              << "  help                           显示帮助\n"
              << "  policy <FIFO|LRU|CLOCK>        设置页面置换策略\n"
              << "  alloc <pid> <bytes>            为进程分配逻辑内存\n"
              << "  free <pid>                     释放进程内存\n"
              << "  access <pid> <addr>            访问逻辑地址，例如 0x1000\n"
              << "  page <pid> <page_no>           访问逻辑页号\n"
              << "  translate <pid> <addr>         地址转换\n"
              << "  resize <pid> <delta_bytes>     动态扩容/缩容\n"
              << "  share <pid1> <pid2> <pages>    共享页\n"
              << "  bitmap                         打印物理页框位图\n"
              << "  stat                           打印内存统计信息\n"
              << "  reset                          重置内存统计\n"
              << "  exit                           退出\n\n";
}

static uint32_t parse_u32(const std::string& s) {
    return static_cast<uint32_t>(std::strtoul(s.c_str(), nullptr, 0));
}

int main() {
    std::cout << "=== 内存管理模块演示 ===\n";
    std::cout << "输入 help 查看命令。本程序依赖 memory.cpp + disk.cpp。\n";
    print_help();
    std::string line;
    while (true) {
        std::cout << "内存> ";
        if (!std::getline(std::cin, line)) break;
        if (line.empty()) continue;
        std::istringstream iss(line);
        std::string cmd; iss >> cmd;
        if (cmd == "exit" || cmd == "quit") break;
        else if (cmd == "help") print_help();
        else if (cmd == "policy") {
            std::string p; iss >> p;
            if (p == "FIFO" || p == "fifo") mem_manager.set_replacement_policy(ReplacementPolicy::FIFO);
            else if (p == "LRU" || p == "lru") mem_manager.set_replacement_policy(ReplacementPolicy::LRU);
            else if (p == "CLOCK" || p == "clock") mem_manager.set_replacement_policy(ReplacementPolicy::CLOCK);
            else { std::cout << "[失败] 未知策略\n"; continue; }
            std::cout << "[成功] 已设置策略\n";
        }
        else if (cmd == "alloc") { int pid, bytes; iss >> pid >> bytes; std::cout << (mem_manager.alloc_mem(pid, bytes) ? "[成功]\n" : "[失败]\n"); }
        else if (cmd == "free") { int pid; iss >> pid; std::cout << (mem_manager.free_mem(pid) ? "[成功]\n" : "[失败]\n"); }
        else if (cmd == "access") { int pid; std::string a; iss >> pid >> a; std::cout << (mem_manager.access_addr(pid, parse_u32(a)) ? "[成功]\n" : "[失败]\n"); }
        else if (cmd == "page") { int pid, page; iss >> pid >> page; std::cout << (mem_manager.access_page(pid, page) ? "[成功]\n" : "[失败]\n"); }
        else if (cmd == "translate") {
            int pid; std::string a; iss >> pid >> a; int frame = -1; size_t off = 0;
            if (mem_manager.translate(pid, parse_u32(a), frame, off)) std::cout << "页框=" << frame << " 偏移=" << off << "\n";
            else std::cout << "[失败]\n";
        }
        else if (cmd == "resize") { int pid, delta; iss >> pid >> delta; std::cout << (mem_manager.dynamic_alloc(pid, delta) ? "[成功]\n" : "[失败]\n"); }
        else if (cmd == "share") { int a,b,n; iss >> a >> b >> n; std::cout << (mem_manager.share_mem(a,b,n) ? "[成功]\n" : "[失败]\n"); }
        else if (cmd == "bitmap") {
            auto bm = mem_manager.get_memory_bitmap();
            std::cout << "页框位图: ";
            for (size_t i = 0; i < bm.size(); ++i) std::cout << bm[i] << ((i + 1) % 32 == 0 ? '\n' : ' ');
            if (bm.size() % 32) std::cout << "\n";
        }
        else if (cmd == "stat") mem_manager.print_memory_statistics();
        else if (cmd == "reset") { mem_manager.reset_memory_statistics(); std::cout << "[成功]\n"; }
        else std::cout << "未知命令，请输入 help 查看帮助。\n";
    }
    std::cout << "已退出。\n";
    return 0;
}
