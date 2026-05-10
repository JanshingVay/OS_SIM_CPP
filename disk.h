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

// ==========================================
// 数据结构：索引节点 (iNode)
// 大小严格校验为 64 字节，一个 1KB 的块刚好存 16 个
// ==========================================
struct iNode {
    int i_num;             // inode 编号 (4字节)
    int i_mode;            // 0: 目录, 1: 普通文件 (4字节)
    int i_size;            // 文件大小 (字节) (4字节)
    int is_readonly;       // 0为读写，1为只读 (4字节)
    int direct_blocks[10]; // 直接数据块指针 (40字节)
    char padding[8];       // 填充 (8字节) -> 总计 64 字节

    iNode() {
        i_num = -1;
        i_mode = 1;
        i_size = 0;
        is_readonly = 0;   // 默认可读写
        for (int i = 0; i < 10; ++i)
            direct_blocks[i] = -1;
        memset(padding, 0, sizeof(padding));
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
};

// 使用单例模式替代全局变量，解决静态初始化顺序(SIOF)问题
DiskManager& get_disk_manager();