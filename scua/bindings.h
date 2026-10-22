// SCUA integration for this altered LÖVE fork. See ../license.txt.
#pragma once

#include <array>
#include <scua.h>
#include <set>
static_assert(SCUA_VERSION_MAJOR == 0 && SCUA_VERSION_MINOR == 37 && SCUA_VERSION_PATCH == 0,
              "Build lovescua with the SCUA 0.37.0 SDK");
#include "graphics/Graphics.h"
#include "window/sdl/Window.h"

namespace love::joystick
{
class JoystickModule;
}
namespace love::audio
{
class Audio;
class Source;
} // namespace love::audio

namespace love::mouse
{
class Mouse;
}
namespace love::sensor
{
class Sensor;
}

namespace lovescua
{
enum class ObjectKind
{
    Font,
    ImageData,
    Shader,
    SpriteBatch,
    Particles,
    SoundData,
    AudioSource,
    Decoder,
    Recording,
    Random,
    Curve,
    Transform,
    World,
    Body,
    Collider,
    Joint,
    Mesh,
    Text,
    Joystick,
    Video,
    Readback,
    Cursor,
    Buffer,
    CompressedImageData,
    Rasterizer,
    GlyphData,
    Count
};
struct ImageResource
{
    love::graphics::Texture *texture;
};
struct QuadResource
{
    love::graphics::Texture *texture;
    love::graphics::Quad *quad;
};
struct DialogInbox;
struct Host
{
    love::window::sdl::Window *window = nullptr;
    love::graphics::Graphics *graphics = nullptr;
    love::joystick::JoystickModule *joysticks = nullptr;
    love::audio::Audio *audio = nullptr;
    love::sensor::Sensor *sensors = nullptr;
    love::mouse::Mouse *mouse = nullptr;
    bool quitting = false;
    bool drawing = false;
    bool checking = false;
    bool cpuOnly = false, reloadRequested = false, staging = false;
    std::set<scua_fn> stagingUnsafe;
    std::string reloadError;
    std::shared_ptr<DialogInbox> dialogs;
    std::set<scua_fn> cpuAllowed;
    std::set<std::string> registeredBindings;
    scua_UserdataType imageType{}, quadType{};
    size_t liveImages = 0, liveQuads = 0;
    size_t imagesCreated = 0, imagesDestroyed = 0, quadsCreated = 0, quadsDestroyed = 0;
    std::array<scua_UserdataType, static_cast<size_t>(ObjectKind::Count)> objectTypes{};
    size_t liveObjects = 0;
    std::vector<std::pair<scua_Handle, scua_Handle>> pendingEvents;
    bool keyRepeat = false;
    bool ignoreInput = false;
    double delta = 0, elapsed = 0, averageDelta = 0;
    uint64_t frameCount = 0;
    uint64_t nextPhysicsWorldId = 1;
};

void registerBindings(scua_partition *partition, Host &host);
void registerGraphicsExtras(scua_partition *, Host &);
void registerResources(scua_partition *, Host &);
void registerImageExtras(scua_partition *, Host &);
void registerGraphics(scua_partition *, Host &);
void registerRendering(scua_partition *, Host &);
void registerBuffers(scua_partition *, Host &);
void registerTextures(scua_partition *, Host &);
void registerFonts(scua_partition *, Host &);
void registerPlatformExtras(scua_partition *, Host &);
void registerPlatform(scua_partition *, Host &);
void registerInput(scua_partition *, Host &);
void shutdownInput(Host &) noexcept;
void registerDrawables(scua_partition *, Host &);
void registerVideo(scua_partition *, Host &);
void registerPhysics(scua_partition *, Host &);
void registerBatches(scua_partition *, Host &);
void registerParticleSettings(scua_partition *, Host &);
void registerAudio(scua_partition *, Host &);
void registerAudioControls(scua_partition *, Host &);
love::audio::Audio &audioModule(scua_ctx *);
love::audio::Source *newAudioStream(scua_ctx *, const uint8_t *, size_t);
void shutdownAudio(Host &);
void registerData(scua_partition *, Host &);
void registerMath(scua_partition *, Host &);
void registerObjects(scua_partition *, Host &);
void returnImage(scua_ctx *, love::graphics::Texture *);
const std::vector<std::string> &bindingModules();
} // namespace lovescua
