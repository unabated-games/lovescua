// Managed fonts, CPU image data, canvases and shaders for SCUA.
#include "binding_util.h"
#include "filesystem/FileData.h"
#include "font/Font.h"
#include "graphics/Font.h"
#include "graphics/Shader.h"
#include "image/ImageData.h"

namespace lovescua
{
namespace
{
using namespace binding;
template <typename T> struct Release
{
    void operator()(T *p) const noexcept
    {
        if (p)
            p->release();
    }
};
template <typename T> using Owned = std::unique_ptr<T, Release<T>>;

Owned<love::filesystem::FileData> bytes(scua_ctx *ctx, int index)
{
    const uint8_t *data = nullptr;
    size_t size = 0;
    if (scua_arg_bytes(ctx, index, &data, &size) != SCUA_OK || size == 0 || size > 32 * 1024 * 1024)
        throw std::invalid_argument("expected encoded bytes (1 byte to 32 MiB)");
    Owned<love::filesystem::FileData> result(new love::filesystem::FileData(size, "resource"));
    std::memcpy(result->getData(), data, size);
    return result;
}
love::graphics::Font *font(scua_ctx *ctx, int index = 0)
{
    return objectArgument<love::graphics::Font>(ctx, index, ObjectKind::Font);
}
love::image::ImageData *image(scua_ctx *ctx, int index = 0)
{
    return objectArgument<love::image::ImageData>(ctx, index, ObjectKind::ImageData);
}
love::graphics::Shader *shader(scua_ctx *ctx, int index = 0)
{
    return objectArgument<love::graphics::Shader>(ctx, index, ObjectKind::Shader);
}

void new_font(scua_ctx *ctx)
{
    arity(ctx, 1);
    auto size = integer(ctx, 0, 1, 512);
    returnObject(ctx, ObjectKind::Font, graphics(ctx, false).newDefaultFont(size, {}));
}
void new_font_data(scua_ctx *ctx)
{
    arity(ctx, 2);
    auto size = integer(ctx, 1, 1, 512);
    auto data = bytes(ctx, 0);
    auto module = love::Module::getInstance<love::font::Font>(love::Module::M_FONT);
    Owned<love::font::Rasterizer> rasterizer(module->newTrueTypeRasterizer(data.get(), size, {}));
    returnObject(ctx, ObjectKind::Font, graphics(ctx, false).newFont(rasterizer.get()));
}
void set_font(scua_ctx *ctx)
{
    arity(ctx, 1);
    if (!supplied(ctx, 0))
    {
        graphics(ctx, false).setFont(nullptr);
        return;
    }
    auto f = font(ctx);
    if (f)
        graphics(ctx, false).setFont(f);
}
void get_font(scua_ctx *ctx)
{
    arity(ctx, 0);
    auto f = graphics(ctx, false).getFont();
    f->retain();
    returnObject(ctx, ObjectKind::Font, f);
}
void font_width(scua_ctx *ctx)
{
    arity(ctx, 2);
    auto text = string(ctx, 1);
    auto f = font(ctx);
    if (f)
        scua_return_float(ctx, f->getWidth(text));
}
void font_metrics(scua_ctx *ctx)
{
    arity(ctx, 1);
    auto f = font(ctx);
    if (!f)
        return;
    Record r(ctx);
    r.num("height", f->getHeight());
    r.num("line_height", f->getLineHeight());
    r.num("baseline", f->getBaseline());
    r.integer("ascent", f->getAscent());
    r.integer("descent", f->getDescent());
    r.done();
}
void font_has_glyphs(scua_ctx *ctx)
{
    arity(ctx, 2);
    auto text = string(ctx, 1);
    auto f = font(ctx);
    if (f)
        scua_return_bool(ctx, f->hasGlyphs(text));
}
void font_set_line_height(scua_ctx *ctx)
{
    arity(ctx, 2);
    auto h = number(ctx, 1);
    if (h <= 0 || h > 100)
        throw std::invalid_argument("line height must be in (0,100]");
    auto f = font(ctx);
    if (f)
        f->setLineHeight(h);
}
void font_kerning(scua_ctx *ctx)
{
    arity(ctx, 3);
    auto a = string(ctx, 1), b = string(ctx, 2);
    auto f = font(ctx);
    if (f)
        scua_return_float(ctx, f->getKerning(a, b));
}
void font_wrap(scua_ctx *ctx)
{
    arity(ctx, 3);
    auto text = string(ctx, 1);
    auto limit = number(ctx, 2);
    if (limit <= 0)
        throw std::invalid_argument("wrap limit must be positive");
    auto f = font(ctx);
    if (!f)
        return;
    std::vector<std::string> lines;
    f->getWrap({{text, love::Colorf(1, 1, 1, 1)}}, limit, lines);
    returnStrings(ctx, lines);
}
void printf_text(scua_ctx *ctx)
{
    arity(ctx, 4);
    auto text = string(ctx, 0);
    float pos[2];
    vector(ctx, 1, pos, 2);
    auto width = number(ctx, 2);
    auto align = string(ctx, 3);
    love::graphics::Font::AlignMode mode;
    if (!love::graphics::Font::getConstant(align.c_str(), mode) || width <= 0)
        throw std::invalid_argument("invalid printf width or alignment");
    love::Matrix4 m(pos[0], pos[1], 0, 1, 1, 0, 0, 0, 0);
    graphics(ctx).printf({{text, love::Colorf(1, 1, 1, 1)}}, width, mode, m);
}
void new_image_data(scua_ctx *ctx)
{
    if (scua_arg_count(ctx) < 2 || scua_arg_count(ctx) > 4)
        throw std::invalid_argument("new_data expects dimensions and optional format/raw bytes");
    int width = integer(ctx, 0, 1, 4096), height = integer(ctx, 1, 1, 4096);
    auto format = love::PIXELFORMAT_RGBA8_UNORM;
    if (supplied(ctx, 2))
    {
        auto name = string(ctx, 2);
        if (!love::getConstant(name.c_str(), format) || !love::image::ImageData::validPixelFormat(format))
            throw std::invalid_argument("unsupported CPU pixel format");
    }
    if (love::getPixelFormatBlockSize(format) == 0)
        throw std::invalid_argument("CPU images require a concrete pixel format");
    size_t size = love::getPixelFormatBlockSize(format) * size_t(width) * height;
    if (size > 64 * 1024 * 1024)
        throw std::invalid_argument("CPU image limit is 64 MiB");
    Owned<love::image::ImageData> data(new love::image::ImageData(width, height, format));
    if (supplied(ctx, 3))
    {
        const uint8_t *raw;
        size_t n;
        if (scua_arg_bytes(ctx, 3, &raw, &n) != SCUA_OK || n != size)
            throw std::invalid_argument(
                "raw pixel byte size must exactly match the image format and dimensions");
        std::memcpy(data->getData(), raw, n);
    }
    returnObject(ctx, ObjectKind::ImageData, data.release());
}
void decode_image_data(scua_ctx *ctx)
{
    arity(ctx, 1);
    auto data = bytes(ctx, 0);
    Owned<love::image::ImageData> decoded(new love::image::ImageData(data.get()));
    if (decoded->getWidth() > 4096 || decoded->getHeight() > 4096)
        throw std::invalid_argument("image too large");
    returnObject(ctx, ObjectKind::ImageData, decoded.release());
}
void image_data_dimensions(scua_ctx *ctx)
{
    arity(ctx, 1);
    auto data = image(ctx);
    if (data)
        returnVec(ctx, data->getWidth(), data->getHeight());
}
void image_data_size(scua_ctx *ctx)
{
    arity(ctx, 1);
    auto data = image(ctx);
    if (data)
        scua_return_int(ctx, data->getSize());
}
void get_pixel(scua_ctx *ctx)
{
    arity(ctx, 3);
    auto data = image(ctx);
    if (!data)
        return;
    int x = integer(ctx, 1, 0, data->getWidth() - 1), y = integer(ctx, 2, 0, data->getHeight() - 1);
    auto c = data->getPixel(x, y);
    float v[] = {c.r, c.g, c.b, c.a};
    scua_return_vec(ctx, v, 4);
}
void set_pixel(scua_ctx *ctx)
{
    arity(ctx, 4);
    float c[4];
    vector(ctx, 3, c, 4);
    auto data = image(ctx);
    if (!data)
        return;
    int x = integer(ctx, 1, 0, data->getWidth() - 1), y = integer(ctx, 2, 0, data->getHeight() - 1);
    data->setPixel(x, y, {c[0], c[1], c[2], c[3]});
}
void clone_image_data(scua_ctx *ctx)
{
    arity(ctx, 1);
    auto data = image(ctx);
    if (data)
    {
        auto copy = data->clone();
        copy->setLinear(data->isLinear());
        returnObject(ctx, ObjectKind::ImageData, copy);
    }
}
void encode_png(scua_ctx *ctx)
{
    arity(ctx, 1);
    auto data = image(ctx);
    if (!data)
        return;
    Owned<love::filesystem::FileData> encoded(
        data->encode(love::image::FormatHandler::ENCODED_PNG, "image.png", false));
    scua_return_bytes(ctx, static_cast<const uint8_t *>(encoded->getData()), encoded->getSize());
}
void image_data_bytes(scua_ctx *ctx)
{
    arity(ctx, 1);
    auto data = image(ctx);
    if (data)
        scua_return_bytes(ctx, static_cast<const uint8_t *>(data->getData()), data->getSize());
}
void new_image_from_data(scua_ctx *ctx)
{
    arity(ctx, 1);
    auto data = image(ctx);
    if (!data)
        return;
    love::graphics::Texture::Slices slices(love::graphics::TEXTURE_2D);
    slices.set(0, 0, data);
    returnImage(ctx, graphics(ctx, false).newTexture({}, &slices));
}
void new_canvas(scua_ctx *ctx)
{
    arity(ctx, 2);
    love::graphics::Texture::Settings settings;
    settings.width = integer(ctx, 0, 1, 4096);
    settings.height = integer(ctx, 1, 1, 4096);
    settings.renderTarget = true;
    returnImage(ctx, graphics(ctx, false).newTexture(settings));
}
void set_canvas(scua_ctx *ctx)
{
    arity(ctx, 1);
    auto &gfx = graphics(ctx);
    if (!supplied(ctx, 0))
    {
        gfx.setRenderTarget();
        return;
    }
    auto canvas = imageArgument(ctx, 0);
    if (!canvas)
        return;
    if (!canvas->texture->isRenderTarget())
        throw std::invalid_argument("set_canvas requires a canvas");
    gfx.setRenderTarget(love::graphics::Graphics::RenderTarget(canvas->texture), 0);
}
void new_shader(scua_ctx *ctx)
{
    arity(ctx, 1);
    auto source = string(ctx, 0);
    if (source.size() > 1024 * 1024)
        throw std::invalid_argument("shader source too large");
    returnObject(ctx, ObjectKind::Shader, graphics(ctx, false).newShader({source}, {}));
}
void set_shader(scua_ctx *ctx)
{
    arity(ctx, 1);
    if (!supplied(ctx, 0))
    {
        graphics(ctx, false).setShader();
        return;
    }
    auto s = shader(ctx);
    if (s)
        graphics(ctx, false).setShader(s);
}
void shader_warnings(scua_ctx *ctx)
{
    arity(ctx, 1);
    auto s = shader(ctx);
    if (s)
        returnString(ctx, s->getWarnings());
}
void shader_has_uniform(scua_ctx *ctx)
{
    arity(ctx, 2);
    auto name = string(ctx, 1);
    auto s = shader(ctx);
    if (s)
        scua_return_bool(ctx, s->getUniformInfo(name) != nullptr);
}
void shader_send_texture(scua_ctx *ctx)
{
    arity(ctx, 3);
    auto name = string(ctx, 1);
    auto s = shader(ctx);
    auto texture = imageArgument(ctx, 2);
    if (!s || !texture)
        return;
    auto info = s->getUniformInfo(name);
    if (!info || info->baseType != love::graphics::Shader::UNIFORM_SAMPLER)
        throw std::invalid_argument("expected an active texture sampler uniform");
    auto value = texture->texture;
    s->sendTextures(info, &value, 1);
}
void shader_send(scua_ctx *ctx)
{
    arity(ctx, 3);
    auto name = string(ctx, 1);
    auto s = shader(ctx);
    if (!s)
        return;
    auto info = s->getUniformInfo(name);
    if (!info)
        throw std::invalid_argument("shader uniform is absent or optimized away: " + name);
    if (info->baseType == love::graphics::Shader::UNIFORM_FLOAT)
    {
        float value[4];
        int n = scua_to_vec(scua_ctx_partition(ctx), scua_arg(ctx, 2), value, 4);
        if (n == 0)
        {
            value[0] = number(ctx, 2);
            n = 1;
        }
        if (n != info->components)
            throw std::invalid_argument("shader uniform component count mismatch");
        for (int i = 0; i < n; ++i)
            if (!std::isfinite(value[i]))
                throw std::invalid_argument("uniform must be finite");
        std::copy(value, value + n, info->floats);
        s->updateUniform(info, 1);
    }
    else if (info->baseType == love::graphics::Shader::UNIFORM_INT ||
             info->baseType == love::graphics::Shader::UNIFORM_BOOL)
    {
        if (info->components != 1)
            throw std::invalid_argument("integer vector uniforms are not yet supported");
        info->ints[0] = info->baseType == love::graphics::Shader::UNIFORM_BOOL
                            ? boolean(ctx, 2)
                            : integer(ctx, 2, INT_MIN, INT_MAX);
        s->updateUniform(info, 1);
    }
    else
        throw std::invalid_argument("shader_send supports float, vec2/3/4, int and bool uniforms");
}

double uniformNumber(scua_partition *p, scua_Handle v)
{
    int type = scua_typeof(p, v);
    double n = scua_to_num(p, v);
    if ((type != SCUA_TYPE_INT && type != SCUA_TYPE_FLOAT) || !std::isfinite(n) ||
        std::abs(n) > std::numeric_limits<float>::max())
        throw std::invalid_argument("uniform values must be finite numbers");
    return n;
}
std::vector<double> uniformComponents(scua_partition *p, scua_Handle value, int expected,
                                      bool booleans = false)
{
    std::vector<double> values;
    float vec[4];
    int n = scua_to_vec(p, value, vec, 4);
    if (n)
    {
        if (booleans)
            throw std::invalid_argument("uniform requires boolean values");
        for (int i = 0; i < n; ++i)
        {
            if (!std::isfinite(vec[i]))
                throw std::invalid_argument("uniform vector must be finite");
            values.push_back(vec[i]);
        }
    }
    else if (scua_typeof(p, value) == SCUA_TYPE_ARRAY)
    {
        for (int i = 0; i <= expected; ++i)
        {
            auto item = scua_index(p, value, i);
            if (!scua_handle_valid(p, item))
                break;
            if (i == expected)
                throw std::invalid_argument("uniform component count mismatch");
            if (booleans)
            {
                if (scua_typeof(p, item) != SCUA_TYPE_BOOL)
                    throw std::invalid_argument("uniform requires boolean values");
                values.push_back(scua_to_bool(p, item));
            }
            else
                values.push_back(uniformNumber(p, item));
        }
    }
    else if (booleans)
    {
        if (scua_typeof(p, value) != SCUA_TYPE_BOOL)
            throw std::invalid_argument("uniform requires boolean values");
        values.push_back(scua_to_bool(p, value));
    }
    else
        values.push_back(uniformNumber(p, value));
    if (int(values.size()) != expected)
        throw std::invalid_argument("uniform component count mismatch");
    return values;
}
void shader_send_array(scua_ctx *ctx)
{
    arity(ctx, 3);
    auto name = string(ctx, 1);
    auto s = shader(ctx);
    if (!s)
        return;
    auto info = s->getUniformInfo(name);
    if (!info)
        throw std::invalid_argument("shader uniform is absent or optimized away: " + name);
    using Shader = love::graphics::Shader;
    if (info->baseType != Shader::UNIFORM_FLOAT && info->baseType != Shader::UNIFORM_MATRIX &&
        info->baseType != Shader::UNIFORM_INT && info->baseType != Shader::UNIFORM_UINT &&
        info->baseType != Shader::UNIFORM_BOOL)
        throw std::invalid_argument("shader_send_array requires a numeric uniform");
    bool matrix = info->baseType == Shader::UNIFORM_MATRIX;
    int components = matrix ? info->matrix.columns * info->matrix.rows : info->components;
    if (components < 1 || components > 16)
        throw std::runtime_error("unsupported uniform component count");
    auto p = scua_ctx_partition(ctx);
    auto input = scua_arg(ctx, 2);
    if (scua_typeof(p, input) != SCUA_TYPE_ARRAY)
        throw std::invalid_argument("uniform values must be an array");
    std::vector<float> floats;
    std::vector<int> ints;
    std::vector<unsigned> uints;
    int count = 0;
    for (; count <= std::min(info->count, 1024); ++count)
    {
        auto item = scua_index(p, input, count);
        if (!scua_handle_valid(p, item))
            break;
        if (count == std::min(info->count, 1024))
            throw std::invalid_argument("uniform array exceeds declared size or 1024 values");
        auto values = uniformComponents(p, item, components, info->baseType == Shader::UNIFORM_BOOL);
        if (matrix)
            for (int column = 0; column < info->matrix.columns; ++column)
                for (int row = 0; row < info->matrix.rows; ++row)
                    floats.push_back(values[row * info->matrix.columns + column]);
        else
            for (double value : values)
            {
                if (info->baseType == Shader::UNIFORM_FLOAT)
                    floats.push_back(value);
                else if (info->baseType == Shader::UNIFORM_UINT)
                {
                    if (value < 0 || value > UINT_MAX || std::floor(value) != value)
                        throw std::invalid_argument("unsigned uniform value outside integer range");
                    uints.push_back(value);
                }
                else
                {
                    if (value < INT_MIN || value > INT_MAX || std::floor(value) != value)
                        throw std::invalid_argument("uniform value outside integer range");
                    ints.push_back(value);
                }
            }
    }
    if (count == 0)
        throw std::invalid_argument("uniform array must not be empty");
    // Validate the complete batch before changing the native uniform storage.
    if (!floats.empty())
        std::copy(floats.begin(), floats.end(), info->floats);
    if (!ints.empty())
        std::copy(ints.begin(), ints.end(), info->ints);
    if (!uints.empty())
        std::copy(uints.begin(), uints.end(), info->uints);
    s->updateUniform(info, count);
}
void shader_send_matrix(scua_ctx *ctx)
{
    if (scua_arg_count(ctx) < 3 || scua_arg_count(ctx) > 4)
        throw std::invalid_argument("shader_send_matrix expects 3 or 4 arguments");
    auto name = string(ctx, 1);
    auto s = shader(ctx);
    if (!s)
        return;
    auto info = s->getUniformInfo(name);
    if (!info || info->baseType != love::graphics::Shader::UNIFORM_MATRIX)
        throw std::invalid_argument("expected an active matrix uniform");
    int rows = info->matrix.rows, columns = info->matrix.columns;
    if (rows < 2 || rows > 4 || columns < 2 || columns > 4)
        throw std::runtime_error("unsupported matrix size");
    auto values = uniformComponents(scua_ctx_partition(ctx), scua_arg(ctx, 2), rows * columns);
    bool columnMajor = supplied(ctx, 3) ? boolean(ctx, 3) : false;
    for (int column = 0; column < columns; ++column)
        for (int row = 0; row < rows; ++row)
            info->floats[column * rows + row] =
                values[columnMajor ? column * rows + row : row * columns + column];
    s->updateUniform(info, 1);
}
void shader_uniform_info(scua_ctx *ctx)
{
    arity(ctx, 2);
    auto name = string(ctx, 1);
    auto s = shader(ctx);
    if (!s)
        return;
    auto info = s->getUniformInfo(name);
    if (!info)
    {
        scua_return_value(ctx, scua_nil(scua_ctx_partition(ctx)));
        return;
    }
    bool matrix = info->baseType == love::graphics::Shader::UNIFORM_MATRIX;
    const char *kind = "other";
    using Shader = love::graphics::Shader;
    switch (info->baseType)
    {
    case Shader::UNIFORM_FLOAT:
        kind = "float";
        break;
    case Shader::UNIFORM_MATRIX:
        kind = "matrix";
        break;
    case Shader::UNIFORM_INT:
        kind = "int";
        break;
    case Shader::UNIFORM_UINT:
        kind = "uint";
        break;
    case Shader::UNIFORM_BOOL:
        kind = "bool";
        break;
    case Shader::UNIFORM_SAMPLER:
        kind = "sampler";
        break;
    default:
        break;
    }
    Record r(ctx);
    r.str("kind", kind);
    r.integer("count", info->count);
    r.integer("components", matrix ? info->matrix.rows * info->matrix.columns : info->components);
    r.integer("rows", matrix ? info->matrix.rows : 0);
    r.integer("columns", matrix ? info->matrix.columns : 0);
    r.done();
}

} // namespace
void registerObjects(scua_partition *p, Host &state)
{
    registerObjectType(p, state, ObjectKind::Font);
    registerObjectType(p, state, ObjectKind::ImageData);
    registerObjectType(p, state, ObjectKind::Shader);
    registerFunctions(
        p, state,
        {
            {"love_graphics", "new_font", guarded<new_font>, "new_font(size: int) -> any",
             "Create a managed built-in font."},
            {"love_graphics", "new_font_data", guarded<new_font_data>,
             "new_font_data(encoded: bytes, size: int) -> any",
             "Create a managed TrueType/OpenType font from resource bytes."},
            {"love_graphics", "set_font", guarded<set_font>, "set_font(font: any)",
             "Select a font; nil restores the default."},
            {"love_graphics", "get_font", guarded<get_font>, "get_font() -> any",
             "Get a managed alias of the active font."},
            {"love_graphics", "font_width", guarded<font_width>,
             "font_width(font: any, text: string) -> float", "Measure UTF-8 text."},
            {"love_graphics", "font_metrics", guarded<font_metrics>, "font_metrics(font: any) -> any",
             "Get font metrics."},
            {"love_graphics", "font_has_glyphs", guarded<font_has_glyphs>,
             "font_has_glyphs(font: any, text: string) -> bool", "Check glyph coverage."},
            {"love_graphics", "font_set_line_height", guarded<font_set_line_height>,
             "font_set_line_height(font: any, height: float)", "Set font line spacing."},
            {"love_graphics", "font_kerning", guarded<font_kerning>,
             "font_kerning(font: any, left: string, right: string) -> float", "Get glyph kerning."},
            {"love_graphics", "font_wrap", guarded<font_wrap>,
             "font_wrap(font: any, text: string, width: float) -> any", "Wrap text into lines."},
            {"love_graphics", "printf", guarded<printf_text>,
             "printf(text: string, position: vec2, width: float, align: string)",
             "Draw wrapped and aligned text."},
            {"love_image", "new_data", guarded<new_image_data>,
             "new_data(width: int, height: int, format: string = \"rgba8\", raw: bytes = nil) -> any",
             "Create zeroed CPU image data in a checked native format, optionally from exact raw bytes."},
            {"love_image", "decode", guarded<decode_image_data>, "decode(encoded: bytes) -> any",
             "Decode CPU image data."},
            {"love_image", "dimensions", guarded<image_data_dimensions>, "dimensions(data: any) -> vec2",
             "Image-data dimensions."},
            {"love_image", "size", guarded<image_data_size>, "size(data: any) -> int",
             "Image-data byte size."},
            {"love_image", "get_pixel", guarded<get_pixel>, "get_pixel(data: any, x: int, y: int) -> vec4",
             "Read an RGBA pixel."},
            {"love_image", "set_pixel", guarded<set_pixel>,
             "set_pixel(data: any, x: int, y: int, color: vec4)", "Write an RGBA pixel."},
            {"love_image", "clone", guarded<clone_image_data>, "clone(data: any) -> any", "Copy image data."},
            {"love_image", "encode_png", guarded<encode_png>, "encode_png(data: any) -> bytes",
             "Encode PNG bytes without writing host files."},
            {"love_image", "bytes", guarded<image_data_bytes>, "bytes(data: any) -> bytes",
             "Copy raw pixel bytes."},
            {"love_graphics", "new_image_from_data", guarded<new_image_from_data>,
             "new_image_from_data(data: any) -> any", "Upload CPU image data."},
            {"love_graphics", "new_canvas", guarded<new_canvas>, "new_canvas(width: int, height: int) -> any",
             "Create an RGBA render target."},
            {"love_graphics", "set_canvas", guarded<set_canvas>, "set_canvas(canvas: any)",
             "Set the render target; nil selects the screen."},
            {"love_graphics", "new_shader", guarded<new_shader>, "new_shader(source: string) -> any",
             "Compile a managed LÖVE shader."},
            {"love_graphics", "set_shader", guarded<set_shader>, "set_shader(shader: any)",
             "Select a shader; nil restores the default."},
            {"love_graphics", "shader_warnings", guarded<shader_warnings>,
             "shader_warnings(shader: any) -> string", "Get compiler warnings."},
            {"love_graphics", "shader_has_uniform", guarded<shader_has_uniform>,
             "shader_has_uniform(shader: any, name: string) -> bool", "Check for an active uniform."},
            {"love_graphics", "shader_send_texture", guarded<shader_send_texture>,
             "shader_send_texture(shader: any, name: string, texture: any)",
             "Bind and retain a texture sampler uniform."},
            {"love_graphics", "shader_send", guarded<shader_send>,
             "shader_send(shader: any, name: string, value: any)", "Send a scalar or vector uniform."},
            {"love_graphics", "shader_send_array", guarded<shader_send_array>,
             "shader_send_array(shader: any, name: string, values: any)",
             "Send an array of scalars/vectors or flat row-major matrices, maximum 1024 elements."},
            {"love_graphics", "shader_send_matrix", guarded<shader_send_matrix>,
             "shader_send_matrix(shader: any, name: string, values: any, column_major: bool = false)",
             "Send a flat 2x2..4x4 matrix, row-major by default."},
            {"love_graphics", "shader_uniform_info", guarded<shader_uniform_info>,
             "shader_uniform_info(shader: any, name: string) -> any",
             "Get active uniform kind, count, components and matrix shape, or nil."},
        });
}
} // namespace lovescua
