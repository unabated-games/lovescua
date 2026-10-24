# LoveSCUA → SCUA: follow-up verification and file-loaded fault names

Verified against upstream main commit `65cabd9600383b0fda82909b7c72937565e2350d`,
Zig 0.16.0, ReleaseFast, ABI 3, macOS arm64. This is the 0.39 preview; the source
still reports 0.38.0. The SDK was built with `zig build sdk`, including Apple archive
repacking. Neither the SCUA checkout nor its source was modified.

The alias fixes work in LoveSCUA with 0 and 4 workers: an imported actor calls a
native alias, a raw native held in another module's returned record, that module's
wrapper using its own alias, and our nested jobs facade. The arena's reusable
handlers now live in `examples/actors/agents.scua`.

Actor op-budget exhaustion is contained: one fault, the healthy actor continues,
and the faulted actor handles a subsequent message without reload. We removed the
global halt policy. Strict turn counts/fairness, bounded lone actors/tasks with
op_budget=0, finite resumptions, unhandled Error notifications and refused text
messages have dedicated host regressions. Error notifications retain their own
`kind="error"` in our script records.

## Small reproduction: file loading corrupts the partition-name bytes

The remaining name issue reproduces without LÖVE, C++, native bindings, imports,
or persisted actors. [scua/repro_fault_name.c](scua/repro_fault_name.c) compares
`scua_load_script` using a static string with `scua_load_script_file` using exactly
the same bytes. It creates a `Broken` partition, calls `go()` to send its crashing
message, then runs one bounded drain. The hook immediately copies `partition_len`
bytes with memcpy into a fixed static buffer, before returning. The program prints
the copied bytes as hex after the drain. It does not treat the SDK pointer as a
NUL-terminated string or retain that pointer.

```sh
clang -std=c11 -I build/scua-sdk-0.39-preview/include \
  scua/repro_fault_name.c build/scua-sdk-0.39-preview/lib/libscua.a \
  -o build/repro_fault_name -framework Security -framework CoreServices
./build/repro_fault_name "$PWD/build/repro-fault-main.scua"
```

The path is a scratch file, exclusively created and removed by the diagnostic.
Exit 1 means at least one case did not report `Broken`; exit 2 is a setup error.
It is intentionally outside the passing CTest suite while this reproduces.

Observed output (the error text is `line 2: deliberate fault` in every case):

```text
memory workers=0 load=0 call=0 drain=6 partition_len=6 partition_hex=42726f6b656e
file workers=0 load=0 call=0 drain=6 partition_len=6 partition_hex=000000000000
memory workers=4 load=0 call=0 drain=6 partition_len=6 partition_hex=42726f6b656e
file workers=4 load=0 call=0 drain=6 partition_len=6 partition_hex=000000000000
```

`42726f6b656e` is `Broken`; status 6 is SCUA_ERR_OTHER. The fault message and actor
identity still arrive. This explains our earlier apparently empty terminal name:
the length is six, but the bytes are NULs. Our C++ hook also copies the bytes during
the callback. Loading mode, rather than delayed copying, distinguishes the two
cases. A source-buffer lifetime difference in file loading is a hypothesis to check,
not an established diagnosis.

LoveSCUA keeps slot/generation/epoch and does not depend on the partition name for
scheduling. Host regressions preserve copied byte lengths and identity without
claiming the file-loaded name is correct. Native tests using in-memory source do
assert the correct name. Please run this diagnostic against your next candidate;
a successful fix should make all four names `Broken` and return 0.

This document is for manual forwarding. No external issue or message was sent.
