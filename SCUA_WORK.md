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
