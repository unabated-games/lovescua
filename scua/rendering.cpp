// Additional render state and managed asynchronous GPU readback for SCUA.
#include "binding_util.h"
#include "graphics/GraphicsReadback.h"
#include "image/ImageData.h"
namespace lovescua
{
namespace
{
using namespace binding;
using Graphics = love::graphics::Graphics;
auto readback(scua_ctx *ctx)
{
    return objectArgument<love::graphics::GraphicsReadback>(ctx, 0, ObjectKind::Readback);
}
// An array element has no native argument index. Borrow it briefly, retain the
// native texture, and release the runtime lease before constructing render state.
love::StrongRef<love::graphics::Texture> textureValue(scua_ctx *ctx, scua_Handle value)
{
    auto p = scua_ctx_partition(ctx);
    void *storage = nullptr;
    scua_UserdataLease lease{};
    if (scua_userdata_borrow(p, value, host(ctx).imageType, &storage, &lease) != SCUA_OK)
        throw std::invalid_argument("canvases must contain live managed textures");
    love::StrongRef<love::graphics::Texture> result(static_cast<ImageResource *>(storage)->texture);
    if (scua_userdata_release(p, lease) != SCUA_OK)
        throw std::runtime_error("could not release texture lease");
    return result;
}
void set_canvases(scua_ctx *ctx)
{
    int count = scua_arg_count(ctx);
    if (count < 1 || count > 3)
        throw std::invalid_argument("set_canvases expects 1 to 3 arguments");
    auto &gfx = graphics(ctx);
    auto p = scua_ctx_partition(ctx);
    auto array = scua_arg(ctx, 0);
    if (scua_typeof(p, array) != SCUA_TYPE_ARRAY)
        throw std::invalid_argument("canvases must be an array");
    Graphics::RenderTargetsStrongRef targets;
    for (int i = 0; i <= 8; ++i)
    {
        auto item = scua_index(p, array, i);
        if (!scua_handle_valid(p, item))
            break;
        if (i == 8)
            throw std::invalid_argument("maximum 8 color targets");
        auto texture = textureValue(ctx, item);
        if (!texture->isRenderTarget())
            throw std::invalid_argument("set_canvases requires canvases");
        for (auto &target : targets.colors)
            if (target.texture.get() == texture.get())
                throw std::invalid_argument("duplicate render target");
        targets.colors.emplace_back(texture.get());
    }
    if (supplied(ctx, 1) && boolean(ctx, 1))
        targets.temporaryRTFlags |= Graphics::TEMPORARY_RT_DEPTH;
    if (supplied(ctx, 2) && boolean(ctx, 2))
        targets.temporaryRTFlags |= Graphics::TEMPORARY_RT_STENCIL;
    if (targets.colors.empty())
    {
        if (targets.temporaryRTFlags)
            throw std::invalid_argument("temporary depth/stencil requires a canvas");
        gfx.setRenderTarget();
    }
    else
        gfx.setRenderTargets(targets);
}
void clear(scua_ctx *ctx)
{
    int count = scua_arg_count(ctx);
    if (count < 1 || count > 3)
        throw std::invalid_argument("clear expects 1 to 3 arguments");
    float color[4];
    vector(ctx, 0, color, 4);
    int stencil = supplied(ctx, 1) ? integer(ctx, 1, 0, 255) : 0;
    double depth = optionalNumber(ctx, 2, 1);
    if (depth < 0 || depth > 1)
        throw std::invalid_argument("clear depth must be in [0,1]");
    graphics(ctx).clear(love::ColorD(color[0], color[1], color[2], color[3]), love::OptionalInt(stencil),
                        love::OptionalDouble(depth));
}
void set_stencil_mode(scua_ctx *ctx)
{
    arity(ctx, 2);
    auto name = string(ctx, 0);
    love::graphics::StencilMode mode;
    if (!love::graphics::getConstant(name.c_str(), mode) || mode == love::graphics::STENCIL_MODE_CUSTOM)
        throw std::invalid_argument("stencil mode must be off, draw or test");
    int value = integer(ctx, 1, 0, 255);
    graphics(ctx).setStencilMode(mode, value);
}
void stencil_mode(scua_ctx *ctx)
{
    arity(ctx, 0);
    int value;
    auto mode = graphics(ctx, false).getStencilMode(value);
    const char *name = "unknown";
    love::graphics::getConstant(mode, name);
    Record r(ctx);
    r.str("mode", name);
    r.integer("value", value);
    r.done();
}
void set_stencil_state(scua_ctx *ctx)
{
    arity(ctx, 5);
    love::graphics::StencilState state;
    auto action = string(ctx, 0), compare = string(ctx, 1);
    if (!love::graphics::getConstant(action.c_str(), state.action) ||
        !love::graphics::getConstant(compare.c_str(), state.compare))
        throw std::invalid_argument("invalid stencil action or comparison");
    state.value = integer(ctx, 2, 0, 255);
    state.readMask = integer(ctx, 3, 0, 255);
    state.writeMask = integer(ctx, 4, 0, 255);
    graphics(ctx).setStencilState(state);
}
void set_depth_mode(scua_ctx *ctx)
{
    arity(ctx, 2);
    auto name = string(ctx, 0);
    love::graphics::CompareMode mode;
    if (!love::graphics::getConstant(name.c_str(), mode))
        throw std::invalid_argument("invalid depth comparison");
    bool write = boolean(ctx, 1);
    graphics(ctx).setDepthMode(mode, write);
}
void depth_mode(scua_ctx *ctx)
{
    arity(ctx, 0);
    love::graphics::CompareMode mode;
    bool write;
    graphics(ctx, false).getDepthMode(mode, write);
    const char *name = "unknown";
    love::graphics::getConstant(mode, name);
    Record r(ctx);
    r.str("compare", name);
    r.boolean("write", write);
    r.done();
}
void set_color_mask(scua_ctx *ctx)
{
    arity(ctx, 4);
    love::graphics::ColorChannelMask mask{boolean(ctx, 0), boolean(ctx, 1), boolean(ctx, 2), boolean(ctx, 3)};
    graphics(ctx).setColorMask(mask);
}
void texture_readback_async(scua_ctx *ctx)
{
    arity(ctx, 1);
    auto t = imageArgument(ctx, 0);
    if (t)
    {
        love::Rect rect{0, 0, t->texture->getPixelWidth(), t->texture->getPixelHeight()};
        returnObject(ctx, ObjectKind::Readback,
                     graphics(ctx, false).readbackTextureAsync(t->texture, 0, 0, rect, nullptr, 0, 0));
    }
}
void readback_status(scua_ctx *ctx)
{
    arity(ctx, 1);
    auto r = readback(ctx);
    if (r)
    {
        r->update();
        returnString(ctx, r->hasError() ? "error" : r->isComplete() ? "complete" : "waiting");
    }
}
void readback_wait(scua_ctx *ctx)
{
    arity(ctx, 1);
    auto r = readback(ctx);
    if (r)
    {
        r->wait();
        if (r->hasError())
            throw std::runtime_error("GPU readback failed");
    }
}
void readback_image(scua_ctx *ctx)
{
    arity(ctx, 1);
    auto r = readback(ctx);
    if (r)
    {
        r->update();
        if (r->hasError())
            throw std::runtime_error("GPU readback failed");
        auto image = r->getImageData();
        if (image)
        {
            image->retain();
            returnObject(ctx, ObjectKind::ImageData, image);
        }
        else
            scua_return_value(ctx, scua_nil(scua_ctx_partition(ctx)));
    }
}
} // namespace
void registerRendering(scua_partition *p, Host &state)
{
    registerObjectType(p, state, ObjectKind::Readback);
    registerFunctions(
        p, state,
        {
            {"love_graphics", "set_canvases", guarded<set_canvases>,
             "set_canvases(canvases: any, depth: bool = false, stencil: bool = false)",
             "Bind up to eight color targets with optional temporary depth/stencil; [] restores the window."},
            {"love_graphics", "clear", guarded<clear>,
             "clear(color: vec4, stencil: int = 0, depth: float = 1)",
             "Clear the active color, stencil and depth targets."},
            {"love_graphics", "set_stencil_mode", guarded<set_stencil_mode>,
             "set_stencil_mode(mode: string, value: int)",
             "Set off, draw or test mode; draw disables color writes, test/off enables them."},
            {"love_graphics", "stencil_mode", guarded<stencil_mode>, "stencil_mode() -> any",
             "Get stencil mode and reference value."},
            {"love_graphics", "set_stencil_state", guarded<set_stencil_state>,
             "set_stencil_state(action: string, compare: string, value: int, read_mask: int, write_mask: "
             "int)",
             "Set explicit stencil state with eight-bit masks; color mask is independent."},
            {"love_graphics", "set_depth_mode", guarded<set_depth_mode>,
             "set_depth_mode(compare: string, write: bool)", "Set depth comparison and writes."},
            {"love_graphics", "depth_mode", guarded<depth_mode>, "depth_mode() -> any",
             "Get depth compare and write state."},
            {"love_graphics", "set_color_mask", guarded<set_color_mask>,
             "set_color_mask(red: bool, green: bool, blue: bool, alpha: bool)",
             "Select writable color channels."},
            {"love_graphics", "texture_readback_async", guarded<texture_readback_async>,
             "texture_readback_async(texture: any) -> any", "Start a managed GPU-to-CPU texture transfer."},
            {"love_graphics", "readback_status", guarded<readback_status>,
             "readback_status(readback: any) -> string", "Poll waiting, complete or error without blocking."},
            {"love_graphics", "readback_wait", guarded<readback_wait>, "readback_wait(readback: any)",
             "Wait for a GPU transfer to finish."},
            {"love_graphics", "readback_image", guarded<readback_image>,
             "readback_image(readback: any) -> any", "Retain completed ImageData, or nil while waiting."},
        });
}
} // namespace lovescua
