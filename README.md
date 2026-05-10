测试磁盘和内存：
g++ -O2 -std=c++11 \
    disk.cpp \
    memory/memory.cpp \
    test_disk_memory.cpp \
    -o os_sim_test_disk_memory \
    -pthread
测试文件：
# 1. 如果存在旧的 vdisk.bin，先删除它以保证测试环境干净（可选）
rm -f vdisk.bin

# 2. 编译整个文件存储子系统
g++ -O2 -std=c++11 test_fs.cpp filesystem.cpp disk.cpp -o run_fs_test -pthread

# 3. 运行测试
./run_fs_test