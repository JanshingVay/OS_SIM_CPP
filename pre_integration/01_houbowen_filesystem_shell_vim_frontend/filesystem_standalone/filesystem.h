#pragma once
#include <string>
#include "disk.h"

// 声明全局当前目录inode（需在cpp中定义）
extern int current_dir_inode;
// 声明全局当前路径字符串（用于pwd显示）
extern std::string current_path;

class FileSystem
{
private:
    struct DirEntry
    {
        char filename[32]; // 文件名（32字节，含结束符）
        int inode_id;      // 对应inode编号
        bool is_valid;     // 目录项是否有效

        DirEntry();
        DirEntry(const std::string &name, int ino);
    };

    static const int ROOT_INODE_ID = 0; // 根目录固定inode编号

    // 私有辅助函数
    void _init_fs();
    int _find_file_in_dir(int dir_inode_id, const std::string &filename);
    bool _add_file_to_dir(int dir_inode_id, const std::string &filename, int file_inode_id);
    bool _remove_file_from_dir(int dir_inode_id, const std::string &filename);
    void _free_file_blocks(int inode_id);
    bool _is_dir_empty(int dir_inode_id);

public:
    FileSystem();
    ~FileSystem() = default;

    // 核心功能
    void format(); // ★ 新增：显式格式化文件系统
    int create_file(const std::string &filename, bool is_dir = false);
    bool delete_file(const std::string &filename);
    int write_file(const std::string &filename, const std::string &data, int offset = 0);
    std::string read_file(const std::string &filename, int offset = 0, int len = -1);
    bool set_file_permission(const std::string &filename, bool readonly);
    bool get_file_info(const std::string &filename, iNode &out_inode);

    // 目录/路径操作
    void ls();                           // 列出当前目录内容
    void pwd();                          // 打印当前路径
    bool cd(const std::string &dirname); // 切换目录（支持..和子目录）
    bool rename(const std::string &oldname, const std::string &newname);
    void tree(int dir_inode_id = -1, int depth = 0);
};

// 全局文件系统实例
extern FileSystem fs;
