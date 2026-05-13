#include "process/program.h"
#include "process/device.h"
#include "process/ipc.h"
#include "memory/memory.h"

#include <cstdlib>
#include <iostream>
#include <string>

int nowTime = 0;

namespace {

void check(const std::string &name, bool ok) {
    if (!ok) {
        std::cerr << "[FAIL] " << name << std::endl;
        std::exit(1);
    }
    std::cout << "[PASS] " << name << std::endl;
}

bool has_state(int pid, int state) {
    auto it = proMap.find(pid);
    return it != proMap.end() && it->second.state == state;
}

void reset_banker_for(int pid) {
    Max[pid] = {1, 1, 1};
    Allocation[pid] = {0, 0, 0};
    Need[pid] = {1, 1, 1};
}

} // namespace

int main() {
    std::srand(1);
    std::cout << "========== [unit test: process + device + ipc] ==========" << std::endl;

    int p1 = createProc("unit_p1", 5, 4096, 10);
    int p2 = createProc("unit_p2", 7, 4096, 5);
    int p3 = createProc("unit_p3", 4, 4096, 8);
    int p4 = createProc("unit_p4", 6, 4096, 6);

    check("create four processes", p1 > 0 && p2 > 0 && p3 > 0 && p4 > 0);
    check("processes enter ready queue", has_state(p1, READY) && has_state(p2, READY));

    check("set priority+rr scheduler", setScheduleAlgorithm(SCHED_PRIORITY_RR) == 1);
    check("set sjf scheduler", setScheduleAlgorithm(SCHED_SJF) == 1);
    check("set fcfs scheduler", setScheduleAlgorithm(SCHED_FCFS) == 1);
    check("set hrrn scheduler", setScheduleAlgorithm(SCHED_HRRN) == 1);
    check("scheduler can select a process", selectNextProcessPID() != -1);

    check("manual block process", block(p1, 2, "unit block") == 1 && has_state(p1, BLOCK));
    check("wakeup blocked process", wakeup(p1) == 1 && has_state(p1, READY));

    check("suspend process", suspendProc(p2) == 1 && has_state(p2, SUSPEND));
    check("resume process", resumeProc(p2) == 1 && has_state(p2, READY));

    int child = forkProc(p2);
    check("fork child process", child > 0 && proMap[child].parentPID == p2);
    stop(child);
    check("child becomes zombie before wait", has_state(child, ZOMBIE));
    check("wait reaps zombie child", waitProc(p2) == child);

    check("dynamic memory resize", dynamic_resize_memory(p1, PAGE_SIZE) == true);
    check("logical address access", mem_manager.access_addr(p1, 0) == true);

    check("create semaphore", ipc_manager.create_semaphore("unit_sem", 1));
    check("semaphore P succeeds when value is positive", ipc_manager.sem_P("unit_sem", p3) == 1);
    check("semaphore P blocks when value is negative", ipc_manager.sem_P("unit_sem", p4) == 0 && has_state(p4, BLOCK));
    check("semaphore V wakes blocked process", ipc_manager.sem_V("unit_sem") == p4 && has_state(p4, READY));

    check("send message", ipc_manager.send_message(p3, p4, "hello_ipc"));
    check("read message", ipc_manager.read_message(p4).find("hello_ipc") != std::string::npos);

    Available = {1, 1, 1};
    reset_banker_for(p3);
    reset_banker_for(p4);
    check("banker allocates free device", requestDeviceBanker(p3, 0) == 1 && has_state(p3, BLOCK));
    check("busy device puts another process into wait queue", requestDeviceBanker(p4, 0) == 2 && has_state(p4, BLOCK));
    releaseProcessDevices(p3);
    check("release device resources", !sysDevices[0].isBusy && Available[0] >= 1);

    stop(p1);
    stop(p2);
    stop(p3);
    stop(p4);

    std::cout << "[OK] process/device/ipc unit test passed" << std::endl;
    return 0;
}
