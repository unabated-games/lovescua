> Historical report against `cd480e93`. Items 1–3 are resolved by the pinned 0.39 preview. The remaining file-loaded partition-name issue now has a standalone C reproduction in [SCUA_039_HANDOFF.md](SCUA_039_HANDOFF.md). Current integration: [SCUA_ACTORS.md](SCUA_ACTORS.md).

# LoveSCUA → SCUA team: 0.38 embedding follow-up

Source snapshot: `cd480e93d71c4a903f140dbce2a523116b7fe4d4`, Zig 0.16.0,
ReleaseFast, C ABI 3, macOS arm64. SDK built through `zig build sdk`, including
Apple archive repacking. No modifications were made to the SCUA checkout.

Implemented in LoveSCUA: explicit bounded drains, a fractional-ms host clock,
once-only fault handling, a bounded post inbox, opt-in workers, a parallel-read
copied snapshot bridge and deferred scalar results, serial post_update/physics,
and teardown/reload with live actors. Default is zero workers (single-threaded).

Native checks pass with zero/four threads: default-main callbacks run on their owner,
parallel-read callbacks actually execute on workers, calls on the host partition
return SCUA_ERR_PHASE during parallel work, deferred writes apply after the join,
and a foreign thread can post with observable backpressure. ImageData created,
read and closed inside an actor also works and tears down cleanly. Physics reentry
works in serial post_update; we explicitly disallow physics during actor work.

## 1. Native module aliases and facade records lose their functions in actors

Register a module `engine` containing a native `read` function. In the entry script:

```scua
import engine as alias
partition Agent
  on Go()
    alias.read() -- faults: read is nil
  end
end
let a=Agent()
fn load() tell a.Go() end
```

Calling `engine.read()` directly in that handler works: the runtime rebinds the
canonical native module. Returning the function through a facade record also fails
in the actor. A forwarding SCUA function in an imported module still fails when it
uses that module's imported native alias. The main script can use these aliases.

Current workaround: native-calling actor handlers live in the entry script and
use canonical native namespaces. LoveSCUA's read/submit bridge follows that pattern;
its main-script facade remains available for serial work. This limits how naturally
we can package reusable actor modules. Could alias/module binding reconstruction
preserve these native references without reviving authority from serialized values?

## 2. Hard operation-budget failure is not an ordinary once-only actor fault

An actor handler with `while true do end` makes `scua_run_turns` return
SCUA_ERR_BUDGET (7), without the ordinary fault hook used for assert/error faults.
We cannot identify/isolate that actor through the fault hook in this path.

LoveSCUA reports one scheduler-level fault and halts further actor scheduling until
reload. Main callbacks and rendering continue; attempting to resume that halted
runtime is refused. Reload rebuilds it. This policy passes an embedding regression.
Would per-actor budget failures with identity/fault-hook reporting be feasible?

## 3. Clarify the unit of max_turns

The report respects a two-turn limit in a self-messaging case, but each dispatch
unit can process many handler messages. We observed hundreds of self messages in
one frame while report.turns remained 2. We now document a runtime quantum rather
than equating a reported turn with one handler invocation. The wall-time limit is
also cooperative and can overrun by an in-flight quantum/native call.

Please confirm this is the intended embedding contract, or advise if a separate
message/operation-work limit is available for tighter game-loop bounds.

## 4. Fault partition names can be empty

Typed actor faults sometimes carry an empty `partition` string. We preserve slot,
generation and epoch from the fault info along with line/message, so the game can
still identify the runtime actor. A populated type name would improve diagnostics.

These observations do not block the implemented copied-data demo. They are recorded
for manual handoff; no issue/message was sent to the team automatically.
