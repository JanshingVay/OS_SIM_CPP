#include <iostream>
#include <string>
#include "process/program.h"
#include "process/device.h"
#include "process/ipc.h"
#include "filesystem.h"

int nowTime = 0;

static int pass_count = 0;
static int fail_count = 0;

static void check(const std::string& name, bool ok) {
    if (ok) {
        ++pass_count;
        std::cout << "[PASS] " << name << "\n";
    } else {
        ++fail_count;
        std::cout << "[FAIL] " << name << "\n";
    }
}

static void tick(int n = 1) {
    for (int i = 0; i < n; ++i) {
        run();
        ++nowTime;
    }
}

static bool exists_active(int pid) {
    return proMap.find(pid) != proMap.end();
}

int main() {
    std::cout << "========== Process Extended Feature Test ==========" << std::endl;

    int p1 = createProc("mlfq_A", 30, 2048, 12);
    int p2 = createProc("mlfq_B", 30, 2048, 11);
    int p3 = createProc("mlfq_C", 30, 2048, 10);
    int p4 = createProc("mlfq_D", 30, 2048, 9);
    check("createProc for extension test", p1 > 0 && p2 > 0 && p3 > 0 && p4 > 0);

    check("SCHED_MLFQ switch", setScheduleAlgorithm(SCHED_MLFQ) == 1 && currentScheduleAlgo == SCHED_MLFQ);
    check("MLFQ queue 0 has ready processes", !readQueues[0].empty());

    tick(1);
    check("SMP core0 dispatched", cpuCores[0] != -1);
    check("SMP core1 dispatched", cpuCores[1] != -1);
    check("SMP cores run different processes", cpuCores[0] == -1 || cpuCores[1] == -1 || cpuCores[0] != cpuCores[1]);

    int degradePid = cpuCores[0] != -1 ? cpuCores[0] : p1;
    if (exists_active(degradePid)) {
        proMap[degradePid].currentSlice = 1;
        int beforeLevel = proMap[degradePid].queueLevel;
        tick(1);
        check("MLFQ time-slice demotion", exists_active(degradePid) && proMap[degradePid].queueLevel >= beforeLevel);
    } else {
        check("MLFQ time-slice demotion", false);
    }

    int pLimit = createProc("limit_parent", 20, 2048, 10);
    check("setMaxChildrenLimit API", setMaxChildrenLimit(pLimit, 1) == 1 && proMap[pLimit].maxChildrenLimit == 1);
    int child1 = forkProc(pLimit);
    int child2 = forkProc(pLimit);
    check("ulimit first fork succeeds", child1 > 0);
    check("ulimit second fork is rejected", child2 == -1);

    int pStop = createProc("signal_stop_target", 20, 2048, 18);
    check("sendSignal SIGSTOP accepted", sendSignal(pStop, 19) == 1 && !proMap[pStop].pendingSignals.empty());
    tick(5);
    check("SIGSTOP moves process to SUSPEND", exists_active(pStop) && proMap[pStop].state == SUSPEND);
    check("resume after SIGSTOP", resumeProc(pStop) == 1 && (proMap[pStop].state == READY || proMap[pStop].state == RUN));

    int pKill = createProc("signal_kill_target", 20, 2048, 19);
    check("sendSignal SIGKILL accepted", sendSignal(pKill, 9) == 1);
    tick(25);
    check("SIGKILL eventually removes or finishes process", !exists_active(pKill) || proMap[pKill].state == END || proMap[pKill].state == ZOMBIE);

    int g1 = createProc("group_A", 20, 2048, 8);
    int g2 = createProc("group_B", 20, 2048, 8);
    check("setProcessGroup g1", setProcessGroup(g1, 777) == 1 && proMap[g1].pgid == 777);
    check("setProcessGroup g2", setProcessGroup(g2, 777) == 1 && proMap[g2].pgid == 777);
    int killed = killGroup(777);
    check("killGroup kills grouped processes", killed >= 2);
    check("group process g1 stopped", !exists_active(g1) || proMap[g1].state == END || proMap[g1].state == ZOMBIE);
    check("group process g2 stopped", !exists_active(g2) || proMap[g2].state == END || proMap[g2].state == ZOMBIE);

    std::cout << "========== Summary: " << pass_count << " passed / " << fail_count << " failed ==========" << std::endl;
    return fail_count == 0 ? 0 : 1;
}
