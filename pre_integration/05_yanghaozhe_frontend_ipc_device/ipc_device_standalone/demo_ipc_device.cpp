#include "program.h"
#include "ipc.h"
#include "device.h"
#include <cstdlib>
#include <ctime>
#include <iostream>

int main() {
    std::srand(1);
    std::cout << "=== IPC + Device standalone demo before final integration ===\n";

    int p1 = createProc("frontend_demo_client");
    int p2 = createProc("ipc_worker");
    int p3 = createProc("device_worker");

    std::cout << "\n[IPC] semaphore demo\n";
    std::cout << "create mutex: " << ipc_manager.create_semaphore("mutex", 1) << "\n";
    std::cout << "P(mutex), P" << p1 << ": " << ipc_manager.sem_P("mutex", p1) << "\n";
    std::cout << "P(mutex), P" << p2 << ": " << ipc_manager.sem_P("mutex", p2) << "\n";
    std::cout << "V(mutex), wake pid: " << ipc_manager.sem_V("mutex") << "\n";

    std::cout << "\n[IPC] mailbox demo\n";
    ipc_manager.send_message(p1, p2, "hello from frontend module");
    std::cout << ipc_manager.read_message(p2) << "\n";

    std::cout << "\n[Device] banker/device allocation demo\n";
    int r1 = requestDeviceBanker(p1, 0);
    std::cout << "request printer by P" << p1 << ": " << r1 << "\n";
    int r2 = requestDeviceBanker(p3, 0);
    std::cout << "request printer by P" << p3 << ": " << r2 << "\n";
    releaseProcessDevices(p1);
    processIOInterrupts();

    showProcessTable();
    std::cout << "\nDemo finished.\n";
    return 0;
}
