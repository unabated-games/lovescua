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
