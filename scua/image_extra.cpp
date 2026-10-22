// Additional CPU image operations over the engine's actual format handlers.
#include "data/ByteData.h"
#include "image/CompressedImageData.h"
#include "image/Image.h"
#include "options.h"
namespace lovescua
{
namespace
{
using namespace binding;
using love::image::ImageData;
template <class T> auto own(T *p)
{
    return std::unique_ptr<T, void (*)(T *)>(p,
                                             [](T *v)
                                             {
                                                 if (v)
                                                     v->release();
                                             });
}
auto image(scua_ctx *c, int i = 0)
{
    return objectArgument<ImageData>(c, i, ObjectKind::ImageData);
}
love::PixelFormat format(const std::string &name)
{
    love::PixelFormat f;
    if (!love::getConstant(name.c_str(), f) || !ImageData::validPixelFormat(f) ||
        love::getPixelFormatBlockSize(f) == 0)
        throw std::invalid_argument("unsupported CPU pixel format: " + name);
    return f;
}
void image_info(scua_ctx *c)
{
    arity(c, 1);
    auto d = image(c);
    if (!d)
        return;
    Record r(c);
    r.integer("width", d->getWidth());
    r.integer("height", d->getHeight());
    r.integer("size", d->getSize());
    r.integer("pixel_size", d->getPixelSize());
    r.str("format", love::getPixelFormatName(d->getFormat()));
    r.boolean("linear", d->isLinear());
    r.boolean("pixel_access", d->getPixelGetFunction() && d->getPixelSetFunction());
    r.done();
}
void formats(scua_ctx *c)
{
    arity(c, 0);
    auto p = scua_ctx_partition(c);
    auto a = scua_new_array(p, 0);
    for (int i = 0; i < love::PIXELFORMAT_MAX_ENUM; ++i)
        if (ImageData::validPixelFormat(love::PixelFormat(i)) &&
            love::getPixelFormatBlockSize(love::PixelFormat(i)) > 0)
        {
            auto name = love::getPixelFormatName(love::PixelFormat(i));
            if (scua_array_push(p, a, scua_string(p, name, std::strlen(name))) != SCUA_OK)
                throw std::runtime_error("image formats allocation failed");
        }
    scua_return_value(c, a);
}
void set_linear(scua_ctx *c)
{
    arity(c, 2);
    bool enabled = boolean(c, 1);
    if (auto d = image(c))
        d->setLinear(enabled);
}
void encode(scua_ctx *c)
{
    arity(c, 2);
    auto name = string(c, 1);
    love::image::FormatHandler::EncodedFormat f;
    if (!ImageData::getConstant(name.c_str(), f))
        throw std::invalid_argument("unknown encoded image format");
    auto d = image(c);
    if (!d)
        return;
    auto encoded = own(d->encode(f, ("image." + name).c_str(), false));
    scua_return_bytes(c, static_cast<const uint8_t *>(encoded->getData()), encoded->getSize());
}
void can_encode(scua_ctx *c)
{
    arity(c, 2);
    auto raw = format(string(c, 0));
    auto name = string(c, 1);
    love::image::FormatHandler::EncodedFormat f;
    if (!ImageData::getConstant(name.c_str(), f))
        throw std::invalid_argument("unknown encoded image format");
    auto &module = *love::Module::getInstance<love::image::Image>(love::Module::M_IMAGE);
    bool supported = false;
    for (auto h : module.getFormatHandlers())
        supported |= h->canEncode(raw, f);
    scua_return_bool(c, supported);
}
void convert(scua_ctx *c)
{
    arity(c, 2);
    auto f = format(string(c, 1));
    auto d = image(c);
    if (!d)
        return;
    if (love::getPixelFormatBlockSize(f) * size_t(d->getWidth()) * d->getHeight() > 64 * 1024 * 1024)
        throw std::invalid_argument("CPU image limit is 64 MiB");
    if (!d->getPixelGetFunction() || !ImageData::getPixelSetFunction(f))
        throw std::invalid_argument(
            "pixel conversion is unavailable for integer/unsupported formats; use raw bytes");
    auto out = own(new ImageData(d->getWidth(), d->getHeight(), f));
    out->setLinear(d->isLinear());
    for (int y = 0; y < d->getHeight(); ++y)
        for (int x = 0; x < d->getWidth(); ++x)
            out->setPixel(x, y, d->getPixel(x, y));
    returnObject(c, ObjectKind::ImageData, out.release());
}
void paste(scua_ctx *c)
{
    arity(c, 8);
    auto dst = image(c), src = image(c, 1);
    if (!dst || !src)
        return;
    int dx = integer(c, 2, 0, dst->getWidth()), dy = integer(c, 3, 0, dst->getHeight()),
        sx = integer(c, 4, 0, src->getWidth()), sy = integer(c, 5, 0, src->getHeight()),
        w = integer(c, 6, 0, 4096), h = integer(c, 7, 0, 4096);
    if (w > src->getWidth() - sx || h > src->getHeight() - sy || w > dst->getWidth() - dx ||
        h > dst->getHeight() - dy)
        throw std::invalid_argument("paste region exceeds source or destination");
    // Snapshot self-pastes: overlapping rows must not depend on traversal order.
    auto copy = own(src == dst ? src->clone() : nullptr);
    dst->paste(copy ? copy.get() : src, dx, dy, sx, sy, w, h);
}
void map_pixels(scua_ctx *c)
{
    if (scua_arg_count(c) < 2 || scua_arg_count(c) > 3)
        throw std::invalid_argument("map_pixels expects image, callback and optional region");
    auto d = image(c);
    if (!d)
        return;
    love::StrongRef<ImageData> keep(d);
    int x = 0, y = 0, w = d->getWidth(), h = d->getHeight();
    if (supplied(c, 2))
    {
        float region[4];
        vector(c, 2, region, 4);
        for (float v : region)
            if (v != std::floor(v) || v < 0 || v > 4096)
                throw std::invalid_argument("image region must have nonnegative integer components");
        x = region[0];
        y = region[1];
        w = region[2];
        h = region[3];
    }
    if (x > d->getWidth() || y > d->getHeight() || w > d->getWidth() - x || h > d->getHeight() - y ||
        size_t(w) * h > 16384)
        throw std::invalid_argument("map region out of bounds or exceeds 16384 pixels");
    std::vector<love::Colorf> colors;
    colors.reserve(size_t(w) * h);
    auto p = scua_ctx_partition(c);
    auto fn = scua_arg(c, 1);
    for (int row = y; row < y + h; ++row)
        for (int col = x; col < x + w; ++col)
        {
            auto scope = scua_scope_enter(p);
            struct Leave
            {
                scua_partition *p;
                uint32_t s;
                ~Leave()
                {
                    scua_scope_leave(p, s);
                }
            } leave{p, scope};
            auto color = d->getPixel(col, row);
            float rgba[] = {color.r, color.g, color.b, color.a};
            scua_Handle args[] = {scua_int(p, col), scua_int(p, row), scua_vec(p, rgba, 4)}, result{};
            char error[2048]{};
            if (scua_pcall(p, fn, args, 3, &result, error, sizeof(error)) != SCUA_OK)
                throw std::runtime_error(std::string("map_pixels callback: ") + error);
            if (scua_to_vec(p, result, rgba, 4) != 4)
                throw std::invalid_argument("map_pixels callback must return vec4");
            for (float v : rgba)
                if (!std::isfinite(v))
                    throw std::invalid_argument("map_pixels returned a non-finite component");
            colors.push_back({rgba[0], rgba[1], rgba[2], rgba[3]});
        }
    size_t i = 0;
    for (int row = y; row < y + h; ++row)
        for (int col = x; col < x + w; ++col)
            d->setPixel(col, row, colors[i++]);
}
void split(scua_ctx *c, bool cube)
{
    arity(c, 1);
    auto d = image(c);
    if (!d)
        return;
    auto &module = *love::Module::getInstance<love::image::Image>(love::Module::M_IMAGE);
    auto images = cube ? module.newCubeFaces(d) : module.newVolumeLayers(d);
    if (images.size() > 256)
        throw std::invalid_argument("image slice limit is 256");
    auto p = scua_ctx_partition(c);
    auto a = scua_new_array(p, 0);
    for (auto &v : images)
    {
        scua_Handle handle{};
        if (scua_userdata_new(p, host(c).objectTypes[size_t(ObjectKind::ImageData)], sizeof(ObjectResource),
                              v.get(), &handle) != SCUA_OK)
            throw std::runtime_error("could not allocate image slice");
        ++host(c).liveObjects;
        int status = scua_array_push(p, a, handle);
        scua_unpin_global(p, handle);
        if (status != SCUA_OK)
            throw std::runtime_error("could not append image slice");
    }
    scua_return_value(c, a);
}
void cube_faces(scua_ctx *c)
{
    split(c, true);
}
void volume_layers(scua_ctx *c)
{
    split(c, false);
}
void compressed_clone(scua_ctx *c)
{
    arity(c, 1);
    auto d = objectArgument<love::image::CompressedImageData>(c, 0, ObjectKind::CompressedImageData);
    if (d)
    {
        auto copy = d->clone();
        copy->setLinear(d->isLinear());
        returnObject(c, ObjectKind::CompressedImageData, copy);
    }
}
void compressed_linear(scua_ctx *c)
{
    arity(c, 2);
    bool enabled = boolean(c, 1);
    auto d = objectArgument<love::image::CompressedImageData>(c, 0, ObjectKind::CompressedImageData);
    if (d)
        d->setLinear(enabled);
}
void is_compressed(scua_ctx *c)
{
    arity(c, 1);
    const uint8_t *b;
    size_t n;
    if (scua_arg_bytes(c, 0, &b, &n) != SCUA_OK || n > 32 * 1024 * 1024)
        throw std::invalid_argument("expected encoded bytes up to 32 MiB");
    auto bytes = own(new love::data::ByteData(b, n));
    auto &module = *love::Module::getInstance<love::image::Image>(love::Module::M_IMAGE);
    scua_return_bool(c, module.isCompressed(bytes.get()));
}
} // namespace
void registerImageExtras(scua_partition *p, Host &s)
{
    registerFunctions(
        p, s,
        {{"love_image", "compressed_clone", guarded<compressed_clone>, "compressed_clone(data: any) -> any",
          "Clone compressed blocks and preserve linear metadata."},
         {"love_image", "compressed_set_linear", guarded<compressed_linear>,
          "compressed_set_linear(data: any, linear: bool)",
          "Set compressed color-space metadata without altering blocks."},
         {"love_image", "info", guarded<image_info>, "info(data: any) -> any",
          "Get format, linear flag, dimensions and raw byte layout."},
         {"love_image", "formats", guarded<formats>, "formats() -> any",
          "List CPU pixel formats supported by native ImageData."},
         {"love_image", "set_linear", guarded<set_linear>, "set_linear(data: any, linear: bool)",
          "Set color-space metadata without changing pixels."},
         {"love_image", "encode", guarded<encode>, "encode(data: any, format: string) -> bytes",
          "Encode PNG, TGA, EXR or QOI bytes using native format support."},
         {"love_image", "can_encode", guarded<can_encode>,
          "can_encode(pixel_format: string, encoded_format: string) -> bool",
          "Check native encoder support for a CPU pixel format."},
         {"love_image", "convert", guarded<convert>, "convert(data: any, format: string) -> any",
          "Convert CPU pixel representation; preserve linear metadata."},
         {"love_image", "paste", guarded<paste>,
          "paste(destination: any, source: any, dx: int, dy: int, sx: int, sy: int, width: int, height: int)",
          "Copy a checked region with native format conversion and overlapping self-copy support."},
         {"love_image", "map_pixels", guarded<map_pixels>,
          "map_pixels(data: any, callback: any, region: vec4 = nil)",
          "Map up to 16384 pixels through a callback; stage writes until every callback succeeds."},
         {"love_image", "cube_faces", guarded<cube_faces>, "cube_faces(data: any) -> any",
          "Extract retained cube faces from an engine-supported image layout."},
         {"love_image", "volume_layers", guarded<volume_layers>, "volume_layers(data: any) -> any",
          "Extract retained volume slices from an engine-supported strip layout."},
         {"love_image", "is_compressed", guarded<is_compressed>, "is_compressed(encoded: bytes) -> bool",
          "Identify native compressed image containers."}});
}
} // namespace lovescua
