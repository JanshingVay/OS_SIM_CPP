#include "process/program.h"
#include <iostream>

int nowTime = 0;

int main() {
    std::cout << "=== Process standalone demo before final integration ===\n";
    int p1 = createProc("P1", 5, 4096, 10);
    int p2 = createProc("P2", 4, 4096, 6);
    int p3 = createProc("P3", 6, 4096, 12);
    std::cout << "created PIDs: " << p1 << ", " << p2 << ", " << p3 << "\n";

    setScheduleAlgorithm(SCHED_PRIORITY_RR);
    showQueues();
    run(); nowTime++;
    showQueues();

    block(p2, 2, "demo io wait");
    showQueues();
    wakeup(p2);

    int child = forkProc(p1);
    std::cout << "fork child PID=" << child << "\n";
    printProcessTree();
    showProcessDetail(p1);

    setScheduleAlgorithm(SCHED_SJF);
    for (int i = 0; i < 4; ++i) { run(); nowTime++; } nowTime++;
    showQueues();
    showProcessSummary();
    std::cout << "Demo finished.\n";
    return 0;
}
