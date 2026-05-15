#include "filesystem.h"
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <sstream>
#include <algorithm>

FileSystem fs;
int current_dir_inode = 0;
std::string current_path = "/";

// ==================== DirEntry 实现 ====================
FileSystem::DirEntry::DirEntry()
    : inode_id(-1), is_valid(false)
{
    memset(filename, 0, sizeof(filename));
}

FileSystem::DirEntry::DirEntry(const std::string &name, int ino)
    : inode_id(ino), is_valid(true)
{
    strncpy(filename, name.c_str(), sizeof(filename) - 1);
    filename[sizeof(filename) - 1] = '\0';
}

// ==================== 构造函数 & 初始化 ====================
FileSystem::FileSystem()
{
    try
    {
        _init_fs();
    }
    catch (const std::runtime_error &e)
    {
        std::cerr << "[文件系统] 初始化失败: " << e.what() << "\n";
        exit(1);
    }
    catch (...)
    {
        std::cerr << "[文件系统] 初始化失败\n";
        exit(1);
    }
}

// ★ 新增：格式化整个文件系统并重建根目录
void FileSystem::format()
{
    std::cout << "[文件系统] 开始格式化虚拟磁盘...\n";
    get_disk_manager().format_disk(); // 清空底层磁盘的 super block 和 bitmap
    current_dir_inode = ROOT_INODE_ID;
    current_path = "/";
    _init_fs(); // 重新创建根目录
    std::cout << "[文件系统] 格式化完成，已就绪。\n";
}

void FileSystem::_init_fs()
{
    iNode root_inode;
    bool root_exists = get_disk_manager().read_inode(ROOT_INODE_ID, root_inode);

    bool valid_root = root_exists &&
                      root_inode.i_num == ROOT_INODE_ID &&
                      root_inode.i_mode == 0 &&
                      root_inode.direct_blocks[0] >= DATA_BLOCK_START &&
                      root_inode.direct_blocks[0] < TOTAL_BLOCKS &&
                      root_inode.i_size >= BLOCK_SIZE;

    if (!valid_root)
    {
        std::cout << "[文件系统] 根目录无效或未格式化，正在创建...\n";

        int root_ino = get_disk_manager().allocate_inode();
        if (root_ino != ROOT_INODE_ID)
        {
            if (root_ino != -1)
                get_disk_manager().free_inode(root_ino);
            throw std::runtime_error("根目录inode分配失败，磁盘可能已损坏");
        }

        root_inode.i_num = ROOT_INODE_ID;
        root_inode.i_mode = 0;
        root_inode.i_size = 0;
        root_inode.is_readonly = 0;
        memset(root_inode.direct_blocks, -1, sizeof(root_inode.direct_blocks));

        int b = get_disk_manager().allocate_block();
        if (b == -1)
        {
            get_disk_manager().free_inode(ROOT_INODE_ID);
            throw std::runtime_error("根目录数据块分配失败");
        }

        root_inode.direct_blocks[0] = b;
        root_inode.i_size = BLOCK_SIZE;

        if (!get_disk_manager().write_inode(ROOT_INODE_ID, root_inode))
        {
            get_disk_manager().free_block(b);
            get_disk_manager().free_inode(ROOT_INODE_ID);
            throw std::runtime_error("根目录inode写入失败");
        }

        char buf[BLOCK_SIZE] = {0};
        if (!get_disk_manager().write_data_block(b, buf))
        {
            throw std::runtime_error("根目录数据块初始化失败");
        }

        std::cout << "[文件系统] 根目录创建成功\n";
    }
    else
    {
        std::cout << "[文件系统] 根目录有效，加载成功\n";
    }
}

// ==================== 私有辅助函数 ====================
int FileSystem::_find_file_in_dir(int dir_inode_id, const std::string &filename)
{
    iNode dir_inode;
    if (!get_disk_manager().read_inode(dir_inode_id, dir_inode) || dir_inode.i_mode != 0)
        return -1;

    for (int i = 0; i < 10 && dir_inode.direct_blocks[i] != -1; ++i)
    {
        char buf[BLOCK_SIZE];
        if (!get_disk_manager().read_data_block(dir_inode.direct_blocks[i], buf))
            continue;

        DirEntry *entries = (DirEntry *)buf;
        int entry_count = BLOCK_SIZE / sizeof(DirEntry);

        for (int j = 0; j < entry_count; ++j)
        {
            if (entries[j].is_valid && filename == entries[j].filename)
                return entries[j].inode_id;
        }
    }
    return -1;
}

bool FileSystem::_add_file_to_dir(int dir_inode_id, const std::string &filename, int file_inode_id)
{
    iNode dir_inode;
    if (!get_disk_manager().read_inode(dir_inode_id, dir_inode) || dir_inode.i_mode != 0)
        return false;

    for (int i = 0; i < 10; ++i)
    {
        if (dir_inode.direct_blocks[i] == -1)
        {
            int new_block = get_disk_manager().allocate_block();
            if (new_block == -1)
                return false;

            dir_inode.direct_blocks[i] = new_block;
            dir_inode.i_size += BLOCK_SIZE;
            if (!get_disk_manager().write_inode(dir_inode_id, dir_inode))
            {
                get_disk_manager().free_block(new_block);
                return false;
            }

            char buf[BLOCK_SIZE] = {0};
            if (!get_disk_manager().write_data_block(new_block, buf))
            {
                get_disk_manager().free_block(new_block);
                return false;
            }
        }

        char buf[BLOCK_SIZE];
        if (!get_disk_manager().read_data_block(dir_inode.direct_blocks[i], buf))
            continue;

        DirEntry *entries = (DirEntry *)buf;
        int entry_count = BLOCK_SIZE / sizeof(DirEntry);

        for (int j = 0; j < entry_count; ++j)
        {
            if (!entries[j].is_valid)
            {
                entries[j] = DirEntry(filename, file_inode_id);
                return get_disk_manager().write_data_block(dir_inode.direct_blocks[i], buf);
            }
        }
    }
    std::cerr << "[文件系统] 目录项已满，无法添加新文件\n";
    return false;
}

bool FileSystem::_remove_file_from_dir(int dir_inode_id, const std::string &filename)
{
    iNode dir_inode;
    if (!get_disk_manager().read_inode(dir_inode_id, dir_inode) || dir_inode.i_mode != 0)
        return false;

    for (int i = 0; i < 10 && dir_inode.direct_blocks[i] != -1; ++i)
    {
        char buf[BLOCK_SIZE];
        if (!get_disk_manager().read_data_block(dir_inode.direct_blocks[i], buf))
            continue;

        DirEntry *entries = (DirEntry *)buf;
        int entry_count = BLOCK_SIZE / sizeof(DirEntry);

        for (int j = 0; j < entry_count; ++j)
        {
            if (entries[j].is_valid && filename == entries[j].filename)
            {
                entries[j].is_valid = false;
                return get_disk_manager().write_data_block(dir_inode.direct_blocks[i], buf);
            }
        }
    }
    return false;
}

void FileSystem::_free_file_blocks(int inode_id)
{
    iNode file_inode;
    if (!get_disk_manager().read_inode(inode_id, file_inode))
        return;

    get_disk_manager().free_all_data_blocks(file_inode);
    get_disk_manager().write_inode(inode_id, file_inode);
}

bool FileSystem::_is_dir_empty(int dir_inode_id)
{
    iNode dir_inode;
    if (!get_disk_manager().read_inode(dir_inode_id, dir_inode) || dir_inode.i_mode != 0)
        return false;

    for (int i = 0; i < 10 && dir_inode.direct_blocks[i] != -1; ++i)
    {
        char buf[BLOCK_SIZE];
        if (!get_disk_manager().read_data_block(dir_inode.direct_blocks[i], buf))
            continue;

        DirEntry *entries = (DirEntry *)buf;
        int entry_count = BLOCK_SIZE / sizeof(DirEntry);

        for (int j = 0; j < entry_count; ++j)
        {
            if (entries[j].is_valid)
                return false;
        }
    }
    return true;
}

// ==================== 公共接口 ====================
int FileSystem::create_file(const std::string &filename, bool is_dir)
{
    if (filename.empty())
    {
        std::cerr << "[文件系统] 错误：文件名不能为空\n";
        return -1;
    }

    int exist_ino = _find_file_in_dir(current_dir_inode, filename);
    if (exist_ino != -1)
    {
        std::cerr << "[文件系统] 错误：" << filename << " 已存在\n";
        return -1;
    }

    int new_ino = get_disk_manager().allocate_inode();
    if (new_ino == -1)
    {
        std::cerr << "[文件系统] 错误：inode分配失败（磁盘满）\n";
        return -1;
    }

    iNode new_inode;
    new_inode.i_num = new_ino;
    new_inode.i_mode = is_dir ? 0 : 1;
    new_inode.i_size = 0;
    new_inode.is_readonly = 0;
    memset(new_inode.direct_blocks, -1, sizeof(new_inode.direct_blocks));

    if (is_dir)
    {
        int dir_block = get_disk_manager().allocate_block();
        if (dir_block == -1)
        {
            std::cerr << "[文件系统] 错误：目录数据块分配失败\n";
            get_disk_manager().free_inode(new_ino);
            return -1;
        }
        new_inode.direct_blocks[0] = dir_block;
        new_inode.i_size = BLOCK_SIZE;

        char buf[BLOCK_SIZE] = {0};
        if (!get_disk_manager().write_data_block(dir_block, buf))
        {
            get_disk_manager().free_block(dir_block);
            get_disk_manager().free_inode(new_ino);
            return -1;
        }
    }

    if (!get_disk_manager().write_inode(new_ino, new_inode))
    {
        if (is_dir && new_inode.direct_blocks[0] != -1)
            get_disk_manager().free_block(new_inode.direct_blocks[0]);
        get_disk_manager().free_inode(new_ino);
        return -1;
    }

    if (!_add_file_to_dir(current_dir_inode, filename, new_ino))
    {
        _free_file_blocks(new_ino);
        get_disk_manager().free_inode(new_ino);
        return -1;
    }

    std::cout << "[文件系统] 成功创建：" << (is_dir ? "目录" : "文件") << " " << filename << "\n";
    return new_ino;
}

bool FileSystem::delete_file(const std::string &filename)
{
    int file_ino = _find_file_in_dir(current_dir_inode, filename);
    if (file_ino == -1)
    {
        std::cerr << "[文件系统] 错误：" << filename << " 不存在\n";
        return false;
    }

    iNode file_inode;
    if (!get_disk_manager().read_inode(file_ino, file_inode))
    {
        std::cerr << "[文件系统] 错误：读取inode失败\n";
        return false;
    }

    if (file_inode.is_readonly)
    {
        std::cerr << "[文件系统] 错误：" << filename << " 是只读的，无法删除\n";
        return false;
    }

    if (file_inode.i_mode == 0)
    {
        if (!_is_dir_empty(file_ino))
        {
            std::cerr << "[文件系统] 错误：目录 " << filename << " 非空，无法删除\n";
            return false;
        }
    }

    _free_file_blocks(file_ino);
    _remove_file_from_dir(current_dir_inode, filename);
    get_disk_manager().free_inode(file_ino);

    std::cout << "[文件系统] 成功删除：" << filename << "\n";
    return true;
}

int FileSystem::write_file(const std::string &filename, const std::string &data, int offset)
{
    if (offset < 0)
    {
        std::cerr << "[文件系统] 错误：偏移量不能为负数\n";
        return -1;
    }

    int file_ino = _find_file_in_dir(current_dir_inode, filename);
    if (file_ino == -1)
    {
        std::cerr << "[文件系统] 错误：文件 " << filename << " 不存在" << std::endl;
        return -1;
    }

    iNode file_inode;
    if (!get_disk_manager().read_inode(file_ino, file_inode))
    {
        return -1;
    }

    if (file_inode.i_mode != 1)
    {
        std::cerr << "[文件系统] 错误：" << filename << " 不是普通文件，无法写入\n";
        return -1;
    }

    if (file_inode.is_readonly)
    {
        std::cerr << "[文件系统] 错误：" << filename << " 是只读的，无法写入\n";
        return -1;
    }

    int new_size = offset + data.size();
    int old_size = file_inode.i_size;

    if (new_size < old_size)
    {
        int old_blocks = (old_size + BLOCK_SIZE - 1) / BLOCK_SIZE;
        int new_blocks = (new_size + BLOCK_SIZE - 1) / BLOCK_SIZE;

        for (int i = new_blocks; i < old_blocks; ++i)
        {
            int block_id = get_disk_manager().get_nth_block(file_inode, i);
            if (block_id != -1)
            {
                get_disk_manager().free_block(block_id);
            }
        }

        if (new_blocks > 0)
        {
            int last_block_id = get_disk_manager().get_nth_block(file_inode, new_blocks - 1);
            if (last_block_id != -1)
            {
                int bytes_to_keep = new_size % BLOCK_SIZE;
                if (bytes_to_keep > 0)
                {
                    char last_buf[BLOCK_SIZE];
                    get_disk_manager().read_data_block(last_block_id, last_buf);
                    memset(last_buf + bytes_to_keep, 0, BLOCK_SIZE - bytes_to_keep);
                    get_disk_manager().write_data_block(last_block_id, last_buf);
                }
            }
        }
    }

    int data_len = data.size();
    int written = 0;
    int block_idx = offset / BLOCK_SIZE;
    int block_off = offset % BLOCK_SIZE;

    while (written < data_len)
    {
        int phys_block = get_disk_manager().allocate_nth_block(file_inode, block_idx);
        if (phys_block == -1)
        {
            std::cerr << "[文件系统] 错误：数据块分配失败（磁盘满）\n";
            break;
        }

        char block_buf[BLOCK_SIZE];
        if (!get_disk_manager().read_data_block(phys_block, block_buf))
        {
            std::cerr << "[文件系统] 错误：读取数据块失败\n";
            break;
        }

        int write_len = std::min(data_len - written, BLOCK_SIZE - block_off);
        memcpy(block_buf + block_off, data.c_str() + written, write_len);

        if (!get_disk_manager().write_data_block(phys_block, block_buf))
        {
            std::cerr << "[文件系统] 错误：写入数据块失败\n";
            break;
        }

        written += write_len;
        block_off = 0;
        ++block_idx;
    }

    if (written > 0)
    {
        if (offset == 0)
            file_inode.i_size = offset + written;              // 普通 write：覆盖并截断
        else
            file_inode.i_size = std::max(old_size, offset + written); // write_at：保留尾部
        get_disk_manager().write_inode(file_ino, file_inode);
        std::cout << "[文件系统] 成功写入 " << written << " 字节到 " << filename << "\n";
    }

    return written > 0 ? written : -1;
}

std::string FileSystem::read_file(const std::string &filename, int offset, int len)
{
    if (offset < 0 || len < -1)
    {
        std::cerr << "[文件系统] 错误：参数不合法\n";
        return "";
    }

    int file_ino = _find_file_in_dir(current_dir_inode, filename);
    if (file_ino == -1)
    {
        std::cerr << "[文件系统] 错误：" << filename << " 不存在\n";
        return "";
    }

    iNode file_inode;
    if (!get_disk_manager().read_inode(file_ino, file_inode))
    {
        std::cerr << "[文件系统] 错误：读取inode失败\n";
        return "";
    }

    if (file_inode.i_mode != 1)
    {
        std::cerr << "[文件系统] 错误：" << filename << " 不是普通文件，无法读取\n";
        return "";
    }

    if (len == -1)
        len = file_inode.i_size - offset;

    if (offset >= file_inode.i_size || len <= 0)
        return "";

    len = std::min(len, file_inode.i_size - offset);

    std::string result;
    result.reserve(len);

    int read = 0;
    int block_idx = offset / BLOCK_SIZE;
    int block_off = offset % BLOCK_SIZE;

    while (read < len)
    {
        int phys_block = get_disk_manager().get_nth_block(file_inode, block_idx);
        if (phys_block == -1)
            break;

        char block_buf[BLOCK_SIZE];
        if (!get_disk_manager().read_data_block(phys_block, block_buf))
        {
            std::cerr << "[文件系统] 错误：读取数据块失败\n";
            break;
        }

        int read_len = std::min(len - read, BLOCK_SIZE - block_off);
        result.append(block_buf + block_off, read_len);
        read += read_len;
        ++block_idx;
        block_off = 0;
    }

    return result;
}

bool FileSystem::set_file_permission(const std::string &filename, bool readonly)
{
    int file_ino = _find_file_in_dir(current_dir_inode, filename);
    if (file_ino == -1)
    {
        std::cerr << "[文件系统] 错误：" << filename << " 不存在\n";
        return false;
    }

    iNode file_inode;
    if (!get_disk_manager().read_inode(file_ino, file_inode))
    {
        std::cerr << "[文件系统] 错误：读取inode失败\n";
        return false;
    }

    file_inode.is_readonly = readonly ? 1 : 0;
    get_disk_manager().write_inode(file_ino, file_inode);

    std::cout << "[文件系统] 成功设置 " << filename << " 为 " << (readonly ? "只读" : "可写") << "\n";
    return true;
}

bool FileSystem::get_file_info(const std::string &filename, iNode &out_inode)
{
    int file_ino = _find_file_in_dir(current_dir_inode, filename);
    if (file_ino == -1)
        return false;

    return get_disk_manager().read_inode(file_ino, out_inode);
}

void FileSystem::ls()
{
    iNode dir_inode;
    if (!get_disk_manager().read_inode(current_dir_inode, dir_inode) || dir_inode.i_mode != 0)
    {
        std::cerr << "[文件系统] 错误：无法读取当前目录\n";
        return;
    }

    std::cout << "\n[当前目录内容] " << current_path << "\n";
    std::cout << "-----------------------------\n";

    bool has_entry = false;
    for (int i = 0; i < 10 && dir_inode.direct_blocks[i] != -1; ++i)
    {
        char buf[BLOCK_SIZE];
        if (!get_disk_manager().read_data_block(dir_inode.direct_blocks[i], buf))
            continue;

        DirEntry *entries = (DirEntry *)buf;
        int entry_count = BLOCK_SIZE / sizeof(DirEntry);

        for (int j = 0; j < entry_count; ++j)
        {
            if (entries[j].is_valid)
            {
                has_entry = true;
                iNode entry_inode;
                if (get_disk_manager().read_inode(entries[j].inode_id, entry_inode))
                {
                    std::cout << (entry_inode.i_mode == 0 ? "[目录]" : "[文件]") << " ";
                    std::cout << entries[j].filename << " ";
                    std::cout << "(大小: " << entry_inode.i_size << " 字节, ";
                    std::cout << (entry_inode.is_readonly ? "只读)" : "可写)") << "\n";
                }
                else
                {
                    std::cout << "[未知] " << entries[j].filename << " (inode读取失败)\n";
                }
            }
        }
    }

    if (!has_entry)
        std::cout << "（空目录）\n";

    std::cout << "-----------------------------\n\n";
}

void FileSystem::pwd()
{
    std::cout << "当前路径：" << current_path << "\n";
}

bool FileSystem::cd(const std::string &dirname)
{
    if (dirname == "..")
    {
        current_dir_inode = 0;
        current_path = "/";
        std::cout << "[文件系统] 已返回根目录\n";
        return true;
    }

    int dir_ino = _find_file_in_dir(current_dir_inode, dirname);
    if (dir_ino == -1)
    {
        std::cerr << "[文件系统] 错误：目录 " << dirname << " 不存在\n";
        return false;
    }

    iNode dir_inode;
    if (!get_disk_manager().read_inode(dir_ino, dir_inode) || dir_inode.i_mode != 0)
    {
        std::cerr << "[文件系统] 错误：" << dirname << " 不是目录\n";
        return false;
    }

    current_dir_inode = dir_ino;
    if (current_path == "/")
        current_path += dirname;
    else
        current_path += "/" + dirname;

    std::cout << "[文件系统] 已进入目录：" << current_path << "\n";
    return true;
}

bool FileSystem::rename(const std::string &oldname, const std::string &newname)
{
    if (_find_file_in_dir(current_dir_inode, newname) != -1)
    {
        std::cerr << "[文件系统] 错误：新名称 " << newname << " 已存在\n";
        return false;
    }

    int file_ino = _find_file_in_dir(current_dir_inode, oldname);
    if (file_ino == -1)
    {
        std::cerr << "[文件系统] 错误：原文件 " << oldname << " 不存在\n";
        return false;
    }

    iNode dir_inode;
    if (!get_disk_manager().read_inode(current_dir_inode, dir_inode) || dir_inode.i_mode != 0)
        return false;

    for (int i = 0; i < 10 && dir_inode.direct_blocks[i] != -1; ++i)
    {
        char buf[BLOCK_SIZE];
        if (!get_disk_manager().read_data_block(dir_inode.direct_blocks[i], buf))
            continue;

        DirEntry *entries = (DirEntry *)buf;
        int entry_count = BLOCK_SIZE / sizeof(DirEntry);

        for (int j = 0; j < entry_count; ++j)
        {
            if (entries[j].is_valid && entries[j].inode_id == file_ino)
            {
                strncpy(entries[j].filename, newname.c_str(), sizeof(entries[j].filename) - 1);
                entries[j].filename[sizeof(entries[j].filename) - 1] = '\0';

                if (get_disk_manager().write_data_block(dir_inode.direct_blocks[i], buf))
                {
                    std::cout << "[文件系统] 成功重命名：" << oldname << " -> " << newname << "\n";
                    return true;
                }
                else
                {
                    std::cerr << "[文件系统] 错误：写入目录项失败\n";
                    return false;
                }
            }
        }
    }

    std::cerr << "[文件系统] 错误：未找到原文件的目录项\n";
    return false;
}
// ==================== 打印目录树 (tree) ====================
void FileSystem::tree(int dir_inode_id, int depth)
{
    if (dir_inode_id == -1)
        dir_inode_id = current_dir_inode;

    iNode dir_inode;
    if (!get_disk_manager().read_inode(dir_inode_id, dir_inode) || dir_inode.i_mode != 0)
        return;

    for (int i = 0; i < 10 && dir_inode.direct_blocks[i] != -1; ++i)
    {
        char buf[BLOCK_SIZE];
        if (!get_disk_manager().read_data_block(dir_inode.direct_blocks[i], buf))
            continue;

        DirEntry *entries = (DirEntry *)buf;
        int entry_count = BLOCK_SIZE / sizeof(DirEntry);

        for (int j = 0; j < entry_count; ++j)
        {
            if (entries[j].is_valid)
            {
                for (int k = 0; k < depth; ++k)
                    std::cout << "  ";
                std::cout << "|-- " << entries[j].filename << "\n";

                // 如果是目录，递归往下读
                iNode child;
                if (get_disk_manager().read_inode(entries[j].inode_id, child) && child.i_mode == 0)
                {
                    tree(entries[j].inode_id, depth + 1);
                }
            }
        }
    }
}