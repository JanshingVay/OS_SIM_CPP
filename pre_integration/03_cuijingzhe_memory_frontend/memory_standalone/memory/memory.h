#pragma once
#include <mutex>
#include <thread>
#include <vector>
#include <map>
#include <queue>
#include <list>
#include <iostream>
#include <string>
#include <cstdint>
#include <atomic>

#define PAGE_SIZE 4096   // 标准 32位系统 4KB 页大小
#define TOTAL_PAGES 32   // 模拟 128KB 物理内存

extern std::mutex mem_mutex;

enum class ReplacementPolicy {
    FIFO = 0,
    LRU = 1,
    CLOCK = 2
};

// ================= TLB 快表 =================
struct TLBEntry {
    int pid;
    uint32_t logical_page;
    int physical_frame;
    long last_access_time;
    bool valid;
};

class TLB {
private:
    int capacity;
    std::vector<TLBEntry> entries;
    long time_counter = 0;
public:
    int hits = 0;
    int misses = 0;

    TLB(int cap = 16);
    int lookup(int pid, uint32_t logical_page);
    void insert(int pid, uint32_t logical_page, int physical_frame);
    void invalidate(int pid, uint32_t logical_page);
    void invalidate_process(int pid);
    void clear();
};

// ================= 32位多级页表 =================
struct PTE {
    int physical_page = -1;
    bool valid = false;
    int swap_block_id = -1; // -1 表示尚未分配 swap 空间
};

struct PageTable {
    PTE entries[1024]; // 10位页表 = 1024 项
};

struct PageDirectory {
    PageTable* tables[1024]; // 10位页目录 = 1024 项
    PageDirectory() {
        for (int i = 0; i < 1024; i++) tables[i] = nullptr;
    }
    ~PageDirectory() {
        for (int i = 0; i < 1024; i++) if (tables[i]) delete tables[i];
    }
};

struct MemRecord {
    int pid;
    int total_logical_pages;
    PageDirectory* pgdir;
};

struct FrameRecord {
    bool is_free;
    int pid;
    uint32_t logical_page; // 扩充为 uint32_t 适应大页号
};

class MemoryManager {
private:
    FrameRecord frame_tracker[TOTAL_PAGES];
    std::map<int, MemRecord> process_memory_map;

    std::queue<int> fifo_queue;
    std::list<int> lru_list;

    // 映射: 物理帧 -> 多个(PID, 逻辑页号) [用于共享内存]
    std::map<int, std::vector<std::pair<int, uint32_t>>> frame_mappings;

    ReplacementPolicy replacement_policy = ReplacementPolicy::FIFO;

    int active_physical_pages = TOTAL_PAGES;

    bool ref_bit[TOTAL_PAGES];
    int clock_hand = 0;

    std::string monitor_log_path;

    /** 每个物理页框按需从操作系统堆区申请空间，空闲时释放。
        保留页框、页表、TLB、Swap 等模拟逻辑，同时避免固定二维数组常驻内存。 */
    unsigned char* frame_pages[TOTAL_PAGES];

    std::thread monitor_thread;
    std::atomic<bool> is_running;

    void load_config_file(const char* path);
    void tracker_task();

    bool handle_page_fault(int pid, uint32_t logical_page);

    void touch_frame_on_hit(int physical_frame);
    void push_loaded_frame_tracking(int physical_frame);

    int alloc_free_frame();
    int select_victim_frame();
    int select_victim_clock();

    bool evict_frame_to_disk(int frame);
    bool remove_one_mapping(int frame, int pid, uint32_t logical_page);

    void ensure_frame_buffer(int frame);
    void release_frame_buffer(int frame);

public:
    TLB tlb;

    std::uint64_t stat_memory_accesses = 0;
    std::uint64_t stat_page_hits = 0;
    std::uint64_t stat_page_faults = 0;
    std::uint64_t stat_segment_faults = 0;

    MemoryManager();
    ~MemoryManager();
    std::vector<int> get_memory_bitmap();

    bool alloc_mem(int pid, int size);
    bool free_mem(int pid);

    // 找回原始的 API 兼容老代码
    bool access_page(int pid, int logical_page);
    bool translate(int pid, size_t logical_addr, int& out_physical_frame, size_t& out_offset_in_page);

    // MMU 核心机制：真实 32位 地址访问
    bool access_addr(int pid, uint32_t logical_addr);
    PTE* walk_page_table(int pid, uint32_t logical_page, bool create_if_missing = false);

    bool dynamic_alloc(int pid, int delta_bytes);
    bool share_mem(int pid1, int pid2, int num_pages);

    void set_replacement_policy(ReplacementPolicy p);
    ReplacementPolicy get_replacement_policy() const { return replacement_policy; }

    int get_active_physical_pages() const { return active_physical_pages; }

    void print_memory_statistics(std::ostream& out = std::cout);
    void reset_memory_statistics();
};

extern MemoryManager mem_manager;