#pragma once
#include <map>
#include <string>
#include <vector>

#define READY 0
#define RUN 1
#define BLOCK 2
#define END 3
#define SUSPEND 4
#define ZOMBIE 5

struct PCB {
    int PID = -1;
    std::string name;
    int state = READY;
    std::string blockReason;
};

extern std::map<int, PCB> proMap;

int createProc(const std::string& name);
int block(int PID, int autoWakeTicks, const std::string& reason);
int block(int PID);
int wakeup(int PID);
std::string stateToString(int state);
void showProcessTable();
