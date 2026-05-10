// os_sim_main.cpp - OS Simulator Web Kernel Entry
// 修订版：移除开机自动格式化逻辑，确保文件持久化。
// 增加进程管理扩展：fork, wait, exit, ptree, 僵尸进程回收与进程树结构。

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <map>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "httplib.h"
#include "json.hpp"

#include "memory/memory.h"
#include "process/program.h"
#include "process/device.h"
#include "process/ipc.h"
#include "filesystem.h"
#include <csignal> // 用于捕捉 Ctrl+C

using json = nlohmann::json;

int nowTime = 0;
bool is_os_running = true;
bool is_booted = false;
std::mutex kernel_mutex;

namespace
{

    void set_cors(httplib::Response &res)
    {
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
        res.set_header("Access-Control-Allow-Headers", "Content-Type");
    }

    void serve_html_file(httplib::Response &res, const std::string &filename)
    {
        set_cors(res);
        std::ifstream in(filename, std::ios::binary);
        if (!in)
        {
            res.status = 404;
            res.set_content(filename + " not found", "text/plain; charset=utf-8");
            return;
        }
        std::ostringstream ss;
        ss << in.rdbuf();
        res.set_content(ss.str(), "text/html; charset=utf-8");
    }

    std::string trim(const std::string &s)
    {
        size_t b = 0, e = s.size();
        while (b < e && std::isspace(static_cast<unsigned char>(s[b])))
            ++b;
        while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1])))
            --e;
        return s.substr(b, e - b);
    }

    std::string lower_copy(std::string s)
    {
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c)
                       { return static_cast<char>(std::tolower(c)); });
        return s;
    }

    std::vector<std::string> split_cmd(const std::string &str)
    {
        std::vector<std::string> tokens;
        std::string cur;
        bool in_quote = false;
        char quote_char = '\0';
        bool escaping = false;

        for (char ch : str)
        {
            if (escaping)
            {
                cur.push_back(ch);
                escaping = false;
                continue;
            }
            if (ch == '\\')
            {
                escaping = true;
                continue;
            }
            if (in_quote)
            {
                if (ch == quote_char)
                {
                    in_quote = false;
                    quote_char = '\0';
                }
                else
                {
                    cur.push_back(ch);
                }
                continue;
            }
            if (ch == '\'' || ch == '"')
            {
                in_quote = true;
                quote_char = ch;
                continue;
            }
            if (std::isspace(static_cast<unsigned char>(ch)))
            {
                if (!cur.empty())
                {
                    tokens.push_back(cur);
                    cur.clear();
                }
            }
            else
            {
                cur.push_back(ch);
            }
        }
        if (!cur.empty())
            tokens.push_back(cur);
        return tokens;
    }

    std::string join_tokens(const std::vector<std::string> &args, size_t begin, const std::string &sep = " ")
    {
        std::ostringstream oss;
        for (size_t i = begin; i < args.size(); ++i)
        {
            if (i > begin)
                oss << sep;
            oss << args[i];
        }
        return oss.str();
    }

    int parse_int(const std::string &s)
    {
        size_t idx = 0;
        int base = 10;
        if (s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))
            base = 16;
        int v = std::stoi(s, &idx, base);
        if (idx != s.size())
            throw std::invalid_argument("invalid integer");
        return v;
    }

    uint32_t parse_u32(const std::string &s)
    {
        size_t idx = 0;
        int base = 10;
        if (s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))
            base = 16;
        unsigned long v = std::stoul(s, &idx, base);
        if (idx != s.size())
            throw std::invalid_argument("invalid uint32");
        return static_cast<uint32_t>(v);
    }

    std::string capture_stdout(const std::function<void()> &fn)
    {
        std::stringstream buffer;
        std::streambuf *old_cout = std::cout.rdbuf(buffer.rdbuf());
        try
        {
            fn();
        }
        catch (...)
        {
            std::cout.rdbuf(old_cout);
            throw;
        }
        std::cout.rdbuf(old_cout);
        return buffer.str();
    }

    std::string capture_stdout_stderr(const std::function<void()> &fn)
    {
        std::stringstream buffer;
        std::streambuf *old_cout = std::cout.rdbuf(buffer.rdbuf());
        std::streambuf *old_cerr = std::cerr.rdbuf(buffer.rdbuf());
        try
        {
            fn();
        }
        catch (...)
        {
            std::cout.rdbuf(old_cout);
            std::cerr.rdbuf(old_cerr);
            throw;
        }
        std::cout.rdbuf(old_cout);
        std::cerr.rdbuf(old_cerr);
        return buffer.str();
    }

// ================= 真正的文件压缩引擎 (LZW 算法) =================
    std::string compress_lzw(const std::string& uncompressed) {
        if (uncompressed.empty()) return "";
        std::map<std::string, int> dict;
        for (int i = 0; i < 256; i++) dict[std::string(1, static_cast<char>(i))] = i;
        
        std::string w;
        std::vector<int> compressed;
        for (char c : uncompressed) {
            std::string wc = w + c;
            if (dict.count(wc)) {
                w = wc;
            } else {
                compressed.push_back(dict[w]);
                dict[wc] = dict.size();
                w = std::string(1, c);
            }
        }
        if (!w.empty()) compressed.push_back(dict[w]);
        
        // 序列化为逗号分隔的数字，防止特殊二进制字符破坏前端 JSON 传输
        std::string out;
        for (int code : compressed) out += std::to_string(code) + ",";
        return out;
    }

    std::string decompress_lzw(const std::string& compressed_str) {
        if (compressed_str.empty()) return "";
        std::vector<int> compressed;
        std::stringstream ss(compressed_str);
        std::string token;
        while (std::getline(ss, token, ',')) {
            if(!token.empty()) compressed.push_back(std::stoi(token));
        }
        if (compressed.empty()) return "";

        std::map<int, std::string> dict;
        for (int i = 0; i < 256; i++) dict[i] = std::string(1, static_cast<char>(i));
        
        std::string w(1, static_cast<char>(compressed[0]));
        std::string result = w;
        for (size_t i = 1; i < compressed.size(); i++) {
            int k = compressed[i];
            std::string entry;
            if (dict.count(k)) {
                entry = dict[k];
            } else if (k == dict.size()) {
                entry = w + w[0];
            } else {
                return ""; // 数据损坏
            }
            result += entry;
            dict[dict.size()] = w + entry[0];
            w = entry;
        }
        return result;
    }
    // ==================================================================

// ================= 管道解析器 =================
    // 智能切分带有管道符的命令，但会忽略引号内的 '|'
    std::vector<std::string> split_pipeline(const std::string& str) {
        std::vector<std::string> commands;
        std::string cur;
        bool in_quote = false;
        char quote_char = '\0';
        bool escaping = false;

        for (char ch : str) {
            if (escaping) {
                cur.push_back(ch);
                escaping = false;
                continue;
            }
            if (ch == '\\') {
                escaping = true;
                cur.push_back(ch);
                continue;
            }
            if (in_quote) {
                if (ch == quote_char) {
                    in_quote = false;
                    quote_char = '\0';
                }
                cur.push_back(ch);
                continue;
            }
            if (ch == '\'' || ch == '"') {
                in_quote = true;
                quote_char = ch;
                cur.push_back(ch);
                continue;
            }
            if (ch == '|') {
                if (!trim(cur).empty()) commands.push_back(trim(cur));
                cur.clear();
                continue;
            }
            cur.push_back(ch);
        }
        if (!trim(cur).empty()) commands.push_back(trim(cur));
        return commands;
    }
    // ==============================================

    json pcb_to_json(const PCB &pcb)
    {
        json p;
        p["pid"] = pcb.PID;
        p["name"] = pcb.name;
        p["state"] = stateToString(pcb.state);
        p["stateCode"] = pcb.state;
        p["priority"] = pcb.priority;
        p["dynamicPriority"] = pcb.dynamicPriority;
        p["timeSlice"] = pcb.timeSlice;
        p["currentSlice"] = pcb.currentSlice;
        p["size"] = pcb.size;
        p["arriveTime"] = pcb.arriveTime;
        p["needTime"] = pcb.needTime;
        p["remainTime"] = pcb.remainTime;
        p["runTime"] = pcb.runTime;
        p["waitTime"] = pcb.waitTime;
        p["blockTime"] = pcb.blockTime;
        p["firstRunTime"] = pcb.firstRunTime;
        p["finishTime"] = pcb.finishTime;
        p["turnaroundTime"] = pcb.turnaroundTime;
        p["weightedTurnaround"] = pcb.weightedTurnaround;
        p["contextSwitches"] = pcb.contextSwitches;
        p["cpuBursts"] = pcb.cpuBursts;
        p["blockRemain"] = pcb.blockRemain;
        p["blockReason"] = pcb.blockReason;
        p["admitted"] = pcb.admitted;
        p["parentPID"] = pcb.parentPID; // 暴露父进程PID以便前端展示
        return p;
    }

    json process_summary_json()
    {
        json s;
        s["finishedCount"] = static_cast<int>(endVector.size());
        double avgTurn = 0.0, avgWTurn = 0.0, avgResp = 0.0;
        for (const auto &p : endVector)
        {
            avgTurn += p.turnaroundTime;
            avgWTurn += p.weightedTurnaround;
            if (p.firstRunTime >= 0)
                avgResp += (p.firstRunTime - p.arriveTime);
        }
        if (!endVector.empty())
        {
            avgTurn /= endVector.size();
            avgWTurn /= endVector.size();
            avgResp /= endVector.size();
        }
        s["avgTurnaroundTime"] = avgTurn;
        s["avgWeightedTurnaround"] = avgWTurn;
        s["avgResponseTime"] = avgResp;
        return s;
    }

    std::string replacement_policy_name(ReplacementPolicy p)
    {
        switch (p)
        {
        case ReplacementPolicy::FIFO:
            return "FIFO";
        case ReplacementPolicy::LRU:
            return "LRU";
        case ReplacementPolicy::CLOCK:
            return "CLOCK";
        default:
            return "UNKNOWN";
        }
    }

    ReplacementPolicy parse_policy(const std::string &raw)
    {
        std::string p = lower_copy(raw);
        if (p == "0" || p == "fifo")
            return ReplacementPolicy::FIFO;
        if (p == "1" || p == "lru")
            return ReplacementPolicy::LRU;
        if (p == "2" || p == "clock")
            return ReplacementPolicy::CLOCK;
        throw std::invalid_argument("unknown replacement policy");
    }

    json inode_to_json(const std::string &name, const iNode &inode)
    {
        json j;
        j["name"] = name;
        j["inode"] = inode.i_num;
        j["type"] = (inode.i_mode == 0 ? "Directory" : "File");
        j["mode"] = inode.i_mode;
        j["size"] = inode.i_size;
        j["readonly"] = (inode.is_readonly != 0);
        j["permission"] = inode.is_readonly ? "只读" : "可写";
        j["blocks"] = json::array();
        std::ostringstream loc;
        bool first_block = true;
        for (int b : inode.direct_blocks)
        {
            if (b != -1)
            {
                j["blocks"].push_back(b);
                if (!first_block)
                    loc << ",";
                loc << b;
                first_block = false;
            }
        }
        j["location"] = first_block ? std::string("Disk") : loc.str();
        return j;
    }

    json current_files_json()
    {
        std::string out = capture_stdout([]()
                                         { fs.ls(); });
        std::stringstream ss(out);
        std::string line;
        json arr = json::array();

        while (std::getline(ss, line))
        {
            line = trim(line);
            if (line.empty() || line.find("----") != std::string::npos || line.find("[当前目录内容]") != std::string::npos || line.find("空目录") != std::string::npos)
            {
                continue;
            }

            std::string name;
            if (line.rfind("[目录]", 0) == 0 || line.rfind("[文件]", 0) == 0)
            {
                size_t pos = line.find(']');
                std::string rest = trim(line.substr(pos + 1));
                size_t meta_pos = rest.find(" (大小:");
                name = trim(meta_pos == std::string::npos ? rest : rest.substr(0, meta_pos));
            }
            else
            {
                name = line;
            }

            iNode inode;
            if (!name.empty() && fs.get_file_info(name, inode))
            {
                arr.push_back(inode_to_json(name, inode));
            }
        }
        return arr;
    }

    bool banker_is_safe_silent()
    {
        int resource_types = static_cast<int>(Available.size());
        std::vector<int> work = Available;
        std::map<int, bool> finish;

        for (const auto &pair : Allocation)
            finish[pair.first] = false;

        bool progress = true;
        while (progress)
        {
            progress = false;
            for (const auto &pair : Need)
            {
                int pid = pair.first;
                if (finish.find(pid) == finish.end())
                    finish[pid] = false;
                if (finish[pid])
                    continue;

                bool can_allocate = true;
                for (int j = 0; j < resource_types; ++j)
                {
                    if (j >= static_cast<int>(pair.second.size()) || pair.second[j] > work[j])
                    {
                        can_allocate = false;
                        break;
                    }
                }
                if (can_allocate)
                {
                    auto alloc_it = Allocation.find(pid);
                    if (alloc_it != Allocation.end())
                    {
                        for (int j = 0; j < resource_types && j < static_cast<int>(alloc_it->second.size()); ++j)
                        {
                            work[j] += alloc_it->second[j];
                        }
                    }
                    finish[pid] = true;
                    progress = true;
                }
            }
        }

        for (const auto &pair : finish)
            if (!pair.second)
                return false;
        return true;
    }

    json banker_json()
    {
        json b;
        b["available"] = Available;
        b["max"] = json::object();
        b["allocation"] = json::object();
        b["need"] = json::object();
        for (const auto &kv : Max)
            b["max"][std::to_string(kv.first)] = kv.second;
        for (const auto &kv : Allocation)
            b["allocation"][std::to_string(kv.first)] = kv.second;
        for (const auto &kv : Need)
            b["need"][std::to_string(kv.first)] = kv.second;
        b["safe"] = banker_is_safe_silent();
        return b;
    }

    json status_json()
    {
        rebuildSpecialQueues();

        json j;
        j["booted"] = is_booted;
        j["nowTime"] = nowTime;
        j["currentAlgo"] = getScheduleAlgorithmName();
        j["currentAlgoCode"] = currentScheduleAlgo;

        j["memoryBitmap"] = mem_manager.get_memory_bitmap();
        j["activePhysicalPages"] = mem_manager.get_active_physical_pages();
        j["replacementPolicy"] = replacement_policy_name(mem_manager.get_replacement_policy());
        j["replacementPolicyCode"] = static_cast<int>(mem_manager.get_replacement_policy());
        j["tlb_hits"] = mem_manager.tlb.hits;
        j["tlb_misses"] = mem_manager.tlb.misses;
        j["page_faults"] = mem_manager.stat_page_faults;
        j["memory_accesses"] = mem_manager.stat_memory_accesses;
        j["page_hits"] = mem_manager.stat_page_hits;
        j["segment_faults"] = mem_manager.stat_segment_faults;
        j["tlb"] = {
            {"hits", mem_manager.tlb.hits},
            {"misses", mem_manager.tlb.misses}};
        j["memoryStats"] = {
            {"accesses", mem_manager.stat_memory_accesses},
            {"pageHits", mem_manager.stat_page_hits},
            {"pageFaults", mem_manager.stat_page_faults},
            {"segmentFaults", mem_manager.stat_segment_faults}};

        j["devices"] = json::array();
        for (const auto &dev : sysDevices)
        {
            json d;
            d["id"] = dev.id;
            d["name"] = dev.name;
            d["isBusy"] = dev.isBusy;
            d["currentPID"] = dev.currentPID;
            d["waitQueue"] = dev.waitQueue;
            j["devices"].push_back(d);
        }
        j["banker"] = banker_json();

        j["semaphores"] = json::array();
        for (const auto &kv : ipc_manager.semaphores)
        {
            std::vector<int> wq;
            auto q = kv.second.waitQueue;
            while (!q.empty())
            {
                wq.push_back(q.front());
                q.pop();
            }
            json sem = json::object();
            sem["name"] = kv.first;
            sem["value"] = kv.second.value;
            sem["waitQueue"] = wq;
            j["semaphores"].push_back(sem);
        }
        j["messages"] = json::array();
        for (const auto &kv : ipc_manager.mailboxes)
        {
            json mb = json::object();
            mb["pid"] = kv.first;
            mb["count"] = kv.second.msgs.size();
            j["messages"].push_back(mb);
        }

        if (currentRunningPID != -1 && proMap.find(currentRunningPID) != proMap.end())
        {
            j["runningProcess"] = pcb_to_json(proMap[currentRunningPID]);
        }
        else
        {
            j["runningProcess"] = nullptr;
        }

        j["readyQueue"] = json::array();
        for (const auto &pcb : readVector)
            j["readyQueue"].push_back(pcb_to_json(pcb));

        j["blockQueue"] = json::array();
        for (const auto &pcb : blockVector)
            j["blockQueue"].push_back(pcb_to_json(pcb));

        j["suspendQueue"] = json::array();
        for (const auto &pcb : suspendVector)
            j["suspendQueue"].push_back(pcb_to_json(pcb));

        // 向前端暴露僵尸队列
        j["zombieQueue"] = json::array();
        for (const auto &pcb : zombieVector)
            j["zombieQueue"].push_back(pcb_to_json(pcb));

        j["finishedQueue"] = json::array();
        for (const auto &pcb : endVector)
            j["finishedQueue"].push_back(pcb_to_json(pcb));

        j["processCount"] = proMap.size();
        j["processStats"] = process_summary_json();
        j["cwd"] = current_path;

        return j;
    }



    
    std::string help_text()
    {
        return R"HELP(可用命令：

[进程管理]
  create <name> <needTime> <memBytes> <priority>    创建进程
  fork <pid>                                        为指定进程创建子进程
  wait <pid>                                        让父进程回收已死子进程，若无子进程死则阻塞
  exit <pid> / kill <pid>                           终止进程(有子进程则过继给 PID 1)
  ptree                                             打印系统进程族谱树
  block <pid> [ticks] [reason]                      阻塞进程
  wakeup <pid>                                      唤醒阻塞进程
  suspend <pid>                                     挂起进程并释放内存
  resume <pid>                                      恢复挂起进程
  setsched <0|1|2|3|priority|sjf|fcfs|hrrn>          切换调度算法
  ps / queues                                       查看所有进程队列
  pinfo <pid>                                       查看单个进程详情
  pstat                                             查看调度统计
  resize <pid> <deltaBytes>                         动态扩/缩进程内存

[内存管理]
  memstat                                           查看统计
  memreset                                          重置内存统计
  setmem <FIFO|LRU|CLOCK>                           设置页面置换算法
  access <pid> <logicalAddr>                        模拟访问逻辑地址
  translate <pid> <logicalAddr>                     地址转换
  shm <pid1> <pid2> <pages>                         建立共享内存映射

[文件系统]
  ls                                                列出当前目录
  pwd                                               显示当前路径
  cd <dir|..>                                       切换目录
  touch <file>                                      创建文件
  mkdir <dir>                                       创建目录
  rm <file|emptyDir>                                删除文件或空目录
  cat <file>                                        读取完整文件
  read_at <file> <offset> <len>                     按偏移量读取
  write <file> <content...>                         写入文件内容
  write_at <file> <offset> <content...>             按偏移量写入
  chmod <file> <ro|rw>                              设置属性
  stat <file>                                       查看 inode 信息
  rename <old> <new>                                重命名
  format                                            格式化虚拟磁盘
  tree                                              树状图显示结构
  cp <src> <dst>                                    复制文件
  grep <pattern> <file>                             在文件中搜索
  tar <-c|-x> <archive> [file...]                   打包(-c)或解压(-x)
  vim <file>                                        打开终端编辑器
  echo <text>                                       输出文本 (常配合管道使用)

[设备 / IPC]
  io <pid> <deviceId>                               申请设备
  release <pid>                                     释放设备
  sem_create <name> <initValue>                     创建信号量
  P <name> <pid>                                    P 操作
  V <name>                                          V 操作
  msg_send <fromPid> <toPid> <text...>              发送消息
  msg_read <pid>                                    读取消息
)HELP";
    }

    std::string run_command(const std::string &cmd, const std::string &piped_input = "")
    {
        std::vector<std::string> args = split_cmd(cmd);
        if (args.empty())
            return "空指令";

        std::string action = lower_copy(args[0]);
        std::ostringstream out;

        if (action == "help" || action == "?")
        {
            return help_text();
        }
        // --- [新增] echo 命令，常用于管道头部 ---
        else if (action == "echo")
        {
            if (args.size() >= 2) out << join_tokens(args, 1);
            else out << piped_input;
        }

        // 1. 进程管理。
        else if (action == "create" && args.size() >= 5)
        {
            int pid = createProc(args[1], parse_int(args[2]), parse_int(args[3]), parse_int(args[4]));
            out << "成功创建进程: " << args[1] << " (PID: " << pid << ")";
        }
        else if (action == "fork" && args.size() >= 2)
        {
            int parent_pid = parse_int(args[1]);
            int child_pid = forkProc(parent_pid);
            if (child_pid != -1)
            {
                out << "成功 Fork 进程，父 PID: " << parent_pid << " -> 子 PID: " << child_pid;
            }
            else
            {
                out << "Fork 失败: 找不到父进程 PID " << parent_pid << " 或内存不足已挂起";
            }
        }
        else if (action == "wait" && args.size() >= 2)
        {
            int parent_pid = parse_int(args[1]);
            int res = waitProc(parent_pid);
            if (res > 0)
            {
                out << "Wait 成功，回收僵尸子进程 PID: " << res;
            }
            else if (res == 0)
            {
                out << "没有已死子进程，父进程 PID " << parent_pid << " 已自我阻塞等待";
            }
            else
            {
                out << "Wait 失败: 找不到父进程 PID " << parent_pid << " 或该进程没有子进程";
            }
        }
        else if (action == "ptree")
        {
            out << capture_stdout([]()
                                  { printProcessTree(); });
        }
        else if ((action == "kill" || action == "stop" || action == "exit") && args.size() >= 2)
        {
            int pid = parse_int(args[1]);
            stop(pid);
            out << "已尝试终止/退出进程 PID: " << pid;
        }
        else if (action == "block" && args.size() >= 2)
        {
            int pid = parse_int(args[1]);
            int ticks = args.size() >= 3 ? parse_int(args[2]) : 0;
            std::string reason = args.size() >= 4 ? join_tokens(args, 3) : "手动阻塞";
            out << (block(pid, ticks, reason) ? "阻塞成功: PID " : "阻塞失败: PID ") << pid;
        }
        else if (action == "wakeup" && args.size() >= 2)
        {
            int pid = parse_int(args[1]);
            out << (wakeup(pid) ? "唤醒成功: PID " : "唤醒失败: PID ") << pid;
        }
        else if (action == "suspend" && args.size() >= 2)
        {
            int pid = parse_int(args[1]);
            out << (suspendProc(pid) ? "挂起成功: PID " : "挂起失败: PID ") << pid;
        }
        else if (action == "resume" && args.size() >= 2)
        {
            int pid = parse_int(args[1]);
            out << (resumeProc(pid) ? "恢复成功: PID " : "恢复失败，可能内存不足或 PID 不在挂起队列: PID ") << pid;
        }
        else if (action == "setsched" && args.size() >= 2)
        {
            std::string a = lower_copy(args[1]);
            int algo = 0;
            if (a == "0" || a == "priority" || a == "rr" || a == "priority_rr")
                algo = SCHED_PRIORITY_RR;
            else if (a == "1" || a == "sjf")
                algo = SCHED_SJF;
            else if (a == "2" || a == "fcfs")
                algo = SCHED_FCFS;
            else if (a == "3" || a == "hrrn")
                algo = SCHED_HRRN;
            else
                throw std::invalid_argument("unknown scheduling algorithm");
            setScheduleAlgorithm(algo);
            out << "调度算法已切换为: " << getScheduleAlgorithmName();
        }
        else if (action == "ps" || action == "queues")
        {
            out << capture_stdout([]()
                                  { showQueues(); });
        }
        else if (action == "pinfo" && args.size() >= 2)
        {
            int pid = parse_int(args[1]);
            out << capture_stdout([&]()
                                  { showProcessDetail(pid); });
        }
        else if (action == "pstat")
        {
            out << capture_stdout([]()
                                  { showProcessSummary(); });
        }
        else if (action == "resize" && args.size() >= 3)
        {
            int pid = parse_int(args[1]);
            int delta = parse_int(args[2]);
            out << (dynamic_resize_memory(pid, delta) ? "动态调整内存成功: PID " : "动态调整内存失败: PID ") << pid << ", delta=" << delta;
        }

        // 2. 设备管理 / 银行家算法。
        else if (action == "io" && args.size() >= 3)
        {
            int pid = parse_int(args[1]);
            int dev_id = parse_int(args[2]);
            int result = requestDeviceBanker(pid, dev_id);
            if (result == 1)
                out << "[银行家算法] 安全，已成功分配设备 " << dev_id << " 给 PID " << pid;
            else if (result == 2)
                out << "设备忙，PID " << pid << " 已进入设备等待队列";
            else if (result == 0)
                out << "[安全拦截] 银行家算法检测到死锁风险，驳回分配";
            else
                out << "设备申请失败：PID 或 deviceId 参数无效";
        }
        else if (action == "release" && args.size() >= 2)
        {
            int pid = parse_int(args[1]);
            releaseProcessDevices(pid);
            out << "已释放 PID " << pid << " 占用的全部设备资源";
        }

        // 3. 文件系统。
        else if (action == "touch" && args.size() >= 2)
        {
            int ino = fs.create_file(args[1], false);
            out << (ino >= 0 ? "文件系统: 成功创建文件 " : "文件系统: 创建文件失败 ") << args[1];
        }
        else if (action == "mkdir" && args.size() >= 2)
        {
            int ino = fs.create_file(args[1], true);
            out << (ino >= 0 ? "文件系统: 成功创建目录 " : "文件系统: 创建目录失败 ") << args[1];
        }
        else if ((action == "rm" || action == "rmdir") && args.size() >= 2)
        {
            bool ok = fs.delete_file(args[1]);
            out << (ok ? "文件系统: 删除成功 " : "文件系统: 删除失败 ") << args[1];
        }
        // --- 修改：支持管道的 cat ---
        else if (action == "cat")
        {
            if (args.size() >= 2) {
                // 读取文件并去掉了干扰管道的 "文件内容:\n" 提示
                out << fs.read_file(args[1]); 
            } else {
                out << piped_input;
            }
        }
        else if (action == "read_at" && args.size() >= 4)
        {
            int offset = parse_int(args[2]);
            int len = parse_int(args[3]);
            out << "文件片段 [offset=" << offset << ", len=" << len << "]:\n"
                << fs.read_file(args[1], offset, len);
        }
        // --- 修改：支持管道的 write ---
        else if (action == "write" && (args.size() >= 3 || (args.size() == 2 && !piped_input.empty())))
        {
            std::string content = args.size() >= 3 ? join_tokens(args, 2) : piped_input;
            if (content.size() >= 2 && content.front() == '"' && content.back() == '"')
            {
                content = content.substr(1, content.size() - 2);
            }
            int bytes = fs.write_file(args[1], content, 0);
            out << (bytes >= 0 ? "文件系统: 写入成功，字节数=" : "文件系统: 写入失败，返回值=") << bytes;
        }
        else if (action == "write_at" && args.size() >= 4)
        {
            int offset = parse_int(args[2]);
            std::string content = args.size() == 4 ? args[3] : join_tokens(args, 3);
            if (content.size() >= 2 && content.front() == '"' && content.back() == '"')
            {
                content = content.substr(1, content.size() - 2);
            }
            int bytes = fs.write_file(args[1], content, offset);
            out << (bytes >= 0 ? "文件系统: 偏移写入成功，字节数=" : "文件系统: 偏移写入失败，返回值=") << bytes;
        }
        else if (action == "chmod" && args.size() >= 3)
        {
            std::string mode = lower_copy(args[2]);
            bool readonly;
            if (mode == "ro" || mode == "readonly" || mode == "read-only" || mode == "1")
                readonly = true;
            else if (mode == "rw" || mode == "write" || mode == "writable" || mode == "0")
                readonly = false;
            else
                throw std::invalid_argument("chmod mode must be ro or rw");
            bool ok = fs.set_file_permission(args[1], readonly);
            out << (ok ? "文件系统: 权限修改成功 " : "文件系统: 权限修改失败 ") << args[1] << " -> " << (readonly ? "只读" : "可写");
        }
        else if (action == "stat" && args.size() >= 2)
        {
            iNode inode;
            if (fs.get_file_info(args[1], inode))
            {
                out << "文件/目录信息:\n";
                out << "  name: " << args[1] << "\n";
                out << "  inode: " << inode.i_num << "\n";
                out << "  type: " << (inode.i_mode == 0 ? "目录" : "普通文件") << "\n";
                out << "  size: " << inode.i_size << " bytes\n";
                out << "  permission: " << (inode.is_readonly ? "只读" : "可写") << "\n";
                out << "  direct_blocks:";
                bool any = false;
                for (int b : inode.direct_blocks)
                {
                    if (b != -1)
                    {
                        out << ' ' << b;
                        any = true;
                    }
                }
                if (!any)
                    out << " (none)";
            }
            else
            {
                out << "文件系统: stat 失败，文件不存在: " << args[1];
            }
        }
        else if (action == "rename" && args.size() >= 3)
        {
            bool ok = fs.rename(args[1], args[2]);
            out << (ok ? "文件系统: 重命名成功 " : "文件系统: 重命名失败 ") << args[1] << " -> " << args[2];
        }
        else if (action == "format")
        {
            fs.format();
            out << "文件系统: 已格式化虚拟磁盘";
        }
        else if (action == "cd" && args.size() >= 2)
        {
            bool ok = fs.cd(args[1]);
            out << (ok ? "文件系统: 当前路径 " : "文件系统: 目录切换失败，当前路径 ") << current_path;
        }
        else if (action == "tree")
        {
            out << "当前目录树:\n";
            out << capture_stdout([]()
                                  { fs.tree(); });
        }
        else if (action == "cp" && args.size() >= 3)
        {
            std::string src = args[1], dst = args[2];
            iNode src_inode;
            if (!fs.get_file_info(src, src_inode) || src_inode.i_mode != 1)
            {
                out << "cp 失败: 源文件不存在或是目录";
            }
            else
            {
                std::string content = fs.read_file(src);
                if (fs.create_file(dst, false) >= 0)
                {
                    fs.write_file(dst, content, 0);
                    out << "cp 成功: " << src << " -> " << dst;
                }
                else
                {
                    out << "cp 失败: 无法创建目标文件 " << dst;
                }
            }
        }
 // --- 修改：支持管道的 grep ---
        else if (action == "grep" && args.size() >= 2)
        {
            std::string pattern = args[1];
            std::string content = "";
            
            // 如果提供了第三个参数，则去文件里搜；否则从管道输入里搜
            if (args.size() >= 3) {
                content = fs.read_file(args[2]);
                if (content.empty()) { out << "grep: 文件为空或不存在"; return out.str(); }
            } else if (!piped_input.empty()) {
                content = piped_input;
            } else {
                out << "grep: 缺少文件参数，且没有管道输入"; return out.str();
            }
            
            std::istringstream iss(content);
            std::string line;
            int line_num = 1;
            bool found = false;
            while (std::getline(iss, line))
            {
                if (line.find(pattern) != std::string::npos)
                {
                    // 从文件读打印行号；从管道读不打印行号
                    if (args.size() >= 3) out << line_num << ": " << line << "\n";
                    else out << line << "\n";
                    found = true;
                }
                line_num++;
            }
            if (!found)
                out << "grep: 未找到匹配项 '" << pattern << "'";
        }
else if (action == "tar" && args.size() >= 3) {
            std::string mode = args[1]; 
            std::string archive = args[2];
            if (mode == "-c" && args.size() >= 4) {
                std::string tar_content = "";
                for (size_t i = 3; i < args.size(); ++i) {
                    iNode inode;
                    if (fs.get_file_info(args[i], inode) && inode.i_mode == 1) {
                        std::string c = fs.read_file(args[i]);
                        tar_content += args[i] + "\n" + std::to_string(c.size()) + "\n" + c + "\n";
                    }
                }
                
                // ★ 真正的压缩在这里执行
                std::string compressed_data = compress_lzw(tar_content);
                
                if (fs.create_file(archive, false) >= 0) {
                    fs.write_file(archive, compressed_data, 0);
                    
                    // 计算理论上的二进制真实压缩率
                    int original_size = tar_content.empty() ? 1 : tar_content.size();
                    // 数一下压缩数据里有多少个逗号，就知道有多少个有效编码
                    int code_count = std::count(compressed_data.begin(), compressed_data.end(), ',');
                    int real_binary_size = code_count * 2; // 真实环境 LZW 每个编码约占 2 字节
                    
                    int ratio = 100 - (real_binary_size * 100 / original_size);
                    
                    if (ratio >= 0) {
                        out << "tar: 成功打包并压缩文件到 " << archive << " (真实体积减少了 " << ratio << "%)，好耶！";
                    } else {
                        out << "tar: 成功打包到 " << archive << " (体积反向膨胀了 " << -ratio << "%，因为文件实在太小了！)";
                    }
                } else out << "tar 失败: 无法创建 " << archive;
                
            } else if (mode == "-x") {
                std::string raw_data = fs.read_file(archive);
                if (raw_data.empty()) out << "tar: 归档文件为空或不存在";
                else {
                    // ★ 解压数据恢复原始打包状态
                    std::string c = decompress_lzw(raw_data);
                    
                    std::istringstream iss(c);
                    std::string fname, slen;
                    while (std::getline(iss, fname) && std::getline(iss, slen)) {
                        int len = std::stoi(slen);
                        std::string fcontent(len, '\0');
                        iss.read(&fcontent[0], len);
                        std::string empty; std::getline(iss, empty); 
                        if (fs.create_file(fname, false) >= 0) {
                            fs.write_file(fname, fcontent, 0);
                            out << "解压: " << fname << "\n";
                        }
                    }
                    out << "tar: 解压完毕";
                }
            } else {
                out << "用法: tar -c <包名> <文件1> ... 或 tar -x <包名>";
            }
        }

        else if (action == "vim" && args.size() >= 2)
        {
            iNode tmp_inode;
            if (!fs.get_file_info(args[1], tmp_inode))
            {
                fs.create_file(args[1], false);
            }
            out << "[VIM_TRIGGER] " << args[1];
        }
        else if (action == "pwd")
        {
            out << "当前路径: " << current_path;
        }
        else if (action == "ls")
        {
            out << capture_stdout([]()
                                  { fs.ls(); });
        }

        // 4. 内存管理。
        else if (action == "memstat")
        {
            out << capture_stdout([]()
                                  { mem_manager.print_memory_statistics(); });
        }
        else if (action == "memreset")
        {
            mem_manager.reset_memory_statistics();
            out << "内存统计已重置";
        }
        else if (action == "setmem" && args.size() >= 2)
        {
            ReplacementPolicy p = parse_policy(args[1]);
            mem_manager.set_replacement_policy(p);
            out << "页面置换算法已切换为: " << replacement_policy_name(p);
        }
        else if (action == "access" && args.size() >= 3)
        {
            int pid = parse_int(args[1]);
            uint32_t addr = parse_u32(args[2]);
            bool ok = mem_manager.access_addr(pid, addr);
            out << (ok ? "地址访问成功: " : "地址访问失败/段错误: ") << "PID=" << pid << ", logicalAddr=0x" << std::hex << addr << std::dec;
        }
        else if (action == "translate" && args.size() >= 3)
        {
            int pid = parse_int(args[1]);
            uint32_t addr = parse_u32(args[2]);
            int frame = -1;
            size_t offset = 0;
            bool ok = mem_manager.translate(pid, addr, frame, offset);
            if (ok)
            {
                out << "地址转换成功: PID=" << pid << ", logicalAddr=0x" << std::hex << addr << std::dec
                    << ", physicalFrame=" << frame << ", offset=" << offset;
            }
            else
            {
                out << "地址转换失败: PID=" << pid << ", logicalAddr=0x" << std::hex << addr << std::dec;
            }
        }
        else if (action == "shm" && args.size() >= 4)
        {
            int p1 = parse_int(args[1]);
            int p2 = parse_int(args[2]);
            int pages = parse_int(args[3]);
            bool ok = mem_manager.share_mem(p1, p2, pages);
            out << (ok ? "共享内存建立成功: " : "共享内存失败: ") << "PID " << p1 << " <-> PID " << p2 << ", pages=" << pages;
        }

        // 5. IPC。
        else if (action == "sem_create" && args.size() >= 3)
        {
            bool ok = ipc_manager.create_semaphore(args[1], parse_int(args[2]));
            out << (ok ? "创建信号量成功: " : "创建信号量失败，可能已存在: ") << args[1];
        }
        else if ((action == "p" || action == "wait_sem") && args.size() >= 3)
        {
            int pid = parse_int(args[2]);
            int res = ipc_manager.sem_P(args[1], pid);
            if (res == 1)
                out << "P(wait) 成功，PID " << pid << " 获得资源继续执行";
            else if (res == 0)
                out << "资源不足，PID " << pid << " 已阻塞等待信号量 " << args[1];
            else if (res == -2)
                out << "P(wait) 失败：PID " << pid << " 不存在或已结束";
            else
                out << "P(wait) 失败：信号量不存在";
        }
        else if ((action == "v" || action == "signal") && args.size() >= 2)
        {
            int res = ipc_manager.sem_V(args[1]);
            if (res > 0)
                out << "V(signal) 成功，释放资源并唤醒 PID " << res;
            else if (res == 0)
                out << "V(signal) 成功，当前无进程等待";
            else
                out << "V(signal) 失败：信号量不存在";
        }
        else if (action == "msg_send" && args.size() >= 4)
        {
            int from = parse_int(args[1]);
            int to = parse_int(args[2]);
            std::string text = join_tokens(args, 3);
            bool ok = ipc_manager.send_message(from, to, text);
            out << (ok ? "消息发送成功: " : "消息发送失败，目标进程不存在: ") << "P" << from << " -> P" << to;
        }
        else if (action == "msg_read" && args.size() >= 2)
        {
            int pid = parse_int(args[1]);
            std::string msg = ipc_manager.read_message(pid);
            if (msg.empty())
                out << "P" << pid << " 的信箱为空";
            else
                out << "P" << pid << " 读取消息: " << msg;
        }
        else
        {
            out << "未知或参数错误的指令: " << args[0] << "。输入 help 查看可用命令。";
        }

        return out.str();
    }

    void boot_if_needed()
    {
        if (is_booted)
            return;
        createProc("idle", 999999, 1, 1);
        createProc("init", 999999, 2048, 20);
        // ★ 修改：移除自动格式化，确保每次重启保留之前的磁盘数据
        is_booted = true;
    }

} // namespace

void system_tick()
{
    while (is_os_running)
    {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        std::lock_guard<std::mutex> lock(kernel_mutex);
        if (is_booted)
        {
            nowTime++;
            run();
        }
    }
}

// ★ 全局 HTTP 服务器指针，用于在信号中断时优雅关闭
httplib::Server *global_svr = nullptr;

// ★ 信号捕捉处理函数
void handle_signal(int sig)
{
    std::cout << "\n\n[OS Kernel] 接收到系统终止信号 (" << sig << ")，正在执行安全关机程序 (Graceful Shutdown)..." << std::endl;
    is_os_running = false;
    if (global_svr)
    {
        global_svr->stop(); // 解除 svr.listen() 的阻塞
    }
}

void start_http_server()
{
    httplib::Server svr;

    global_svr = &svr; // 将实例赋给全局指针

    svr.Options(R"(.*)", [&](const httplib::Request &, httplib::Response &res)
                { set_cors(res); });

    svr.Get("/", [&](const httplib::Request &, httplib::Response &res)
            { serve_html_file(res, "vimplus.html"); });

    svr.Get("/index.html", [&](const httplib::Request &, httplib::Response &res)
            { serve_html_file(res, "index.html"); });

    svr.Get("/vimplus.html", [&](const httplib::Request &, httplib::Response &res)
            { serve_html_file(res, "vimplus.html"); });

    svr.Post("/api/boot", [&](const httplib::Request &, httplib::Response &res)
             {
        std::lock_guard<std::mutex> lock(kernel_mutex);
        set_cors(res);
        json response_json;
        response_json["status"] = "success";

        bool first_boot = !is_booted;
        boot_if_needed();

        response_json["logs"] = {
            "[0.000000] Booting OS Simulator Kernel v1.0.0...",
            "[0.012030] Power-On Self-Test (POST) ... [OK]",
            "[0.034010] Initializing Memory Management Unit (MMU) ... [OK]",
            "[0.051200] Scanning Physical RAM: 32 Pages ... [OK]",
            "[0.082300] Initializing Disk Subsystem ... [OK]",
            "[0.104500] Mounting virtual disk 'vdisk.bin' ... [OK]",
            "[0.125000] Checking Superblock & Inode Bitmap ... [OK]",
            "[0.156000] Loading File System drivers ... [OK]",
            "[0.180000] Initializing Process Manager ... [OK]",
            "[0.201000] Starting CPU Scheduler (Algorithm: Priority + RR) ... [OK]",
            "[0.223000] Creating [idle] process ... [OK]",
            "[0.254000] Creating [init] process ... [OK]",
            "[0.280000] Initializing I/O Controllers (Printer, Keyboard, Disk) ... [OK]",
            "[0.295000] Initializing IPC (Semaphores & Mailboxes) ... [OK]",
            "[0.310000] Starting system clock tick ... [OK]",
            first_boot ? "[0.350000] System boot successful." : "[0.350000] System already booted."
        };
        response_json["statusSnapshot"] = status_json();
        res.set_content(response_json.dump(), "application/json; charset=utf-8"); });

    svr.Get("/api/status", [&](const httplib::Request &, httplib::Response &res)
            {
        std::lock_guard<std::mutex> lock(kernel_mutex);
        set_cors(res);
        res.set_content(status_json().dump(), "application/json; charset=utf-8"); });

    svr.Get("/api/files", [&](const httplib::Request &, httplib::Response &res)
            {
        std::lock_guard<std::mutex> lock(kernel_mutex);
        set_cors(res);
        res.set_content(current_files_json().dump(), "application/json; charset=utf-8"); });

    svr.Get(R"(/api/file/(.+))", [&](const httplib::Request &req, httplib::Response &res)
            {
        std::lock_guard<std::mutex> lock(kernel_mutex);
        set_cors(res);
        std::string name = httplib::decode_path_component(req.matches[1].str());
        iNode inode;
        if (!fs.get_file_info(name, inode)) {
            res.status = 404;
            res.set_content(json({ {"status", "error"}, {"msg", "file not found"} }).dump(), "application/json; charset=utf-8");
            return;
        }
        json j = inode_to_json(name, inode);
        j["status"] = "success";
        if (inode.i_mode == 1) j["content"] = fs.read_file(name);
        res.set_content(j.dump(), "application/json; charset=utf-8"); });

    svr.Post("/api/command", [&](const httplib::Request &req, httplib::Response &res)
             {
        std::lock_guard<std::mutex> lock(kernel_mutex);
        set_cors(res);
        try {
            json body = json::parse(req.body);
            std::string cmd = body.value("command", "");
            if (trim(cmd).empty()) {
                res.set_content(json({ {"status", "error"}, {"msg", "空指令"} }).dump(), "application/json; charset=utf-8");
                return;
            }
            if (!is_booted) boot_if_needed();

            std::string output_msg = capture_stdout_stderr([&]() {
                // 1. 切分管道命令 (例如 "cat 1.txt | grep a" 变成 ["cat 1.txt", "grep a"])
                std::vector<std::string> pipeline = split_pipeline(cmd);
                std::string current_input = "";
                
                // 2. 像接力赛一样，把上一个输出当成下一个输入
                for (size_t i = 0; i < pipeline.size(); ++i) {
                    if (pipeline[i].empty()) continue;
                    // 执行单条命令，并将返回值赋给 current_input
                    current_input = run_command(pipeline[i], current_input);
                }
                
                // 3. 把最终的结果打印给前端控制台
                std::cout << current_input;
            });


            json response_json = {
                {"status", "success"},
                {"msg", output_msg},
                {"statusSnapshot", status_json()}
            };
            res.set_content(response_json.dump(), "application/json; charset=utf-8");
        }
        catch (const std::exception& e) {
            res.status = 400;
            res.set_content(json({ {"status", "error"}, {"msg", std::string("解析或执行错误: ") + e.what()} }).dump(), "application/json; charset=utf-8");
        } });

    std::cout << "[OS Kernel] 内核服务已就绪。等待开机指令..." << std::endl;
    std::cout << "监听端口: 8080" << std::endl;
    svr.listen("0.0.0.0", 8080);

    global_svr = nullptr; // 服务器停止后置空
}

int main()
{
    // 注册信号监听 (Ctrl+C 和 `kill` 终止)
    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);

    std::thread clock_thread(system_tick);

    // 启动 Web 服务 (会阻塞在这里，直到接收到 HTTP stop 或 Ctrl+C)
    start_http_server();

    // 到这里说明服务已停止，开始清理资源
    is_os_running = false;
    if (clock_thread.joinable())
    {
        clock_thread.join();
    }

    std::cout << "[OS Kernel] 正在强制刷新磁盘数据 (Syncing Disk)..." << std::endl;
    // 通知底层磁盘模块强制落盘，防止数据丢失
    get_disk_manager().sync_disk();

    std::cout << "[OS Kernel] 所有子系统已安全关闭。OS 停机完成。" << std::endl;
    return 0;
}