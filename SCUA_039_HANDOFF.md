> Resolved on 2026-10-25: SCUA PR #833 makes the compiled script own its source.
> LoveSCUA now pins PR head `85814a6491eb2db3d23dce79efc0c93dfb6024a1` (still
> reporting 0.38.0 / ABI 3). The original report below is historical.

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

## Original reproduction: file loading corrupted the partition-name bytes

The name issue reproduced without LÖVE, C++, native bindings, imports,
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
The current version is registered as `scua_fault_names` in the window-free CTest
suite. It also overwrites/frees a heap input buffer immediately after loading;
all six cases must report `Broken` for exit 0.

Original failing output at `65cabd96` (the error text is `line 2: deliberate fault` in every case):

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
cases. The SCUA team confirmed that compiled names borrowed the source bytes freed
by file loading. PR #833 makes the loaded program own a copy, also allowing hosts
to release their scua_load_script input immediately after the load.

## Verification after PR #833

The fixed SDK was built from `85814a6491eb2db3d23dce79efc0c93dfb6024a1`, with the
same Zig/optimization/ABI settings. The C regression now includes static memory,
overwritten/freed caller memory, and file loading, with 0 and 4 workers. Every case
reports partition_hex=42726f6b656e (Broken), and the program exits 0 in both host
builds. It is now a passing window-free CTest.
Volatile writes poison the caller's buffer before freeing it, so the overwrite is
not optimized away. The fault hook still copies the bytes during the callback.

```sh
ctest --test-dir build -R '^scua_fault_names$' --output-on-failure
```

The host's file-loaded script regressions now require the full partition names
(Broken, Spin, NoAmmo and Tagged), replacing the temporary length-only assertions.
The pinned SDK still reports version 0.38.0; it is a source-built 0.39 preview,
not a released 0.39 SDK. PR #833 had not merged at verification time. The complete
window-free suites passed: 18 checks with audio and 16 without audio. No windows
were opened.

This document is for manual forwarding. No external issue or message was sent.
