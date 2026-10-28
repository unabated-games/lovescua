// SCUA integration for this altered LÖVE fork. See ../license.txt.
#include "binding_util.h"
#include "filesystem/FileData.h"
#include "image/ImageData.h"

#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>

namespace lovescua
{
namespace
{
using namespace binding;
template <typename T> struct Release
{
    void operator()(T *value) const noexcept
    {
        if (value)
            value->release();
    }
};
template <typename T> using EnginePtr = std::unique_ptr<T, Release<T>>;

int initializeImage(void *storage, size_t, void *argument) noexcept
{
    auto texture = static_cast<love::graphics::Texture *>(argument);
    texture->retain();
    static_cast<ImageResource *>(storage)->texture = texture;
    return SCUA_OK;
}

void destroyImage(void *storage, size_t, void *context) noexcept
{
    static_cast<ImageResource *>(storage)->texture->release();
    auto &state = *static_cast<Host *>(context);
    --state.liveImages;
    ++state.imagesDestroyed;
}

int initializeQuad(void *storage, size_t, void *argument) noexcept
{
    auto resource = static_cast<QuadResource *>(argument);
    resource->quad->retain();
    *static_cast<QuadResource *>(storage) = *resource;
    return SCUA_OK;
}

void destroyQuad(void *storage, size_t, void *context) noexcept
{
    // The SCUA dependency keeps the image alive until this destructor finishes.
    static_cast<QuadResource *>(storage)->quad->release();
    auto &state = *static_cast<Host *>(context);
    --state.liveQuads;
    ++state.quadsDestroyed;
}

void setMode(scua_ctx *ctx)
{
    arity(ctx, 2);
    double width = number(ctx, 0), height = number(ctx, 1);
    if (width < 64 || height < 64 || width > 8192 || height > 8192 || std::floor(width) != width ||
        std::floor(height) != height)
        throw std::invalid_argument("window dimensions must be integers between 64 and 8192");
    love::window::WindowSettings settings;
    settings.resizable = true;
    if (!host(ctx).window->setWindow(static_cast<int>(width), static_cast<int>(height), &settings))
        throw std::runtime_error(std::string("could not set window mode: ") + SDL_GetError());
}

void setTitle(scua_ctx *ctx)
{
    arity(ctx, 1);
    host(ctx).window->setWindowTitle(string(ctx, 0));
}

void dimensions(scua_ctx *ctx)
{
    arity(ctx, 0);
    float v[] = {static_cast<float>(host(ctx).graphics->getWidth()),
                 static_cast<float>(host(ctx).graphics->getHeight())};
    scua_return_vec(ctx, v, 2);
}

void setColor(scua_ctx *ctx)
{
    arity(ctx, 1);
    float c[4];
    vector(ctx, 0, c, 4);
    for (float &component : c)
        component = std::clamp(component, 0.0f, 1.0f);
    graphics(ctx).setColor(love::Colorf(c[0], c[1], c[2], c[3]));
}

void circle(scua_ctx *ctx)
{
    arity(ctx, 3);
    auto drawMode = mode(ctx);
    float pos[2];
    vector(ctx, 1, pos, 2);
    double radius = number(ctx, 2);
    if (radius <= 0 || radius > 8192)
        throw std::invalid_argument("radius must be in (0, 8192]");
    graphics(ctx).circle(drawMode, pos[0], pos[1], static_cast<float>(radius));
}

void rectangle(scua_ctx *ctx)
{
    arity(ctx, 3);
    auto drawMode = mode(ctx);
    float pos[2], size[2];
    vector(ctx, 1, pos, 2);
    vector(ctx, 2, size, 2);
    graphics(ctx).rectangle(drawMode, pos[0], pos[1], size[0], size[1]);
}

void printText(scua_ctx *ctx)
{
    arity(ctx, 2);
    auto text = string(ctx, 0);
    float pos[2];
    vector(ctx, 1, pos, 2);
    love::Matrix4 matrix(pos[0], pos[1], 0, 1, 1, 0, 0, 0, 0);
    graphics(ctx).print({{text, love::Colorf(1, 1, 1, 1)}}, matrix);
}

void newImage(scua_ctx *ctx)
{
    arity(ctx, 1);
    const uint8_t *bytes = nullptr;
    size_t length = 0;
    if (scua_arg_bytes(ctx, 0, &bytes, &length) != SCUA_OK)
        throw std::invalid_argument("new_image expects encoded image bytes");
    if (length == 0 || length > 16 * 1024 * 1024)
        throw std::invalid_argument("encoded image must contain 1 byte to 16 MiB");
    // Copy borrowed bytes before any further SCUA call can allocate or collect.
    EnginePtr<love::filesystem::FileData> file(new love::filesystem::FileData(length, "image"));
    std::memcpy(file->getData(), bytes, length);
    EnginePtr<love::image::ImageData> pixels(new love::image::ImageData(file.get()));
    if (pixels->getWidth() > 4096 || pixels->getHeight() > 4096)
        throw std::invalid_argument("image dimensions must not exceed 4096 pixels");
    love::graphics::Texture::Slices slices(love::graphics::TEXTURE_2D);
    slices.set(0, 0, pixels.get());
    love::graphics::Texture::Settings settings;
    returnImage(ctx, graphics(ctx, false).newTexture(settings, &slices));
}

void imageDimensions(scua_ctx *ctx)
{
    arity(ctx, 1);
    auto image = imageArgument(ctx, 0);
    if (!image)
        return;
    float size[] = {static_cast<float>(image->texture->getWidth()),
                    static_cast<float>(image->texture->getHeight())};
    scua_return_vec(ctx, size, 2);
}

void setFilter(scua_ctx *ctx)
{
    arity(ctx, 2);
    auto filter = string(ctx, 1);
    love::graphics::SamplerState::FilterMode mode;
    if (filter == "nearest")
        mode = love::graphics::SamplerState::FILTER_NEAREST;
    else if (filter == "linear")
        mode = love::graphics::SamplerState::FILTER_LINEAR;
    else
        throw std::invalid_argument("filter must be 'nearest' or 'linear'");
    auto image = imageArgument(ctx, 0);
    if (!image)
        return;
    auto sampler = image->texture->getSamplerState();
    sampler.minFilter = sampler.magFilter = mode;
    image->texture->setSamplerState(sampler);
}

void newQuad(scua_ctx *ctx)
{
    arity(ctx, 3);
    float position[2], size[2];
    vector(ctx, 1, position, 2);
    vector(ctx, 2, size, 2);
    auto image = imageArgument(ctx, 0);
    if (!image)
        return;
    if (std::abs(position[0]) > 1e6 || std::abs(position[1]) > 1e6 || size[0] <= 0 || size[1] <= 0 ||
        size[0] > 1e6 || size[1] > 1e6)
        throw std::invalid_argument("quad position/size exceeds supported range");
    love::graphics::Quad::Viewport viewport{position[0], position[1], size[0], size[1]};
    EnginePtr<love::graphics::Quad> quad(
        graphics(ctx, false).newQuad(viewport, image->texture->getWidth(), image->texture->getHeight()));
    QuadResource resource{image->texture, quad.get()};
    const int providerArgument = 0;
    if (scua_return_userdata_with_dependencies(ctx, host(ctx).quadType, sizeof(QuadResource), &resource,
                                               &providerArgument, 1) != SCUA_OK)
        return;
    ++host(ctx).liveQuads;
    ++host(ctx).quadsCreated;
}

void quadDimensions(scua_ctx *ctx)
{
    arity(ctx, 1);
    auto quad = quadArgument(ctx, 0);
    if (!quad)
        return;
    auto viewport = quad->quad->getViewport();
    float size[] = {static_cast<float>(viewport.w), static_cast<float>(viewport.h)};
    scua_return_vec(ctx, size, 2);
}

void drawImage(scua_ctx *ctx)
{
    auto matrix = transform(ctx);
    auto &gfx = graphics(ctx);
    auto image = imageArgument(ctx, 0);
    if (image)
        gfx.draw(image->texture, matrix);
}

void drawQuad(scua_ctx *ctx)
{
    auto matrix = transform(ctx);
    auto &gfx = graphics(ctx);
    auto quad = quadArgument(ctx, 0);
    if (quad)
        gfx.draw(quad->texture, quad->quad, matrix);
}

void releaseResource(scua_ctx *ctx)
{
    arity(ctx, 1);
    // Do not borrow first: an active native borrow would make close Busy.
    scua_close_userdata(ctx, 0);
}

void keyDown(scua_ctx *ctx)
{
    arity(ctx, 1);
    auto name = string(ctx, 0);
    SDL_Scancode key = SDL_GetScancodeFromKey(SDL_GetKeyFromName(name.c_str()), nullptr);
    if (key == SDL_SCANCODE_UNKNOWN)
        throw std::invalid_argument("unknown key name: " + name);
    int count = 0;
    const bool *keys = SDL_GetKeyboardState(&count);
    scua_return_bool(ctx, !host(ctx).ignoreInput && key < count && keys[key]);
}

void mousePosition(scua_ctx *ctx)
{
    arity(ctx, 0);
    float x, y;
    SDL_GetMouseState(&x, &y);
    if (host(ctx).ignoreInput)
        x = y = 0;
    double dx = x, dy = y;
    host(ctx).window->windowToDPICoords(&dx, &dy);
    float pos[] = {static_cast<float>(dx), static_cast<float>(dy)};
    scua_return_vec(ctx, pos, 2);
}

void quit(scua_ctx *ctx)
{
    arity(ctx, 0);
    host(ctx).quitting = true;
}

} // namespace

void returnImage(scua_ctx *ctx, love::graphics::Texture *texture)
{
    EnginePtr<love::graphics::Texture> owned(texture);
    if (scua_return_userdata(ctx, binding::host(ctx).imageType, sizeof(ImageResource), texture) != SCUA_OK)
        return;
    ++binding::host(ctx).liveImages;
    ++binding::host(ctx).imagesCreated;
}

void registerBindings(scua_partition *p, Host &state)
{
    scua_UserdataLimits limits{};
    limits.struct_size = sizeof(limits);
    limits.version = 1;
    limits.max_bytes = 1024 * 1024;
    limits.max_objects = 4096;
    limits.max_types = 32;
    limits.max_leases = 64;
    limits.max_dependencies = 4096;
    limits.max_graph_work = 16384;
    // Destructors run only at host frame boundaries or partition teardown.
    limits.auto_drain = 0;
    if (scua_userdata_configure(p, &limits) != SCUA_OK)
        throw std::runtime_error("could not configure managed resources");
    scua_UserdataTypeSpec type{};
    type.struct_size = sizeof(type);
    type.version = 1;
    type.automatic = 1;
    type.size = sizeof(ImageResource);
    type.alignment = alignof(ImageResource);
    type.initialize = initializeImage;
    type.destroy = destroyImage;
    type.destroy_context = &state;
    if (scua_userdata_register(p, &type, &state.imageType) != SCUA_OK)
        throw std::runtime_error("could not register image resource type");
    type.size = sizeof(QuadResource);
    type.alignment = alignof(QuadResource);
    type.initialize = initializeQuad;
    type.destroy = destroyQuad;
    if (scua_userdata_register(p, &type, &state.quadType) != SCUA_OK)
        throw std::runtime_error("could not register quad resource type");
    struct Binding
    {
        const char *module, *name;
        scua_fn fn;
        const char *signature, *doc;
    };
    const Binding bindings[] = {
        {"love_window", "set_mode", guarded<setMode>, "set_mode(width: int, height: int)",
         "Open a resizable window in logical pixels."},
        {"love_window", "set_title", guarded<setTitle>, "set_title(title: string)", "Set the window title."},
        {"love_graphics", "dimensions", guarded<dimensions>, "dimensions() -> vec2",
         "Logical drawing dimensions, independent of display DPI."},
        {"love_graphics", "set_color", guarded<setColor>, "set_color(color: vec4)",
         "Set RGBA drawing color; components are clamped to 0..1."},
        {"love_graphics", "circle", guarded<circle>, "circle(mode: string, position: vec2, radius: float)",
         "Draw a filled or outlined circle."},
        {"love_graphics", "rectangle", guarded<rectangle>,
         "rectangle(mode: string, position: vec2, size: vec2)", "Draw a filled or outlined rectangle."},
        {"love_graphics", "print", guarded<printText>, "print(text: string, position: vec2)",
         "Draw UTF-8 text with LÖVE's built-in font."},
        {"love_graphics", "new_image", guarded<newImage>, "new_image(encoded: bytes) -> any",
         "Decode image bytes into a managed GPU image."},
        {"love_graphics", "image_dimensions", guarded<imageDimensions>,
         "image_dimensions(image: any) -> vec2", "Get the size of a live image."},
        {"love_graphics", "set_filter", guarded<setFilter>, "set_filter(image: any, filter: string)",
         "Select nearest or linear image filtering."},
        {"love_graphics", "new_quad", guarded<newQuad>,
         "new_quad(image: any, position: vec2, size: vec2) -> any",
         "Create a sprite region which retains its source image."},
        {"love_graphics", "quad_dimensions", guarded<quadDimensions>, "quad_dimensions(quad: any) -> vec2",
         "Get a sprite region's size."},
        {"love_graphics", "draw", guarded<drawImage>,
         "draw(image: any, position: vec2, rotation: float = 0.0, scale: vec2 = vec2(1, 1), origin: vec2 = "
         "vec2(0, 0))",
         "Draw an image; rotation is radians, negative scale flips it."},
        {"love_graphics", "draw_quad", guarded<drawQuad>,
         "draw_quad(quad: any, position: vec2, rotation: float = 0.0, scale: vec2 = vec2(1, 1), origin: vec2 "
         "= vec2(0, 0))",
         "Draw a sprite region with position, rotation, scale and origin."},
        {"love_graphics", "release", guarded<releaseResource>, "release(resource: any)",
         "Close an image or quad; unused resources also clean up automatically."},
        {"love_keyboard", "is_down", guarded<keyDown>, "is_down(key: string) -> bool",
         "Read an SDL key name, such as Left or Space."},
        {"love_mouse", "position", guarded<mousePosition>, "position() -> vec2",
         "Mouse position in logical pixels."},
        {"love_event", "quit", guarded<quit>, "quit()", "Request a clean exit after this callback."},
    };
    for (auto &binding : bindings)
        registerFunction(p, state,
                         {binding.module, binding.name, binding.fn, binding.signature, binding.doc});
    registerGraphics(p, state);
    registerRendering(p, state);
    registerBuffers(p, state);
    registerTextures(p, state);
    registerPlatform(p, state);
    registerObjects(p, state);
    registerFonts(p, state);
    registerMath(p, state);
    registerData(p, state);
    registerAudio(p, state);
    registerAudioControls(p, state);
    registerBatches(p, state);
    registerParticleSettings(p, state);
    registerPhysics(p, state);
    registerVideo(p, state);
    registerDrawables(p, state);
    registerInput(p, state);
    registerImageExtras(p, state);
    registerPlatformExtras(p, state);
    registerResources(p, state);
    registerGraphicsExtras(p, state);
    registerJobs(p, state);
    registerScreenshots(p, state);
}

const std::vector<std::string> &bindingModules()
{
    static const std::vector<std::string> modules = {"love_window",
                                                     "love_object",
                                                     "love_jobs",
                                                     "love_job_reads",
                                                     "love_job_commands",
                                                     "love_graphics",
                                                     "love_keyboard",
                                                     "love_mouse",
                                                     "love_event",
                                                     "love_timer",
                                                     "love_system",
                                                     "love_image",
                                                     "love_font",
                                                     "love_math",
                                                     "love_data",
                                                     "love_video",
                                                     "love_physics",
                                                     "love_joystick",
                                                     "love_sensor",
                                                     "love_touch"
#ifdef LOVE_SCUA_AUDIO
                                                     ,
                                                     "love_audio",
                                                     "love_sound"
#endif
    };
    return modules;
}
} // namespace lovescua
