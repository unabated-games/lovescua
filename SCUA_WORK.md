> Current status: trusted native paths and the behavioural follow-up are implemented and verified below. The isolated SDK includes PR #833 plus a filesystem-root boundary fix. Earlier entries retain historical SDK behavior/counts.

# Completed feature conversion batch

User scope: finish texture/GPU features, fonts/rasterizers, particles and remaining
audio/physics features end to end, then revisit concurrency. The requested feature
batches are implemented and verified below. Graphical checks were kept to affected
scenes at batch end. Concurrency is the next separate batch.

## Batch 1: textures and GPU drawing

Implemented and verified on Metal and OpenGL (three-frame scene). Metal covers
compute writes, retained native views and both indirect paths. OpenGL checks its
supported fallback and explicit view rejection. No-window checker regression
cases also pass. Both shader-driven indirect command offset bugs are fixed.

Delivered:

- Array, cube and volume creation from retained ImageData subresources.
- DDS/KTX/PVR/ASTC compressed data, metadata/block extraction and native uploads.
- Mipmap/layer updates, filtering/wrap/depth sampling, native retained views.
- Persistent depth/stencil attachments and explicit target layers/mipmaps.
- Compute-writable textures, format capability queries and storage image binding.
- GPU buffer/texture copies and same-format texture copies using GPU staging.
- Direct/indexed/indirect/indexed-indirect shader-generated draws.
- Indexed indirect command offset fix in the engine.
- One texture integration scene covering pixels, retention and rejected inputs.
- No-window script-definition validation to catch SCUA errors before graphics runs.

## Subsequent batches

- Fonts/particles: implemented and verified on Metal/OpenGL (three-frame scene). Native rasterizers,
  glyph data/metrics, Unicode metadata, fallback fonts, filtering, image and BMFont
  fonts, colored/cached text drawing and measurement. Complete particle record
  configuration/snapshots, retained texture/quad aliases and deterministic scene.
- Audio: controls, routing/getters, groups, device controls, PCM conversion,
  tracker decoding and embedded Vorbis video audio implemented. Silent-device
  three-frame integration passed. Ogg bounds and no-file-import unit checks passed.
  CPU-only tracker/Vorbis EOF, seek-after-EOF and rewind regressions also passed.
- Physics: implemented world policies/statistics, custom mass/shape properties,
  retained lookup aliases with shared invalidation, all joint family snapshots and
  checked settings, query/filter callbacks and persistent contact editing. The
  three-frame controls scene passed on Metal/OpenGL; existing Metal contact and
  joint scenes also passed. Typed SCUA entity records hold game-owned data.
- Final audit: registry/declarations refreshed (616 functions, 18 modules),
  build instructions and remaining-work list corrected. Both audio-enabled and
  audio-disabled builds passed. No-window declaration/script checks passed, and
  the physics scene definitions also load in the audio-disabled host.
- Final physics regression: distance intervals can move beyond their old maximum;
  rejected joint creation removes its unpublished native joint. Both renderer
  controls scenes passed again after these final changes.

A function count is not a parity claim. Physical playback device switching, HRTF
listening and microphone capture remain unverified; automated audio checks use
OpenAL’s silent backend. Renderer capabilities still apply: macOS OpenGL does not
support the tested Metal compute, indirect drawing and native-view paths. The
texture scene checks supported fallbacks and explicit rejection. Full LÖVE parity
remains separate work, listed in SCUA.md.

# Remaining macOS conversion batch (2026-10-22)

Authorized scope: complete remaining image APIs, platform/window operations and
sensor callbacks, logical game-state persistence, transactional hot reload,
stronger SCUA resource types, and a method-level upstream compatibility audit.
Concurrency awaits the SCUA team. Windows/Linux and distribution work are excluded.
Run only affected graphical scenes sequentially at batch end; use no-window checks
for codec/state/type validation. Hardware-only observations must be recorded as
observed, unavailable or requiring a manual check, never inferred from mocks.

Delivered:
- Concrete CPU formats/raw buffers, HDR pixels, conversion, overlap-safe paste,
  staged map callbacks, cube/volume extraction, compressed clones and native encoders.
- Full native window settings, display/platform metadata, clipboard/URL and IME APIs,
  asynchronous dialog inboxes and selected-file/folder read/write operations.
- Sensor callbacks, richer controller records/mappings and native event checks.
- Versioned logical JSON state, atomic saves and migration; headless execution;
  source/import watch and staged partition replacement with snapshot/restore.
- Optional enum-branded resource records, checked native kinds and close operations.
- Mesh/batch metadata/ranges/layers/attributes, pipeline/default sampler state,
  projection/discard/flush/current-object APIs, full curve/transform operations and
  spring parameter conversion identified by the upstream audit.
- Generated declarations: 700 functions across 19 modules. The method migration
  audit enumerates 1,090 Lua registrations from 73 files with explicit destinations,
  record families, language/host replacements, exclusions and pending concurrency.
  Family mappings are porting guidance, not certification of every Lua overload.

Validation:
- Both audio-enabled and audio-disabled builds compile; focused CPU/image/state,
  lifecycle/type, virtual input/sensor, dialog, checker and audit tests pass.
- Three short sequential graphical checks passed: platform on Metal/OpenGL and
  the existing Metal texture scene. No full graphical suite was run.
- Fixed a lifecycle test race using atomic replacement and an explicit handshake;
  pinned snapshot/restore callbacks; three repeated lifecycle runs pass in each build.
- Live CoreAudio: four outputs/four recording devices; current output reopened
  silently; one HRTF filter reported, disabled. No controllers/sensors attached.
  Capture, listening and real chooser interaction remain manual checks.

Details and limits are in SCUA_RUNTIME.md and SCUA_VALIDATION.md. Reload preserves
VM/resource and saved graphics/window state on failure, not external filesystem
writes or arbitrary OS effects; use reloaded() for external actions. Typed resources
are optional SCUA records around raw userdata, not nominal native declarations.
Windows/Linux, distribution and released-0.38 concurrency remain excluded/pending.

SCUA-team update: 0.38 frame control and multicore are implemented in unmerged PRs,
with the multicore C ABI moving to 3. They are not released. This batch remains
on the installed 0.37.0 ABI 2 SDK and does not introduce concurrency code.


## Platform keyboard follow-up

Fixed a manual-input regression reported on macOS: the platform example declared
`keypressed(key, repeat)` although the host supplies one scancode-name argument.
Its shortcuts also compared lowercase names against SDL's uppercase letter names.
The handler now follows the existing callback contract, uses O/S/M/C/U/R and
supports Escape to quit. A no-window regression executes the example's actual
handler with one argument and stubs OS actions, checking every shortcut. The prior
`--no-input` graphical checks did not exercise this callback.


## Sensor demo follow-up

Fixed the missing sensor setup in examples/platform. A SCUA monitor detects/enables
available accelerometers/gyroscopes, reports controller names separately from motion
capabilities, routes readings by controller instance ID, and rescans on hotplug.
First-update initialization avoids reload staging, already enabled sensors stay on,
and each scan closes temporary controller aliases. Errors remain visible without
terminating the demo. A window-free embedded SCUA regression covers unavailable,
controller-only, enabled, reading, hotplug/reload and failure paths. The native virtual
controller test now covers real sensor enable/data calls through the bindings and
runs the same monitor module against them, checking alias cleanup. Focused tests
pass in both builds, including a native run with MFI disabled. No windows opened.

Actual macOS check: this build links installed SDL 3.4.18; default settings enumerated
no controller, while SDL_JOYSTICK_MFI=0 detected an 8BitDo Ultimate 2C Wireless.
It advertised neither motion sensor. Backend selection remains an environment choice;
the example documents/shows the hint. Physical gyro/accelerometer readings remain
unverified; no graphics windows were opened for this follow-up.


## Video demo follow-up

The original demo used the upstream logo fixture: less than one second of video
and only a few distinct frames, so it appeared to be a stationary sprite after
startup. Replaced the demo input with an original 8-second Theora/Vorbis clip:
moving shapes, frame digits, progress bar and quiet ticks. Added automatic looping,
a pause state that prevents accidental restart while paused, and R to restart.
The asset generator is checked in; it uses libtheora and FFmpeg only at regeneration.

A window-free regression executes the actual demo controls. Renderer checks run
one short demo window and compare decoded/drawn canvas pixels at separate times,
paused, rewound and after EOF restart, alongside the existing callback verification.
Audio is routed to OpenAL's silent backend during verification.

Validation: controls pass in audio-enabled/audio-disabled builds; definitions load;
FFprobe reports 8 seconds and 240 video frames. Two short Metal runs passed
pixel-change, pause, rewind and EOF/restart checks with a video-only probe. The
final run also verifies the actual demo's audio-synchronized clock and rendered
frames advance, using the silent backend. No full graphical
suite or physical listening check was run. No engine C++ changes were required.


## Embedded concurrency batch (2026-10-24)

Completed the single-thread baseline and opt-in multicore integration using SCUA
0.38.0 ABI 3 from commit `cd480e93d71c4a903f140dbce2a523116b7fe4d4`.
The reproducible SDK helper snapshots tracked source without changing its checkout;
both local host builds now link that matching SDK.

- Explicit actor/task drains, host-owned fractional-ms time, pause/time scale and
  configurable cooperative time/turn budgets. The default is 64 runtime turns/2 ms,
  with zero workers; `--workers 4` enables the tested multicore path.
- Owner-thread engine bindings, parallel reads of copied snapshots and deferred
  scalar results. All physics operations and image mapping stay in serial callbacks
  to preserve synchronous SCUA reentry. Added post_update after workers join and
  deferred calls apply, before rendering.
- Once-only bounded faults, post inbox/backpressure, ordered command drains and
  runtime-local order keys. Hard operation-budget failure halts scheduling until
  reload; normal actor faults leave other work running. Reload recreates workers,
  actor identities and queues while restoring logical state/host time.
- Eight-agent actor arena: typed messages/records and vector planning; the main
  script owns Box2D objects and applies results. Native aliases fail inside actors
  in this SDK, so handlers use canonical native names in the entry script.
- Updated declarations/inventory: 710 functions in 22 modules. The upstream audit
  now describes actors as a language replacement for Lua threads/channels, without
  claiming shared-channel or Lua API equivalence.
- Fixed definition-only checking to honor explicit save-folder grants, allowing
  save-enabled examples to be checked without opening windows.

Validation: both builds pass their window-free suites (17 audio-enabled, 15 without
sound); affected scheduler/checker checks also pass after the final recovery/grant
changes. All 26 example entry scripts load under --check with an explicit temporary
save grant, and all 22 generated declaration modules match the executable. One
10-frame Metal run with four workers passed, produced 80 actor results with zero
faults, and its screenshot was inspected. No full graphical suite was run.

Design, SDK reproduction and limits: [SCUA_ACTORS.md](SCUA_ACTORS.md).
Manual SCUA-team handoff: [SCUA_038_HANDOFF.md](SCUA_038_HANDOFF.md).
Windows/Linux and distribution remain outside this batch.


## SCUA 0.39 preview follow-up (2026-10-24)

Built and pinned upstream main `65cabd9600383b0fda82909b7c72937565e2350d` in an
isolated build copy; the sibling checkout remains unchanged. These fixes ship in
0.39, but the source still reports 0.38.0 / ABI 3. CMake/headers require the new
fault notification API; version/ABI matching alone does not distinguish old 0.38.

- Moved the arena's native-calling handlers into `examples/actors/agents.scua`.
  Imported native aliases, raw native facades, imported wrappers and the nested
  jobs facade work with 0 and 4 workers.
- Removed global actor halt/reload recovery and info().halted. A positive-budget
  runaway faults once; another actor and the same actor's next message continue.
- Added distinct kind="error" records for unhandled Error results and visible
  type-mismatch post diagnostics. Immediate/direct and queued text refusal paths
  have native coverage, including copied hook strings.
- Updated strict turn accounting/fairness checks. Native fixtures verify lone
  infinite handlers/tasks with op_budget=0 return under either bound, and finite
  long handlers/tasks finish across calls without losing state.
- Isolated the remaining name problem to file loading with a standalone C SDK
  diagnostic: memory source reports Broken; the same file reports six NUL bytes,
  with both worker modes. This is reproducible while copying inside the hook.
  The diagnostic is separate from passing CTests and currently exits 1 as expected.

Validation: all 17 audio-enabled and 15 audio-disabled window-free checks pass;
all 26 example definitions load with audio, and the 22 compatible entries load
without audio. Generated declarations match both executables. No windows opened.

The previous 0.38 halt/alias limitations above are historical. Current behavior,
SDK build instructions and limits are in SCUA_ACTORS.md. The new manually
forwardable handoff, reproduction source and observed output are in
[SCUA_039_HANDOFF.md](SCUA_039_HANDOFF.md). Windows/Linux/distribution remain excluded.


## Source ownership / PR #833 (2026-10-25)

Pinned the SDK to PR #833 head `85814a6491eb2db3d23dce79efc0c93dfb6024a1`,
containing source-ownership fix `0160c979`. The PR was fetched into the isolated
build mirror, without changing the sibling SCUA checkout. It was not yet merged
into upstream main when fetched. The preview still reports 0.38.0 / ABI 3.

The compiled script now owns the source bytes its names borrow. Rebuilt both
hosts and promoted repro_fault_name.c to the window-free scua_fault_names CTest.
It checks static source, an overwritten/freed caller buffer, and file loading,
with both 0 and 4 workers. All six cases report Broken and exit 0. Volatile writes
ensure the caller-buffer overwrite survives optimization. Script actor/error/post
regressions now assert complete partition names instead of accepting byte lengths.

Validation: all 18 audio-enabled and 16 audio-disabled window-free checks pass.
No graphical scenes were run. Updated SDK reproduction instructions and marked
the historical name handoff resolved; there is no remaining failure from our
reported embedding issues in these checks. Platform/distribution exclusions and
manual hardware checks remain unchanged.

## Remaining feature gaps and audit (2026-10-28)

Completed within the requested three-hour cap, excluding platforms/distribution:

- Rooted SCUA streams with chunked bytes, UTF-8 lines, seek/tell/EOF/size,
  buffering, flush/sync, append/exclusive/update modes and scoped cleanup.
  Named bytes provide FileData metadata without native pointers.
- Runtime screenshot callbacks and polled results, bounded outstanding requests,
  post-presentation script delivery, managed image ownership and reload/teardown
  cancellation. An optional SCUA helper encodes and atomically saves captures.
- Shader colour uploads with clamping, gamma-aware RGB conversion and atomic
  input validation. Added the startup `--gamma-correct` option.
- Completed metadata/property gaps for texture mip/depth/layers/linearity/view
  formats, font DPI, buffer usages, quad viewport/source dimensions/layer setters,
  bulk body state/damping/mass and world-space joint axes. Desktop background-music
  queries match the upstream false result; system vibration remains unavailable.
- Removed all 402 class-wide Family mappings. The 1,090-entry index now names
  concrete methods/record fields or explicit language/host/platform boundaries.
  Unknown methods and object/global naming collisions remain visible as gaps.

Validation: 20 audio-enabled and 18 audio-disabled window-free CTests pass;
27/23 compatible example definitions load and 22/20 declaration modules match
their executables. The three-frame compatibility scene passes on Metal (normal
and gamma-correct) and gamma-correct OpenGL, checking pixels and capture lifetimes.
The saved PNG was inspected. No full graphical suite or new hardware checks ran.

The SDK rejects a typed-return record's missing `any = nil` default. The capture
helper uses `Error("capture pending")`; a small manual repro is recorded for the
SCUA team. Stream buffer finalization, append position/path identity, Lua overload
differences, capability boundaries and renderer limits remain explicit contracts,
not claims of complete Lua compatibility. See SCUA_RUNTIME.md and SCUA_VALIDATION.md.

## Trusted native paths and behaviour (2026-10-28 follow-up)

Implemented the requested explicit `--allow-native-fs` option and `nativefs`
facade, reusing SCUA's file capability and the existing stream implementation.
The grant survives reload, defaults remain rooted, and disabled access is tested.
A bug in the pinned SDK's `/` boundary check required a small isolated source
patch. The helper records the patch hash; the SCUA checkout stays unchanged. The
patch, reproduction and verified results are recorded for manual forwarding.

Reviewed upstream wrappers and corrected concrete behavioural mismatches: legacy
noise dimensional algorithms, explicit 1–4D noise, colour clamping/alpha handling,
RNG seed/default forms, curve defaults, nonmutating transform multiplication, PCM
defaults/interleaved samples and audio time units. Shader scalar/vector/matrix
uploads now cover integer/uint/bool data and consistently reject inactive uniforms.
The migration index points multiplication and simplex noise to their correct
destinations and records native paths as an opt-in adaptation.

Added direct-engine math reference comparisons, standard binary hash/codec cases,
PCM/source unit checks and native filesystem/reload tests. All 23 audio-enabled
and 20 audio-disabled window-free CTests pass. The three short sequential renderer
checks pass, and all 27/23 compatible example definitions and 23/21 declaration
modules are checked. The SDK's three filtered root tests pass. The API inventory
is now 721 functions/23 modules. No game, new platform or distribution work was done.

Remaining behaviour differences and the bounded coverage of these checks are
explicit in SCUA_RUNTIME.md and SCUA_VALIDATION.md; this is not a claim of complete
Lua overload equivalence or hardware certification.
