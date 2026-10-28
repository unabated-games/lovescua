// Layered/compressed/depth/storage textures, GPU copies and shader-driven draws.
#include "data/ByteData.h"
#include "graphics/Buffer.h"
#include "graphics/GraphicsReadback.h"
#include "image/CompressedImageData.h"
#include "image/Image.h"
#include "options.h"
namespace lovescua
{
namespace
{
using namespace binding;
using namespace love::graphics;
using love::StrongRef;
template <class T> using Owned = std::unique_ptr<T, void (*)(T *)>;
template <class T> Owned<T> own(T *p)
{
    return {p, [](T *v)
            {
                if (v)
                    v->release();
            }};
}
Texture *texture(scua_ctx *ctx, int index = 0)
{
    auto r = imageArgument(ctx, index);
    return r ? r->texture : nullptr;
}
Buffer *buffer(scua_ctx *ctx, int index)
{
    return objectArgument<Buffer>(ctx, index, ObjectKind::Buffer);
}
template <class T> StrongRef<T> resource(scua_ctx *ctx, scua_Handle value, scua_UserdataType type)
{
    auto p = scua_ctx_partition(ctx);
    void *storage = nullptr;
    scua_UserdataLease lease{};
    if (scua_userdata_borrow(p, value, type, &storage, &lease) != SCUA_OK)
        throw std::invalid_argument("expected a live managed texture/image");
    T *object;
    if constexpr (std::is_same_v<T, Texture>)
        object = static_cast<ImageResource *>(storage)->texture;
    else
        object = static_cast<T *>(static_cast<ObjectResource *>(storage)->object);
    StrongRef<T> result(object);
    if (scua_userdata_release(p, lease) != SCUA_OK)
        throw std::runtime_error("could not release resource lease");
    return result;
}
Texture::Settings settings(scua_ctx *ctx, int index)
{
    Options o(ctx, index);
    Texture::Settings s;
    s.width = o.integer("width", 1, 1, 4096);
    s.height = o.integer("height", 1, 1, 4096);
    s.layers = o.integer("layers", 1, 1, 256);
    auto type = o.str("type", "2d"), format = o.str("format", "normal"), mips = o.str("mipmaps", "none");
    if (!Texture::getConstant(type.c_str(), s.type) || !love::getConstant(format.c_str(), s.format) ||
        !Texture::getConstant(mips.c_str(), s.mipmaps))
        throw std::invalid_argument("invalid texture type, format or mipmaps mode");
    s.mipmapCount = o.integer("mipmap_count", 0, 0, 13);
    s.linear = o.boolean("linear", false);
    s.renderTarget = o.boolean("render_target", false);
    s.computeWrite = o.boolean("compute_write", false);
    s.msaa = o.integer("msaa", 1, 1, 16);
    s.dpiScale = o.num("dpi_scale", 1);
    if (s.dpiScale <= 0 || s.dpiScale > 16)
        throw std::invalid_argument("dpi_scale must be in (0,16]");
    if (o.has("readable"))
        s.readable = love::OptionalBool(o.boolean("readable", true));
    s.debugName = o.str("name", "");
    if (o.has("view_formats"))
    {
        auto array = o.field("view_formats");
        if (scua_typeof(o.p, array) != SCUA_TYPE_ARRAY)
            throw std::invalid_argument("view_formats must be an array");
        for (int i = 0; i <= 16; ++i)
        {
            auto item = scua_index(o.p, array, i);
            if (!scua_handle_valid(o.p, item))
                break;
            if (i == 16)
                throw std::invalid_argument("maximum 16 view formats");
            size_t size = 0;
            auto text = scua_to_lstring(o.p, item, &size);
            love::PixelFormat format;
            if (!text || std::memchr(text, 0, size) || !love::getConstant(text, format))
                throw std::invalid_argument("invalid view format");
            s.viewFormats.push_back(format);
        }
    }
    return s;
}
void allocationLimit(const Texture::Settings &s, int w, int h, int layers, love::PixelFormat format)
{
    if (w < 1 || h < 1 || w > 4096 || h > 4096 || layers < 1 || layers > 256)
        throw std::invalid_argument("texture dimensions exceed 4096 pixels or 256 slices");
    if (format == love::PIXELFORMAT_NORMAL || format == love::PIXELFORMAT_HDR)
        format = love::PIXELFORMAT_RGBA32_FLOAT;
    uint64_t size = love::getPixelFormatSliceSize(format, w, h);
    size *= layers;
    size *= s.msaa > 1 ? s.msaa + 1 : 1;
    if (s.mipmaps != Texture::MIPMAPS_NONE)
        size *= 2;
    if (size > 256 * 1024 * 1024)
        throw std::invalid_argument("texture allocation exceeds 256 MiB");
}
void new_texture(scua_ctx *ctx)
{
    if (scua_arg_count(ctx) < 1 || scua_arg_count(ctx) > 2)
        throw std::invalid_argument("new_texture expects settings and optional image slices");
    auto s = settings(ctx, 0);
    Texture::Slices slices(s.type);
    bool hasData = supplied(ctx, 1);
    if (hasData)
    {
        auto p = scua_ctx_partition(ctx);
        auto input = scua_arg(ctx, 1);
        if (scua_typeof(p, input) != SCUA_TYPE_ARRAY)
            throw std::invalid_argument("texture data must be an array of image slices");
        for (int i = 0; i <= 4096; i++)
        {
            auto value = scua_index(p, input, i);
            if (!scua_handle_valid(p, value))
                break;
            if (i == 4096)
                throw std::invalid_argument("maximum 4096 texture subresources");
            int slice = i, mip = 0;
            if (scua_typeof(p, value) == SCUA_TYPE_TABLE)
            {
                Options entry(p, value);
                slice = entry.integer("layer", 0, 0, 255);
                mip = entry.integer("mipmap", 0, 0, 12);
                value = entry.field("data");
            }
            if (slice > 255)
                throw std::invalid_argument("maximum 256 image slices");
            auto data = resource<love::image::ImageData>(
                ctx, value, host(ctx).objectTypes[size_t(ObjectKind::ImageData)]);
            if (data->getWidth() > 4096 || data->getHeight() > 4096)
                throw std::invalid_argument("image slice exceeds 4096 pixels");
            if (slices.get(slice, mip))
                throw std::invalid_argument("duplicate texture layer/mipmap");
            slices.set(slice, mip, data.get());
        }
        if (!slices.get(0, 0))
            throw std::invalid_argument("texture data requires layer zero, mipmap zero");
        if (s.type == TEXTURE_2D && slices.getSliceCount() != 1)
            throw std::invalid_argument("2d textures require one image layer");
        if (!slices.validate())
            throw std::invalid_argument("incomplete texture slices or mismatched image formats/dimensions");
        auto first = slices.get(0, 0);
        allocationLimit(s, first->getWidth(), first->getHeight(), slices.getSliceCount(), first->getFormat());
    }
    else
        allocationLimit(s, int(s.width * s.dpiScale + 0.5), int(s.height * s.dpiScale + 0.5),
                        s.type == TEXTURE_CUBE ? 6 : s.layers, s.format);
    returnImage(ctx, graphics(ctx, false).newTexture(s, hasData ? &slices : nullptr));
}
auto compressed(scua_ctx *ctx, int index = 0)
{
    return objectArgument<love::image::CompressedImageData>(ctx, index, ObjectKind::CompressedImageData);
}
void decode_compressed(scua_ctx *ctx)
{
    arity(ctx, 1);
    const uint8_t *data = nullptr;
    size_t size = 0;
    if (scua_arg_bytes(ctx, 0, &data, &size) != SCUA_OK || size == 0 || size > 32 * 1024 * 1024)
        throw std::invalid_argument("compressed image file must contain 1 byte to 32 MiB");
    auto bytes = own(new love::data::ByteData(size));
    std::memcpy(bytes->getData(), data, size);
    auto image = love::Module::getInstance<love::image::Image>(love::Module::M_IMAGE);
    auto result = own(image->newCompressedData(bytes.get()));
    if (result->getWidth() > 4096 || result->getHeight() > 4096 || result->getSliceCount() > 256 ||
        result->getMipmapCount() > 13 || result->getSize() > 32 * 1024 * 1024)
        throw std::invalid_argument("compressed image exceeds texture limits");
    returnObject(ctx, ObjectKind::CompressedImageData, result.release());
}
void compressed_info(scua_ctx *ctx)
{
    if (scua_arg_count(ctx) < 1 || scua_arg_count(ctx) > 2)
        throw std::invalid_argument("compressed_info expects data and optional mip level");
    auto c = compressed(ctx);
    if (!c)
        return;
    int mip = supplied(ctx, 1) ? integer(ctx, 1, 0, c->getMipmapCount() - 1) : 0;
    const char *format = "unknown";
    love::getConstant(c->getFormat(), format);
    Record r(ctx);
    r.str("format", format);
    r.boolean("linear", c->isLinear());
    r.integer("width", c->getWidth(mip));
    r.integer("height", c->getHeight(mip));
    r.integer("slices", c->getSliceCount(mip));
    r.integer("mipmaps", c->getMipmapCount());
    r.integer("size", c->getSize());
    r.done();
}
void compressed_bytes(scua_ctx *ctx)
{
    arity(ctx, 3);
    auto c = compressed(ctx);
    if (!c)
        return;
    int mip = integer(ctx, 2, 0, c->getMipmapCount() - 1),
        slice = integer(ctx, 1, 0, c->getSliceCount(mip) - 1);
    auto data = c->getSlice(slice, mip);
    scua_return_bytes(ctx, static_cast<const uint8_t *>(data->getData()), data->getSize());
}
void new_compressed_texture(scua_ctx *ctx)
{
    if (scua_arg_count(ctx) < 1 || scua_arg_count(ctx) > 2)
        throw std::invalid_argument("expected compressed data and optional settings");
    auto c = compressed(ctx);
    if (!c)
        return;
    auto s = settings(ctx, 1);
    allocationLimit(s, c->getWidth(), c->getHeight(), c->getSliceCount(), c->getFormat());
    Texture::Slices slices(s.type);
    for (int mip = 0; mip < c->getMipmapCount(); ++mip)
        for (int slice = 0; slice < c->getSliceCount(mip); ++slice)
            slices.set(slice, mip, c->getSlice(slice, mip));
    if (c->getMipmapCount() > 1 && s.mipmaps == Texture::MIPMAPS_NONE)
        s.mipmaps = Texture::MIPMAPS_MANUAL;
    returnImage(ctx, graphics(ctx, false).newTexture(s, &slices));
}
void texture_info(scua_ctx *ctx)
{
    if (scua_arg_count(ctx) < 1 || scua_arg_count(ctx) > 2)
        throw std::invalid_argument("texture_info expects a texture and optional mip level");
    auto t = texture(ctx);
    if (!t)
        return;
    int mip = supplied(ctx, 1) ? integer(ctx, 1, 0, t->getMipmapCount() - 1) : 0;
    const char *type = "unknown", *format = "unknown", *mips = "unknown";
    Texture::getConstant(t->getTextureType(), type);
    love::getConstant(t->getPixelFormat(), format);
    Texture::getConstant(t->getMipmapsMode(), mips);
    Record r(ctx);
    r.str("type", type);
    r.str("format", format);
    r.str("mipmaps_mode", mips);
    r.str("debug_name", t->getDebugName());
    r.integer("width", t->getWidth(mip));
    r.integer("height", t->getHeight(mip));
    r.integer("pixel_width", t->getPixelWidth(mip));
    r.integer("pixel_height", t->getPixelHeight(mip));
    r.integer("depth", t->getDepth(mip));
    r.integer("layer_count", t->getLayerCount());
    r.integer("mip", mip);
    r.integer("layers", t->getSliceCount(mip));
    r.integer("mipmaps", t->getMipmapCount());
    r.num("dpi_scale", t->getDPIScale());
    r.integer("msaa", t->getMSAA());
    r.boolean("readable", t->isReadable());
    r.boolean("render_target", t->isRenderTarget());
    r.boolean("compute_write", t->isComputeWritable());
    r.boolean("compressed", t->isCompressed());
    r.boolean("linear", t->isFormatLinear());
    auto views = scua_new_array(r.p, 0);
    for (auto format : t->getViewFormats())
    {
        auto name = love::getPixelFormatName(format);
        if (scua_array_push(r.p, views, scua_string(r.p, name, std::strlen(name))) != SCUA_OK)
            throw std::runtime_error("texture view formats allocation failed");
    }
    r.set("view_formats", views);
    r.done();
}
void texture_format_supported(scua_ctx *ctx)
{
    arity(ctx, 2);
    auto name = string(ctx, 0);
    love::PixelFormat format;
    if (!love::getConstant(name.c_str(), format))
        throw std::invalid_argument("unknown pixel format");
    auto p = scua_ctx_partition(ctx);
    auto a = scua_arg(ctx, 1);
    uint32_t flags = 0;
    if (scua_typeof(p, a) != SCUA_TYPE_ARRAY)
        throw std::invalid_argument("format usages must be an array");
    for (int i = 0; i <= PIXELFORMATUSAGE_MAX_ENUM; i++)
    {
        auto v = scua_index(p, a, i);
        if (!scua_handle_valid(p, v))
            break;
        if (i == PIXELFORMATUSAGE_MAX_ENUM)
            throw std::invalid_argument("too many format usages");
        size_t n = 0;
        auto text = scua_to_lstring(p, v, &n);
        static const char *names[] = {"sample", "linear",       "rendertarget", "blend",
                                      "msaa",   "computewrite", "shaderatomics"};
        if (!text || std::memchr(text, 0, n))
            throw std::invalid_argument("format usages must be strings");
        int usage = 0;
        for (; usage < PIXELFORMATUSAGE_MAX_ENUM; ++usage)
            if (std::string(text, n) == names[usage])
                break;
        if (usage == PIXELFORMATUSAGE_MAX_ENUM)
            throw std::invalid_argument("unknown pixel format usage");
        flags |= 1u << usage;
    }
    scua_return_bool(ctx, graphics(ctx, false).isPixelFormatSupported(format, flags));
}
int mipmap(scua_ctx *ctx, Texture *t, int index)
{
    return supplied(ctx, index) ? integer(ctx, index, 0, t->getMipmapCount() - 1) : 0;
}
int layer(scua_ctx *ctx, Texture *t, int index, int mip)
{
    return supplied(ctx, index) ? integer(ctx, index, 0, t->getSliceCount(mip) - 1) : 0;
}
love::Rect region(scua_ctx *ctx, Texture *t, int mip, int index)
{
    love::Rect r{0, 0, t->getPixelWidth(mip), t->getPixelHeight(mip)};
    if (supplied(ctx, index))
    {
        float v[4];
        vector(ctx, index, v, 4);
        for (float n : v)
            if (n < 0 || n > 4096 || n != std::floor(n))
                throw std::invalid_argument("region must contain integer pixel coordinates");
        r = {int(v[0]), int(v[1]), int(v[2]), int(v[3])};
    }
    if (r.w < 1 || r.h < 1 || r.x + r.w > t->getPixelWidth(mip) || r.y + r.h > t->getPixelHeight(mip))
        throw std::invalid_argument("texture region is out of bounds");
    return r;
}
void texture_read(scua_ctx *ctx)
{
    if (scua_arg_count(ctx) < 1 || scua_arg_count(ctx) > 4)
        throw std::invalid_argument("texture_read expects texture, layer, mipmap, region");
    auto t = texture(ctx);
    if (!t)
        return;
    int mip = mipmap(ctx, t, 2), slice = layer(ctx, t, 1, mip);
    auto r = region(ctx, t, mip, 3);
    returnObject(ctx, ObjectKind::ImageData,
                 graphics(ctx, false).readbackTexture(t, slice, mip, r, nullptr, 0, 0));
}
void texture_read_async(scua_ctx *ctx)
{
    if (scua_arg_count(ctx) < 1 || scua_arg_count(ctx) > 4)
        throw std::invalid_argument("texture_read_async expects texture, layer, mipmap, region");
    auto t = texture(ctx);
    if (!t)
        return;
    int mip = mipmap(ctx, t, 2), slice = layer(ctx, t, 1, mip);
    auto r = region(ctx, t, mip, 3);
    returnObject(ctx, ObjectKind::Readback,
                 graphics(ctx, false).readbackTextureAsync(t, slice, mip, r, nullptr, 0, 0));
}
void texture_replace(scua_ctx *ctx)
{
    if (scua_arg_count(ctx) < 2 || scua_arg_count(ctx) > 7)
        throw std::invalid_argument("texture_replace expects 2 to 7 arguments");
    auto t = texture(ctx);
    auto d = objectArgument<love::image::ImageData>(ctx, 1, ObjectKind::ImageData);
    if (!t || !d)
        return;
    int mip = mipmap(ctx, t, 3), slice = layer(ctx, t, 2, mip);
    int x = supplied(ctx, 4) ? integer(ctx, 4, 0, 4096) : 0,
        y = supplied(ctx, 5) ? integer(ctx, 5, 0, 4096) : 0;
    bool reload = supplied(ctx, 6) ? boolean(ctx, 6) : false;
    t->replacePixels(d, slice, mip, x, y, reload);
}
void texture_generate_mipmaps(scua_ctx *ctx)
{
    arity(ctx, 1);
    auto t = texture(ctx);
    if (t)
        t->generateMipmaps();
}
void texture_set_sampler(scua_ctx *ctx)
{
    arity(ctx, 2);
    auto t = texture(ctx);
    if (!t)
        return;
    Options o(ctx, 1);
    auto s = t->getSamplerState();
    auto min = o.str("min_filter", s.minFilter == SamplerState::FILTER_NEAREST ? "nearest" : "linear");
    auto mag = o.str("mag_filter", s.magFilter == SamplerState::FILTER_NEAREST ? "nearest" : "linear");
    auto mip = o.str("mipmap_filter", "none");
    if (!SamplerState::getConstant(min.c_str(), s.minFilter) ||
        !SamplerState::getConstant(mag.c_str(), s.magFilter))
        throw std::invalid_argument("invalid texture filter");
    if (o.has("mipmap_filter") && !SamplerState::getConstant(mip.c_str(), s.mipmapFilter))
        throw std::invalid_argument("invalid mipmap filter");
    for (auto pair : {std::make_pair("wrap_u", &s.wrapU), std::make_pair("wrap_v", &s.wrapV),
                      std::make_pair("wrap_w", &s.wrapW)})
        if (o.has(pair.first))
        {
            auto n = o.str(pair.first, "");
            if (!SamplerState::getConstant(n.c_str(), *pair.second))
                throw std::invalid_argument("invalid wrap mode");
        }
    s.maxAnisotropy = o.integer("anisotropy", s.maxAnisotropy, 1, 255);
    s.lodBias = o.num("lod_bias", s.lodBias);
    s.minLod = o.integer("min_lod", s.minLod, 0, 255);
    s.maxLod = o.integer("max_lod", s.maxLod, 0, 255);
    if (s.minLod > s.maxLod)
        throw std::invalid_argument("minimum LOD exceeds maximum LOD");
    if (o.has("depth_compare"))
    {
        auto n = o.str("depth_compare", "");
        s.depthSampleMode.hasValue = n != "none";
        if (s.depthSampleMode.hasValue && !getConstant(n.c_str(), s.depthSampleMode.value))
            throw std::invalid_argument("invalid depth sample comparison");
    }
    t->setSamplerState(s);
}
void texture_sampler(scua_ctx *ctx)
{
    arity(ctx, 1);
    auto t = texture(ctx);
    if (!t)
        return;
    auto s = t->getSamplerState();
    Record r(ctx);
    const char *n = "unknown";
    SamplerState::getConstant(s.minFilter, n);
    r.str("min_filter", n);
    SamplerState::getConstant(s.magFilter, n);
    r.str("mag_filter", n);
    SamplerState::getConstant(s.mipmapFilter, n);
    r.str("mipmap_filter", n);
    SamplerState::getConstant(s.wrapU, n);
    r.str("wrap_u", n);
    SamplerState::getConstant(s.wrapV, n);
    r.str("wrap_v", n);
    SamplerState::getConstant(s.wrapW, n);
    r.str("wrap_w", n);
    r.integer("anisotropy", s.maxAnisotropy);
    r.num("lod_bias", s.lodBias);
    r.integer("min_lod", s.minLod);
    r.integer("max_lod", s.maxLod);
    n = "none";
    if (s.depthSampleMode.hasValue)
        getConstant(s.depthSampleMode.value, n);
    r.str("depth_compare", n);
    r.done();
}
void texture_view(scua_ctx *ctx)
{
    arity(ctx, 2);
    auto t = texture(ctx);
    if (!t)
        return;
    Options o(ctx, 1);
    Texture::ViewSettings s;
    if (o.has("format"))
    {
        auto n = o.str("format", "");
        s.format.hasValue = true;
        if (!love::getConstant(n.c_str(), s.format.value))
            throw std::invalid_argument("invalid view format");
    }
    if (o.has("type"))
    {
        auto n = o.str("type", "");
        s.type.hasValue = true;
        if (!Texture::getConstant(n.c_str(), s.type.value))
            throw std::invalid_argument("invalid view type");
    }
    for (auto pair :
         {std::make_pair("layer_start", &s.layerStart), std::make_pair("layer_count", &s.layerCount),
          std::make_pair("mipmap_start", &s.mipmapStart), std::make_pair("mipmap_count", &s.mipmapCount)})
        if (o.has(pair.first))
        {
            pair.second->hasValue = true;
            pair.second->value = o.integer(pair.first, 0, std::strstr(pair.first, "count") ? 1 : 0, 4096);
        }
    s.debugName = o.str("name", "");
    returnImage(ctx, graphics(ctx, false).newTextureView(t, s));
}
Graphics::RenderTargetStrongRef target(scua_ctx *ctx, scua_Handle value)
{
    auto p = scua_ctx_partition(ctx);
    int slice = 0, mip = 0;
    if (scua_typeof(p, value) == SCUA_TYPE_TABLE)
    {
        Options o(p, value);
        slice = o.integer("layer", 0, 0, 255);
        mip = o.integer("mipmap", 0, 0, 12);
        value = o.field("texture");
    }
    auto t = resource<Texture>(ctx, value, host(ctx).imageType);
    if (!t->isRenderTarget() || mip >= t->getMipmapCount() || !t->isValidSlice(slice, mip))
        throw std::invalid_argument("invalid render-target texture/layer/mipmap");
    return {t.get(), slice, mip};
}
void set_render_targets(scua_ctx *ctx)
{
    if (scua_arg_count(ctx) < 1 || scua_arg_count(ctx) > 2)
        throw std::invalid_argument("expected color targets and optional depth/stencil target");
    auto p = scua_ctx_partition(ctx);
    auto input = scua_arg(ctx, 0);
    Graphics::RenderTargetsStrongRef targets;
    if (scua_typeof(p, input) != SCUA_TYPE_ARRAY)
        throw std::invalid_argument("color targets must be an array");
    for (int i = 0; i <= 8; i++)
    {
        auto item = scua_index(p, input, i);
        if (!scua_handle_valid(p, item))
            break;
        if (i == 8)
            throw std::invalid_argument("maximum eight color targets");
        auto rt = target(ctx, item);
        if (love::isPixelFormatDepthStencil(rt.texture->getPixelFormat()))
            throw std::invalid_argument("depth/stencil texture used as color target");
        for (auto &old : targets.colors)
            if (old.texture.get() == rt.texture.get() && old.slice == rt.slice && old.mipmap == rt.mipmap)
                throw std::invalid_argument("duplicate render target");
        targets.colors.push_back(rt);
    }
    if (supplied(ctx, 1))
    {
        targets.depthStencil = target(ctx, scua_arg(ctx, 1));
        if (!love::isPixelFormatDepthStencil(targets.depthStencil.texture->getPixelFormat()))
            throw std::invalid_argument("depth target requires depth/stencil format");
    }
    if (targets.colors.empty() && !targets.depthStencil.texture)
        graphics(ctx).setRenderTarget();
    else
        graphics(ctx).setRenderTargets(targets);
}
void copy_texture_to_buffer(scua_ctx *ctx)
{
    if (scua_arg_count(ctx) < 2 || scua_arg_count(ctx) > 7)
        throw std::invalid_argument("copy_texture_to_buffer expects 2 to 7 arguments");
    auto t = texture(ctx);
    auto b = buffer(ctx, 1);
    if (!t || !b)
        return;
    int mip = mipmap(ctx, t, 3), slice = layer(ctx, t, 2, mip);
    auto r = region(ctx, t, mip, 4);
    size_t offset = supplied(ctx, 5) ? integer(ctx, 5, 0, b->getSize()) : 0;
    int width = supplied(ctx, 6) ? integer(ctx, 6, 0, 4096) : r.w;
    if (width != 0 && width < r.w)
        throw std::invalid_argument("row width must cover the copied region");
    graphics(ctx, false).copyTextureToBuffer(t, b, slice, mip, r, offset, width);
}
void copy_buffer_to_texture(scua_ctx *ctx)
{
    if (scua_arg_count(ctx) < 2 || scua_arg_count(ctx) > 7)
        throw std::invalid_argument("copy_buffer_to_texture expects 2 to 7 arguments");
    auto b = buffer(ctx, 0);
    auto t = texture(ctx, 1);
    if (!t || !b)
        return;
    int mip = mipmap(ctx, t, 3), slice = layer(ctx, t, 2, mip);
    auto r = region(ctx, t, mip, 4);
    size_t offset = supplied(ctx, 5) ? integer(ctx, 5, 0, b->getSize()) : 0;
    int width = supplied(ctx, 6) ? integer(ctx, 6, 0, 4096) : r.w;
    if (width != 0 && width < r.w)
        throw std::invalid_argument("row width must cover the copied region");
    graphics(ctx, false).copyBufferToTexture(b, t, offset, width, slice, mip, r);
}
void copy_texture(scua_ctx *ctx)
{
    if (scua_arg_count(ctx) < 2 || scua_arg_count(ctx) > 3)
        throw std::invalid_argument("copy_texture expects source, destination and optional settings");
    auto source = texture(ctx), dest = texture(ctx, 1);
    if (!source || !dest)
        return;
    Options o(ctx, 2);
    int sm = o.integer("source_mipmap", 0, 0, source->getMipmapCount() - 1),
        dm = o.integer("mipmap", 0, 0, dest->getMipmapCount() - 1);
    int sl = o.integer("source_layer", 0, 0, source->getSliceCount(sm) - 1),
        dl = o.integer("layer", 0, 0, dest->getSliceCount(dm) - 1);
    if (source->getPixelFormat() != dest->getPixelFormat())
        throw std::invalid_argument("texture copy formats must match");
    if (source == dest && sm == dm && sl == dl)
        throw std::invalid_argument("texture copy source and destination subresources must differ");
    love::Rect src{o.integer("source_x", 0, 0, 4096), o.integer("source_y", 0, 0, 4096),
                   o.integer("width", source->getPixelWidth(sm), 1, 4096),
                   o.integer("height", source->getPixelHeight(sm), 1, 4096)};
    love::Rect dst{o.integer("x", 0, 0, 4096), o.integer("y", 0, 0, 4096), src.w, src.h};
    size_t size = love::getPixelFormatSliceSize(source->getPixelFormat(), src.w, src.h);
    if (size == 0 || size % 4 || size > 32 * 1024 * 1024)
        throw std::invalid_argument(
            "texture copy staging size must be a multiple of four and at most 32 MiB");
    auto temporary = own(graphics(ctx, false)
                             .newBuffer(Buffer::Settings(BUFFERUSAGEFLAG_VERTEX, BUFFERDATAUSAGE_DYNAMIC),
                                        {{"value", DATAFORMAT_UINT32}}, nullptr, 0, size / 4));
    graphics(ctx, false).copyTextureToBuffer(source, temporary.get(), sl, sm, src, 0, src.w);
    graphics(ctx, false).copyBufferToTexture(temporary.get(), dest, 0, src.w, dl, dm, dst);
}
void texture_replace_compressed(scua_ctx *ctx)
{
    if (scua_arg_count(ctx) < 2 || scua_arg_count(ctx) > 3)
        throw std::invalid_argument("expected texture, compressed data and optional settings");
    auto t = texture(ctx);
    auto c = compressed(ctx, 1);
    if (!t || !c)
        return;
    Options o(ctx, 2);
    int sm = o.integer("source_mipmap", 0, 0, c->getMipmapCount() - 1),
        sl = o.integer("source_layer", 0, 0, c->getSliceCount(sm) - 1);
    int dm = o.integer("mipmap", 0, 0, t->getMipmapCount() - 1),
        dl = o.integer("layer", 0, 0, t->getSliceCount(dm) - 1);
    t->replacePixels(c->getSlice(sl, sm), dl, dm, o.integer("x", 0, 0, 4096), o.integer("y", 0, 0, 4096),
                     o.boolean("reload_mipmaps", false));
}
void draw_layer(scua_ctx *ctx)
{
    auto matrix = transform(ctx, 2);
    auto t = texture(ctx);
    if (!t)
        return;
    int slice = integer(ctx, 1, 0, t->getSliceCount(0) - 1);
    t->drawLayer(&graphics(ctx), slice, matrix);
}
Texture *optionalTexture(scua_ctx *ctx, int i)
{
    return supplied(ctx, i) ? texture(ctx, i) : nullptr;
}
void draw_from_shader(scua_ctx *ctx)
{
    if (scua_arg_count(ctx) < 2 || scua_arg_count(ctx) > 4)
        throw std::invalid_argument("draw_from_shader expects 2 to 4 arguments");
    auto n = string(ctx, 0);
    PrimitiveType mode;
    if (!getConstant(n.c_str(), mode))
        throw std::invalid_argument("invalid primitive type");
    int vertices = integer(ctx, 1, 0, 1048576),
        instances = supplied(ctx, 2) ? integer(ctx, 2, 1, 1048576) : 1;
    graphics(ctx).drawFromShader(mode, vertices, instances, optionalTexture(ctx, 3));
}
void draw_from_shader_indexed(scua_ctx *ctx)
{
    if (scua_arg_count(ctx) < 2 || scua_arg_count(ctx) > 5)
        throw std::invalid_argument("draw_from_shader_indexed expects 2 to 5 arguments");
    auto b = buffer(ctx, 0);
    if (!b)
        return;
    int count = integer(ctx, 1, 0, 1048576), instances = supplied(ctx, 2) ? integer(ctx, 2, 1, 1048576) : 1,
        start = supplied(ctx, 3) ? integer(ctx, 3, 0, 1048576) : 0;
    graphics(ctx).drawFromShader(b, count, instances, start, optionalTexture(ctx, 4));
}
void draw_from_shader_indirect(scua_ctx *ctx)
{
    if (scua_arg_count(ctx) < 2 || scua_arg_count(ctx) > 4)
        throw std::invalid_argument("draw_from_shader_indirect expects 2 to 4 arguments");
    auto n = string(ctx, 0);
    PrimitiveType mode;
    if (!getConstant(n.c_str(), mode))
        throw std::invalid_argument("invalid primitive type");
    auto b = buffer(ctx, 1);
    if (!b)
        return;
    int index = supplied(ctx, 2) ? integer(ctx, 2, 0, b->getArrayLength() - 1) : 0;
    graphics(ctx).drawFromShaderIndirect(mode, b, index, optionalTexture(ctx, 3));
}
void draw_from_shader_indexed_indirect(scua_ctx *ctx)
{
    if (scua_arg_count(ctx) < 2 || scua_arg_count(ctx) > 4)
        throw std::invalid_argument("draw_from_shader_indexed_indirect expects 2 to 4 arguments");
    auto indices = buffer(ctx, 0), args = buffer(ctx, 1);
    if (!indices || !args)
        return;
    int index = supplied(ctx, 2) ? integer(ctx, 2, 0, args->getArrayLength() - 1) : 0;
    graphics(ctx).drawFromShaderIndirect(indices, args, index, optionalTexture(ctx, 3));
}
} // namespace
void registerTextures(scua_partition *p, Host &state)
{
    registerObjectType(p, state, ObjectKind::CompressedImageData);
    registerFunctions(
        p, state,
        {
            {"love_graphics", "new_texture", guarded<new_texture>,
             "new_texture(settings: any, slices: any = nil) -> any",
             "Create 2d, array, cube or volume textures; slices are ImageData or {data, layer, mipmap}."},
            {"love_image", "decode_compressed", guarded<decode_compressed>,
             "decode_compressed(encoded: bytes) -> any",
             "Decode DDS/KTX/PVR/ASTC containers into managed compressed image data."},
            {"love_image", "compressed_info", guarded<compressed_info>,
             "compressed_info(data: any, mip: int = 0) -> any",
             "Get compressed format, dimensions, slices, mipmaps and size."},
            {"love_image", "compressed_bytes", guarded<compressed_bytes>,
             "compressed_bytes(data: any, layer: int, mipmap: int) -> bytes",
             "Copy compressed block bytes for a subresource."},
            {"love_graphics", "new_compressed_texture", guarded<new_compressed_texture>,
             "new_compressed_texture(data: any, settings: any = nil) -> any",
             "Upload compressed blocks and mipmaps without CPU decompression."},
            {"love_graphics", "texture_info", guarded<texture_info>,
             "texture_info(texture: any, mip: int = 0) -> any",
             "Get selected mip dimensions/depth, layer count, color space, view formats and usage."},
            {"love_graphics", "texture_format_supported", guarded<texture_format_supported>,
             "texture_format_supported(format: string, usages: any) -> bool",
             "Check sample, linear, rendertarget, computewrite and other native format usages."},
            {"love_graphics", "texture_read", guarded<texture_read>,
             "texture_read(texture: any, layer: int = 0, mipmap: int = 0, region: vec4 = nil) -> any",
             "Read a texture subresource/rectangle into managed ImageData."},
            {"love_graphics", "texture_read_async", guarded<texture_read_async>,
             "texture_read_async(texture: any, layer: int = 0, mipmap: int = 0, region: vec4 = nil) -> any",
             "Start an asynchronous subresource/rectangle readback."},
            {"love_graphics", "texture_replace", guarded<texture_replace>,
             "texture_replace(texture: any, data: any, layer: int = 0, mipmap: int = 0, x: int = 0, y: int = "
             "0, reload_mipmaps: bool = false)",
             "Upload ImageData into a selected texture layer/mipmap."},
            {"love_graphics", "texture_generate_mipmaps", guarded<texture_generate_mipmaps>,
             "texture_generate_mipmaps(texture: any)", "Generate configured texture mipmaps."},
            {"love_graphics", "texture_set_sampler", guarded<texture_set_sampler>,
             "texture_set_sampler(texture: any, settings: any)",
             "Set filtering, wrapping, LOD, anisotropy and depth comparison from a record."},
            {"love_graphics", "texture_sampler", guarded<texture_sampler>,
             "texture_sampler(texture: any) -> any", "Get all texture sampler settings."},
            {"love_graphics", "texture_view", guarded<texture_view>,
             "texture_view(texture: any, settings: any) -> any",
             "Create a retained native texture view with selected layers, mipmaps or format."},
            {"love_graphics", "set_render_targets", guarded<set_render_targets>,
             "set_render_targets(colors: any, depth_stencil: any = nil)",
             "Bind color and persistent depth/stencil textures; entries accept {texture,layer,mipmap}."},
            {"love_graphics", "copy_texture_to_buffer", guarded<copy_texture_to_buffer>,
             "copy_texture_to_buffer(texture: any, buffer: any, layer: int = 0, mipmap: int = 0, region: "
             "vec4 = nil, offset: int = 0, row_width: int = 0)",
             "Copy texture bytes on the GPU into a buffer with checked bounds/alignment."},
            {"love_graphics", "copy_buffer_to_texture", guarded<copy_buffer_to_texture>,
             "copy_buffer_to_texture(buffer: any, texture: any, layer: int = 0, mipmap: int = 0, region: "
             "vec4 = nil, offset: int = 0, row_width: int = 0)",
             "Copy GPU buffer bytes into a texture subresource."},
            {"love_graphics", "copy_texture", guarded<copy_texture>,
             "copy_texture(source: any, destination: any, settings: any = nil)",
             "Copy same-format GPU subresources through a GPU-only staging buffer; regions up to 32 MiB."},
            {"love_graphics", "texture_replace_compressed", guarded<texture_replace_compressed>,
             "texture_replace_compressed(texture: any, data: any, settings: any = nil)",
             "Upload a selected compressed source layer/mipmap to a texture region."},
            {"love_graphics", "draw_layer", guarded<draw_layer>,
             "draw_layer(texture: any, layer: int, position: vec2, rotation: float = 0, scale: vec2 = nil, "
             "origin: vec2 = nil)",
             "Draw an array texture layer with the normal drawable transform."},
            {"love_graphics", "draw_from_shader", guarded<draw_from_shader>,
             "draw_from_shader(mode: string, vertices: int, instances: int = 1, texture: any = nil)",
             "Draw shader-generated vertices without a mesh."},
            {"love_graphics", "draw_from_shader_indexed", guarded<draw_from_shader_indexed>,
             "draw_from_shader_indexed(indices: any, count: int, instances: int = 1, start: int = 0, "
             "texture: any = nil)",
             "Draw shader-generated indexed triangles."},
            {"love_graphics", "draw_from_shader_indirect", guarded<draw_from_shader_indirect>,
             "draw_from_shader_indirect(mode: string, arguments: any, index: int = 0, texture: any = nil)",
             "Draw shader-generated vertices using GPU indirect arguments."},
            {"love_graphics", "draw_from_shader_indexed_indirect", guarded<draw_from_shader_indexed_indirect>,
             "draw_from_shader_indexed_indirect(indices: any, arguments: any, index: int = 0, texture: any = "
             "nil)",
             "Draw shader-generated indexed triangles using GPU indirect arguments."},
        });
}
} // namespace lovescua
