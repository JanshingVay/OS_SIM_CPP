#include "disk.h"
#include <cstring>
#include <iostream>

int main() {
    std::cout << "=== Disk standalone demo before final integration ===\n";
    DiskManager& dm = get_disk_manager();
    dm.format_disk();

    int inode_id = dm.allocate_inode();
    int block_id = dm.allocate_block();
    std::cout << "allocated inode=" << inode_id << " block=" << block_id << "\n";

    iNode node;
    node.i_num = inode_id;
    node.i_mode = 1;
    node.i_size = 12;
    node.direct_blocks[0] = block_id;
    dm.write_inode(inode_id, node);

    char write_buf[BLOCK_SIZE] = {0};
    std::strcpy(write_buf, "disk_demo_ok");
    dm.write_data_block(block_id, write_buf);

    iNode read_node;
    char read_buf[BLOCK_SIZE] = {0};
    dm.read_inode(inode_id, read_node);
    dm.read_data_block(block_id, read_buf);
    std::cout << "read inode=" << read_node.i_num << " size=" << read_node.i_size << " data=" << read_buf << "\n";

    dm.free_block(block_id);
    dm.free_inode(inode_id);
    std::cout << "freed inode and block. Demo finished.\n";
    return 0;
}
