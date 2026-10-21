// Managed rasterizers/glyphs, explicit bitmap pages and complete font/text settings.
#include "filesystem/FileData.h"
#include "font/BMFontRasterizer.h"
#include "font/Font.h"
#include "graphics/TextBatch.h"
#include "options.h"
namespace lovescua
{
namespace
{
using namespace binding;
using Rasterizer = love::font::Rasterizer;
using Glyph = love::font::GlyphData;
using Font = love::graphics::Font;
using Text = love::graphics::TextBatch;
template <class T> auto owned(T *p)
{
    return std::unique_ptr<T, void (*)(T *)>(p,
                                             [](T *v)
                                             {
                                                 if (v)
                                                     v->release();
                                             });
}
auto module()
{
    return love::Module::getInstance<love::font::Font>(love::Module::M_FONT);
}
auto raster(scua_ctx *c)
{
    return objectArgument<Rasterizer>(c, 0, ObjectKind::Rasterizer);
}
auto glyph(scua_ctx *c)
{
    return objectArgument<Glyph>(c, 0, ObjectKind::GlyphData);
}
auto font(scua_ctx *c, int i = 0)
{
    return objectArgument<Font>(c, i, ObjectKind::Font);
}
auto text(scua_ctx *c)
{
    return objectArgument<Text>(c, 0, ObjectKind::Text);
}
float dpi(scua_ctx *c, int i)
{
    double n = optionalNumber(c, i, 1);
    if (n <= 0 || n > 16)
        throw std::invalid_argument("DPI scale must be in (0,16]");
    return n;
}
template <class T> love::StrongRef<T> borrow(scua_ctx *ctx, scua_Handle value, ObjectKind kind)
{
    auto p = scua_ctx_partition(ctx);
    void *storage = nullptr;
    scua_UserdataLease lease{};
    if (scua_userdata_borrow(p, value, host(ctx).objectTypes[size_t(kind)], &storage, &lease) != SCUA_OK)
        throw std::invalid_argument("expected a live managed font/image");
    love::StrongRef<T> result(static_cast<T *>(static_cast<ObjectResource *>(storage)->object));
    if (scua_userdata_release(p, lease) != SCUA_OK)
        throw std::runtime_error("could not release font/image lease");
    return result;
}
void new_rasterizer(scua_ctx *ctx)
{
    if (scua_arg_count(ctx) < 1 || scua_arg_count(ctx) > 3)
        throw std::invalid_argument(
            "new_rasterizer expects size, optional encoded TrueType bytes and settings");
    int size = integer(ctx, 0, 1, 512);
    Options o(ctx, 2);
    love::font::TrueTypeRasterizer::Settings s;
    auto name = o.str("hinting", "normal");
    if (!love::font::TrueTypeRasterizer::getConstant(name.c_str(), s.hinting))
        throw std::invalid_argument("invalid TrueType hinting");
    if (o.has("dpi_scale"))
    {
        float n = o.num("dpi_scale", 1);
        if (n <= 0 || n > 16)
            throw std::invalid_argument("invalid DPI scale");
        s.dpiScale = love::OptionalFloat(n);
    }
    s.sdf = o.boolean("sdf", false);
    if (!supplied(ctx, 1))
    {
        returnObject(ctx, ObjectKind::Rasterizer, module()->newTrueTypeRasterizer(size, s));
        return;
    }
    const uint8_t *bytes = nullptr;
    size_t length = 0;
    if (scua_arg_bytes(ctx, 1, &bytes, &length) != SCUA_OK || !length || length > 32 * 1024 * 1024)
        throw std::invalid_argument("font bytes must contain 1 byte to 32 MiB");
    auto data = owned(new love::filesystem::FileData(length, "font.ttf"));
    std::memcpy(data->getData(), bytes, length);
    returnObject(ctx, ObjectKind::Rasterizer, module()->newTrueTypeRasterizer(data.get(), size, s));
}
void new_image_rasterizer(scua_ctx *ctx)
{
    if (scua_arg_count(ctx) < 2 || scua_arg_count(ctx) > 4)
        throw std::invalid_argument("expected image, glyph string, optional extra spacing and DPI scale");
    auto chars = string(ctx, 1);
    if (chars.empty() || chars.size() > 65536)
        throw std::invalid_argument("glyph string must contain 1..65536 bytes");
    int spacing = supplied(ctx, 2) ? integer(ctx, 2, -4096, 4096) : 0;
    float scale = dpi(ctx, 3);
    auto data = objectArgument<love::image::ImageData>(ctx, 0, ObjectKind::ImageData);
    if (data)
        returnObject(ctx, ObjectKind::Rasterizer, module()->newImageRasterizer(data, chars, spacing, scale));
}
void new_bmfont_rasterizer(scua_ctx *ctx)
{
    if (scua_arg_count(ctx) < 2 || scua_arg_count(ctx) > 3)
        throw std::invalid_argument(
            "expected BMFont definition, explicit page images and optional DPI scale");
    auto definition = string(ctx, 0);
    if (definition.empty() || definition.size() > 1024 * 1024)
        throw std::invalid_argument("BMFont definition must contain 1 byte to 1 MiB");
    float scale = dpi(ctx, 2);
    auto p = scua_ctx_partition(ctx);
    auto array = scua_arg(ctx, 1);
    if (scua_typeof(p, array) != SCUA_TYPE_ARRAY)
        throw std::invalid_argument("BMFont pages must be an ImageData array");
    std::vector<love::StrongRef<love::image::ImageData>> images;
    std::vector<love::image::ImageData *> pages;
    for (int i = 0; i <= 16; i++)
    {
        auto v = scua_index(p, array, i);
        if (!scua_handle_valid(p, v))
            break;
        if (i == 16)
            throw std::invalid_argument("maximum 16 BMFont pages");
        images.push_back(borrow<love::image::ImageData>(ctx, v, ObjectKind::ImageData));
        pages.push_back(images.back().get());
    }
    if (pages.empty())
        throw std::invalid_argument("BMFont page images must be supplied explicitly");
    auto data = owned(new love::filesystem::FileData(definition.size(), "font.fnt"));
    std::memcpy(data->getData(), definition.data(), definition.size());
    returnObject(ctx, ObjectKind::Rasterizer,
                 new love::font::BMFontRasterizer(data.get(), pages, scale, false));
}
void new_font_from_rasterizer(scua_ctx *ctx)
{
    arity(ctx, 1);
    auto r = raster(ctx);
    if (r)
        returnObject(ctx, ObjectKind::Font, graphics(ctx, false).newFont(r));
}
void rasterizer_info(scua_ctx *ctx)
{
    arity(ctx, 1);
    auto r = raster(ctx);
    if (!r)
        return;
    Record v(ctx);
    v.integer("height", r->getHeight());
    v.integer("advance", r->getAdvance());
    v.integer("ascent", r->getAscent());
    v.integer("descent", r->getDescent());
    v.integer("line_height", r->getLineHeight());
    v.integer("glyphs", r->getGlyphCount());
    v.num("dpi_scale", r->getDPIScale());
    v.str("type", r->getDataType() == Rasterizer::DATA_TRUETYPE ? "truetype" : "image");
    v.done();
}
uint32_t codepoint(scua_ctx *c, int i)
{
    int n = integer(c, i, 0, 0x10ffff);
    if (n >= 0xd800 && n <= 0xdfff)
        throw std::invalid_argument("surrogate is not a Unicode scalar");
    return n;
}
void rasterizer_glyph_info(scua_ctx *ctx)
{
    arity(ctx, 2);
    auto n = codepoint(ctx, 1);
    auto r = raster(ctx);
    if (!r)
        return;
    Record v(ctx);
    v.boolean("has", r->hasGlyph(n));
    v.integer("index", r->getGlyphIndex(n));
    v.integer("spacing", r->getGlyphSpacing(n));
    v.done();
}
void rasterizer_has_glyphs(scua_ctx *ctx)
{
    arity(ctx, 2);
    auto s = string(ctx, 1);
    auto r = raster(ctx);
    if (r)
        scua_return_bool(ctx, r->hasGlyphs(s));
}
void rasterizer_kerning(scua_ctx *ctx)
{
    arity(ctx, 3);
    auto a = codepoint(ctx, 1), b = codepoint(ctx, 2);
    auto r = raster(ctx);
    if (r)
        scua_return_float(ctx, r->getKerning(a, b));
}
void new_glyph(scua_ctx *ctx)
{
    arity(ctx, 2);
    auto s = string(ctx, 1);
    if (s.empty())
        throw std::invalid_argument("glyph string must not be empty");
    auto r = raster(ctx);
    if (r)
        returnObject(ctx, ObjectKind::GlyphData, r->getGlyphData(s));
}
void new_glyph_codepoint(scua_ctx *ctx)
{
    arity(ctx, 2);
    auto n = codepoint(ctx, 1);
    auto r = raster(ctx);
    if (r)
        returnObject(ctx, ObjectKind::GlyphData, r->getGlyphData(n));
}
void glyph_info(scua_ctx *ctx)
{
    arity(ctx, 1);
    auto g = glyph(ctx);
    if (!g)
        return;
    Record v(ctx);
    const char *format = "unknown";
    love::getConstant(g->getFormat(), format);
    v.str("format", format);
    v.integer("codepoint", g->getGlyph());
    v.str("character", g->getGlyphString());
    v.integer("width", g->getWidth());
    v.integer("height", g->getHeight());
    v.integer("advance", g->getAdvance());
    v.integer("bearing_x", g->getBearingX());
    v.integer("bearing_y", g->getBearingY());
    v.integer("min_x", g->getMinX());
    v.integer("min_y", g->getMinY());
    v.integer("max_x", g->getMaxX());
    v.integer("max_y", g->getMaxY());
    v.integer("size", g->getSize());
    v.done();
}
void glyph_bytes(scua_ctx *ctx)
{
    arity(ctx, 1);
    auto g = glyph(ctx);
    if (g)
        scua_return_bytes(ctx, static_cast<const uint8_t *>(g->getData()), g->getSize());
}
void glyph_clone(scua_ctx *ctx)
{
    arity(ctx, 1);
    auto g = glyph(ctx);
    if (g)
        returnObject(ctx, ObjectKind::GlyphData, g->clone());
}
void glyph_image(scua_ctx *ctx)
{
    arity(ctx, 1);
    auto g = glyph(ctx);
    if (!g)
        return;
    if (!g->getWidth() || !g->getHeight())
    {
        scua_return_value(ctx, scua_nil(scua_ctx_partition(ctx)));
        return;
    }
    if (uint64_t(g->getWidth()) * g->getHeight() > 4 * 1024 * 1024)
        throw std::invalid_argument("glyph image exceeds 16 MiB");
    auto image =
        owned(new love::image::ImageData(g->getWidth(), g->getHeight(), love::PIXELFORMAT_RGBA8_UNORM));
    auto src = static_cast<const uint8_t *>(g->getData());
    auto dest = static_cast<uint8_t *>(image->getData());
    if (g->getFormat() == love::PIXELFORMAT_RGBA8_UNORM)
        std::memcpy(dest, src, g->getSize());
    else
        for (int i = 0; i < g->getWidth() * g->getHeight(); i++)
        {
            dest[4 * i] = dest[4 * i + 1] = dest[4 * i + 2] = src[2 * i];
            dest[4 * i + 3] = src[2 * i + 1];
        }
    returnObject(ctx, ObjectKind::ImageData, image.release());
}
void font_set_filter(scua_ctx *ctx)
{
    arity(ctx, 4);
    auto f = font(ctx);
    auto min = string(ctx, 1), mag = string(ctx, 2);
    int anisotropy = integer(ctx, 3, 1, 255);
    if (!f)
        return;
    auto s = f->getSamplerState();
    if (!love::graphics::SamplerState::getConstant(min.c_str(), s.minFilter) ||
        !love::graphics::SamplerState::getConstant(mag.c_str(), s.magFilter))
        throw std::invalid_argument("invalid font filter");
    s.maxAnisotropy = anisotropy;
    f->setSamplerState(s);
}
void font_filter(scua_ctx *ctx)
{
    arity(ctx, 1);
    auto f = font(ctx);
    if (!f)
        return;
    auto s = f->getSamplerState();
    Record v(ctx);
    const char *n = "unknown";
    love::graphics::SamplerState::getConstant(s.minFilter, n);
    v.str("min", n);
    love::graphics::SamplerState::getConstant(s.magFilter, n);
    v.str("mag", n);
    v.integer("anisotropy", s.maxAnisotropy);
    v.num("dpi_scale", f->getDPIScale());
    v.done();
}
void font_set_fallbacks(scua_ctx *ctx)
{
    arity(ctx, 2);
    auto f = font(ctx);
    if (!f)
        return;
    auto p = scua_ctx_partition(ctx);
    auto a = scua_arg(ctx, 1);
    if (scua_typeof(p, a) != SCUA_TYPE_ARRAY)
        throw std::invalid_argument("fallbacks must be an array");
    std::vector<love::StrongRef<Font>> refs;
    std::vector<Font *> fonts;
    for (int i = 0; i <= 16; i++)
    {
        auto v = scua_index(p, a, i);
        if (!scua_handle_valid(p, v))
            break;
        if (i == 16)
            throw std::invalid_argument("maximum 16 fallback fonts");
        refs.push_back(borrow<Font>(ctx, v, ObjectKind::Font));
        if (refs.back().get() == f)
            throw std::invalid_argument("font cannot be its own fallback");
        fonts.push_back(refs.back().get());
    }
    f->setFallbacks(fonts);
}
std::vector<love::font::ColoredString> colored(scua_ctx *ctx, int index)
{
    auto p = scua_ctx_partition(ctx);
    auto a = scua_arg(ctx, index);
    if (scua_typeof(p, a) != SCUA_TYPE_ARRAY)
        throw std::invalid_argument("colored text must be an array of {text,color} records");
    std::vector<love::font::ColoredString> result;
    size_t size = 0;
    for (int i = 0; i <= 4096; i++)
    {
        auto v = scua_index(p, a, i);
        if (!scua_handle_valid(p, v))
            break;
        if (i == 4096)
            throw std::invalid_argument("maximum 4096 text segments");
        Options o(p, v);
        auto str = o.str("text", "");
        size += str.size();
        if (size > 1024 * 1024)
            throw std::invalid_argument("colored text exceeds 1 MiB");
        float c[4] = {1, 1, 1, 1};
        if (o.has("color") && scua_to_vec(p, o.field("color"), c, 4) != 4)
            throw std::invalid_argument("text color must be vec4");
        for (float n : c)
            if (!std::isfinite(n))
                throw std::invalid_argument("text colors must be finite");
        result.push_back({str, {c[0], c[1], c[2], c[3]}});
    }
    return result;
}
void print_colored(scua_ctx *ctx)
{
    auto m = transform(ctx, 1);
    auto colors = colored(ctx, 0);
    graphics(ctx).print(colors, m);
}
void printf_colored(scua_ctx *ctx)
{
    auto m = transform(ctx, 3);
    auto colors = colored(ctx, 0);
    double width = number(ctx, 1);
    auto name = string(ctx, 2);
    Font::AlignMode align;
    if (width <= 0 || !Font::getConstant(name.c_str(), align))
        throw std::invalid_argument("invalid wrap width or alignment");
    graphics(ctx).printf(colors, width, align, m);
}
void font_wrap_info(scua_ctx *ctx)
{
    arity(ctx, 3);
    auto colors = colored(ctx, 1);
    double width = number(ctx, 2);
    if (width <= 0)
        throw std::invalid_argument("wrap width must be positive");
    auto f = font(ctx);
    if (!f)
        return;
    std::vector<std::string> lines;
    std::vector<float> widths;
    f->getWrap(colors, width, lines, &widths);
    auto p = scua_ctx_partition(ctx);
    auto a = scua_new_array(p, 0);
    for (size_t i = 0; i < lines.size(); ++i)
    {
        Record r(ctx);
        r.str("text", lines[i]);
        r.num("width", widths[i]);
        if (scua_array_push(p, a, r.value) != SCUA_OK)
            throw std::runtime_error("could not build wrapped lines");
    }
    scua_return_value(ctx, a);
}
void text_set_font(scua_ctx *ctx)
{
    arity(ctx, 2);
    auto t = text(ctx);
    auto f = font(ctx, 1);
    if (t && f)
        t->setFont(f);
}
void text_font(scua_ctx *ctx)
{
    arity(ctx, 1);
    auto t = text(ctx);
    if (t)
    {
        auto f = t->getFont();
        f->retain();
        returnObject(ctx, ObjectKind::Font, f);
    }
}
void text_set_colored(scua_ctx *ctx)
{
    if (scua_arg_count(ctx) < 2 || scua_arg_count(ctx) > 4)
        throw std::invalid_argument("expected text, segments, optional wrap width and alignment");
    auto colors = colored(ctx, 1);
    auto t = text(ctx);
    if (!t)
        return;
    if (!supplied(ctx, 2))
    {
        t->set(colors);
        return;
    }
    double width = number(ctx, 2);
    auto name = supplied(ctx, 3) ? string(ctx, 3) : "left";
    Font::AlignMode align;
    if (width <= 0 || !Font::getConstant(name.c_str(), align))
        throw std::invalid_argument("invalid text width/alignment");
    t->set(colors, width, align);
}
void text_add_colored(scua_ctx *ctx)
{
    auto m = transform(ctx, 2);
    auto colors = colored(ctx, 1);
    auto t = text(ctx);
    if (t)
        scua_return_int(ctx, t->add(colors, m));
}
void text_add_wrapped(scua_ctx *ctx)
{
    auto m = transform(ctx, 4);
    auto colors = colored(ctx, 1);
    double width = number(ctx, 2);
    auto name = string(ctx, 3);
    Font::AlignMode align;
    if (width <= 0 || !Font::getConstant(name.c_str(), align))
        throw std::invalid_argument("invalid wrap width or alignment");
    auto t = text(ctx);
    if (t)
        scua_return_int(ctx, t->addf(colors, width, align, m));
}
void text_entry_dimensions(scua_ctx *ctx)
{
    arity(ctx, 2);
    int i = integer(ctx, 1, 0, 1048576);
    auto t = text(ctx);
    if (t)
        returnVec(ctx, t->getWidth(i), t->getHeight(i));
}
} // namespace
void registerFonts(scua_partition *p, Host &state)
{
    registerObjectType(p, state, ObjectKind::Rasterizer);
    registerObjectType(p, state, ObjectKind::GlyphData);
    registerFunctions(
        p, state,
        {
            {"love_font", "new_rasterizer", guarded<new_rasterizer>,
             "new_rasterizer(size: int, encoded: bytes = nil, settings: any = nil) -> any",
             "Create a TrueType rasterizer (built-in font when encoded is nil); hinting, dpi_scale and sdf "
             "settings."},
            {"love_font", "new_image_rasterizer", guarded<new_image_rasterizer>,
             "new_image_rasterizer(image: any, glyphs: string, extra_spacing: int = 0, dpi_scale: float = 1) "
             "-> any",
             "Create a retained image-strip rasterizer with separator pixels."},
            {"love_font", "new_bmfont_rasterizer", guarded<new_bmfont_rasterizer>,
             "new_bmfont_rasterizer(definition: string, pages: any, dpi_scale: float = 1) -> any",
             "Create a BMFont from explicit ImageData pages; never load page paths from the filesystem."},
            {"love_font", "rasterizer_info", guarded<rasterizer_info>,
             "rasterizer_info(rasterizer: any) -> any",
             "Get rasterizer metrics, data type, glyph count and DPI scale."},
            {"love_font", "rasterizer_glyph_info", guarded<rasterizer_glyph_info>,
             "rasterizer_glyph_info(rasterizer: any, codepoint: int) -> any",
             "Get glyph existence, native index and advance spacing."},
            {"love_font", "rasterizer_has_glyphs", guarded<rasterizer_has_glyphs>,
             "rasterizer_has_glyphs(rasterizer: any, text: string) -> bool", "Check UTF-8 glyph coverage."},
            {"love_font", "rasterizer_kerning", guarded<rasterizer_kerning>,
             "rasterizer_kerning(rasterizer: any, left: int, right: int) -> float",
             "Get kerning between Unicode scalars."},
            {"love_font", "new_glyph", guarded<new_glyph>,
             "new_glyph(rasterizer: any, character: string) -> any",
             "Rasterize the first Unicode character into managed GlyphData."},
            {"love_font", "new_glyph_codepoint", guarded<new_glyph_codepoint>,
             "new_glyph_codepoint(rasterizer: any, codepoint: int) -> any", "Rasterize a Unicode scalar."},
            {"love_font", "glyph_info", guarded<glyph_info>, "glyph_info(glyph: any) -> any",
             "Get Unicode character, dimensions, advance, bearings, bounding box, format and byte size."},
            {"love_font", "glyph_bytes", guarded<glyph_bytes>, "glyph_bytes(glyph: any) -> bytes",
             "Copy native luminance/alpha or RGBA glyph pixels."},
            {"love_font", "glyph_clone", guarded<glyph_clone>, "glyph_clone(glyph: any) -> any",
             "Clone managed glyph pixels and metrics."},
            {"love_font", "glyph_image", guarded<glyph_image>, "glyph_image(glyph: any) -> any",
             "Convert a glyph to RGBA ImageData; nil for empty glyphs."},
            {"love_graphics", "new_font_from_rasterizer", guarded<new_font_from_rasterizer>,
             "new_font_from_rasterizer(rasterizer: any) -> any",
             "Create a managed GPU font retaining its rasterizer."},
            {"love_graphics", "font_set_filter", guarded<font_set_filter>,
             "font_set_filter(font: any, min_filter: string, mag_filter: string, anisotropy: int)",
             "Set font texture filtering."},
            {"love_graphics", "font_filter", guarded<font_filter>, "font_filter(font: any) -> any",
             "Get filtering, anisotropy and DPI scale."},
            {"love_graphics", "font_set_fallbacks", guarded<font_set_fallbacks>,
             "font_set_fallbacks(font: any, fallbacks: any)",
             "Retain up to 16 same-type/same-DPI fallback font rasterizers."},
            {"love_graphics", "font_wrap_info", guarded<font_wrap_info>,
             "font_wrap_info(font: any, segments: any, width: float) -> any",
             "Wrap colored text and return each line with its measured width."},
            {"love_graphics", "print_colored", guarded<print_colored>,
             "print_colored(segments: any, position: vec2, rotation: float = 0, scale: vec2 = nil, origin: "
             "vec2 = nil)",
             "Draw {text,color} records with a full drawable transform."},
            {"love_graphics", "printf_colored", guarded<printf_colored>,
             "printf_colored(segments: any, width: float, alignment: string, position: vec2, rotation: float "
             "= 0, scale: vec2 = nil, origin: vec2 = nil)",
             "Draw wrapped/aligned colored text."},
            {"love_graphics", "text_set_font", guarded<text_set_font>, "text_set_font(text: any, font: any)",
             "Change a cached text object's retained font."},
            {"love_graphics", "text_font", guarded<text_font>, "text_font(text: any) -> any",
             "Get a retained font alias from cached text."},
            {"love_graphics", "text_set_colored", guarded<text_set_colored>,
             "text_set_colored(text: any, segments: any, width: float = nil, alignment: string = \"left\")",
             "Replace cached colored text, optionally wrapped."},
            {"love_graphics", "text_add_colored", guarded<text_add_colored>,
             "text_add_colored(text: any, segments: any, position: vec2, rotation: float = 0, scale: vec2 = "
             "nil, origin: vec2 = nil) -> int",
             "Append transformed colored text."},
            {"love_graphics", "text_add_wrapped", guarded<text_add_wrapped>,
             "text_add_wrapped(text: any, segments: any, width: float, alignment: string, position: vec2, "
             "rotation: float = 0, scale: vec2 = nil, origin: vec2 = nil) -> int",
             "Append transformed wrapped colored text."},
            {"love_graphics", "text_entry_dimensions", guarded<text_entry_dimensions>,
             "text_entry_dimensions(text: any, index: int) -> vec2",
             "Get dimensions of one zero-based cached text entry."},
        });
}
} // namespace lovescua
