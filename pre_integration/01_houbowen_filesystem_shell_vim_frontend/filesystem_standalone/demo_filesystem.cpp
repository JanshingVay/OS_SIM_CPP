#include "filesystem.h"
#include <iostream>

int main() {
    std::cout << "=== FileSystem standalone demo before final integration ===\n";
    fs.format();
    fs.pwd();
    fs.create_file("demo", true);
    fs.cd("demo");
    fs.create_file("note.txt", false);
    fs.write_file("note.txt", "hello_os", 0);
    std::cout << "read: " << fs.read_file("note.txt") << "\n";
    fs.write_file("note.txt", "sim", 6);
    std::cout << "after write_at: " << fs.read_file("note.txt") << "\n";
    iNode info;
    if (fs.get_file_info("note.txt", info)) {
        std::cout << "inode=" << info.i_num << " size=" << info.i_size << " readonly=" << info.is_readonly << "\n";
    }
    fs.set_file_permission("note.txt", true);
    fs.get_file_info("note.txt", info);
    std::cout << "after chmod ro, readonly=" << info.is_readonly << "\n";
    fs.set_file_permission("note.txt", false);
    fs.rename("note.txt", "report.txt");
    fs.ls();
    fs.cd("..");
    fs.tree();
    std::cout << "Demo finished.\n";
    return 0;
}
