# OS_SIM_CPP 操作系统课程设计

OS_SIM_CPP 是一个用 C++ 实现的操作系统原型模拟器，面向操作系统课程设计验收。项目通过 Web 前端、HTTP API 和命令解释器，把进程管理、内存管理、文件系统、磁盘持久化、设备管理和 IPC 组合成一个可演示的小型 OS 模拟环境。

## 功能概览

- 进程管理：PCB、进程创建/终止、阻塞/唤醒、挂起/恢复、父子进程、僵尸进程回收、进程树、队列状态展示。
- 调度算法：动态优先级 + 时间片轮转、SJF、FCFS、HRRN，可运行时切换。
- 内存管理：分页式虚拟内存、页表、TLB、缺页中断、FIFO/LRU/CLOCK 页面置换、共享内存、动态扩缩容。
- 文件系统：基于虚拟磁盘 `vdisk.bin` 的 inode 文件系统，支持目录、文件、读写、偏移读写、权限、重命名、复制、搜索和打包。
- 磁盘管理：块设备模拟、inode 位图、数据块位图、数据块读写、格式化和持久化。
- 设备与 IPC：I/O 设备申请与释放、银行家算法安全检查、信号量 P/V 操作、进程消息邮箱。
- 前端交互：启动动画、状态面板、命令行、文件查看、Vim 风格编辑入口、页面功能和视觉优化。

## 目录结构

```text
.
├── os_sim_main.cpp          # 集成主程序，HTTP 服务和命令分发
├── index.html               # Web 前端页面
├── vimplus.html             # Vim 风格编辑页面
├── process/                 # 进程、设备、IPC 模块
├── memory/                  # 内存管理/MMU 模块
├── disk.cpp / disk.h        # 虚拟磁盘模块
├── filesystem.cpp / .h      # 文件系统模块
├── test_fs.cpp              # 文件系统和磁盘测试
├── test_disk_memory.cpp     # 磁盘和内存集成测试
├── test_os_pressure.py      # HTTP 并发压力测试脚本
├── os_memory_config.txt     # 内存模块配置
└── docs/                    # 课程验收文档
```

## 环境要求

- C++ 编译器：支持 C++14，推荐 Linux g++ 或 Cygwin/MinGW g++。
- 线程库：需要 `pthread`。
- 浏览器：用于访问 Web 前端。
- Python 3：仅在运行 `test_os_pressure.py` 压力测试时需要。

## 编译

主程序：

```bash
g++ -O2 -std=c++14 -D_DEFAULT_SOURCE \
    os_sim_main.cpp \
    memory/memory.cpp \
    process/program.cpp \
    process/device.cpp \
    process/ipc.cpp \
    filesystem.cpp \
    disk.cpp \
    -o os_simulator \
    -pthread
```

文件系统测试：

```bash
g++ -O2 -std=c++11 test_fs.cpp filesystem.cpp disk.cpp -o run_fs_test -pthread
```

磁盘和内存测试：

```bash
g++ -O2 -std=c++11 \
    disk.cpp \
    memory/memory.cpp \
    test_disk_memory.cpp \
    -o os_sim_test_disk_memory \
    -pthread
```

说明：在部分 Cygwin 环境中，`httplib.h` 需要 `-D_DEFAULT_SOURCE` 才能正确暴露 socket/addrinfo 相关声明。

## 运行

```bash
./os_simulator
```

程序启动后监听：

```text
http://127.0.0.1:8080/
```

浏览器打开页面后点击开机，或直接通过命令接口执行系统命令。

## 常用命令

进程管理：

```text
create P1 8 4096 10
create P2 6 2048 6
setsched priority
ps
queues
block 3 3 io_wait
wakeup 3
suspend 4
resume 4
fork 2
wait 2
kill 3
ptree
pstat
resize 2 4096
```

内存管理：

```text
memstat
setmem FIFO
setmem LRU
setmem CLOCK
access 2 0x1000
translate 2 0x1000
shm 2 3 1
memreset
```

文件系统：

```text
pwd
ls
mkdir docs
cd docs
touch readme.txt
write readme.txt hello_os
cat readme.txt
read_at readme.txt 0 5
write_at readme.txt 6 simulator
chmod readme.txt ro
stat readme.txt
chmod readme.txt rw
rename readme.txt note.txt
tree
vim note.txt
```

设备与 IPC：

```text
io 2 0
release 2
sem_create mutex 1
P mutex 2
V mutex
msg_send 2 3 hello
msg_read 3
```

## 测试

### 分模块测试（每人负责模块独立验证）

进程管理测试（徐舸山）：

```bash
g++ -O2 -std=c++14 -D_DEFAULT_SOURCE ^
    test_process.cpp memory/memory.cpp process/program.cpp ^
    process/device.cpp process/ipc.cpp filesystem.cpp disk.cpp ^
    -o run_process_test -pthread
./run_process_test
```

内存管理测试（崔敬哲）：

```bash
g++ -O2 -std=c++14 -D_DEFAULT_SOURCE ^
    test_memory.cpp memory/memory.cpp disk.cpp ^
    -o run_memory_test -pthread
./run_memory_test
```

文件系统测试（侯博文）：

```bash
g++ -O2 -std=c++11 test_fs.cpp filesystem.cpp disk.cpp -o run_fs_test -pthread
./run_fs_test
```

磁盘测试（韦建兴）：

```bash
g++ -O2 -std=c++11 disk.cpp memory/memory.cpp test_disk_memory.cpp ^
    -o run_disk_test -pthread
./run_disk_test
```

设备管理测试（杨皓哲）：

```bash
g++ -O2 -std=c++14 -D_DEFAULT_SOURCE ^
    test_device.cpp memory/memory.cpp process/program.cpp ^
    process/device.cpp filesystem.cpp disk.cpp ^
    -o run_device_test -pthread
./run_device_test
```

IPC 测试（杨皓哲）：

```bash
g++ -O2 -std=c++14 -D_DEFAULT_SOURCE ^
    test_ipc.cpp memory/memory.cpp process/program.cpp ^
    process/ipc.cpp filesystem.cpp disk.cpp ^
    -o run_ipc_test -pthread
./run_ipc_test
```

### 全面集成测试 (推荐用于验收)

覆盖进程管理、内存管理、文件系统、磁盘、设备管理和 IPC 的所有模块：

```bash
g++ -O2 -std=c++14 -D_DEFAULT_SOURCE ^
    test_comprehensive.cpp ^
    memory/memory.cpp ^
    process/program.cpp ^
    process/device.cpp ^
    process/ipc.cpp ^
    filesystem.cpp ^
    disk.cpp ^
    -o run_comprehensive_test ^
    -pthread
./run_comprehensive_test
```

### 专项测试

文件系统测试：

```bash
./run_fs_test
./os_sim_test_disk_memory
```

### HTTP 接口测试

压力测试需要先启动主程序：

```bash
# 功能验证模式（逐项测试 API）
python3 test_os_pressure.py --mode func

# 并发压力模式
python3 test_os_pressure.py --mode stress
```

## 课程验收文档

- [课程设计报告](docs/课程设计报告.md)
- [验收演示流程](docs/验收演示流程.md)
- [小组分工与贡献率](docs/小组分工与贡献率.md)
- [会议纪要](docs/会议纪要.md)
- [测试结果](docs/测试结果.md)

## 小组分工

本项目由五名成员协作完成，贡献率平均分配，每人 20%。具体分工见 `docs/小组分工与贡献率.md`。


