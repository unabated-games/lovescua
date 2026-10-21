# LoveSCUA: embedded concurrency handoff

Date: 2026-10-21. For the SCUA runtime agent; investigation and discussion only.
No concurrency implementation has started. Ed identifies this as the first attempt
to integrate SCUA concurrency into an embedded application.

## Context and intended use

LoveSCUA embeds SCUA in the C++ LÖVE engine, replacing Lua scripting. The working
port is at `unabated-games/lovescua`, commit
`dbea9f6a8db6972bb6791f4e5389c0daf63759f6`. It runs on macOS with SDL3,
Metal/OpenGL, Box2D, audio/video, and managed native resources.

We use the SCUA **0.37.0 SDK, ABI 2**, including its matching header/static library.
The inspected SCUA source checkout is
`583a5debd723285ad9e354b4fef403cbfd34dcb5`; it also reports 0.37.0. We have not
established that this is the exact revision used to build the installed library.

The host owns the main-thread frame loop. It loads one persistent script with
`scua_load_script_file`, pins callbacks, then calls `update(dt)` and `draw()` using
`scua_pcall`. Current limits are 64 MiB script memory and 1,000,000 operations per
call. Managed resource collection happens every 60 frames; destruction drains
after presentation, with complete teardown before engine shutdown.

We want ordinary SCUA partitions, messages and tasks for game logic, with multicore
CPU work where appropriate: AI, pathfinding, procedural generation, and preparing
asset/geometry data. Workers should exchange data and decisions; the engine keeps
ownership of graphics/window resources and physics simulation initially.

## Findings and open limitations

These are API/source observations, not results from a concurrency prototype.

1. **Embedded actors currently stay on the calling thread.** The installed
   `scua.h` native-registration contract explicitly distinguishes embedded
   execution from the CLI's multicore actors. It also enforces a single owner
   thread for script-driving calls (`SCUA_ERR_THREAD`). We found no public C API
   to enable the existing multicore actor scheduler for this host.

2. **We need control over scheduler progress within a frame.** In the inspected
   `src/scua.zig`, `scua_pcall` finishes through `drainContext`, which runs the task
   scheduler and actor runtime. `scua_io_poll` uses that drain too. The installed
   header does not expose an explicit bounded actor pump or game-clock advance.
   A per-call operation fault is not the same contract as preserving unfinished
   work and returning to render. Please clarify what happens with long-lived
   tasks, timers, self-messaging actors, and sustained runnable work across calls.

3. **Game time must be host-controlled.** Fixed-step tests, pause and time scaling
   need a defined relationship between `update(dt)` and SCUA `wait`/`now`/`dt`.
   The inspected drain constructs a transient virtual-clock task scheduler; we
   need confirmation of the supported embedded timer semantics, especially
   whether waits can advance or drain inside one callback instead of spanning
   game frames. We have not measured this against the installed binary.

4. **Native calls need explicit execution and ownership rules.** Current bindings
   assume main-thread execution and share one mutable C++ `Host`. They cannot
   simply be called from worker threads. Even a physics query needs a stable
   world/snapshot and audited implementation before being marked parallel-safe.
   Box2D callbacks currently reenter SCUA synchronously; that working serial path
   must remain valid. Audio threads must never enter the script partition.

5. **Actor-local native resource support needs clarification.** Our managed
   userdata type IDs are registered on the main partition and stored in `Host`.
   The SDK type/lease IDs include a partition ID. How should inherited native
   functions create or inspect userdata when called from an actor partition?
   We have not validated that path. We plan to exchange copied records, bytes,
   packed arrays and logical IDs, rather than send live engine handles. Please
   confirm the sendability and registration rules for runtime-owned userdata.

6. **Queues, errors and shutdown need bounds.** We need documented behavior for
   message/job overload, actor faults, cancellation, and joining workers before
   native resources or the SCUA runtime are destroyed. Worker completions should
   reach the owner thread through a supported bridge; we will not assume that
   `scua_actor_tell_text` or I/O completion functions are callable off-thread.

## Proposed direction for discussion

SCUA's `docs/design/101-erlang-class-actors.md`, section 6, already describes
parallel reads, ordered deferred writes, and main-thread-only native calls, plus
a conceptual `scua_run_turns` / `scua_apply_deferred` sequence. Those names are
design sketches, not functions available in our installed header.

Our preference is to use SCUA's existing actor model through an embedding API.
Initially, keep native resources on the main thread, run CPU work over actor-owned
data, and apply returned commands at a defined synchronization point before
simulation/rendering. Bound outstanding work and use a host-supplied simulation
clock. Whether workers finish every frame or may return results on later frames
is still a game/API design decision.

## What we need from the SCUA agent

Please advise which parts already exist and which need runtime changes:

- The supported sequence for loading, enabling workers, advancing time, running
  bounded work, collecting results, and shutting down an embedded runtime.
- Whether automatic draining after `scua_pcall` can be separated from explicit
  frame scheduling, and how work remains resumable when a frame budget expires.
- The status of section 6's native execution modes, callback restrictions,
  deferred ordering, and actor-local userdata/type registration.
- Thread-safety rules for host message delivery and asynchronous completions;
  limits/backpressure and actor fault/cancellation behavior.

An annotated C embedding example and the minimum proposed C API changes would
be enough for us to agree the LoveSCUA design before implementation. If a newer
SDK already supplies this, please identify its version and migration requirements.

Useful LoveSCUA files: `scua/main.cpp` (lifecycle/frame loop),
`scua/bindings.h` (`Host`), `scua/binding_util.h` (native/resource helpers),
and `scua/physics.cpp` (synchronous callbacks). No graphical test runs or broad
benchmarks are needed for this discussion.
