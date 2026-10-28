# CPU images, desktop services and game lifecycle

These additions target macOS and the pinned SCUA 0.39 preview / ABI 3 SDK (still reporting 0.38.0). Bounded actors,
host-driven time and opt-in workers are implemented; see [SCUA_ACTORS.md](SCUA_ACTORS.md). Windows/Linux and distribution work are outside this batch.

## Run without a window

```sh
./build/lovescua --headless --verify --frames 1 examples/cpu_images
./build/lovescua --headless --verify --frames 3 \
  --save-dir "$PWD/build/demo-saves" examples/state_reload
./build/lovescua --headless examples/hardware
```

`--headless` executes `load`, a bounded number of `update` callbacks and optional
`verify`, without creating a window or graphics context. It defaults to one frame.
Images, math, physics, sound/audio, timer and managed object operations are
available. Audio is optional and can still open a real device: headless means no
window, not silent audio. Rendering, window and input operations fault explicitly.
Resource mounts and save grants work in this mode. `--check GAME` only loads
script definitions and executes no callbacks; all top-level engine calls fault.

## CPU images

`love.image.formats()` enumerates concrete native CPU formats. `new_data(w,h,
format,bytes)` accepts an optional exact-size raw buffer. `info` reports size,
format, linearity and `pixel_access`. Some integer formats only support raw bytes;
pixel access/conversion is rejected for them. Float formats preserve HDR channels.
Images are limited to 4096 pixels per axis and 64 MiB for checked allocations.
Encoded decode remains bounded by input/dimension checks, but native decoder
allocation occurs before the decoded dimension check; it is outside the VM cap.

`convert`, `paste`, `set_linear`, `clone`, `cube_faces` and `volume_layers` work
on retained native data. Overlapping self-paste copies the source region first.
`map_pixels(image, callback, region: vec4)` calls
`callback(x, y, old_color: vec4)` and expects a finite vec4. The optional region is `vec4(x,y,width,height)` and defaults
to the full image. A map is limited to 16,384 pixels, stages its own pixel writes, and commits none of them if the callback fails.
Callbacks should not mutate the image directly; arbitrary callback side effects
are not rolled back. Large image operations should use raw bytes
or several bounded regions. PNG/TGA/QOI/EXR encoding uses native format support;
check `can_encode(pixel_format, encoded_format)` rather than assuming all pairs work.
Compressed images have separate metadata, block bytes, clone and linearity APIs.

## Optional typed resource records

```scua
import love
import handles
fn red(data: handles.ImageData) -> float
  return love.image.get_pixel(data.raw, 0, 0).x
end
fn load()
  let pixels = handles.image_data(love.image.new_data(8, 8))
  assert(red(pixels) == 0)
  handles.close(pixels)
end
```

`handles` supplies 28 resource record types with distinct enum brands. Factories
validate the native resource kind; the compiler rejects passing a Texture record
where an ImageData record is required. Native functions still accept the opaque
value in `.raw`, and runtime validation remains authoritative. These are optional
SCUA records, not a change to the language's userdata type system. Do not construct
or forge their fields yourself. `love.object.kind`, `assert_kind`, `is_open` and
`close` also work on raw resources. `is_open` describes the wrapper handle; physics
objects may have been explicitly destroyed even while their wrapper is open.
Closing a wrapper releases its ownership; native dependents may retain the object.

## Logical saves and reload

`game_state.encode/decode/save/load` use a versioned JSON envelope and Ok/Error
results for decode and file operations. Saves use atomic replacement through the
existing `filesystem` facade and require a save grant. The encoded limit is 4 MiB.
A migration callback receives `(data, old_version, new_version)` and returns the
new logical data. Native resources cannot be serialized. Keep paths, entity IDs,
player records and progress in the save; rebuild engine objects after loading.
JSON does not preserve SCUA enum/record identity automatically: validate and rebuild
your application's typed records when decoding untrusted or older saves.

`--watch GAME` watches project `.scua` files (including imports), with limits of
2,048 files and eight nested directory levels. It does not watch external runtime
modules or assets. `love.event.reload()` requests the same reload at the next
frame boundary. `reload_status()` returns `requested` and the most recent error.

The host builds a fresh partition and validates definitions first, then calls the
old game's optional `snapshot() -> string`, the new `load()` and optional
`restore(encoded: string)`. If a nonempty snapshot exists, restore is required.
Syntax, load or restore failure keeps the old partition active. Graphics state and
window mode/title are restored on failure. On success the new partition commits,
old resources are released, old audio sources stop, and optional `reloaded()` runs.
A failure in `reloaded()` is reported after commit; it does not roll back the new game.
Snapshot strings must fit 4 MiB. Reload temporarily holds two VM/resource sets,
which increases peak memory. This is source reload with logical state transfer,
not native-resource persistence or a general transaction across the operating system.

Use `load` and `restore` to build resources and logical data. Use `reloaded` for
external effects such as starting audio, changing input modes, dialogs, clipboard
writes or focus requests. On initial launch, start these from a one-time first-update
step; `reloaded` is called only after a replacement commits. The host rejects these operations while staging a reload.
Direct SCUA `fs` writes made through a granted capability are **not rolled back**;
keep them out of load/restore. Files, GPU image contents, device effects and mutations
performed outside the saved graphics/window state do not gain automatic rollback.
See `examples/state_reload` and the graphical reload in `examples/platform`.

## Desktop services and input

`love.window.configure(settings)` exposes the full native window settings record;
`mode()` reports it. Settings use zero-based displays and include fullscreen type,
vsync, MSAA, stencil/depth, resizability, border/position, DPI scaling and size limits.
Invalid settings fail before calling the OS. Display modes/orientation, theme,
locales, icon aliases, key/scancode conversion and IME placement are also exposed.
New mesh/batch metadata and checked draw ranges complement the existing GPU APIs;
`pipeline_state/configure_pipeline` expose custom blending, culling and winding.
`default_sampler/set_default_sampler` control newly created texture filters.
Projection, flush, discard, current-shader and active-target operations use native
engine state. See the declarations for exact signatures and record fields.

`show_file_dialog(kind, settings)` returns a request ID. Poll it on the main thread
with `poll_file_dialog`, then release it. Kinds are `open`, `folder` and `save`.
The SDL callback only copies a bounded result into a mutex-protected inbox;
it never calls SCUA. Completion remains safe if the game is replaced meanwhile.
There are at most 32 pending/retained requests and 256 selected paths per request.
Cancellation and errors are distinct results. Releasing a request does not close
a chooser already shown by the OS.

Selected paths do not grant general filesystem access. `read_selected_file` reads
a successful open selection; `write_selected_file` writes a successful save selection.
A chosen folder grants `list_selected_folder` and bounded relative reads inside its
canonical root; absolute paths, parent traversal and symlink escapes are rejected.
These checks assume no hostile process races the filesystem between checking and
opening a path; they are not an OS sandbox or race-proof descriptor capability.
Dialog reads/writes are limited to 32 MiB. Clipboard strings are limited to 1 MiB.
`open_url` accepts only http, https and mailto. Platform-wide vibration reports false
on macOS. Interactive dialogs are disabled with `--no-input`.

`examples/platform` provides manual O/S/M/C/U keys for file open/save, message box,
clipboard and URL actions, plus R to reload. Automatic verification opens no dialogs
and restores the clipboard immediately after its check. Standalone sensors call
`sensorupdated(id, kind, value: vec3, time_seconds)`; gamepad sensors call
`gamepadsensorupdated` with the same arguments. Enable supported sensors explicitly.
The platform example now detects/enables both kinds on the first update and rescans
when controllers connect or disconnect. It lists controller names separately from
motion capabilities, shows waiting/live readings and enable errors, and releases
scan aliases. Accelerometer values use m/s² and gyroscope values use rad/s.
Initialization runs outside reload staging and preserves sensors already enabled.

Some macOS controllers need Apple's controller backend disabled at process startup:

```sh
SDL_JOYSTICK_MFI=0 ./build/lovescua examples/platform
```

The hint changes detection; it does not add motion capabilities to a controller.
The host leaves backend selection to SDL and your environment.
Physical controller/sensor coverage depends on attached hardware; see
[the validation record](SCUA_VALIDATION.md).

## Rooted file streams

`import filesystem` now includes `filesystem.stream` (also available as
`import file_stream`). `filesystem.open(path, mode)` returns `Ok(Stream)` or
`Error(reason)`. Modes are `r`, `w` (create/truncate), `a` (append), `create`
(exclusive new file), and `update` (overwrite an existing file without truncation).
Only `r` reads; `update` is a write mode, following the SDK contract. Unprefixed
reads use `res:`; writes use `user:` and require the host's explicit save grant.
Resource packs are read-only. Traversal and escaping symlinks are refused.

The stream namespace provides `read`, `read_line`, `write`, `seek`, `tell`,
`get_size`, `is_eof`, `flush`, `sync`, `close`, `abort`, `set_buffer`, `get_buffer`,
and filename/mode/open-state queries. I/O returns Results. `read` returns bytes,
defaults to 64 KiB and accepts 0–16 MiB per call. Process larger files in chunks;
the whole file need not fit in one VM value. `read_line` returns UTF-8 text without
its terminator, an empty string for a blank line, and nil at EOF. Its SDK limit is
65,535 bytes per line. Failed text decoding/overlong lines restore the logical
position so callers can recover with byte reads. `lines(stream)` returns a closure
whose successive calls return those same Results; it is not a Lua iterator.

Positions are zero-based byte offsets. Seek origins are `start`, `current` and
`end`; append handles cannot seek. Deflated resource seeks may decode from the
start. `tell` includes pending writes, and `get_size` preserves position and does
not flush. Append size uses the rooted path's stat plus buffered bytes; it cannot
follow the original open file after another writer renames or unlinks that path.
Concurrent appenders can also change the physical end independently of `tell`.

Buffering defaults to `none`. `full` and `line` buffer up to a configured
0–1 MiB (default 8 KiB); size zero writes immediately. Line mode flushes through
the last newline and retains the tail. `flush` writes pending bytes; `sync` also
requests durable disk synchronization. A failed flush keeps pending bytes for an
explicit flush retry. A failed multi-chunk `write` may have accepted a prefix;
retrying the whole write can duplicate it.

Use explicit `close`, or `with_file(path, mode, body)`. The latter catches body
faults, flushes/closes, and returns a Result containing the body's ordinary value.
If the body returns a Result, that remains a nested Result. Failed close flushes
are reported and the handle is released. `abort` deliberately drops pending bytes.
SDK handles close at partition teardown, but SCUA-side buffered writes have no
automatic finalizer: forgotten buffered data is lost. Handles are partition-local,
cannot be messaged to actors or persisted, and should be omitted from logical
snapshots. Game code should not mutate the facade's internal handle/buffer fields.

`filesystem.append`, `mount`, `get_identity`, `new_file_data` and `read_file_data`
complete the rooted facade. FileData is a typed `{data: bytes, filename: string}`
record; pass `.data` to engine byte APIs. Cloning copies metadata while immutable
bytes may be shared. Identity/import roots/resource layers are configured by the
host. `filesystem.exists` usually returns bool but preserves an SDK Error if
permissions or I/O prevent determining existence.

## Runtime screenshots and shader colours

`love.graphics.capture_screenshot(callback = nil)` returns a request ID. After the
next successful presentation, a callback receives managed ImageData. Without a
callback, `screenshot_results()` drains `{id, image, error}` records during a later
update. Requests wait while the window is inactive. Renderer callbacks only retain
pixels; script callbacks run after presentation on the main script. A request made
inside a callback belongs to the next frame. Callback faults are reported like
other main-script callback faults. A renderer failure may deliver nil pixels or
terminate the frame with its native error.

At most eight captures may be outstanding, including unpolled results. Capture
dimensions are limited to 16 million pixels / 64 MiB at request time. Close images
with `love.object.close(image)` when finished, or let ordinary managed collection
reclaim them. Pending callbacks and unpolled images are released on reload/shutdown.
Actors and reload staging cannot request captures; actors can exchange copied
encoded bytes and IDs through their normal messages.

`import screenshots` adds `screenshots.save(path, format = "png")`, using image
encoding and the granted filesystem's atomic write. It returns a typed record
with `id`, `done` and `result`; wait until `done` before consuming the Result.
The initial result is `Error("capture pending")`. This also avoids a current SDK
edge case: a typed return record with an `any = nil` default is rejected as missing
that field. No extra filesystem capability is needed for callback/polled captures;
only the optional save helper imports the filesystem.

`shader_send_color(shader, name, color)` accepts a vec3/vec4, a numeric component
array, or an array of colours. It checks active vec3/vec4 float uniforms, clamps
channels to 0–1, and converts RGB to linear only when gamma correction is enabled.
Alpha remains linear. All supplied colours are validated before uniform mutation;
arrays are bounded by the declared uniform count and 1,024 colours. Start the host
with `--gamma-correct` to enable gamma correction before renderer initialization.
The default remains false, matching the upstream engine's default.

The audit also added texture mip/depth/layer/colour-space/view-format metadata,
font DPI scale, GPU buffer usages, quad viewport/source dimensions/layer editing,
checked bulk body state/damping/mass updates, and world-space joint axes alongside
their local axes. Quad viewports may extend beyond source dimensions for wrapping.
`has_background_music()` returns false on desktop, as upstream does; macOS system
vibration remains unavailable, with gamepad rumble a separate capability.

## Compatibility boundaries

[The method migration audit](SCUA_COMPATIBILITY.md) enumerates all Lua registration
tables in this checkout and checks native destinations against generated declarations.
It distinguishes named functions, explicit record fields with source links,
language replacements, host-owned behavior and exclusions. Unreviewed methods
remain gaps rather than inheriting a class-wide coverage label. There is no Lua
source compatibility or certified method-by-method behavioral parity.

SCUA bytes/packed arrays replace Lua Data and binary pointer access. Files use
res:/user: capabilities rather than Lua File objects and ambient paths. Physics
colliders combine attached shape/fixture lifetimes, meters are per world, and game
records live in SCUA keyed by IDs. Spring conversion uses `spring_parameters` with
forward/inverse and angular settings. Event pumping/presentation belong to the host.
These differences are intentional integration design; upstream scripts need porting.
