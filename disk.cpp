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

    _scheduler_running = true;
    _scheduler_enabled = true;
    _head_position = DATA_BLOCK_START;
    _scheduler_direction = Direction::UP;
    _scheduler_thread = std::thread(&DiskManager::_scheduler_loop, this);
}

DiskManager::~DiskManager() {
    {
        std::lock_guard<std::mutex> lock(_io_queue_mutex);
        _scheduler_running = false;
    }
    _io_cv.notify_all();
    if (_scheduler_thread.joinable()) {
        _scheduler_thread.join();
    }
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

    for (int i = 0; i < TOTAL_INODES; i++) {
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
    if (inode_id < 0 || inode_id >= TOTAL_INODES) return false;
    
    char bitmap[BLOCK_SIZE];
    if (!_read_raw_block(INODE_BITMAP_ID, bitmap)) return false;
    
    _set_bit(bitmap, inode_id, false);
    return _write_raw_block(INODE_BITMAP_ID, bitmap);
}

bool DiskManager::read_inode(int inode_id, iNode& out_inode) {
    std::lock_guard<std::mutex> lock(disk_mutex);
    if (inode_id < 0 || inode_id >= TOTAL_INODES) return false;
    
    // 【修复】消灭硬编码
    int block_id = INODE_TABLE_START + (inode_id / INODES_PER_BLOCK);
    if (block_id < INODE_TABLE_START || block_id >= DATA_BLOCK_START) return false;
    
    int offset = (inode_id % INODES_PER_BLOCK) * sizeof(iNode);
    char buffer[BLOCK_SIZE];
    if (!_read_raw_block(block_id, buffer)) return false;
    
    std::memcpy(&out_inode, buffer + offset, sizeof(iNode));
    out_inode.normalize();
    return true;
}

bool DiskManager::write_inode(int inode_id, const iNode& in_inode) {
    std::lock_guard<std::mutex> lock(disk_mutex);
    if (inode_id < 0 || inode_id >= TOTAL_INODES) return false;
    
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
    if (block_id < DATA_BLOCK_START || block_id >= TOTAL_BLOCKS) return false;

    if (!_scheduler_enabled) {
        std::lock_guard<std::mutex> lock(disk_mutex);
        return _read_raw_block(block_id, buffer);
    }

    IORequest req(block_id, false, buffer);
    std::future<bool> fut = req.done.get_future();

    {
        std::lock_guard<std::mutex> lock(_io_queue_mutex);
        _io_queue.push_back(std::move(req));
    }
    _io_cv.notify_one();

    return fut.get();
}

bool DiskManager::write_data_block(int block_id, const char* buffer) {
    if (block_id < DATA_BLOCK_START || block_id >= TOTAL_BLOCKS) return false;

    if (!_scheduler_enabled) {
        std::lock_guard<std::mutex> lock(disk_mutex);
        return _write_raw_block(block_id, buffer);
    }

    IORequest req(block_id, true, const_cast<char*>(buffer));
    std::future<bool> fut = req.done.get_future();

    {
        std::lock_guard<std::mutex> lock(_io_queue_mutex);
        _io_queue.push_back(std::move(req));
    }
    _io_cv.notify_one();

    return fut.get();
}

// 在 disk.cpp 文件末尾添加：
void DiskManager::sync_disk() {
    std::lock_guard<std::mutex> lock(disk_mutex);
    if (disk_stream.is_open()) {
        disk_stream.flush();
        disk_stream.close();
        std::cout << "[硬件层] 虚拟磁盘数据已安全同步 (fsync) 并卸载。" << std::endl;
    }
}

int DiskManager::_alloc_indirect_data_block(int indirect_block_id, int index) {
    if (index < 0 || index >= PTRS_PER_BLOCK) return -1;

    char buf[BLOCK_SIZE];
    if (!read_data_block(indirect_block_id, buf)) return -1;

    int* ptrs = reinterpret_cast<int*>(buf);
    if (ptrs[index] != -1) return ptrs[index];

    int new_block = allocate_block();
    if (new_block == -1) return -1;

    ptrs[index] = new_block;
    if (!write_data_block(indirect_block_id, buf)) {
        free_block(new_block);
        return -1;
    }
    return new_block;
}

void DiskManager::_free_single_indirect_chain(int indirect_block_id) {
    if (indirect_block_id < DATA_BLOCK_START || indirect_block_id >= TOTAL_BLOCKS) return;

    char buf[BLOCK_SIZE];
    if (!read_data_block(indirect_block_id, buf)) return;

    int* ptrs = reinterpret_cast<int*>(buf);
    for (int i = 0; i < PTRS_PER_BLOCK; ++i) {
        if (ptrs[i] != -1) {
            free_block(ptrs[i]);
            ptrs[i] = -1;
        }
    }
    free_block(indirect_block_id);
}

void DiskManager::_free_double_indirect_chain(int indirect_block_id) {
    if (indirect_block_id < DATA_BLOCK_START || indirect_block_id >= TOTAL_BLOCKS) return;

    char buf[BLOCK_SIZE];
    if (!read_data_block(indirect_block_id, buf)) return;

    int* ptrs = reinterpret_cast<int*>(buf);
    for (int i = 0; i < PTRS_PER_BLOCK; ++i) {
        if (ptrs[i] != -1) {
            _free_single_indirect_chain(ptrs[i]);
            ptrs[i] = -1;
        }
    }
    free_block(indirect_block_id);
}

int DiskManager::get_nth_block(const iNode& node, int n) {
    if (n < 0) return -1;

    if (n < 10) {
        return node.direct_blocks[n];
    }

    n -= 10;

    if (n < PTRS_PER_BLOCK) {
        if (node.single_indirect == -1) return -1;
        char buf[BLOCK_SIZE];
        if (!read_data_block(node.single_indirect, buf)) return -1;
        int* ptrs = reinterpret_cast<int*>(buf);
        return ptrs[n];
    }

    n -= PTRS_PER_BLOCK;

    if (n < PTRS_PER_BLOCK * PTRS_PER_BLOCK) {
        if (node.double_indirect == -1) return -1;
        int first = n / PTRS_PER_BLOCK;
        int second = n % PTRS_PER_BLOCK;

        char buf1[BLOCK_SIZE];
        if (!read_data_block(node.double_indirect, buf1)) return -1;
        int* first_ptrs = reinterpret_cast<int*>(buf1);

        if (first_ptrs[first] == -1) return -1;

        char buf2[BLOCK_SIZE];
        if (!read_data_block(first_ptrs[first], buf2)) return -1;
        int* second_ptrs = reinterpret_cast<int*>(buf2);
        return second_ptrs[second];
    }

    return -1;
}

int DiskManager::allocate_nth_block(iNode& node, int n) {
    if (n < 0) return -1;

    if (n < 10) {
        if (node.direct_blocks[n] == -1) {
            int new_block = allocate_block();
            if (new_block == -1) return -1;
            node.direct_blocks[n] = new_block;
        }
        return node.direct_blocks[n];
    }

    n -= 10;

    if (n < PTRS_PER_BLOCK) {
        if (node.single_indirect == -1) {
            int indirect_block = allocate_block();
            if (indirect_block == -1) return -1;
            node.single_indirect = indirect_block;

            char empty[BLOCK_SIZE];
            memset(empty, -1, BLOCK_SIZE);
            write_data_block(indirect_block, empty);
        }
        return _alloc_indirect_data_block(node.single_indirect, n);
    }

    n -= PTRS_PER_BLOCK;

    if (n < PTRS_PER_BLOCK * PTRS_PER_BLOCK) {
        if (node.double_indirect == -1) {
            int dbl_block = allocate_block();
            if (dbl_block == -1) return -1;
            node.double_indirect = dbl_block;

            char empty[BLOCK_SIZE];
            memset(empty, -1, BLOCK_SIZE);
            write_data_block(dbl_block, empty);
        }

        int first = n / PTRS_PER_BLOCK;
        int second = n % PTRS_PER_BLOCK;

        // 确保一级间接块存在
        char dbl_buf[BLOCK_SIZE];
        if (!read_data_block(node.double_indirect, dbl_buf)) return -1;
        int* first_ptrs = reinterpret_cast<int*>(dbl_buf);

        if (first_ptrs[first] == -1) {
            // 分配新的一级间接块
            int new_first_level = allocate_block();
            if (new_first_level == -1) return -1;

            char empty[BLOCK_SIZE];
            memset(empty, -1, BLOCK_SIZE);
            if (!write_data_block(new_first_level, empty)) {
                free_block(new_first_level);
                return -1;
            }

            first_ptrs[first] = new_first_level;
            if (!write_data_block(node.double_indirect, dbl_buf)) {
                free_block(new_first_level);
                first_ptrs[first] = -1;
                return -1;
            }
        }

        return _alloc_indirect_data_block(first_ptrs[first], second);
    }

    return -1;
}

void DiskManager::free_all_data_blocks(iNode& node) {
    for (int i = 0; i < 10; ++i) {
        if (node.direct_blocks[i] != -1) {
            free_block(node.direct_blocks[i]);
            node.direct_blocks[i] = -1;
        }
    }

    if (node.single_indirect != -1) {
        _free_single_indirect_chain(node.single_indirect);
        node.single_indirect = -1;
    }

    if (node.double_indirect != -1) {
        _free_double_indirect_chain(node.double_indirect);
        node.double_indirect = -1;
    }
}

void DiskManager::_scheduler_loop() {
    while (true) {
        std::unique_lock<std::mutex> lock(_io_queue_mutex);
        _io_cv.wait(lock, [this] {
            return !_io_queue.empty() || !_scheduler_running;
        });

        if (_io_queue.empty()) {
            if (!_scheduler_running) break;
            continue;
        }

        int best_idx = -1;
        int best_dist = TOTAL_BLOCKS * 2;

        for (int i = 0; i < (int)_io_queue.size(); ++i) {
            int dist;
            if (_scheduler_direction == Direction::UP) {
                dist = _io_queue[i].block_id - _head_position;
                if (dist < 0) dist = TOTAL_BLOCKS * 2 + dist;
            } else {
                dist = _head_position - _io_queue[i].block_id;
                if (dist < 0) dist = TOTAL_BLOCKS * 2 + dist;
            }
            if (dist >= 0 && dist < best_dist) {
                best_dist = dist;
                best_idx = i;
            }
        }

        if (best_idx == -1) {
            _scheduler_direction = (_scheduler_direction == Direction::UP)
                                 ? Direction::DOWN : Direction::UP;
            for (int i = 0; i < (int)_io_queue.size(); ++i) {
                int dist;
                if (_scheduler_direction == Direction::UP) {
                    dist = _io_queue[i].block_id - _head_position;
                    if (dist < 0) dist = TOTAL_BLOCKS * 2 + dist;
                } else {
                    dist = _head_position - _io_queue[i].block_id;
                    if (dist < 0) dist = TOTAL_BLOCKS * 2 + dist;
                }
                if (dist >= 0 && dist < best_dist) {
                    best_dist = dist;
                    best_idx = i;
                }
            }
        }

        if (best_idx == -1) continue;

        IORequest req = std::move(_io_queue[best_idx]);
        _io_queue.erase(_io_queue.begin() + best_idx);
        _head_position = req.block_id;

        lock.unlock();

        bool ok = false;
        {
            std::lock_guard<std::mutex> disk_lock(disk_mutex);
            disk_stream.clear();
            if (req.is_write) {
                ok = _write_raw_block(req.block_id, req.buffer);
            } else {
                ok = _read_raw_block(req.block_id, req.buffer);
            }
        }
        req.done.set_value(ok);
    }

    while (true) {
        std::unique_lock<std::mutex> lock(_io_queue_mutex);
        if (_io_queue.empty()) return;

        IORequest req = std::move(_io_queue.back());
        _io_queue.pop_back();
        _head_position = req.block_id;

        lock.unlock();

        bool ok = false;
        {
            std::lock_guard<std::mutex> disk_lock(disk_mutex);
            disk_stream.clear();
            if (req.is_write) {
                ok = _write_raw_block(req.block_id, req.buffer);
            } else {
                ok = _read_raw_block(req.block_id, req.buffer);
            }
        }
        req.done.set_value(ok);
    }
}

void DiskManager::set_scheduler_enabled(bool enabled) {
    std::lock_guard<std::mutex> lock(_io_queue_mutex);
    _scheduler_enabled = enabled;
    if (!enabled && !_io_queue.empty()) {
        std::cout << "[IO调度器] 已关闭，队列尚有 " << _io_queue.size()
                  << " 个待处理请求，将直接执行。" << std::endl;
    }
}