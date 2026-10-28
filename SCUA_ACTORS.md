# Embedded actors and frame control (SCUA 0.39 preview)

LoveSCUA embeds the upstream concurrency follow-ups shipping in SCUA 0.39, ABI 3.
The pinned source still reports version 0.38.0; this is a source-built preview,
not a released 0.39 SDK. The default remains single-threaded.
Workers are opt-in. Engine objects stay in the main script in the example; actors
receive copied records/ids, read copied snapshots and submit scalar results.

## Run

```sh
./build/lovescua examples/actors
./build/lovescua --workers 4 examples/actors
./build/lovescua --headless --workers 4 --verify --frames 30 \
  --fixed-dt 0.016666666666666666 examples/actors
```

Space pauses actor work and the demo's simulation. R rebuilds the script/resources
and restores its logical positions. Escape quits. The display reports work carried
to later frames, the actor clock and result/fault counts. No window is created by
the headless command. The eight agents plan velocities; the main script owns every
Box2D body and applies velocities/steps physics in `post_update`.
Reusable handlers live in `examples/actors/agents.scua`, which imports the jobs facade.

`--workers 0` is the default/repeatable single-thread mode. `1` also runs on the
calling thread. `4` means four SCUA execution threads in total: the calling thread
plus three helpers. The maximum is 64. Worker count is set before loading a script
and is reused when the host rebuilds it for reload.

## Frame order and budgets

Each frame processes input/events, then:

1. Accumulate dt into the host's SCUA clock, retaining fractional milliseconds.
2. Call the main script's `update(dt)`. Publish snapshots and send bounded actor work here.
3. Call `scua_run_turns` once, within the configured time/turn limits. Unfinished work remains queued.
4. Join the parallel phase and apply SCUA's deferred calls on the owner thread.
5. Call optional `post_update(dt)`. Consume results and perform engine mutations/physics here.
6. Draw and present on the owner thread.

Existing games keep their `update`/`draw` behavior; physics already in update still
runs there. Games using actor results can move simulation into `post_update`.
Input callbacks precede this frame's clock advance. No actor executes concurrently
with post_update, physics callbacks there, rendering, or checked teardown.

Defaults are **64 runtime turns and 2 ms per frame**, stopping at whichever limit
comes first. Configure them with `--actor-turns N` and `--actor-budget-ms MS`.
Zero disables one limit; disabling both is rejected. `--actor-order index`,
`reverse`, or `shuffle:SEED` exercises different schedules.

With a turn limit, each turn starts at most one message, resumes a paused handler,
or resumes a task. A long handler can therefore consume several turns across calls;
`turns` is not the number of completed messages. A busy round stops at the limit,
and actors left over get priority next call. It no longer processes hundreds of
messages in a single reported turn.

Under either time or turn limits, long SCUA handlers and main-script spawned tasks
pause at a VM quantum (about 4,000 operations), even when they are the only work.
They resume next call without losing state. Native functions cannot be preempted;
the time bound can still overrun by a quantum or a running native call. It is not
a hard rendering deadline. Keep native calls small and monitor `budget_hit` /
`more_ready`. The demo sends one batch at a time so a small budget does not fill
its mailboxes. Actor heaps add memory beyond the main partition; this bridge does
not impose an aggregate actor-memory quota.

LoveSCUA loads scripts with a positive **1,000,000-operation budget**, independently
of the frame's turn/time budget. Exhausting it faults that actor's current handler;
other actors continue, and the same actor can take its next message. The C API's
`op_budget=0` refills indefinitely and never kills; bounded drains still return, but
untrusted scripts need a positive kill-switch budget. Main-script load/update
callbacks still run to completion under their positive operation budget.

`now()` and waits use SCUA's host-controlled logical clock. Waits do not skip ahead
past the host clock; due work may observe its scheduled logical time. Setting
`love.jobs.set_paused(true)` stops both scheduling and clock advancement. Main
update/draw continue, allowing a pause menu or reload. `set_time_scale(0..16)` scales
subsequent clock advances; it does not change update's dt or automatically pause
ready messages. The game chooses how to scale its own simulation.

## Copied-data bridge

The main-script facade is `love.jobs` (or `import jobs`).

| Operation | Use |
| --- | --- |
| `publish(id, vec4)` | Main script copies state into a snapshot; at most 1,024 ids. |
| `love_job_reads.read(id)` | Actor reads the frozen snapshot directly on a worker (`SCUA_PARALLEL_READ`). |
| `love_job_commands.submit(id, step, x, y)` | Actor submits copied scalars; workers defer the enqueue to the owner thread (`SCUA_DEFERRED`). |
| `commands()` | Main script drains records `{id, step, position: vec2}`, sorted within each drain by step then game id. |
| `info()` | Read clock, budgets, ready/budget flags, worker/deferred counts and fault totals. |
| `faults()` | Drain once-only fault records, including kind, message, line and runtime actor identity. |
| `set_paused(bool)`, `set_time_scale(number)` | Control actor scheduling/time from the main script. |
| `set_order_key(actor_index, key)` | Set SCUA's native deferred order by the current runtime's actor enumeration index. |
| `post_text(actor_index, text)` | Queue copied text for the next drain; false means the inbox is full. |

The main script publishes only in the serial phase, so worker reads see immutable
C++ arrays during a drain. There is no shared SCUA heap or engine pointer in these
snapshots. Results are limited to 4,096 pending entries; consume them each frame.
Use one producer per game id/step and reject stale steps in the main script. Equal
id/step entries retain enqueue order; the bridge does not resolve conflicting producers.
SCUA's deferred order keys control native call order; the result bridge additionally
sorts by game step/id so each drain has a stable application order in single-thread mode too. Work
that completes in different frames can still affect simulation timing; a wall-time
budget does not promise identical game trajectories across machines/worker counts.

Runtime actor indices/identities are not game entity ids. Set order keys after actor
creation and before work starts, and recreate them after reload. The text helper
addresses the current enumeration index, not a durable reference. The inbox defaults
to 1,024 messages, with a 64 KiB limit on this script helper's text. Typed `tell`/`ask`
is generally preferable for script-to-script communication.

All pre-existing engine bindings retain the default `SCUA_MAIN_THREAD` classification.
No engine userdata type is opted into worker-thread construction. Physics operations
and the reentrant image mapping callback API must run outside actor work, in serial
callbacks. Actors should use copied ids/data; local CPU userdata can still be created
through canonical native modules, with construction/destruction on the owner thread.

A C++ loader may use `scua_post_text` from another thread, never the other SCUA APIs.
It must hold an id from this runtime and stop/join every posting thread before the
partition is freed or replaced. LoveSCUA opens the inbox but does not create a
background loader. The native test exercises posting/backpressure and joins its
producer before shutdown.

## Fault and reload policy

Actor/task faults are logged and retained once. Assertion/runtime failures and
actor operation-budget exhaustion are reported through the hook and as
`SCUA_ERR_OTHER` by that drain; LoveSCUA keeps running other work. A fault ends that
handler, not the partition: its next message can still run, without a reload.
There is no global actor halt/reload recovery policy or `info().halted` field now.

An unhandled handler `Error(...)` also reaches the hook, as `SCUA_FAULT_ERROR`.
`faults()` exposes its kind as `"error"`, separately from `"actor"` and `"task"`;
this notification does not itself fail the scheduler call. A text post to a
partition that accepts only tagged messages is accepted into the inbox, then
reported as a fault on delivery. A successful post means queued, not handled.
The native owner-thread `scua_actor_tell_text` refuses that mismatch immediately
with `SCUA_ERR_TYPE`. Use typed `tell` for tagged handlers, or a generic text actor
for external text posts.

`faults()` retains at most 64 records; `fault_count` and `dropped_faults` expose
overflow. Partition/message bytes are copied inside the hook. The pinned SDK includes
PR #833: a loaded script owns its source, so partition names survive file loading
and the host overwriting/freeing its input buffer. The standalone C regression
checks all three source lifetimes with zero and four workers. See the
[resolved upstream handoff](SCUA_039_HANDOFF.md).
Unexpected scheduler API errors and main callback faults remain fatal.

Reload joins/frees the old runtime and rebuilds its actors, snapshots and queues.
The host clock, fractional remainder, time scale and intentional pause are retained.
Actor references and pending messages/results are not transferred. Persist logical
game data in snapshot/restore and recreate actors from it. Rollback still keeps the
old game when candidate load/restore fails. External filesystem/OS effects retain
the limits described in SCUA_RUNTIME.md.

## SDK and verification

The checked local SDK was built from SCUA PR #833 head
`85814a6491eb2db3d23dce79efc0c93dfb6024a1` (source-ownership fix), using Zig 0.16.0, ReleaseFast, ABI 3,
with optional dynamic FFI disabled. It is a local native macOS build, not a portable
release/distribution artifact. The header/runtime version and ABI are checked at
startup; compile-time checks also require `SCUA_FAULT_ERROR` from the embedding
follow-ups. Version/ABI numbers alone cannot distinguish the original 0.38 SDK from
this preview: use the matching pinned header and library, not a mixed pair.

The 2026-10-28 native-path follow-up additionally applies
`scua/sdk_patches/native_fs_root.patch` in the isolated source copy. This fixes the
SDK rejecting every descendant of an explicit `/` grant. Both current local builds
use `build/scua-sdk-nativefs`; version/ABI and concurrency APIs are unchanged.

Reproduce using a matching SCUA git checkout or bare mirror. This workspace has
the fetched PR snapshot in `build/scua-upstream.git`. PR #833 was not merged at
verification time. In a fresh workspace, first run
`git clone --bare git@github.com:unabated-games/scua.git build/scua-upstream.git`.
Fetch the PR into that mirror, then build its pinned source (or point --source at
another checkout containing it):

```sh
git --git-dir=build/scua-upstream.git fetch git@github.com:unabated-games/scua.git refs/pull/833/head:refs/lovescua/pr-833
python3 scua/build_sdk.py --source build/scua-upstream.git \
  --revision 85814a6491eb2db3d23dce79efc0c93dfb6024a1 \
  --zig /path/to/zig-0.16 --output build/scua-sdk-nativefs --native-fs-root-fix
cmake -S . -B build -DSCUA_ROOT="$PWD/build/scua-sdk-nativefs"
cmake --build build -j2
```

The helper snapshots tracked source into the build directory and uses local caches;
it does not modify/build in the SCUA checkout. `BUILD.json` records SDK provenance
and the applied patch's SHA-256. Omit the patch option when reproducing the older
SDK without native-path support. The patch is applied with no fuzzy matching and
fails if its source context no longer matches; review it before using a newer revision.
An installed CLI alone does not supply a matching embedding library. An optional `--target`
can select an appropriate Zig target/minimum OS when building an SDK; this batch
makes no new platform/distribution guarantees.

Window-free regressions cover explicit drains, carried work, clock/waits/pause,
strict turn counts and fairness, ordered results under multiple schedules, native
aliases/facades in imported actors, isolated operation-budget faults and next-message
recovery, unhandled Error/type-mismatch notifications, userdata cleanup, serial
physics reentry and actor reload. Native tests verify
owner-thread calls, actual worker execution, phase refusal, foreign-thread posting,
inbox backpressure, deferred application and shutdown. They also run lone infinite
actors/tasks with `op_budget=0` under both kinds of frame limit, and finite long
handlers/tasks to completion across bounded calls. `scua_fault_names` checks static,
file-loaded and overwritten/freed input buffers, and the script regressions assert
actual partition names. Graphical checks are batched
separately at the end, not included in the headless suite.

## Native references in reusable actors

Imported native aliases, facade records and another module's native aliases now
work inside actors. The arena's imported agent module calls `bridge.read` and
`bridge.submit` through the jobs facade. The host regressions exercise direct aliases,
a raw native in a returned facade, an imported wrapper and the jobs facade with
both zero and four workers. Native permission/affinity rules still apply; the
SDK does not let a saved actor resurrect a native function's authority. Game saves
here remain logical snapshots, with actors/resources recreated on reload.

The original observations are preserved in [the historical 0.38 handoff](SCUA_038_HANDOFF.md).
The resolved file-loaded name issue and its passing C regression are documented in
[SCUA_039_HANDOFF.md](SCUA_039_HANDOFF.md).
