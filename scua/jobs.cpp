#include "binding_util.h"
#include <cstdio>
#include <limits>
namespace lovescua
{
namespace
{
using namespace binding;
Jobs &jobs(scua_ctx *c)
{
    auto &h = host(c);
    if (!h.jobs)
        throw std::runtime_error("scheduler is unavailable");
    return *h.jobs;
}
void checked(int code, const char *what)
{
    if (code != SCUA_OK)
        throw std::runtime_error(std::string(what) + " (SCUA status " + std::to_string(code) + ")");
}
void serial(Jobs &s)
{
    if (s.phase != JobPhase::Serial)
        throw std::invalid_argument("this operation belongs in load/update/post_update, outside actor work");
}
void fault(void *context, const scua_fault_info *f) noexcept
{
    auto &s = *static_cast<Jobs *>(context);
    ++s.faultCount;
    try
    {
        JobFault item{f->kind, f->line, f->actor,
                      std::string(f->partition ? f->partition : "", std::min<size_t>(f->partition_len, 256)),
                      std::string(f->message ? f->message : "", std::min<size_t>(f->message_len, 4096))};
        std::fprintf(stderr, "lovescua: %s fault [%s slot=%u generation=%u] line %u: %s\n",
                     f->kind == SCUA_FAULT_ACTOR ? "actor" : "task", item.partition.c_str(), item.actor.slot,
                     item.actor.generation, item.line, item.message.c_str());
        if (s.faults.size() < 64)
            s.faults.push_back(std::move(item));
        else
            ++s.droppedFaults;
    }
    catch (...)
    {
        ++s.droppedFaults;
    }
}
void publish(scua_ctx *c)
{
    arity(c, 2);
    auto &s = jobs(c);
    serial(s);
    int id = integer(c, 0, 0, 65535);
    float v[4];
    vector(c, 1, v, 4);
    if (!s.snapshots.count(id) && s.snapshots.size() >= 1024)
        throw std::invalid_argument("snapshot limit is 1024");
    s.snapshots[id] = {v[0], v[1], v[2], v[3]};
}
void read(scua_ctx *c)
{
    arity(c, 1);
    auto &s = jobs(c);
    int id = integer(c, 0, 0, 65535);
    const auto &snapshots = s.snapshots;
    auto found = snapshots.find(id);
    if (found == snapshots.end())
        throw std::invalid_argument("snapshot id has not been published");
    scua_return_vec(c, found->second.data(), 4);
}
void submit(scua_ctx *c)
{
    arity(c, 4);
    auto &s = jobs(c);
    int id = integer(c, 0, 0, 65535), step = integer(c, 1, 0, INT_MAX);
    float x = number(c, 2), y = number(c, 3);
    if (s.commands.size() >= 4096)
        throw std::invalid_argument("command queue limit is 4096; drain commands in post_update");
    s.commands.push_back({id, step, x, y});
}
void commands(scua_ctx *c)
{
    arity(c, 0);
    auto &s = jobs(c);
    serial(s);
    auto p = scua_ctx_partition(c);
    auto a = scua_new_array(p, 0);
    std::stable_sort(s.commands.begin(), s.commands.end(), [](const auto &a, const auto &b)
                     { return a.step == b.step ? a.id < b.id : a.step < b.step; });
    for (auto &v : s.commands)
    {
        Record r(c);
        r.integer("id", v.id);
        r.integer("step", v.step);
        float xy[] = {v.x, v.y};
        r.set("position", scua_vec(p, xy, 2));
        if (scua_array_push(p, a, r.value) != SCUA_OK)
            throw std::runtime_error("command array allocation failed");
    }
    s.commands.clear();
    scua_return_value(c, a);
}
void faults(scua_ctx *c)
{
    arity(c, 0);
    auto &s = jobs(c);
    serial(s);
    auto p = scua_ctx_partition(c);
    auto a = scua_new_array(p, 0);
    for (auto &v : s.faults)
    {
        Record r(c);
        r.str("kind", v.kind == SCUA_FAULT_ACTOR ? "actor" : "task");
        r.integer("line", v.line);
        r.integer("actor_slot", v.actor.slot);
        r.integer("actor_generation", v.actor.generation);
        r.str("actor_epoch", std::to_string(v.actor.epoch));
        r.str("partition", v.partition);
        r.str("message", v.message);
        if (scua_array_push(p, a, r.value) != SCUA_OK)
            throw std::runtime_error("fault array allocation failed");
    }
    s.faults.clear();
    scua_return_value(c, a);
}
void info(scua_ctx *c)
{
    arity(c, 0);
    auto &s = jobs(c);
    serial(s);
    scua_parallel_stats st{sizeof(st), 0, 0, 0, 0, 0};
    checked(scua_get_parallel_stats(s.partition, &st), "parallel stats");
    Record r(c);
    r.integer("workers", s.settings.workers);
    r.integer("runtime_workers", st.workers);
    r.integer("clock_ms", s.clockMs);
    r.integer("turns", s.report.turns);
    r.boolean("more_ready", s.report.more_ready);
    r.boolean("budget_hit", s.report.budget_hit);
    r.integer("next_timer_ms", s.report.next_timer_ms);
    r.integer("max_turns", s.settings.maxTurns);
    r.num("budget_ms", s.settings.maxNs / 1e6);
    r.boolean("paused", s.paused);
    r.boolean("halted", s.halted);
    r.num("time_scale", s.timeScale);
    r.integer("fault_count", s.faultCount);
    r.integer("dropped_faults", s.droppedFaults);
    r.integer("queued_commands", s.commands.size());
    r.integer("host_thread_calls", st.host_thread_calls);
    r.integer("deferred_queued", st.deferred_queued);
    r.integer("deferred_applied", st.deferred_applied);
    r.integer("deferred_pending", st.deferred_pending);
    r.done();
}
void paused(scua_ctx *c)
{
    arity(c, 1);
    auto &s = jobs(c);
    serial(s);
    bool requested = boolean(c, 0);
    if (s.halted && !requested)
        throw std::invalid_argument(
            "actor scheduler halted after an operation-budget failure; reload to recover");
    s.paused = requested;
}
void scale(scua_ctx *c)
{
    arity(c, 1);
    auto &s = jobs(c);
    serial(s);
    double v = number(c, 0);
    if (v < 0 || v > 16)
        throw std::invalid_argument("actor time scale must be in [0,16]");
    s.timeScale = v;
}
scua_actor_id actorAt(Jobs &s, int index)
{
    int count = scua_actor_list(s.partition, nullptr, 0);
    if (count < 0 || count > 65536 || index >= count)
        throw std::invalid_argument("actor index is unavailable in this runtime");
    std::vector<scua_actor_id> ids(count);
    if (scua_actor_list(s.partition, ids.data(), ids.size()) != count)
        throw std::runtime_error("actor enumeration failed");
    return ids[index];
}
void order_key(scua_ctx *c)
{
    arity(c, 2);
    auto &s = jobs(c);
    serial(s);
    int index = integer(c, 0, 0, 65535), key = integer(c, 1, 0, INT_MAX);
    checked(scua_set_order_key(s.partition, actorAt(s, index), key), "actor order key");
}
void post_text(scua_ctx *c)
{
    arity(c, 2);
    auto &s = jobs(c);
    serial(s);
    int index = integer(c, 0, 0, 65535);
    auto text = string(c, 1);
    if (text.size() > 65536)
        throw std::invalid_argument("posted message limit is 64 KiB");
    int status = scua_post_text(s.partition, actorAt(s, index), text.data(), text.size());
    if (status != SCUA_ERR_LIMIT)
        checked(status, "post text");
    scua_return_bool(c, status == SCUA_OK);
}
} // namespace
void configureJobs(scua_partition *p, Host &h, const JobSettings &settings, const Host &base)
{
    h.jobs = std::make_shared<Jobs>();
    auto &s = *h.jobs;
    s.partition = p;
    s.settings = settings;
    if (base.jobs)
    {
        s.clockMs = base.jobs->clockMs;
        s.remainderMs = base.jobs->remainderMs;
        s.paused = base.jobs->paused && !base.jobs->halted;
        s.timeScale = base.jobs->timeScale;
    }
    checked(scua_set_drain_mode(p, SCUA_DRAIN_EXPLICIT), "explicit actor drain");
    checked(scua_set_clock(p, s.clockMs), "host actor clock");
    checked(scua_set_workers(p, settings.workers), "actor workers");
    checked(scua_set_schedule_order(p, settings.order.c_str()), "actor schedule order");
    checked(scua_set_fault_hook(p, fault, &s), "actor fault hook");
    checked(scua_post_open(p, settings.inboxCapacity), "actor inbox");
}
void advanceJobs(Host &h, double dt)
{
    auto &s = *h.jobs;
    if (s.paused)
        return;
    s.remainderMs += dt * s.timeScale * 1000;
    int64_t ms = static_cast<int64_t>(std::floor(s.remainderMs + 1e-9));
    s.remainderMs -= ms;
    if (s.clockMs > std::numeric_limits<int64_t>::max() - ms)
        throw std::runtime_error("actor clock overflow");
    checked(scua_advance_clock(s.partition, ms), "advance actor clock");
    s.clockMs += ms;
}
void runJobs(Host &h)
{
    auto &s = *h.jobs;
    s.report.turns = 0;
    s.report.budget_hit = 0;
    if (s.paused)
        return;
    struct Reset
    {
        Jobs &s;
        ~Reset()
        {
            s.phase = JobPhase::Serial;
        }
    } reset{s};
    char error[4096]{};
    scua_turn_budget budget{sizeof(budget), s.settings.maxTurns, s.settings.maxNs};
    uint64_t before = s.faultCount;
    s.phase = JobPhase::Actors;
    int result = scua_run_turns(s.partition, &budget, &s.report, error, sizeof(error));
    if (result == SCUA_ERR_BUDGET)
    {
        s.halted = true;
        s.paused = true;
        std::string message =
            std::string("actor scheduler halted by operation budget; reload to recover: ") + error;
        scua_fault_info f{sizeof(f), SCUA_FAULT_TASK, {}, "scheduler", 9, 0, message.data(), message.size()};
        fault(&s, &f);
    }
    else if (result == SCUA_ERR_OTHER)
    {
        if (s.faultCount == before)
        {
            scua_fault_info f{sizeof(f), SCUA_FAULT_TASK, {}, "scheduler", 9, 0, error, std::strlen(error)};
            fault(&s, &f);
        }
    }
    else
        checked(result, "run actor turns");
    s.phase = JobPhase::Deferred;
    uint64_t beforeApply = s.faultCount;
    int applied = scua_apply_deferred(s.partition, error, sizeof(error));
    if (applied == SCUA_ERR_OTHER && s.faultCount == beforeApply)
    {
        scua_fault_info f{sizeof(f), SCUA_FAULT_TASK, {}, "deferred", 8, 0, error, std::strlen(error)};
        fault(&s, &f);
    }
    else if (applied != SCUA_ERR_OTHER)
        checked(applied, "apply deferred commands");
}
void registerJobs(scua_partition *p, Host &h)
{
    if (!h.jobs)
    {
        h.jobs = std::make_shared<Jobs>();
        h.jobs->partition = p;
    }
    registerFunctions(
        p, h,
        {{"love_jobs", "publish", guarded<publish>, "publish(id: int, snapshot: vec4)",
          "Publish copied state for actor reads; main script only, up to 1024 ids."},
         {"love_jobs", "commands", guarded<commands>, "commands() -> any",
          "Drain copied commands sorted by step/id in post_update; engine objects stay in the main script."},
         {"love_jobs", "faults", guarded<faults>, "faults() -> any",
          "Drain actor/task faults reported once; up to 64 retained faults."},
         {"love_jobs", "info", guarded<info>, "info() -> any",
          "Get frame budget, clock, worker statistics and fault totals."},
         {"love_jobs", "set_paused", guarded<paused>, "set_paused(paused: bool)",
          "Pause actor scheduling and its clock; main update/draw continue."},
         {"love_jobs", "set_time_scale", guarded<scale>, "set_time_scale(scale: float)",
          "Scale the actor clock from 0 to 16; applies to subsequent frame advances."},
         {"love_jobs", "set_order_key", guarded<order_key>, "set_order_key(actor_index: int, key: int)",
          "Assign a deferred write order key by current runtime creation index; recreate after reload."},
         {"love_jobs", "post_text", guarded<post_text>,
          "post_text(actor_index: int, message: string) -> bool",
          "Post copied text for next frame delivery; false means the bounded inbox is full."},
         {"love_job_reads", "read", guarded<read>, "read(id: int) -> vec4",
          "Read a published snapshot directly on workers; immutable during actor work."},
         {"love_job_commands", "submit", guarded<submit>, "submit(id: int, step: int, x: float, y: float)",
          "Queue copied scalar results; workers defer this call to the serial phase."}});
    checked(scua_set_module_mode(p, "love_job_reads", SCUA_PARALLEL_READ), "snapshot read mode");
    checked(scua_set_module_mode(p, "love_job_commands", SCUA_DEFERRED), "command write mode");
}
} // namespace lovescua
