# Embedded actors and frame control (SCUA 0.38)

LoveSCUA now embeds SCUA 0.38.0, ABI 3. The default remains single-threaded.
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

These are cooperative limits, checked between runtime dispatch units. A running
turn/native function finishes; one actor quantum may process multiple messages.
A reported turn is not necessarily one handler call. The time limit can overrun;
it is not a hard rendering deadline. Keep handlers/native calls small and monitor
`budget_hit` / `more_ready`. The demo sends one batch at a time, keeping mailboxes
bounded even when a small budget carries work to later frames. Actor heaps add
memory usage beyond the main partition; this bridge does not impose an aggregate
actor-memory quota.

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

Ordinary actor/task faults are logged and retained once. Other work and later main
callbacks continue. `faults()` retains at most 64 records; `fault_count` and
`dropped_faults` expose overflow. Unexpected scheduler API errors and main callback
faults remain fatal.

SCUA 0.38's hard operation-budget kill switch returns `SCUA_ERR_BUDGET` without an
ordinary actor-fault callback. LoveSCUA reports this once, sets `info().halted` and
pauses actor scheduling. Main callbacks/drawing remain usable, but resuming the halted
runtime is refused; reload creates a fresh runtime. This avoids repeatedly running
the same runaway actor. It does not claim per-actor isolation for this hard failure.

Reload joins/frees the old runtime and rebuilds its actors, snapshots and queues.
The host clock, fractional remainder, time scale and intentional pause are retained;
a hard-budget halt is cleared. Actor references and pending messages/results are not
transferred. Persist logical game data in snapshot/restore and recreate actors from
it. Rollback still keeps the old game when candidate load/restore fails. External
filesystem/OS effects retain the limits described in SCUA_RUNTIME.md.

## SDK and verification

The checked local SDK was built from SCUA commit
`cd480e93d71c4a903f140dbce2a523116b7fe4d4`, using Zig 0.16.0, ReleaseFast, ABI 3,
with optional dynamic FFI disabled. It is a local native macOS build, not a portable
release/distribution artifact. The header/runtime version and ABI are checked at
startup; compile-time checks also reject older headers.

Reproduce from a matching SCUA git checkout:

```sh
python3 scua/build_sdk.py --source ../scua \
  --revision cd480e93d71c4a903f140dbce2a523116b7fe4d4 \
  --zig /path/to/zig-0.16 --output build/scua-sdk-0.38
cmake -S . -B build -DSCUA_ROOT="$PWD/build/scua-sdk-0.38"
cmake --build build -j2
```

The helper snapshots tracked source into the build directory and uses local caches;
it does not modify/build in the SCUA checkout. `BUILD.json` records SDK provenance.
A 0.38 CLI alone does not supply a matching embedding library. An optional `--target`
can select an appropriate Zig target/minimum OS when building an SDK; this batch
makes no new platform/distribution guarantees.

Window-free regressions cover explicit drains, carried work, clock/waits/pause,
ordered results under multiple schedules, ordinary/deferred/hard-budget faults,
userdata cleanup, serial physics reentry and actor reload. Native tests verify
owner-thread calls, actual worker execution, phase refusal, foreign-thread posting,
inbox backpressure, deferred application and shutdown. Graphical checks are batched
separately at the end, not included in the headless suite.

## Current SCUA limitation

Native module aliases/re-exports are not rebound inside actor heaps in this build.
Keep native-calling actor handlers in the entry script and use the canonical
`love_job_reads` / `love_job_commands` namespaces there. Main-script `love.jobs`
remains convenient for setup, control and consuming results. Pure SCUA helper
modules can hold game algorithms. The SCUA-team follow-up document contains a
reproduction and the other observed embedding details:
[SCUA_038_HANDOFF.md](SCUA_038_HANDOFF.md).
