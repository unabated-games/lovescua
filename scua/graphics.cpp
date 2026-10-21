// SCUA adapters for LÖVE graphics state and drawing primitives.
#include "binding_util.h"
namespace lovescua
{
namespace
{
using namespace binding;
void get_color(scua_ctx *ctx)
{
    arity(ctx, 0);
    auto c = graphics(ctx, false).getColor();
    float v[] = {c.r, c.g, c.b, c.a};
    scua_return_vec(ctx, v, 4);
}

void set_background_color(scua_ctx *ctx)
{
    arity(ctx, 1);
    float v[4];
    vector(ctx, 0, v, 4);
    graphics(ctx, false).setBackgroundColor(love::Colorf(v[0], v[1], v[2], v[3]));
}

void get_background_color(scua_ctx *ctx)
{
    arity(ctx, 0);
    auto c = graphics(ctx, false).getBackgroundColor();
    float v[] = {c.r, c.g, c.b, c.a};
    scua_return_vec(ctx, v, 4);
}

void is_active(scua_ctx *ctx)
{
    arity(ctx, 0);
    scua_return_bool(ctx, graphics(ctx, false).isActive());
}

void is_created(scua_ctx *ctx)
{
    arity(ctx, 0);
    scua_return_bool(ctx, graphics(ctx, false).isCreated());
}

void is_wireframe(scua_ctx *ctx)
{
    arity(ctx, 0);
    scua_return_bool(ctx, graphics(ctx, false).isWireframe());
}

void line_width(scua_ctx *ctx)
{
    arity(ctx, 0);
    scua_return_float(ctx, graphics(ctx, false).getLineWidth());
}

void point_size(scua_ctx *ctx)
{
    arity(ctx, 0);
    scua_return_float(ctx, graphics(ctx, false).getPointSize());
}

void dpi_scale(scua_ctx *ctx)
{
    arity(ctx, 0);
    scua_return_float(ctx, graphics(ctx, false).getCurrentDPIScale());
}

void set_line_width(scua_ctx *ctx)
{
    arity(ctx, 1);
    double v = number(ctx, 0);
    if (v <= 0 || v > 8192)
        throw std::invalid_argument("size must be in (0,8192]");
    graphics(ctx, false).setLineWidth(v);
}

void set_point_size(scua_ctx *ctx)
{
    arity(ctx, 1);
    double v = number(ctx, 0);
    if (v <= 0 || v > 8192)
        throw std::invalid_argument("size must be in (0,8192]");
    graphics(ctx, false).setPointSize(v);
}

void set_wireframe(scua_ctx *ctx)
{
    arity(ctx, 1);
    graphics(ctx, false).setWireframe(boolean(ctx, 0));
}

void set_line_style(scua_ctx *ctx)
{
    arity(ctx, 1);
    auto s = string(ctx, 0);
    love::graphics::Graphics::LineStyle v;
    if (!love::graphics::Graphics::getConstant(s.c_str(), v))
        throw std::invalid_argument("invalid set_line_style mode");
    graphics(ctx, false).setLineStyle(v);
}

void set_line_join(scua_ctx *ctx)
{
    arity(ctx, 1);
    auto s = string(ctx, 0);
    love::graphics::Graphics::LineJoin v;
    if (!love::graphics::Graphics::getConstant(s.c_str(), v))
        throw std::invalid_argument("invalid set_line_join mode");
    graphics(ctx, false).setLineJoin(v);
}

void line_style(scua_ctx *ctx)
{
    arity(ctx, 0);
    const char *s = "";
    love::graphics::Graphics::getConstant(graphics(ctx, false).getLineStyle(), s);
    returnString(ctx, s);
}

void line_join(scua_ctx *ctx)
{
    arity(ctx, 0);
    const char *s = "";
    love::graphics::Graphics::getConstant(graphics(ctx, false).getLineJoin(), s);
    returnString(ctx, s);
}

void translate(scua_ctx *ctx)
{
    arity(ctx, 1);
    float v[2];
    vector(ctx, 0, v, 2);
    graphics(ctx).translate(v[0], v[1]);
}

void scale(scua_ctx *ctx)
{
    arity(ctx, 1);
    float v[2];
    vector(ctx, 0, v, 2);
    graphics(ctx).scale(v[0], v[1]);
}

void shear(scua_ctx *ctx)
{
    arity(ctx, 1);
    float v[2];
    vector(ctx, 0, v, 2);
    graphics(ctx).shear(v[0], v[1]);
}

void rotate(scua_ctx *ctx)
{
    arity(ctx, 1);
    graphics(ctx).rotate(number(ctx, 0));
}

void origin(scua_ctx *ctx)
{
    arity(ctx, 0);
    graphics(ctx).origin();
}

void pop(scua_ctx *ctx)
{
    arity(ctx, 0);
    graphics(ctx).pop();
}

void reset(scua_ctx *ctx)
{
    arity(ctx, 0);
    graphics(ctx).reset();
}

void push(scua_ctx *ctx)
{
    if (scua_arg_count(ctx) > 1)
        throw std::invalid_argument("push expects zero or one argument");
    auto mode = love::graphics::Graphics::STACK_TRANSFORM;
    if (supplied(ctx, 0))
    {
        auto s = string(ctx, 0);
        if (!love::graphics::Graphics::getConstant(s.c_str(), mode))
            throw std::invalid_argument("invalid stack mode");
    }
    graphics(ctx).push(mode);
}

void stack_depth(scua_ctx *ctx)
{
    arity(ctx, 0);
    scua_return_int(ctx, graphics(ctx, false).getStackDepth());
}

void transform_point(scua_ctx *ctx)
{
    arity(ctx, 1);
    float v[2];
    vector(ctx, 0, v, 2);
    auto p = graphics(ctx, false).transformPoint({v[0], v[1]});
    returnVec(ctx, p.x, p.y);
}

void inverse_transform_point(scua_ctx *ctx)
{
    arity(ctx, 1);
    float v[2];
    vector(ctx, 0, v, 2);
    auto p = graphics(ctx, false).inverseTransformPoint({v[0], v[1]});
    returnVec(ctx, p.x, p.y);
}

void pixel_dimensions(scua_ctx *ctx)
{
    arity(ctx, 0);
    returnVec(ctx, graphics(ctx, false).getPixelWidth(), graphics(ctx, false).getPixelHeight());
}

void points(scua_ctx *ctx)
{
    arity(ctx, 1);
    auto p = binding::points(ctx, 0);
    graphics(ctx).points(p.data(), nullptr, p.size());
}

void line(scua_ctx *ctx)
{
    arity(ctx, 1);
    auto p = binding::points(ctx, 0, 2);
    graphics(ctx).polyline(p.data(), p.size());
}

void polygon(scua_ctx *ctx)
{
    arity(ctx, 2);
    auto m = mode(ctx);
    auto p = binding::points(ctx, 1, 3);
    graphics(ctx).polygon(m, p.data(), p.size(), false);
}

void ellipse(scua_ctx *ctx)
{
    arity(ctx, 3);
    auto m = mode(ctx);
    float p[2], r[2];
    vector(ctx, 1, p, 2);
    vector(ctx, 2, r, 2);
    if (r[0] <= 0 || r[1] <= 0 || r[0] > 8192 || r[1] > 8192)
        throw std::invalid_argument("ellipse radii must be in (0,8192]");
    graphics(ctx).ellipse(m, p[0], p[1], r[0], r[1]);
}

void arc(scua_ctx *ctx)
{
    arity(ctx, 6);
    auto m = mode(ctx);
    auto s = string(ctx, 1);
    love::graphics::Graphics::ArcMode a;
    if (!love::graphics::Graphics::getConstant(s.c_str(), a))
        throw std::invalid_argument("invalid arc mode");
    float p[2];
    vector(ctx, 2, p, 2);
    auto r = number(ctx, 3);
    if (r <= 0 || r > 8192)
        throw std::invalid_argument("invalid arc radius");
    graphics(ctx).arc(m, a, p[0], p[1], r, number(ctx, 4), number(ctx, 5));
}

void rounded_rectangle(scua_ctx *ctx)
{
    arity(ctx, 4);
    auto m = mode(ctx);
    float p[2], s[2], r[2];
    vector(ctx, 1, p, 2);
    vector(ctx, 2, s, 2);
    vector(ctx, 3, r, 2);
    if (r[0] < 0 || r[1] < 0)
        throw std::invalid_argument("corner radii must be nonnegative");
    graphics(ctx).rectangle(m, p[0], p[1], s[0], s[1], r[0], r[1]);
}

void set_scissor(scua_ctx *ctx)
{
    arity(ctx, 1);
    float v[4];
    vector(ctx, 0, v, 4);
    if (v[2] < 0 || v[3] < 0)
        throw std::invalid_argument("scissor size must be nonnegative");
    graphics(ctx).setScissor({v[0], v[1], v[2], v[3]});
}

void intersect_scissor(scua_ctx *ctx)
{
    arity(ctx, 1);
    float v[4];
    vector(ctx, 0, v, 4);
    if (v[2] < 0 || v[3] < 0)
        throw std::invalid_argument("scissor size must be nonnegative");
    graphics(ctx).intersectScissor({v[0], v[1], v[2], v[3]});
}

void clear_scissor(scua_ctx *ctx)
{
    arity(ctx, 0);
    graphics(ctx).setScissor();
}

void scissor(scua_ctx *ctx)
{
    arity(ctx, 0);
    love::FRect r;
    if (!graphics(ctx, false).getScissor(r))
    {
        scua_return_value(ctx, scua_nil(scua_ctx_partition(ctx)));
        return;
    }
    float v[] = {r.x, r.y, r.w, r.h};
    scua_return_vec(ctx, v, 4);
}

void set_blend_mode(scua_ctx *ctx)
{
    if (scua_arg_count(ctx) < 1 || scua_arg_count(ctx) > 2)
        throw std::invalid_argument("set_blend_mode expects one or two arguments");
    auto s = string(ctx, 0);
    love::graphics::BlendMode m;
    if (!love::graphics::getConstant(s.c_str(), m))
        throw std::invalid_argument("invalid blend mode");
    auto a = love::graphics::BLENDALPHA_MULTIPLY;
    if (supplied(ctx, 1))
    {
        auto name = string(ctx, 1);
        if (!love::graphics::getConstant(name.c_str(), a))
            throw std::invalid_argument("invalid alpha mode");
    }
    graphics(ctx, false).setBlendMode(m, a);
}

void blend_mode(scua_ctx *ctx)
{
    arity(ctx, 0);
    love::graphics::BlendAlpha a;
    auto m = graphics(ctx, false).getBlendMode(a);
    const char *ms = "", *as = "";
    love::graphics::getConstant(m, ms);
    love::graphics::getConstant(a, as);
    Record r(ctx);
    r.str("mode", ms);
    r.str("alpha", as);
    r.done();
}

void renderer_info(scua_ctx *ctx)
{
    arity(ctx, 0);
    auto i = graphics(ctx, false).getRendererInfo();
    Record r(ctx);
    r.str("name", i.name);
    r.str("version", i.version);
    r.str("vendor", i.vendor);
    r.str("device", i.device);
    r.done();
}

void stats(scua_ctx *ctx)
{
    arity(ctx, 0);
    auto s = graphics(ctx, false).getStats();
    Record r(ctx);
    r.integer("draw_calls", s.drawCalls);
    r.integer("batched_draw_calls", s.drawCallsBatched);
    r.integer("textures", s.textures);
    r.integer("fonts", s.fonts);
    r.integer("texture_memory", s.textureMemory);
    r.integer("buffers", s.buffers);
    r.done();
}

void is_supported(scua_ctx *ctx)
{
    arity(ctx, 1);
    auto s = string(ctx, 0);
    love::graphics::Graphics::Feature f;
    if (!love::graphics::Graphics::getConstant(s.c_str(), f))
        throw std::invalid_argument("unknown graphics feature");
    scua_return_bool(ctx, graphics(ctx, false).getCapabilities().features[f]);
}

void system_limit(scua_ctx *ctx)
{
    arity(ctx, 1);
    auto s = string(ctx, 0);
    love::graphics::Graphics::SystemLimit l;
    if (!love::graphics::Graphics::getConstant(s.c_str(), l))
        throw std::invalid_argument("unknown graphics limit");
    scua_return_float(ctx, graphics(ctx, false).getCapabilities().limits[l]);
}

} // namespace
void registerGraphics(scua_partition *p, Host &state)
{
    registerFunctions(
        p, state,
        {
            {"love_graphics", "get_color", guarded<get_color>, "get_color() -> vec4", "get color."},
            {"love_graphics", "set_background_color", guarded<set_background_color>,
             "set_background_color(color: vec4)", "set background color."},
            {"love_graphics", "get_background_color", guarded<get_background_color>,
             "get_background_color() -> vec4", "get background color."},
            {"love_graphics", "is_active", guarded<is_active>, "is_active() -> bool", "is active."},
            {"love_graphics", "is_created", guarded<is_created>, "is_created() -> bool", "is created."},
            {"love_graphics", "is_wireframe", guarded<is_wireframe>, "is_wireframe() -> bool",
             "is wireframe."},
            {"love_graphics", "line_width", guarded<line_width>, "line_width() -> float", "line width."},
            {"love_graphics", "point_size", guarded<point_size>, "point_size() -> float", "point size."},
            {"love_graphics", "dpi_scale", guarded<dpi_scale>, "dpi_scale() -> float", "dpi scale."},
            {"love_graphics", "set_line_width", guarded<set_line_width>, "set_line_width(size: float)",
             "set line width."},
            {"love_graphics", "set_point_size", guarded<set_point_size>, "set_point_size(size: float)",
             "set point size."},
            {"love_graphics", "set_wireframe", guarded<set_wireframe>, "set_wireframe(enabled: bool)",
             "set wireframe."},
            {"love_graphics", "set_line_style", guarded<set_line_style>, "set_line_style(mode: string)",
             "set line style."},
            {"love_graphics", "set_line_join", guarded<set_line_join>, "set_line_join(mode: string)",
             "set line join."},
            {"love_graphics", "line_style", guarded<line_style>, "line_style() -> string", "line style."},
            {"love_graphics", "line_join", guarded<line_join>, "line_join() -> string", "line join."},
            {"love_graphics", "translate", guarded<translate>, "translate(value: vec2)", "translate."},
            {"love_graphics", "scale", guarded<scale>, "scale(value: vec2)", "scale."},
            {"love_graphics", "shear", guarded<shear>, "shear(value: vec2)", "shear."},
            {"love_graphics", "rotate", guarded<rotate>, "rotate(radians: float)", "rotate."},
            {"love_graphics", "origin", guarded<origin>, "origin()", "origin."},
            {"love_graphics", "pop_state", guarded<pop>, "pop_state()", "pop."},
            {"love_graphics", "reset", guarded<reset>, "reset()", "reset."},
            {"love_graphics", "push_state", guarded<push>, "push_state(mode: string = \"transform\")",
             "push."},
            {"love_graphics", "stack_depth", guarded<stack_depth>, "stack_depth() -> int", "stack depth."},
            {"love_graphics", "transform_point", guarded<transform_point>,
             "transform_point(position: vec2) -> vec2", "transform point."},
            {"love_graphics", "inverse_transform_point", guarded<inverse_transform_point>,
             "inverse_transform_point(position: vec2) -> vec2", "inverse transform point."},
            {"love_graphics", "pixel_dimensions", guarded<pixel_dimensions>, "pixel_dimensions() -> vec2",
             "pixel dimensions."},
            {"love_graphics", "points", guarded<points>, "points(vertices: any)", "points."},
            {"love_graphics", "line", guarded<line>, "line(vertices: any)", "line."},
            {"love_graphics", "polygon", guarded<polygon>, "polygon(mode: string, vertices: any)",
             "polygon."},
            {"love_graphics", "ellipse", guarded<ellipse>,
             "ellipse(mode: string, position: vec2, radii: vec2)", "ellipse."},
            {"love_graphics", "arc", guarded<arc>,
             "arc(mode: string, arc_mode: string, position: vec2, radius: float, first: float, last: float)",
             "arc."},
            {"love_graphics", "rounded_rectangle", guarded<rounded_rectangle>,
             "rounded_rectangle(mode: string, position: vec2, size: vec2, radii: vec2)",
             "rounded rectangle."},
            {"love_graphics", "set_scissor", guarded<set_scissor>, "set_scissor(rectangle: vec4)",
             "set scissor."},
            {"love_graphics", "intersect_scissor", guarded<intersect_scissor>,
             "intersect_scissor(rectangle: vec4)", "intersect scissor."},
            {"love_graphics", "clear_scissor", guarded<clear_scissor>, "clear_scissor()", "clear scissor."},
            {"love_graphics", "scissor", guarded<scissor>, "scissor() -> any", "scissor."},
            {"love_graphics", "set_blend_mode", guarded<set_blend_mode>,
             "set_blend_mode(mode: string, alpha: string = \"alphamultiply\")", "set blend mode."},
            {"love_graphics", "blend_mode", guarded<blend_mode>, "blend_mode() -> any", "blend mode."},
            {"love_graphics", "renderer_info", guarded<renderer_info>, "renderer_info() -> any",
             "renderer info."},
            {"love_graphics", "stats", guarded<stats>, "stats() -> any", "stats."},
            {"love_graphics", "is_supported", guarded<is_supported>, "is_supported(feature: string) -> bool",
             "is supported."},
            {"love_graphics", "system_limit", guarded<system_limit>, "system_limit(name: string) -> float",
             "system limit."},
        });
}
} // namespace lovescua
