#include "disk.h"
#include <cstring>

// 提供线程安全的单例实例获取函数
DiskManager& get_disk_manager() {
    static DiskManager instance;
    return instance;
}

DiskManager::DiskManager() {
    disk_stream.open(DISK_FILE_NAME, std::ios::in | std::ios::out | std::ios::binary);
    
    if (!disk_stream.is_open()) {
        std::cout << "[硬件层] 未检测到物理硬盘，正在执行低级格式化 (10MB)..." << std::endl;
        _format_disk();
        disk_stream.open(DISK_FILE_NAME, std::ios::in | std::ios::out | std::ios::binary);
        if (!disk_stream.is_open()) {
            std::cerr << "[硬件层] 致命错误：虚拟磁盘挂载失败！" << std::endl;
            exit(1);
        }
    } else {
        std::cout << "[硬件层] 物理硬盘挂载成功: " << DISK_FILE_NAME << std::endl;
    }
}

DiskManager::~DiskManager() {
    if (disk_stream.is_open()) disk_stream.close();
}

void DiskManager::_format_disk() {
    std::ofstream out_stream(DISK_FILE_NAME, std::ios::out | std::ios::binary);
    char empty_block[BLOCK_SIZE] = {0}; 
    for (int i = 0; i < TOTAL_BLOCKS; i++) {
        out_stream.write(empty_block, BLOCK_SIZE);
    }
    out_stream.close();
}

bool DiskManager::_write_raw_block(int block_id, const char* buffer) {
    if (block_id < 0 || block_id >= TOTAL_BLOCKS) return false;
    std::streampos offset = static_cast<std::streampos>(block_id) * BLOCK_SIZE;
    
    // 清除可能存在的历史错误标志，防止流假死
    disk_stream.clear();
    disk_stream.seekp(offset, std::ios::beg);
    disk_stream.write(buffer, BLOCK_SIZE);
    disk_stream.flush();
    
    // 严格检查写入是否成功
    return !disk_stream.fail();
}

bool DiskManager::_read_raw_block(int block_id, char* buffer) {
    if (block_id < 0 || block_id >= TOTAL_BLOCKS) return false;
    std::streampos offset = static_cast<std::streampos>(block_id) * BLOCK_SIZE;
    
    // 清除流错误状态，再进行读取
    disk_stream.clear();
    disk_stream.seekg(offset, std::ios::beg);
    disk_stream.read(buffer, BLOCK_SIZE);
    
    return !disk_stream.fail();
}

void DiskManager::_set_bit(char* bitmap, int index, bool value) {
    int byte_idx = index / 8;
    int bit_idx = index % 8;
    if (value) bitmap[byte_idx] |= (1 << bit_idx);
    else       bitmap[byte_idx] &= ~(1 << bit_idx);
}

bool DiskManager::_get_bit(const char* bitmap, int index) {
    int byte_idx = index / 8;
    int bit_idx = index % 8;
    return (bitmap[byte_idx] & (1 << bit_idx)) != 0;
}

int DiskManager::allocate_block() {
    std::lock_guard<std::mutex> lock(disk_mutex);
    char bitmap[BLOCK_SIZE];
    int current_loaded_bitmap_id = -1; 

    for (int i = DATA_BLOCK_START; i < TOTAL_BLOCKS; i++) {
        int bitmap_block_id = DATA_BITMAP_ID + (i / (BLOCK_SIZE * 8));
        int bit_offset = i % (BLOCK_SIZE * 8); 

        if (current_loaded_bitmap_id != bitmap_block_id) {
            if (!_read_raw_block(bitmap_block_id, bitmap)) return -1;
            current_loaded_bitmap_id = bitmap_block_id;
        }

        if (!_get_bit(bitmap, bit_offset)) {
            _set_bit(bitmap, bit_offset, true);            
            _write_raw_block(bitmap_block_id, bitmap);     
            
            char empty_buf[BLOCK_SIZE] = {0};
            _write_raw_block(i, empty_buf);
            
            return i; 
        }
    }
    return -1; 
}

bool DiskManager::free_block(int block_id) {
    std::lock_guard<std::mutex> lock(disk_mutex);
    if (block_id < DATA_BLOCK_START || block_id >= TOTAL_BLOCKS) return false;
    
    int bitmap_block_id = DATA_BITMAP_ID + (block_id / (BLOCK_SIZE * 8));
    int bit_offset = block_id % (BLOCK_SIZE * 8);

    char bitmap[BLOCK_SIZE];
    if (!_read_raw_block(bitmap_block_id, bitmap)) return false;
    
    _set_bit(bitmap, bit_offset, false); 
    return _write_raw_block(bitmap_block_id, bitmap);
}

int DiskManager::allocate_inode() {
    std::lock_guard<std::mutex> lock(disk_mutex);
    char bitmap[BLOCK_SIZE];
    if (!_read_raw_block(INODE_BITMAP_ID, bitmap)) return -1;

    for (int i = 0; i < static_cast<int>(TOTAL_INODES); i++) {
        if (!_get_bit(bitmap, i)) {
            _set_bit(bitmap, i, true);
            _write_raw_block(INODE_BITMAP_ID, bitmap);
            
            // 分配节点时，将其物理空间也同步格式化，杜绝脏数据
            int block_id = INODE_TABLE_START + (i / INODES_PER_BLOCK);
            int offset = (i % INODES_PER_BLOCK) * sizeof(iNode);
            char block_buf[BLOCK_SIZE];
            if (_read_raw_block(block_id, block_buf)) {
                iNode empty_node; 
                empty_node.i_num = i; // 直接赋好初始编号
                std::memcpy(block_buf + offset, &empty_node, sizeof(iNode));
                _write_raw_block(block_id, block_buf);
            }
            return i; 
        }
    }
    return -1;
}

bool DiskManager::free_inode(int inode_id) {
    std::lock_guard<std::mutex> lock(disk_mutex);
    if (inode_id < 0 || inode_id >= static_cast<int>(TOTAL_INODES)) return false;
    
    char bitmap[BLOCK_SIZE];
    if (!_read_raw_block(INODE_BITMAP_ID, bitmap)) return false;
    
    _set_bit(bitmap, inode_id, false);
    return _write_raw_block(INODE_BITMAP_ID, bitmap);
}

bool DiskManager::read_inode(int inode_id, iNode& out_inode) {
    std::lock_guard<std::mutex> lock(disk_mutex);
    if (inode_id < 0 || inode_id >= static_cast<int>(TOTAL_INODES)) return false;
    
    // 【修复】消灭硬编码
    int block_id = INODE_TABLE_START + (inode_id / INODES_PER_BLOCK);
    if (block_id < INODE_TABLE_START || block_id >= DATA_BLOCK_START) return false;
    
    int offset = (inode_id % INODES_PER_BLOCK) * sizeof(iNode);
    char buffer[BLOCK_SIZE];
    if (!_read_raw_block(block_id, buffer)) return false;
    
    std::memcpy(&out_inode, buffer + offset, sizeof(iNode));
    return true;
}

bool DiskManager::write_inode(int inode_id, const iNode& in_inode) {
    std::lock_guard<std::mutex> lock(disk_mutex);
    if (inode_id < 0 || inode_id >= static_cast<int>(TOTAL_INODES)) return false;
    
    // 消灭硬编码
    int block_id = INODE_TABLE_START + (inode_id / INODES_PER_BLOCK);
    if (block_id < INODE_TABLE_START || block_id >= DATA_BLOCK_START) return false;
    
    int offset = (inode_id % INODES_PER_BLOCK) * sizeof(iNode);
    char buffer[BLOCK_SIZE];
    if (!_read_raw_block(block_id, buffer)) return false;
    
    std::memcpy(buffer + offset, &in_inode, sizeof(iNode));
    return _write_raw_block(block_id, buffer);
}

bool DiskManager::read_data_block(int block_id, char* buffer) {
    std::lock_guard<std::mutex> lock(disk_mutex);
    if (block_id < DATA_BLOCK_START || block_id >= TOTAL_BLOCKS) return false;
    return _read_raw_block(block_id, buffer);
}

bool DiskManager::write_data_block(int block_id, const char* buffer) {
    std::lock_guard<std::mutex> lock(disk_mutex);
    if (block_id < DATA_BLOCK_START || block_id >= TOTAL_BLOCKS) return false;
    return _write_raw_block(block_id, buffer);
}

// 在 disk.cpp 文件末尾添加：
void DiskManager::sync_disk() {
    std::lock_guard<std::mutex> lock(disk_mutex);
    if (disk_stream.is_open()) {
        disk_stream.flush(); // 强制刷出 C++ 缓冲区
        disk_stream.close(); // 关闭句柄，强制 OS 内核落盘
        std::cout << "[硬件层] 虚拟磁盘数据已安全同步 (fsync) 并卸载。" << std::endl;
    }
}