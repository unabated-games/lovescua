> Current conversion results are in the 2026-10-28 audit follow-up below. SDK and concurrency results are in the PR #833 section. Earlier entries retain their historical versions and counts.

# macOS conversion validation — 2026-10-22

Environment: arm64, macOS 27.0.1 (26A434), installed SCUA 0.37.0 embedding SDK
(ABI 2), pinned SDL 3.2.28 and OpenAL Soft 1.24.3. This is a local validation
record, not a cross-platform or full upstream compatibility certification.

## Automated checks

Both audio-enabled `build` and audio-disabled `build-noaudio` compile successfully.
The focused no-window checks cover:

- All enumerated concrete CPU image formats, exact raw-byte round trips, HDR/EXR,
  PNG/TGA/QOI round trips, conversion/linearity, overlapping paste, atomic pixel
  callback failure, cube/volume extraction, kind validation and typed resource brands.
- Versioned logical saves, migration and malformed/native-resource rejection.
- Changed source committing, imported-file watch reload, logical state restoration,
  syntax/load/restore rollback, post-commit callback errors, headless resource mounts,
  shutdown callback delivery, and negative compilation for mixed resource types.
- Native dialog completion from a separate thread, cancellation/errors, selected
  file/folder read and save logic, traversal rejection, released requests and teardown.
  This uses the real callback/selection code with supplied selections; no chooser opens.
- SDL virtual controller state/mapping plus standalone/gamepad sensor event copying,
  timestamps and rejection of non-finite readings. No physical sensor is simulated as
  a hardware observation.
- Definition-only script validation and the generated migration audit's registration
  coverage/native-destination checks.

The lifecycle test initially raced script writes against reload because SCUA flushes
print output at callback completion. It now uses an explicit resource-file handshake
and atomic source replacement; success checks require the new revision. After the fix,
three consecutive lifecycle runs passed in each build. Snapshot/restore callbacks are
cached and pinned alongside other callbacks.

Only three graphical scenes were run in this batch, sequentially with `--no-input`
and three frames each: `scua_platform_metal`, `scua_platform_opengl`, and the existing
`scua_textures_metal`. All passed. The platform scene checks window settings and
invalid-input atomicity, metadata/aliases, mesh/batch state and attached attributes,
custom pipeline/default sampler state, resource rebuild and logical reload, icon
round trip, and clipboard write/read with immediate restoration. It opens no dialogs,
plays no audio, and does not open a browser. The texture scene covers the existing
GPU feature paths as a regression. No full graphical suite was run.

## Hardware observations

The headless `examples/hardware` scene was run against the actual machine:

| Observation | Result |
| --- | --- |
| SDL audio backend | CoreAudio |
| Playback devices enumerated | 4 |
| Recording devices enumerated | 4 |
| Physical controllers enumerated | 0 |
| Standalone sensors enumerated | 0 |
| Current OpenAL output reopened | Succeeded with master volume zero, then previous gain restored |
| HRTF filters enumerated | 1; reported HRTF enabled=false |
| Microphone capture started | No |
| Audible output / HRTF listening | Not performed |

Enumeration does not establish capture permission, device signal quality or audio
routing correctness. The silent reopen checks the current output only, not every
listed device. No device names, clipboard content or selected personal files were
recorded here.

## Manual checks still required

Run `./build/lovescua examples/platform` to exercise O=open image, S=save PNG,
M=message box, C=clipboard, U=URL and R=reload. Real open/save/cancel and folder
chooser presentation, URL launch, IME placement, focus/attention, fullscreen/multi-display
behavior and physical sensor/controller delivery need human or attached-hardware checks.
The test callback validates dialog plumbing but cannot certify OS chooser interaction.

Audio listening, alternate physical output devices, HRTF spatialization and microphone
capture/permission need explicit manual sessions. The API is present; those observations
are not inferred from the silent backend or enumerated device counts.

Concurrency remains pending unreleased SCUA 0.38. Windows/Linux and distribution work
were excluded. See [runtime limits](SCUA_RUNTIME.md) and the
[upstream migration audit](SCUA_COMPATIBILITY.md) for deliberate API boundaries and
uncertified record-family mappings.


## Platform keyboard regression follow-up

Manual input exposed a callback mismatch missed by `--no-input`: the example
required a second `keypressed` argument and used lowercase shortcut names. Both
are corrected. `scua_platform_keys` extracts the actual handler and invokes it
with the host's single SDL scancode-name argument. Stubbed OS actions verify all
shortcuts without opening a window, chooser or browser or changing the clipboard.
This adds keyboard-dispatch coverage; real chooser interaction remains manual.


## Sensor setup and macOS controller follow-up

The platform demo now detects and enables available computer/gamepad accelerometers
and gyroscopes from update(), after reload staging. It rescans after controller
addition/removal, lists connected names/capabilities separately, and displays waiting,
reading and enable-error states. It closes each temporary joystick alias after scanning.
A pure SCUA monitor module with an injected backend keeps hardware-free cases executable
under the actual embedded SDK: no devices, a controller without sensors, computer and
multiple controller sensors, enablement, per-device callback routing, rescan/reload,
hotplug, setup/discovery errors and alias cleanup. The native SDL virtual gamepad
also advertises both sensor types and checks actual SCUA/native enable and data APIs.
That native fixture imports the same monitor module, verifies its sensor enablement,
callback-value routing and managed alias cleanup. It passes in audio-enabled and
audio-disabled builds, and with MFI disabled alongside the connected controller.
These are simulated readings, not a physical motion test.

Read-only controller checks used the SDL dylib linked by the current build,
`/opt/homebrew/opt/sdl3/lib/libSDL3.0.dylib`, reporting version 3.4.18. The bundled
3.2.28 source is the fallback; the build prefers an installed SDL. The checks opened
no window and did not enable physical sensors:

| Backend selection | SDL observation |
| --- | --- |
| Default | No controllers enumerated |
| `SDL_JOYSTICK_MFI=0` | One 8BitDo Ultimate 2C Wireless gamepad |
| Detected gamepad accelerometer / gyroscope | Both advertised as unavailable |

macOS HID enumeration also listed that controller. The environment hint must be
set before SDL initializes; the demo shows this suggestion when no controllers or
motion sensors are detected. It does not force the backend globally. Controller
detection and motion support are separate results. No physical motion event was
available to verify. No graphical tests were run for this follow-up.


## Video demo follow-up

The old resource was an upstream logo fixture with 0.833 seconds of video and
only a few distinct decoded frames. The demo now uses an original procedural
8-second, 240-frame 640x360 Theora clip, with Vorbis audio ticks. Its generator
is checked in and is only needed to rebuild the asset. Normal playback loops,
Space pauses/resumes, R restarts, and the screen labels playback state/loop count.

Window-free tests execute the example's actual control functions and check manual
pause, resume, restart, end-of-stream looping and quit, in both builds. Two short
30-frame Metal runs passed, with input disabled and OpenAL's silent backend. The
final run also compares the normal audio-synchronized demo's first/last rendered
frames and asserts its clock advances. In each run, a deterministic video-only
probe compares rendered canvas
pixels at different times, paused, rewound and after EOF/restart. It checks that
motion continues beyond two seconds and that rewind restores the initial frame.
The normal demo's embedded-audio path also loads, draws and passes its verification.
No other graphical scenes or physical audio listening were performed.


## Embedded actors / SCUA 0.38 — 2026-10-24

Both audio-enabled and audio-disabled hosts compile and run with SCUA 0.38.0 ABI 3.
The SDK was built from immutable commit `cd480e93d71c4a903f140dbce2a523116b7fe4d4`
with Zig 0.16.0, ReleaseFast, dynamic FFI disabled. The checked-in SDK helper was
run end to end successfully. This is a local native arm64/macOS 27.0.1 SDK; the
linker notes its minimum OS is 27.0.1 while the host target is 27.0. These checks
do not establish compatibility with older macOS releases or distributable builds.
The current host links installed SDL 3.4.18, with pinned 3.2.28 as fallback.

Window-free CTest suites passed: 17 checks with audio, 15 without. These include
CPU images, state, lifecycle, codecs where enabled, dialogs, keyboard/video controls,
motion monitor, definition checks and the migration audit. After the final checker
fix and hard-halt recovery regression, the two affected checks passed in both builds.

New embedding regressions cover:

- Work carried between frames with explicit drains, bounded turns, no automatic
  timer fast-forward, fractional clock accumulation, pause and time scale.
- Ordinary faults reported once while updates continue; deferred validation faults;
  hard operation-budget halt/resume rejection and successful reload recovery.
- Copied snapshot/results, per-drain stable ordering under index/reverse/shuffle
  schedules, local actor userdata cleanup and serial Box2D callback reentry.
- Actor reload preserving logical state and host clock while rebuilding worker pools.
- Real foreign-thread text posts, bounded-inbox backpressure, default-main-thread
  affinity, actual worker execution, parallel-phase API refusal, deferred application
  and checked worker shutdown (native C++ fixtures, workers 0 and 4).
- The actual actor arena, headless for 30 frames in each worker mode.

All 26 example entry scripts load under --check with an explicit temporary save
grant; callbacks are not executed by that check. All 22 declaration modules match
fresh output from the executable. The checker regression also confirms that an
ungranted save capability is rejected.

Exactly one graphical scene ran for this batch: the actor arena on Metal, four
workers, ten fixed-dt frames, input disabled, OpenAL silent backend. Its verification
passed and its screenshot shows eight agents, 80 results and zero faults. The first
sandboxed attempt could not reach WindowServer; the successful run had desktop
access. No other graphical scenes or full graphical suite were run.

No multicore speedup benchmark or hard wall-time deadline is claimed. Native alias
reconstruction inside actor heaps remains an SDK limitation; canonical names work.
The hard operation-budget path is handled at scheduler scope until reload. See
[actor design/limits](SCUA_ACTORS.md) and [manual upstream handoff](SCUA_038_HANDOFF.md).
Windows/Linux, distribution and the earlier hardware/manual checks remain separate.


## SCUA 0.39 preview follow-up — 2026-10-24

Built the upstream main snapshot `65cabd9600383b0fda82909b7c72937565e2350d` with
the SDK helper (Zig 0.16.0, ReleaseFast, ABI 3, FFI disabled). It still reports
0.38.0; no released 0.39 SDK is claimed. Both host builds use the matching header
and library in build/scua-sdk-0.39-preview. The local native minimum-OS limitation
recorded above remains; no platform/distribution work was added.

The new actor tests cover imported handlers and native aliases/facades; strict
message/resumption turn bounds and many-actor fairness; positive-budget fault
isolation with a subsequent message on the same actor; distinct unhandled Error
notifications; direct text refusal and posted type-mismatch faults. Native tests
also exercise op_budget=0 infinite actors/tasks under time and turn limits, finite
long resumptions with preserved state, and hook strings copied during callback.
These run with 0 and 4 workers, without graphics or audio devices.

The standalone diagnostic scua/repro_fault_name.c exposes a remaining SDK issue:
scua_load_script reports partition bytes 42726f6b656e (Broken), while
scua_load_script_file reports 000000000000 for the same six-byte name. Both worker
modes reproduce it in plain C without LÖVE or imports. Fault bytes are memcpy'd
inside the hook. Its exit 1 is the expected known-issue result, not a passing
regression test; it is intentionally outside CTest. The script regressions do not
claim file-loaded names are correct. Runtime identities remain usable.

Final validation: all 17 window-free CTests pass with audio, and all 15 pass
without audio. All 26 example entry scripts load under --check in the audio host;
the 22 entries without audio/hardware imports load in the no-audio host. Generated
declarations match both executables (22 modules with audio, 20 without).
The SDK helper also rejects the legacy source lacking SCUA_FAULT_ERROR before
building. The standalone known-issue diagnostic is excluded from these pass counts.

See [current design](SCUA_ACTORS.md) and the [manual SDK handoff](SCUA_039_HANDOFF.md).
The earlier global-halt/alias constraints in this document are historical and no
longer describe the current host. No graphical scenes were run for this follow-up.


## PR #833 source ownership — 2026-10-25

The fixed preview SDK is built from PR head
`85814a6491eb2db3d23dce79efc0c93dfb6024a1` (fix `0160c979`), using the existing
SDK helper, Zig 0.16.0, ReleaseFast, ABI 3 and FFI disabled. It still reports
0.38.0. The PR was not merged at verification time; the exact snapshot is fetched
into the isolated build mirror. Both hosts link this matching header/library pair.
The sibling SCUA checkout was not changed.

The former diagnostic is now a passing CTest named scua_fault_names in both builds.
It checks static string input, overwritten/freed caller-owned input immediately
after load, and file input, each with 0 and 4 workers. All six cases print
partition_hex=42726f6b656e (Broken) and the program exits 0. The overwrite uses
volatile stores so the compiler cannot discard it. The hook copies names during
the callback, as before. File-loaded script regressions also assert Broken, Spin,
NoAmmo and Tagged by value, replacing the temporary length-only assertions.

All 18 window-free checks pass in the audio-enabled build, and all 16 pass in the
audio-disabled build. This includes actor faults, Error/type-mismatch notifications,
reload, budgets, worker affinity/posting, CPU images, script checking and lifecycle
regressions. No graphical tests were run for this update. The earlier record of a
failing name diagnostic is historical and superseded by these results.

## Method audit and remaining macOS features — 2026-10-28

Both audio-enabled and audio-disabled hosts build against the unchanged pinned
PR #833 SDK. The registry now exposes 717 native functions in 22 modules; the
filesystem/stream/file-data/screenshot facades are additional SCUA code. Other
platforms and distribution work were excluded.

Replaced all 402 blanket Family mappings with explicit method destinations and
record fields. The generator checks field evidence in the named C++ function,
links to its source, resolves facade exports, and refuses same-name global/object
collisions. All 1,090 registrations in 73 wrapper files have a destination or an
explicit boundary: 377 Adapted, 293 Named, 311 Record, 66 Replacement, 15 Host-owned
and 28 Excluded. Zero Gap/Family rows means source coverage, not certified Lua
overload/default equality. Synthetic unreviewed methods/classes remain gaps.

The window-free suites pass 20 checks with audio and 18 without. New checks use
real directory and deflated-pack streams, CRLF/blank/EOF lines, byte positions,
read-ahead, seeking, full/line/zero-sized buffers, non-flushing size queries,
sync/append/exclusive/update writes, body-fault/failed-flush cleanup, named bytes,
rejected traversal/resource writes/symlink escapes, recovery from invalid UTF-8
and overlong lines, and a 34 MiB file processed in bounded chunks. Physics checks
cover bulk state/mass/damping, rejected atomic updates, omitted-field preservation
and rotated world/local joint axes. The desktop music probe matches upstream false.

All 27 example entry scripts load with `--check` and an explicit temporary save
grant; 23 entries compatible with disabled audio load in that host. Generated
declarations match both executables: 22 modules with audio, 20 without.

The three-frame `examples/compatibility` scene passes on Metal with gamma disabled,
Metal with gamma enabled, and OpenGL with gamma enabled. Pixel checks verify
shader colour clamping/conversion and rejected-update atomicity. The scene checks
runtime screenshot callbacks and polling, requests made by callbacks on the next
frame, filesystem PNG saving, cancellation on teardown, texture mip/depth/layer
metadata, quad viewport/layers, buffer usages and font DPI scale. The saved helper
PNG was inspected. Graphical development checks were confined to this small scene,
run sequentially at batch end; failed checks were rerun while diagnosing/fixing
the upstream linearity expectation and the SCUA nil-default record issue.
The full graphical suite and physical hardware checks were not run.

Stream buffered data requires explicit close/with_file; native teardown cannot
flush a forgotten SCUA buffer. Append position/path identity and renderer limits
remain documented in [runtime contracts](SCUA_RUNTIME.md). A typed-return record
with an `any = nil` default is rejected by this SDK; the screenshot helper uses
an explicit pending Error. The minimal observation is in the manual
[SCUA follow-up](SCUA_039_HANDOFF.md#additional-integration-observation--2026-10-28).
