#pragma once
#include <iostream>
#include <fstream>
#include <string>
#include <mutex>
#include <vector>
#include <cstring>

// 页面/块大小为 1K
#define BLOCK_SIZE 1024        
#define TOTAL_BLOCKS 10240      // 总容量 10MB
#define DISK_FILE_NAME "vdisk.bin"

// 磁盘内部区域划分约定 (块号索引)
#define SUPER_BLOCK_ID 0        // 第 0 块：超级块
#define INODE_BITMAP_ID 1       // 第 1 块：iNode 位图 
#define DATA_BITMAP_ID 2        // 第 2~3 块：数据块位图 
#define INODE_TABLE_START 4     // 第 4~100 块：iNode 存储区
#define DATA_BLOCK_START 101    // 第 101 块起：纯数据存储区

#define PTRS_PER_BLOCK (BLOCK_SIZE / sizeof(int))  // 每个间接块可存 256 个指针

// ==========================================
// 数据结构：索引节点 (iNode)
// 大小严格校验为 64 字节，一个 1KB 的块刚好存 16 个
//
// 索引结构：
//   direct_blocks[10]          10 个直接块    → 可寻址 10 KB
//   single_indirect             1 个一级间接  → 可寻址 256 个数据块 (256 KB)
//   double_indirect             1 个二级间接  → 可寻址 256*256 个数据块 (64 MB)
// ==========================================
struct iNode {
    int i_num;             // inode 编号 (4字节)
    int i_mode;            // 0: 目录, 1: 普通文件 (4字节)
    int i_size;            // 文件大小 (字节) (4字节)
    int is_readonly;       // 0为读写，1为只读 (4字节)
    int direct_blocks[10]; // 直接数据块指针 (40字节)
    int single_indirect;   // 一级间接块指针 (4字节)
    int double_indirect;   // 二级间接块指针 (4字节)

    iNode() {
        i_num = -1;
        i_mode = 1;
        i_size = 0;
        is_readonly = 0;
        for (int i = 0; i < 10; ++i)
            direct_blocks[i] = -1;
        single_indirect = -1;
        double_indirect = -1;
    }

    void normalize() {
        for (int i = 0; i < 10; ++i)
            if (direct_blocks[i] == 0) direct_blocks[i] = -1;
        if (single_indirect == 0) single_indirect = -1;
        if (double_indirect == 0) double_indirect = -1;
    }

    int max_data_blocks() const {
        return 10 + PTRS_PER_BLOCK + PTRS_PER_BLOCK * PTRS_PER_BLOCK;
    }
};

// 编译期断言：如果 iNode 不是 64 字节，直接拒绝编译！
static_assert(sizeof(iNode) == 64, "FATAL ERROR: iNode size must be exactly 64 bytes!");

// 杜绝硬编码，自动计算每个块的 iNode 容量
#define INODES_PER_BLOCK (BLOCK_SIZE / sizeof(iNode))
#define TOTAL_INODES ((DATA_BLOCK_START - INODE_TABLE_START) * INODES_PER_BLOCK)

// ==========================================
// 卷管理器：封装底层，对外暴露高级对象接口
// ==========================================
class DiskManager {
private:
    std::fstream disk_stream;
    std::mutex disk_mutex;

    bool _write_raw_block(int block_id, const char* buffer);
    bool _read_raw_block(int block_id, char* buffer);
    void _format_disk();

    void _set_bit(char* bitmap, int index, bool value);
    bool _get_bit(const char* bitmap, int index);

    int _alloc_indirect_data_block(int indirect_block_id, int index);
    void _free_single_indirect_chain(int indirect_block_id);
    void _free_double_indirect_chain(int indirect_block_id);

public:
    DiskManager();
    ~DiskManager();

    // ★ 新增：对外提供公开的格式化接口，调用私有的 _format_disk()
    void format_disk() { _format_disk(); }

    int allocate_block();
    bool free_block(int block_id);

    int allocate_inode();
    bool free_inode(int inode_id);

    bool read_inode(int inode_id, iNode& out_inode);
    bool write_inode(int inode_id, const iNode& in_inode);

    bool read_data_block(int block_id, char* buffer);
    bool write_data_block(int block_id, const char* buffer);

    // 在 disk.h 的 public 下添加：
    void sync_disk();

    int get_nth_block(const iNode& node, int n);
    int allocate_nth_block(iNode& node, int n);
    void free_all_data_blocks(iNode& node);
};

// 使用单例模式替代全局变量，解决静态初始化顺序(SIOF)问题
DiskManager& get_disk_manager();