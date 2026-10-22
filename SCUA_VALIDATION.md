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
