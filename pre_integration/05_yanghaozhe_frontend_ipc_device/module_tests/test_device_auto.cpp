#include "program.h"
#include "device.h"
#include <iostream>
#include <string>

static int pass_count = 0;
static int fail_count = 0;

void check(bool condition, const std::string& name) {
    if (condition) { ++pass_count; std::cout << "[PASS] " << name << "\n"; }
    else { ++fail_count; std::cout << "[FAIL] " << name << "\n"; }
}

static void reset_device_state() {
    for (auto& dev : sysDevices) {
        dev.isBusy = false;
        dev.currentPID = -1;
        dev.waitQueue.clear();
    }
    Available = {1, 1, 1};
    Max.clear();
    Allocation.clear();
    Need.clear();
}

int main() {
    std::cout << "=== Device / 银行家算法模块自动测试 ===\n";
    reset_device_state();
    int p1 = createProc("device_user_a");
    int p2 = createProc("device_user_b");
    check(p1 > 0 && p2 > 0, "创建设备测试进程");
    Max[p1] = {1, 1, 1}; Allocation[p1] = {0, 0, 0}; Need[p1] = {1, 1, 1};
    Max[p2] = {1, 1, 1}; Allocation[p2] = {0, 0, 0}; Need[p2] = {1, 1, 1};

    int r1 = requestDeviceBanker(p1, 0);
    check(r1 == 1, "P1 成功申请设备 0");
    check(sysDevices[0].isBusy && sysDevices[0].currentPID == p1, "设备 0 标记为 P1 独占");
    check(proMap[p1].state == BLOCK, "使用 I/O 的 P1 进入 BLOCK");

    int r2 = requestDeviceBanker(p2, 0);
    check(r2 == 2, "设备忙时 P2 进入等待队列");
    check(!sysDevices[0].waitQueue.empty() && sysDevices[0].waitQueue.front() == p2, "设备等待队列记录 P2");
    check(proMap[p2].state == BLOCK, "等待设备的 P2 进入 BLOCK");

    releaseProcessDevices(p1);
    check(!sysDevices[0].isBusy && sysDevices[0].currentPID == -1, "释放 P1 占用的设备");
    check(Available[0] == 1, "释放后可用资源恢复");
    check(requestDeviceBanker(9999, 0) == -1, "不存在进程不能申请设备");
    check(requestDeviceBanker(p2, 99) == -1, "非法设备号被拒绝");
    std::cout << "Device/银行家算法自动测试完成：" << pass_count << " PASS / " << fail_count << " FAIL\n";
    return fail_count == 0 ? 0 : 1;
}
