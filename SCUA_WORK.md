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
