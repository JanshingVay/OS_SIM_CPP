#include "memory.h"
#include "../disk.h"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <sstream>
#include <cctype>
#include <cstring>
#include <iomanip>

std::mutex mem_mutex;
MemoryManager mem_manager;

static std::string trim_str(std::string s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.erase(s.begin());
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
    return s;
}

void MemoryManager::load_config_file(const char* path) {
    std::ifstream in(path);
    if (!in) return;

    std::string line;
    while (std::getline(in, line)) {
        line = trim_str(line);
        if (line.empty() || line[0] == '#') continue;

        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = trim_str(line.substr(0, eq));
        std::string val = trim_str(line.substr(eq + 1));
        if (key.empty()) continue;

        if (key == "physical_pages" || key == "user_pages") {
            int n = std::stoi(val);
            if (n < 4) n = 4;
            if (n > TOTAL_PAGES) n = TOTAL_PAGES;
            active_physical_pages = n;
        }
        else if (key == "policy" || key == "replacement") {
            for (char& c : val) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (val == "lru") replacement_policy = ReplacementPolicy::LRU;
            else if (val == "clock") replacement_policy = ReplacementPolicy::CLOCK;
            else replacement_policy = ReplacementPolicy::FIFO;
        }
        else if (key == "log_file" || key == "monitor_log") {
            monitor_log_path = val;
        }
    }
}

// ================= TLB 快表实现 =================
TLB::TLB(int cap) : capacity(cap) {}

int TLB::lookup(int pid, uint32_t logical_page) {
    for (auto& entry : entries) {
        if (entry.valid && entry.pid == pid && entry.logical_page == logical_page) {
            entry.last_access_time = ++time_counter;
            hits++;
            return entry.physical_frame;
        }
    }
    misses++;
    return -1;
}

void TLB::insert(int pid, uint32_t logical_page, int physical_frame) {
    for (auto& entry : entries) {
        if (entry.valid && entry.pid == pid && entry.logical_page == logical_page) {
            entry.physical_frame = physical_frame;
            entry.last_access_time = ++time_counter;
            return;
        }
    }
    if (entries.size() < (size_t)capacity) {
        entries.push_back({ pid, logical_page, physical_frame, ++time_counter, true });
    }
    else {
        int oldest_idx = 0;
        for (size_t i = 1; i < entries.size(); i++) {
            if (entries[i].last_access_time < entries[oldest_idx].last_access_time) oldest_idx = i;
        }
        entries[oldest_idx] = { pid, logical_page, physical_frame, ++time_counter, true };
    }
}

void TLB::invalidate_process(int pid) {
    for (auto& entry : entries) {
        if (entry.pid == pid) entry.valid = false;
    }
}

void TLB::invalidate(int pid, uint32_t logical_page) {
    for (auto& entry : entries) {
        if (entry.pid == pid && entry.logical_page == logical_page) entry.valid = false;
    }
}

void TLB::clear() { entries.clear(); }

// ================= 内存管理器实现 =================
MemoryManager::MemoryManager() : tlb(16) {
    for (int i = 0; i < TOTAL_PAGES; i++) {
        frame_tracker[i].is_free = true;
        frame_tracker[i].pid = -1;
        frame_tracker[i].logical_page = 0;
        ref_bit[i] = false;
        frame_pages[i] = nullptr;
    }
    active_physical_pages = TOTAL_PAGES;
    clock_hand = 0;
    load_config_file("os_memory_config.txt");

    while (!fifo_queue.empty()) fifo_queue.pop();
    lru_list.clear();
    frame_mappings.clear();

    is_running = true;
    monitor_thread = std::thread(&MemoryManager::tracker_task, this);
}

MemoryManager::~MemoryManager() {
    is_running = false;
    if (monitor_thread.joinable()) {
        monitor_thread.join();
    }
    for (int i = 0; i < TOTAL_PAGES; ++i) {
        release_frame_buffer(i);
    }
}

void MemoryManager::ensure_frame_buffer(int frame) {
    if (frame < 0 || frame >= TOTAL_PAGES) return;
    if (!frame_pages[frame]) {
        frame_pages[frame] = new unsigned char[PAGE_SIZE];
    }
}

void MemoryManager::release_frame_buffer(int frame) {
    if (frame < 0 || frame >= TOTAL_PAGES) return;
    delete[] frame_pages[frame];
    frame_pages[frame] = nullptr;
}

static const char* policy_cstr(ReplacementPolicy p) {
    switch (p) {
    case ReplacementPolicy::LRU: return "LRU";
    case ReplacementPolicy::CLOCK: return "CLOCK";
    default: return "FIFO";
    }
}

void MemoryManager::set_replacement_policy(ReplacementPolicy p) {
    std::lock_guard<std::mutex> lock(mem_mutex);
    replacement_policy = p;
    std::cout << "[内存管理] 页面置换策略已切换为: " << policy_cstr(p) << std::endl;
}

void MemoryManager::print_memory_statistics(std::ostream& out) {
    std::lock_guard<std::mutex> lock(mem_mutex);
    out << "\n---------- [内存访问统计] ----------\n";
    out << "合法逻辑页访问次数: " << stat_memory_accesses << "\n";
    out << "命中次数: " << stat_page_hits << " | 缺页次数: " << stat_page_faults << "\n";
    out << "非法访问(越界): " << stat_segment_faults << "\n";
    if (stat_memory_accesses > 0) {
        double hr = 100.0 * static_cast<double>(stat_page_hits) / static_cast<double>(stat_memory_accesses);
        out << "命中率: " << std::fixed << std::setprecision(2) << hr << "%\n";
    }
    else {
        out << "命中率: --\n";
    }
    out << "-----------------------------------\n" << std::endl;
}

void MemoryManager::reset_memory_statistics() {
    std::lock_guard<std::mutex> lock(mem_mutex);
    stat_memory_accesses = 0;
    stat_page_hits = 0;
    stat_page_faults = 0;
    stat_segment_faults = 0;
    std::cout << "[内存管理] 统计数据已清零。" << std::endl;
}

void MemoryManager::tracker_task() {
    while (is_running.load()) {
        std::this_thread::sleep_for(std::chrono::seconds(5));
        std::lock_guard<std::mutex> lock(mem_mutex);

        int used = 0;
        std::ostringstream snapshot;

        snapshot << "\n========== [后台内存跟踪线程 (MMU虚拟内存+磁盘Swap)] ==========\n";
        snapshot << "置换策略: " << policy_cstr(replacement_policy) << "\n";
        snapshot << "用户物理页容量: " << active_physical_pages << " 页\n";
        snapshot << "TLB 快表命中: " << tlb.hits << " | 缺页中断: " << stat_page_faults << "\n";
        snapshot << "物理页框状态 [空为闲, 数字为占用该帧的进程PID]: \n";

        for (int i = 0; i < active_physical_pages; i++) {
            if (frame_tracker[i].is_free) {
                snapshot << "[ ] ";
            }
            else {
                snapshot << "[" << frame_tracker[i].pid << "] ";
                used++;
            }
            if ((i + 1) % 8 == 0) snapshot << "\n";
        }
        if (active_physical_pages % 8 != 0) snapshot << "\n";

        if (active_physical_pages < TOTAL_PAGES) {
            snapshot << "(帧 " << active_physical_pages << "～" << (TOTAL_PAGES - 1) << " 未启用)\n";
        }

        snapshot << "物理内存使用率: " << used << " / " << active_physical_pages << " 页\n";
        snapshot << "===============================================================\n" << std::endl;

        std::cout << snapshot.str();

        if (!monitor_log_path.empty()) {
            std::ofstream log(monitor_log_path, std::ios::app);
            if (log) {
                log << snapshot.str();
            }
        }
    }
}

// 核心页表遍历机制 (10位页目录 + 10位页表)
PTE* MemoryManager::walk_page_table(int pid, uint32_t logical_page, bool create_if_missing) {
    auto it = process_memory_map.find(pid);
    if (it == process_memory_map.end()) return nullptr;

    PageDirectory* pd = it->second.pgdir;

    uint32_t dir_idx = (logical_page >> 10) & 0x3FF;
    uint32_t tbl_idx = logical_page & 0x3FF;

    if (pd->tables[dir_idx] == nullptr) {
        if (create_if_missing) {
            pd->tables[dir_idx] = new PageTable();
        }
        else {
            return nullptr;
        }
    }
    return &(pd->tables[dir_idx]->entries[tbl_idx]);
}

bool MemoryManager::alloc_mem(int pid, int size) {
    std::lock_guard<std::mutex> lock(mem_mutex);

    if (process_memory_map.find(pid) != process_memory_map.end()) {
        std::cout << "[虚拟内存] 错误：进程 " << pid << " 已存在，拒绝重复分配。" << std::endl;
        return false;
    }

    int pages_needed = (size + PAGE_SIZE - 1) / PAGE_SIZE;
    if (pages_needed <= 0) pages_needed = 1;

    MemRecord record;
    record.pid = pid;
    record.total_logical_pages = pages_needed;
    record.pgdir = new PageDirectory(); // 创建顶级页目录

    // 注意：walk_page_table() 是通过 process_memory_map 查找页目录的，
    // 所以必须先登记 record，再预创建页表项。原版本先 walk 后登记，
    // 导致页表没有真正建立，调度线程第一次访问进程虚拟地址就会被误判为“段错误”。
    process_memory_map[pid] = record;

    // 预填合法逻辑页的页表项：大部分页面仍采用按需调入；同时预装第 0 页，
    // 便于验收和可视化面板立即看到物理页框占用情况。
    for (int i = 0; i < pages_needed; i++) {
        walk_page_table(pid, static_cast<uint32_t>(i), true);
    }
    handle_page_fault(pid, 0);

    std::cout << "[虚拟内存] 进程 " << pid << " 创建成功，已分配逻辑页表空间 " << pages_needed << " 页。" << std::endl;
    return true;
}

bool MemoryManager::remove_one_mapping(int frame, int pid, uint32_t logical_page) {
    auto fit = frame_mappings.find(frame);
    if (fit == frame_mappings.end()) return false;

    auto& vec = fit->second;
    vec.erase(std::remove_if(vec.begin(), vec.end(),
        [&](const std::pair<int, uint32_t>& p) {
            return p.first == pid && p.second == logical_page;
        }),
        vec.end());

    if (vec.empty()) {
        frame_tracker[frame].is_free = true;
        frame_tracker[frame].pid = -1;
        frame_tracker[frame].logical_page = 0;
        ref_bit[frame] = false;
        lru_list.remove(frame);
        frame_mappings.erase(frame);
        release_frame_buffer(frame);
    }
    return true;
}

bool MemoryManager::evict_frame_to_disk(int frame) {
    auto fit = frame_mappings.find(frame);
    if (fit == frame_mappings.end()) return true;

    char buffer[PAGE_SIZE];
    ensure_frame_buffer(frame);
    std::memcpy(buffer, frame_pages[frame], PAGE_SIZE);

    int swap_blk = -1;
    for (const auto& pr : fit->second) {
        PTE* p = walk_page_table(pr.first, pr.second, false);
        if (p && p->swap_block_id >= 0) {
            swap_blk = p->swap_block_id;
            break;
        }
    }

    if (swap_blk < 0) {
        swap_blk = get_disk_manager().allocate_block();
        if (swap_blk < 0) {
            std::cout << "    -> [Swap] 磁盘数据块分配失败（可能与文件系统争夺同一卷空间）。" << std::endl;
            return false;
        }
    }

    if (!get_disk_manager().write_data_block(swap_blk, buffer)) {
        std::cout << "    -> [Swap] 写入虚拟磁盘失败。" << std::endl;
        return false;
    }

    std::cout << "    -> [Swap Out] 已将物理帧 " << frame << " 写入磁盘块 " << swap_blk
        << "（vdisk 数据区）。" << std::endl;

    for (const auto& pr : fit->second) {
        PTE* p = walk_page_table(pr.first, pr.second, false);
        if (p) {
            p->swap_block_id = swap_blk;
            p->valid = false;
            p->physical_page = -1;
            tlb.invalidate(pr.first, pr.second); // 从 TLB 踢出
        }
    }

    ref_bit[frame] = false;
    frame_mappings.erase(frame);
    release_frame_buffer(frame);
    return true;
}

bool MemoryManager::free_mem(int pid) {
    std::lock_guard<std::mutex> lock(mem_mutex);

    auto it = process_memory_map.find(pid);
    if (it == process_memory_map.end()) return false;

    tlb.invalidate_process(pid); // 刷新 TLB

    MemRecord& record = it->second;
    for (int i = 0; i < record.total_logical_pages; ++i) {
        PTE* pte = walk_page_table(pid, i, false);
        if (pte) {
            if (pte->swap_block_id >= 0) {
                get_disk_manager().free_block(pte->swap_block_id);
                pte->swap_block_id = -1;
            }
            if (pte->valid) {
                remove_one_mapping(pte->physical_page, pid, i);
            }
        }
    }

    delete record.pgdir;
    process_memory_map.erase(it);
    std::cout << "[内存释放] 成功：进程 " << pid << " 的虚拟内存及 Swap 块已回收。" << std::endl;
    return true;
}

void MemoryManager::touch_frame_on_hit(int physical_frame) {
    if (physical_frame < 0 || physical_frame >= TOTAL_PAGES) return;
    if (replacement_policy == ReplacementPolicy::LRU) {
        lru_list.remove(physical_frame);
        lru_list.push_back(physical_frame);
    }
    if (replacement_policy == ReplacementPolicy::CLOCK) {
        ref_bit[physical_frame] = true;
    }
}

void MemoryManager::push_loaded_frame_tracking(int physical_frame) {
    if (replacement_policy == ReplacementPolicy::FIFO) {
        fifo_queue.push(physical_frame);
    }
    if (replacement_policy == ReplacementPolicy::LRU) {
        lru_list.remove(physical_frame);
        lru_list.push_back(physical_frame);
    }
    if (replacement_policy == ReplacementPolicy::CLOCK) {
        ref_bit[physical_frame] = true;
    }
}

int MemoryManager::alloc_free_frame() {
    for (int i = 0; i < active_physical_pages; i++) {
        if (frame_tracker[i].is_free) return i;
    }
    return -1;
}

int MemoryManager::select_victim_clock() {
    int n = active_physical_pages;
    if (n <= 0) return -1;
    clock_hand %= n;

    int steps = 0;
    const int max_steps = n * 6;
    while (steps < max_steps) {
        int f = clock_hand % n;
        if (frame_tracker[f].is_free) {
            clock_hand = (clock_hand + 1) % n;
            steps++;
            continue;
        }
        if (!ref_bit[f]) {
            clock_hand = (f + 1) % n;
            return f;
        }
        ref_bit[f] = false;
        clock_hand = (f + 1) % n;
        steps++;
    }

    for (int i = 0; i < n; ++i) {
        if (!frame_tracker[i].is_free) return i;
    }
    return -1;
}

int MemoryManager::select_victim_frame() {
    if (replacement_policy == ReplacementPolicy::FIFO) {
        while (!fifo_queue.empty()) {
            int candidate = fifo_queue.front();
            fifo_queue.pop();
            if (!frame_tracker[candidate].is_free && candidate < active_physical_pages) return candidate;
        }
    }
    else if (replacement_policy == ReplacementPolicy::LRU) {
        while (!lru_list.empty()) {
            int candidate = lru_list.front();
            lru_list.pop_front();
            if (!frame_tracker[candidate].is_free && candidate < active_physical_pages) return candidate;
        }
    }
    else if (replacement_policy == ReplacementPolicy::CLOCK) {
        return select_victim_clock();
    }

    for (int i = 0; i < active_physical_pages; ++i) {
        if (!frame_tracker[i].is_free) return i;
    }
    return -1;
}

bool MemoryManager::translate(int pid, size_t logical_addr, int& out_physical_frame, size_t& out_offset_in_page) {
    std::lock_guard<std::mutex> lock(mem_mutex);

    auto it = process_memory_map.find(pid);
    if (it == process_memory_map.end()) return false;

    uint32_t logical_page = static_cast<uint32_t>(logical_addr / PAGE_SIZE);
    size_t offset = logical_addr % PAGE_SIZE;
    if (logical_page >= static_cast<uint32_t>(it->second.total_logical_pages)) {
        stat_segment_faults++;
        return false;
    }

    int frame = tlb.lookup(pid, logical_page);
    if (frame != -1) {
        out_physical_frame = frame;
        out_offset_in_page = offset;
        touch_frame_on_hit(frame);
        return true;
    }

    PTE* pte = walk_page_table(pid, logical_page, false);
    if (pte == nullptr) return false;

    if (!pte->valid) {
        // translate 本身也应体现 MMU 行为：未装入页先触发缺页中断再完成地址转换。
        stat_page_faults++;
        if (!handle_page_fault(pid, logical_page)) return false;
        pte = walk_page_table(pid, logical_page, false);
        if (pte == nullptr || !pte->valid) return false;
    }

    out_physical_frame = pte->physical_page;
    out_offset_in_page = offset;
    tlb.insert(pid, logical_page, pte->physical_page);
    touch_frame_on_hit(pte->physical_page);
    return true;
}

bool MemoryManager::dynamic_alloc(int pid, int delta_bytes) {
    std::lock_guard<std::mutex> lock(mem_mutex);

    auto it = process_memory_map.find(pid);
    if (it == process_memory_map.end()) return false;

    MemRecord& record = it->second;

    if (delta_bytes == 0) return true;

    if (delta_bytes > 0) {
        int add_pages = (delta_bytes + PAGE_SIZE - 1) / PAGE_SIZE;
        for (int k = 0; k < add_pages; ++k) {
            walk_page_table(pid, record.total_logical_pages + k, true);
        }
        record.total_logical_pages += add_pages;
        std::cout << "[动态内存] 进程 " << pid << " 扩充 " << add_pages << " 个逻辑页，当前共 "
            << record.total_logical_pages << " 页。" << std::endl;
        return true;
    }

    int abs_bytes = -delta_bytes;
    int remove_pages = (abs_bytes + PAGE_SIZE - 1) / PAGE_SIZE;
    int max_remove = record.total_logical_pages - 1;
    if (max_remove < 0) max_remove = 0;
    if (remove_pages > max_remove) remove_pages = max_remove;
    if (remove_pages <= 0) {
        std::cout << "[动态内存] 缩减幅度过小，未改变逻辑页数。" << std::endl;
        return true;
    }

    for (int k = 0; k < remove_pages; ++k) {
        int idx = record.total_logical_pages - 1;
        PTE* pte = walk_page_table(pid, idx, false);
        if (pte) {
            if (pte->swap_block_id >= 0) {
                get_disk_manager().free_block(pte->swap_block_id);
                pte->swap_block_id = -1;
            }
            if (pte->valid) {
                remove_one_mapping(pte->physical_page, pid, idx);
                tlb.invalidate(pid, idx);
            }
        }
        record.total_logical_pages--;
    }
    std::cout << "[动态内存] 进程 " << pid << " 缩减 " << remove_pages << " 个逻辑页，当前共 "
        << record.total_logical_pages << " 页。" << std::endl;
    return true;
}

bool MemoryManager::share_mem(int pid1, int pid2, int num_pages) {
    std::lock_guard<std::mutex> lock(mem_mutex);

    if (pid1 == pid2 || num_pages <= 0) return false;

    auto it1 = process_memory_map.find(pid1);
    auto it2 = process_memory_map.find(pid2);
    if (it1 == process_memory_map.end() || it2 == process_memory_map.end()) {
        std::cout << "[共享内存] 错误：进程不存在。" << std::endl;
        return false;
    }

    MemRecord& a = it1->second;
    MemRecord& b = it2->second;

    if (a.total_logical_pages < num_pages || b.total_logical_pages < num_pages) {
        std::cout << "[共享内存] 错误：某一进程逻辑页不足 " << num_pages << " 页。" << std::endl;
        return false;
    }

    for (int i = 0; i < num_pages; ++i) {
        PTE* pb = walk_page_table(pid2, i, true);
        PTE* pa = walk_page_table(pid1, i, true);
        if (!pb || !pa) {
            std::cout << "[共享内存] 错误：无法取得页表项。" << std::endl;
            return false;
        }

        if (pb->swap_block_id >= 0) {
            get_disk_manager().free_block(pb->swap_block_id);
            pb->swap_block_id = -1;
        }
        if (pb->valid) {
            remove_one_mapping(pb->physical_page, pid2, i);
            tlb.invalidate(pid2, i);
            pb->valid = false;
            pb->physical_page = -1;
        }

        pb->physical_page = pa->physical_page;
        pb->valid = pa->valid;
        pb->swap_block_id = pa->swap_block_id;

        if (pa->valid) {
            int f = pa->physical_page;
            frame_mappings[f].push_back({ pid2, static_cast<uint32_t>(i) });
            frame_tracker[f].is_free = false;
            ref_bit[f] = true;
        }
    }

    std::cout << "[共享内存] 已将进程 " << pid1 << " 与 " << pid2 << " 的前 " << num_pages
        << " 个逻辑页映射到相同物理帧。" << std::endl;
    return true;
}

// 原始的老 API (按页号访问)
bool MemoryManager::access_page(int pid, int logical_page) {
    return access_addr(pid, logical_page * PAGE_SIZE);
}

// 核心的 32位 虚拟地址 MMU 访问
bool MemoryManager::access_addr(int pid, uint32_t logical_addr) {
    std::lock_guard<std::mutex> lock(mem_mutex);
    stat_memory_accesses++;

    uint32_t logical_page = logical_addr >> 12; // 提取高20位作为页号

    auto rec_it = process_memory_map.find(pid);
    if (rec_it == process_memory_map.end() ||
        logical_page >= static_cast<uint32_t>(rec_it->second.total_logical_pages)) {
        stat_segment_faults++;
        std::cout << "[段错误] 进程 " << pid << " 发生越界内存访问！" << std::endl;
        return false;
    }

    // 1. TLB 快表查找
    int frame = tlb.lookup(pid, logical_page);
    if (frame != -1) {
        stat_page_hits++;
        touch_frame_on_hit(frame);
        return true;
    }

    // 2. 查多级页表
    PTE* pte = walk_page_table(pid, logical_page, false);
    if (pte == nullptr) {
        stat_segment_faults++;
        std::cout << "[段错误] 进程 " << pid << " 发生越界内存访问！" << std::endl;
        return false;
    }

    if (pte->valid) {
        stat_page_hits++;
        tlb.insert(pid, logical_page, pte->physical_page); // 填充 TLB
        touch_frame_on_hit(pte->physical_page);
        std::cout << "[内存命中] 进程 " << pid << " 访问虚拟页 " << logical_page
            << " -> 物理帧 " << pte->physical_page << std::endl;
        return true;
    }

    // 3. 发生缺页中断 (Page Fault)
    stat_page_faults++;
    std::cout << ">>> [缺页中断] 进程 " << pid << " 虚拟页 " << logical_page << " 不在内存中，触发调盘..." << std::endl;

    bool success = handle_page_fault(pid, logical_page);
    if (success) {
        tlb.insert(pid, logical_page, walk_page_table(pid, logical_page)->physical_page);
    }
    return success;
}

bool MemoryManager::handle_page_fault(int pid, uint32_t logical_page) {
    auto rec_it = process_memory_map.find(pid);
    if (rec_it == process_memory_map.end() ||
        logical_page >= static_cast<uint32_t>(rec_it->second.total_logical_pages)) {
        stat_segment_faults++;
        std::cout << "[段错误] 进程 " << pid << " 发生越界内存访问！" << std::endl;
        return false;
    }

    PTE* pte = walk_page_table(pid, logical_page);
    if (pte == nullptr) return false;

    int target_frame = alloc_free_frame();

    if (target_frame == -1) {
        target_frame = select_victim_frame();
        if (target_frame == -1) {
            std::cout << "    -> [致命] 无法选择牺牲页。" << std::endl;
            return false;
        }

        auto fp_it = frame_mappings.find(target_frame);
        size_t victim_count = (fp_it != frame_mappings.end()) ? fp_it->second.size() : 0;
        std::cout << "    -> [页面置换] 内存已满！物理帧 " << target_frame << " 将被换出（影响 "
            << victim_count << " 个映射）。" << std::endl;

        if (!evict_frame_to_disk(target_frame)) {
            return false;
        }

        frame_tracker[target_frame].is_free = true;
        frame_tracker[target_frame].pid = -1;
        frame_tracker[target_frame].logical_page = 0;
        lru_list.remove(target_frame);
        ref_bit[target_frame] = false;
    }

    if (pte->swap_block_id >= 0) {
        ensure_frame_buffer(target_frame);
        if (!get_disk_manager().read_data_block(pte->swap_block_id, reinterpret_cast<char*>(frame_pages[target_frame]))) {
            std::cout << "    -> [Swap In] 从磁盘块 " << pte->swap_block_id << " 读入失败。" << std::endl;
            return false;
        }
        std::cout << "    -> [Swap In] 已从磁盘块 " << pte->swap_block_id << " 装入物理帧 " << target_frame << std::endl;
    }
    else {
        ensure_frame_buffer(target_frame);
        std::memset(frame_pages[target_frame], 0, PAGE_SIZE);
    }

    frame_tracker[target_frame].is_free = false;
    frame_tracker[target_frame].pid = pid;
    frame_tracker[target_frame].logical_page = logical_page;

    pte->physical_page = target_frame;
    pte->valid = true;

    frame_mappings[target_frame].push_back({ pid, logical_page });
    push_loaded_frame_tracking(target_frame);

    std::cout << "    -> [装入内存] 进程 " << pid << " 虚拟页 " << logical_page
        << " 已映射到物理帧 " << target_frame << std::endl;
    return true;
}

std::vector<int> MemoryManager::get_memory_bitmap() {
    std::lock_guard<std::mutex> lock(mem_mutex);
    std::vector<int> bitmap(TOTAL_PAGES, -1);
    for (int i = 0; i < TOTAL_PAGES; i++) {
        if (!frame_tracker[i].is_free) {
            bitmap[i] = frame_tracker[i].pid;
        }
    }
    return bitmap;
}