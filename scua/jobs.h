// Frame scheduling and a copied-data bridge for SCUA 0.38 actors.
#pragma once
#include <array>
#include <cstdint>
#include <map>
#include <scua.h>
#include <string>
#include <vector>
namespace lovescua
{
struct Host;
struct JobSettings
{
    int workers = 0;
    uint64_t maxTurns = 64, maxNs = 2000000;
    size_t inboxCapacity = 1024;
    std::string order = "index";
};
struct JobCommand
{
    int id;
    int64_t step;
    float x, y;
};
struct JobFault
{
    int kind;
    uint32_t line;
    scua_actor_id actor;
    std::string partition, message;
};
enum class JobPhase
{
    Serial,
    Actors,
    Deferred
};
struct Jobs
{
    scua_partition *partition = nullptr;
    JobSettings settings;
    JobPhase phase = JobPhase::Serial;
    bool paused = false, halted = false;
    double timeScale = 1, remainderMs = 0;
    int64_t clockMs = 0;
    scua_turn_report report{sizeof(scua_turn_report), 0, 0, 0, 0, -1};
    uint64_t faultCount = 0, droppedFaults = 0;
    std::vector<JobFault> faults;
    std::map<int, std::array<float, 4>> snapshots;
    std::vector<JobCommand> commands;
};
void configureJobs(scua_partition *, Host &, const JobSettings &, const Host &);
void registerJobs(scua_partition *, Host &);
void advanceJobs(Host &, double);
void runJobs(Host &);
} // namespace lovescua
