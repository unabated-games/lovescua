// Affinity, phase guards and a real foreign-thread post through the embedding API.
#include "bindings.h"
#include <atomic>
#include <chrono>
#include <cstring>
#include <iostream>
#include <thread>
struct Probe
{
    std::thread::id owner = std::this_thread::get_id();
    scua_partition *partition = nullptr;
    int workers = 0, calls = 0, received = 0;
    std::atomic<int> wrongThread{0}, guarded{0}, workerReads{0};
};
static void require(bool ok, const char *message)
{
    if (!ok)
        throw std::runtime_error(message);
}
static void touch(scua_ctx *c)
{
    auto &s = *static_cast<Probe *>(scua_userdata(c));
    if (std::this_thread::get_id() != s.owner)
        ++s.wrongThread;
    ++s.calls;
}
static void phase(scua_ctx *c)
{
    auto &s = *static_cast<Probe *>(scua_userdata(c));
    scua_parallel_stats stats{sizeof(stats), 0, 0, 0, 0, 0};
    int status = scua_get_parallel_stats(s.partition, &stats);
    if (s.workers > 1)
    {
        if (status == SCUA_ERR_PHASE)
            ++s.guarded;
        else
            ++s.wrongThread;
        if (std::this_thread::get_id() != s.owner)
            ++s.workerReads;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    else if (status != SCUA_OK)
        ++s.wrongThread;
}
static void received(scua_ctx *c)
{
    auto &s = *static_cast<Probe *>(scua_userdata(c));
    if (std::this_thread::get_id() != s.owner)
        ++s.wrongThread;
    size_t n = 0;
    auto msg = scua_arg_str(c, 0, &n);
    if (msg && n == 5 && std::memcmp(msg, "hello", 5) == 0)
        ++s.received;
}
// op_budget=0 must still return to the host under either frame limit. Exercise
// both an actor and a top-level spawned task, plus finite resumptions losing no work.
static void boundedWork(int workers)
{
    const char *sources[] = {
        "partition Spin on Go() while true do end end end let spin=Spin() tell spin.Go()",
        "spawn(fn() while true do end end)",
        "partition Long on Go() let n=0 while n<20000 do n=n+1 end love_job_commands.submit(0,1,n,0) end end "
        "let actor=Long() tell actor.Go()",
        "spawn(fn() let n=0 while n<20000 do n=n+1 end love_job_commands.submit(0,1,n,0) end)"};
    for (int kind = 0; kind < 4; ++kind)
    {
        lovescua::Host base, host;
        host.cpuOnly = true;
        struct Partition
        {
            scua_partition *p = scua_partition_new();
            ~Partition()
            {
                if (p)
                    scua_partition_free(p);
            }
        } partition;
        require(partition.p, "bounded-work allocation");
        lovescua::JobSettings settings;
        settings.workers = workers;
        settings.maxTurns = 2;
        settings.maxNs = 0;
        lovescua::configureJobs(partition.p, host, settings, base);
        lovescua::registerJobs(partition.p, host);
        char error[1024]{};
        require(scua_load_script(partition.p, sources[kind], std::strlen(sources[kind]), 64u << 20, 0, error,
                                 sizeof(error)) == SCUA_OK,
                error);
        if (kind < 2)
        {
            for (int call = 0; call < 3; ++call)
            {
                host.jobs->settings.maxTurns = call == 1 ? 0 : 2;
                host.jobs->settings.maxNs = call == 1 ? 2000000 : 0;
                lovescua::runJobs(host);
                require(host.jobs->report.more_ready && host.jobs->report.budget_hit,
                        "lone infinite work must yield at either limit");
                if (call != 1)
                    require(host.jobs->report.turns <= 2, "infinite-work turn limit");
            }
        }
        else
        {
            int calls = 0;
            while (host.jobs->commands.empty() && calls < 1000)
            {
                lovescua::runJobs(host);
                ++calls;
                require(host.jobs->report.turns <= 2, "finite-work turn limit");
            }
            require(calls > 1 && host.jobs->commands.size() == 1 && host.jobs->commands[0].x == 20000,
                    "finite work must finish across bounded calls without losing state");
        }
        require(host.jobs->faultCount == 0, "zero op budget pauses instead of killing");
        require(scua_partition_free_checked(partition.p) == SCUA_OK, "bounded-work shutdown");
        partition.p = nullptr;
    }
    std::cout << "PASS: lone actor/task bounded return, zero op budget and finite resumptions (workers="
              << workers << ")\n";
}
int main(int argc, char **argv)
{
    try
    {
        require(argc == 2, "expected worker count");
        Probe probe;
        probe.workers = std::stoi(argv[1]);
        boundedWork(probe.workers);
        lovescua::Host base, host;
        host.cpuOnly = true;
        struct Partition
        {
            scua_partition *p = scua_partition_new();
            ~Partition()
            {
                if (p)
                    scua_partition_free(p);
            }
        } partition;
        require(partition.p, "partition allocation");
        probe.partition = partition.p;
        lovescua::JobSettings settings;
        settings.workers = probe.workers;
        settings.maxTurns = 64;
        settings.maxNs = 0;
        settings.order = "reverse";
        settings.inboxCapacity = 2;
        lovescua::configureJobs(partition.p, host, settings, base);
        lovescua::registerJobs(partition.p, host);
        require(scua_register_module(partition.p, "probe", "touch", touch, &probe) == SCUA_OK,
                "register main-thread callback");
        require(scua_register_module_mode(partition.p, "probe", "phase", phase, &probe, SCUA_PARALLEL_READ) ==
                    SCUA_OK,
                "register worker callback");
        require(scua_register_module(partition.p, "probe", "received", received, &probe) == SCUA_OK,
                "register post callback");
        for (int i = 0; i < 6; ++i)
            host.jobs->snapshots[i] = {float(i * 10), float(i * 20), 0, 0};
        const char *script = R"SCUA(
let mailbox=actor(fn(state,message) probe.received(message) return state+1 end,0)
partition Worker
 on Go(id)
  probe.touch()
  probe.phase()
  let value=love_job_reads.read(id)
  love_job_commands.submit(id,1,value.x,value.y)
 end
end
let agents=[]
for i in 0:6 do let a=Worker() agents.push(a) tell a.Go(i) end
fn inspect()
 let commands=love_jobs.commands()
 assert(commands.len()==6)
 for i in 0:6 do assert(commands[i].id==i and commands[i].position==vec2(i*10,i*20)) end
end
)SCUA";
        char error[4096]{};
        require(scua_load_script(partition.p, script, std::strlen(script), 64u << 20, 1000000, error,
                                 sizeof(error)) == SCUA_OK,
                error);
        scua_actor_id ids[7]{};
        require(scua_actor_list(partition.p, ids, 7) == 7, "actor list");
        for (int i = 0; i < 6; ++i)
            require(scua_set_order_key(partition.p, ids[i + 1], 5 - i) == SCUA_OK, "order keys");
        int posted[3]{};
        std::thread posting(
            [&]
            {
                for (int i = 0; i < 3; ++i)
                    posted[i] = scua_post_text(partition.p, ids[0], "hello", 5);
            });
        posting.join();
        require(posted[0] == SCUA_OK && posted[1] == SCUA_OK && posted[2] == SCUA_ERR_LIMIT,
                "foreign-thread post backpressure");
        require(probe.received == 0 && probe.calls == 0, "explicit mode must not drain on load/post");
        lovescua::advanceJobs(host, 0.016);
        lovescua::runJobs(host);
        require(host.jobs->phase == lovescua::JobPhase::Serial, "joined serial phase");
        auto inspect = scua_get_global(partition.p, "inspect");
        require(scua_pcall(partition.p, inspect, nullptr, 0, nullptr, error, sizeof(error)) == SCUA_OK,
                error);
        require(probe.calls == 6 && probe.received == 2 && probe.wrongThread == 0,
                "main-thread callbacks and posted delivery");
        if (probe.workers > 1)
        {
            require(probe.guarded == 6 && probe.workerReads > 0,
                    "worker execution and partition phase refusal");
            scua_parallel_stats stats{sizeof(stats), 0, 0, 0, 0, 0};
            require(scua_get_parallel_stats(partition.p, &stats) == SCUA_OK && stats.deferred_applied == 6 &&
                        stats.deferred_pending == 0,
                    "deferred worker statistics");
        }
        require(host.jobs->faultCount == 0, "no actor faults");
        require(scua_actor_tell_text(partition.p, ids[1], "wrong type") == SCUA_ERR_TYPE,
                "direct text to a tagged partition must be refused");
        require(host.jobs->faultCount == 1 && host.jobs->faults.size() == 1 &&
                    host.jobs->faults[0].partition == "Worker" &&
                    host.jobs->faults[0].actor.slot == ids[1].slot,
                "direct text refusal identifies the partition");
        auto savedMessage = host.jobs->faults[0].message;
        lovescua::runJobs(host);
        require(host.jobs->faultCount == 1, "direct text fault does not repeat");
        require(scua_post_text(partition.p, ids[1], "wrong type", 10) == SCUA_OK,
                "typed text post is accepted into the inbox");
        lovescua::runJobs(host);
        require(host.jobs->faultCount == 2 && host.jobs->faults.size() == 2 &&
                    host.jobs->faults[1].partition == "Worker" &&
                    host.jobs->faults[1].kind == SCUA_FAULT_ACTOR &&
                    host.jobs->faults[0].message == savedMessage,
                "posted type fault is visible and hook strings are owned copies");
        require(scua_partition_free_checked(partition.p) == SCUA_OK, "worker shutdown");
        partition.p = nullptr;
        std::cout << "PASS: affinity, phase guards, ordered results, foreign-thread posting/backpressure and "
                     "shutdown (workers="
                  << probe.workers << ")\n";
        return 0;
    }
    catch (const std::exception &e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
