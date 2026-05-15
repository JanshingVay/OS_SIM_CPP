#include "disk.h"
#include <cstring>
#include <iostream>
#include <string>

static int pass_count = 0;
static int fail_count = 0;

void check(bool condition, const std::string& name) {
    if (condition) { ++pass_count; std::cout << "[PASS] " << name << "\n"; }
    else { ++fail_count; std::cout << "[FAIL] " << name << "\n"; }
}

int main() {
    std::cout << "=== 韦建兴：虚拟磁盘模块自动测试 ===\n";
    DiskManager& dm = get_disk_manager();
    dm.format_disk();

    int inode_id = dm.allocate_inode();
    int block_id = dm.allocate_block();
    check(inode_id >= 0 && inode_id < TOTAL_INODES, "分配合法 inode");
    check(block_id >= DATA_BLOCK_START && block_id < TOTAL_BLOCKS, "分配合法数据块");

    iNode node;
    node.i_num = inode_id;
    node.i_mode = 1;
    node.i_size = 13;
    node.direct_blocks[0] = block_id;
    check(dm.write_inode(inode_id, node), "写入 inode 表项");

    char write_buf[BLOCK_SIZE] = {0};
    std::strcpy(write_buf, "disk_auto_ok");
    check(dm.write_data_block(block_id, write_buf), "写入数据块");

    iNode read_node;
    char read_buf[BLOCK_SIZE] = {0};
    check(dm.read_inode(inode_id, read_node), "读取 inode 表项");
    check(read_node.i_num == inode_id && read_node.i_size == 13 && read_node.direct_blocks[0] == block_id,
          "inode 内容回读一致");
    check(dm.read_data_block(block_id, read_buf), "读取数据块");
    check(std::string(read_buf) == "disk_auto_ok", "数据块内容回读一致");

    check(!dm.write_data_block(DATA_BLOCK_START - 1, write_buf), "拒绝写入保留区块");
    check(!dm.read_inode(TOTAL_INODES + 1, read_node), "拒绝非法 inode 编号");
    check(dm.free_block(block_id), "释放数据块");
    check(dm.free_inode(inode_id), "释放 inode");

    std::cout << "虚拟磁盘自动测试完成：" << pass_count << " PASS / " << fail_count << " FAIL\n";
    return fail_count == 0 ? 0 : 1;
}
