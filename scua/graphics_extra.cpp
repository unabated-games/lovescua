// Mesh and batch property APIs surfaced by the compatibility audit.
#include "graphics/Mesh.h"
#include "graphics/SpriteBatch.h"
#include "math/Transform.h"
#include "options.h"
namespace lovescua
{
namespace
{
using namespace binding;
using namespace love::graphics;
auto mesh(scua_ctx *c)
{
    return objectArgument<Mesh>(c, 0, ObjectKind::Mesh);
}
auto batch(scua_ctx *c)
{
    return objectArgument<SpriteBatch>(c, 0, ObjectKind::SpriteBatch);
}
void push(scua_partition *p, scua_Handle a, scua_Handle v)
{
    if (scua_array_push(p, a, v) != SCUA_OK)
        throw std::runtime_error("graphics property allocation failed");
}
void mesh_info(scua_ctx *c)
{
    arity(c, 1);
    auto m = mesh(c);
    if (!m)
        return;
    Record r(c);
    r.integer("count", m->getVertexCount());
    r.integer("stride", m->getVertexStride());
    const char *mode = "unknown";
    getConstant(m->getDrawMode(), mode);
    r.str("mode", mode);
    int start = 0, count = 0;
    if (m->getDrawRange(start, count))
    {
        float xy[] = {float(start), float(count)};
        r.set("range", scua_vec(r.p, xy, 2));
    }
    else
        r.set("range", scua_nil(r.p));
    auto format = scua_new_array(r.p, 0);
    for (auto &f : m->getVertexFormat())
    {
        Record field(c);
        const char *name = "unknown";
        getConstant(f.decl.format, name);
        field.str("name", f.decl.name);
        field.str("format", name);
        field.integer("offset", f.offset);
        field.boolean("enabled", m->isAttributeEnabled(f.decl.name));
        push(r.p, format, field.value);
    }
    r.set("format", format);
    auto attributes = scua_new_array(r.p, 0);
    for (auto &a : m->getAttachedAttributes())
    {
        Record field(c);
        field.str("name", a.name);
        field.str("member", a.nameInBuffer);
        field.integer("start", a.startArrayIndex);
        field.boolean("enabled", a.enabled);
        const char *step = "unknown";
        getConstant(a.step, step);
        field.str("step", step);
        push(r.p, attributes, field.value);
    }
    r.set("attributes", attributes);
    std::vector<uint32_t> map;
    auto indices = scua_new_array(r.p, 0);
    if (m->getVertexMap(map))
    {
        if (map.size() > 1048576)
            throw std::invalid_argument("vertex map exceeds supported size");
        for (auto i : map)
            push(r.p, indices, scua_int(r.p, i));
    }
    r.set("vertex_map", indices);
    r.done();
}
void mesh_configure(scua_ctx *c)
{
    arity(c, 2);
    auto m = mesh(c);
    if (!m)
        return;
    Options o(c, 1);
    const char *current = "unknown";
    getConstant(m->getDrawMode(), current);
    auto name = o.str("mode", current);
    PrimitiveType mode;
    if (!getConstant(name.c_str(), mode))
        throw std::invalid_argument("unknown primitive mode");
    bool clear = o.boolean("clear_range", false);
    int start = 0, count = 0;
    bool has = o.has("range");
    if (has)
    {
        float xy[2];
        if (scua_to_vec(o.p, o.field("range"), xy, 2) != 2 || !std::isfinite(xy[0]) ||
            !std::isfinite(xy[1]) || xy[0] < 0 || xy[1] < 1 || xy[0] != std::floor(xy[0]) ||
            xy[1] != std::floor(xy[1]) || xy[0] > 1048576 || xy[1] > 1048576)
            throw std::invalid_argument("mesh range needs integer start and positive count");
        start = xy[0];
        count = xy[1];
        auto available = m->getIndexBuffer() ? m->getIndexCount() : m->getVertexCount();
        if (size_t(start) + count > available)
            throw std::invalid_argument("draw range exceeds mesh elements");
    }
    m->setDrawMode(mode);
    if (clear)
        m->setDrawRange();
    else if (has)
        m->setDrawRange(start, count);
}
void mesh_texture(scua_ctx *c)
{
    arity(c, 1);
    auto m = mesh(c);
    if (!m)
        return;
    auto t = m->getTexture();
    if (t)
    {
        t->retain();
        returnImage(c, t);
    }
    else
        scua_return_value(c, scua_nil(scua_ctx_partition(c)));
}
void mesh_vertex_bytes(scua_ctx *c)
{
    arity(c, 2);
    auto m = mesh(c);
    if (!m)
        return;
    int index = integer(c, 1, 0, m->getVertexCount() - 1);
    size_t offset;
    auto data = m->checkVertexDataOffset(index, &offset);
    if (!data)
        throw std::invalid_argument("mesh has no CPU-owned vertex data; read its vertex buffer");
    scua_return_bytes(c, static_cast<const uint8_t *>(data), m->getVertexStride());
}
void batch_info(scua_ctx *c)
{
    arity(c, 1);
    auto b = batch(c);
    if (!b)
        return;
    Record r(c);
    r.integer("count", b->getCount());
    r.integer("capacity", b->getBufferSize());
    auto color = b->getColor();
    float rgba[] = {color.r, color.g, color.b, color.a};
    r.set("color", scua_vec(r.p, rgba, 4));
    int start = 0, count = 0;
    if (b->getDrawRange(start, count))
    {
        float range[] = {float(start), float(count)};
        r.set("range", scua_vec(r.p, range, 2));
    }
    else
        r.set("range", scua_nil(r.p));
    r.done();
}
void batch_range(scua_ctx *c)
{
    if (scua_arg_count(c) != 1 && scua_arg_count(c) != 3)
        throw std::invalid_argument("expected batch and optional start/count");
    auto b = batch(c);
    if (!b)
        return;
    if (scua_arg_count(c) == 1)
    {
        b->setDrawRange();
        return;
    }
    int start = integer(c, 1, 0, b->getCount()), count = integer(c, 2, 1, b->getCount());
    if (start + count > b->getCount())
        throw std::invalid_argument("draw range exceeds batch");
    b->setDrawRange(start, count);
}
void batch_texture(scua_ctx *c)
{
    arity(c, 1);
    auto b = batch(c);
    if (b)
    {
        auto t = b->getTexture();
        t->retain();
        returnImage(c, t);
    }
}
void batch_set_texture(scua_ctx *c)
{
    arity(c, 2);
    auto b = batch(c);
    auto t = imageArgument(c, 1);
    if (b && t)
        b->setTexture(t->texture);
}
void batch_layer(scua_ctx *c)
{
    if (scua_arg_count(c) < 3 || scua_arg_count(c) > 6)
        throw std::invalid_argument("expected batch, layer, position and optional rotation, scale, index");
    auto b = batch(c);
    if (!b)
        return;
    if (b->getTexture()->getTextureType() != TEXTURE_2D_ARRAY)
        throw std::invalid_argument("batch layer requires an array texture");
    int layer = integer(c, 1, 0, b->getTexture()->getSliceCount(0) - 1);
    float xy[2], scale[] = {1, 1};
    vector(c, 2, xy, 2);
    float angle = supplied(c, 3) ? number(c, 3) : 0;
    if (supplied(c, 4))
        vector(c, 4, scale, 2);
    int index = supplied(c, 5) ? integer(c, 5, 0, b->getCount() - 1) : -1;
    if (index < 0 && b->getCount() >= 65536)
        throw std::invalid_argument("sprite limit is 65536");
    scua_return_int(
        c, b->addLayer(layer, love::Matrix4(xy[0], xy[1], angle, scale[0], scale[1], 0, 0, 0, 0), index));
}
void batch_attach_attribute(scua_ctx *c)
{
    arity(c, 3);
    auto b = batch(c);
    auto name = string(c, 1);
    if (name.empty() || name.size() > 128)
        throw std::invalid_argument("attribute name needs 1..128 bytes");
    auto buffer = objectArgument<Buffer>(c, 2, ObjectKind::Buffer);
    if (b && buffer)
        b->attachAttribute(name, buffer, nullptr);
}
template <typename T> const char *constant(T value)
{
    const char *name = "unknown";
    getConstant(value, name);
    return name;
}
template <typename T> void optionEnum(const Options &o, const char *name, T &value)
{
    if (o.has(name) && !getConstant(o.str(name, "").c_str(), value))
        throw std::invalid_argument(std::string("invalid ") + name);
}
void pipeline_state(scua_ctx *c)
{
    arity(c, 0);
    auto &g = graphics(c, false);
    Record r(c);
    r.str("cull", constant(g.getMeshCullMode()));
    r.str("winding", constant(g.getFrontFaceWinding()));
    r.boolean("gamma_correct", isGammaCorrect());
    r.boolean("low_power_preferred", isLowPowerPreferred());
    auto mask = g.getColorMask();
    float color[] = {float(mask.r), float(mask.g), float(mask.b), float(mask.a)};
    r.set("color_mask", scua_vec(r.p, color, 4));
    auto b = g.getBlendState();
    Record blend(c);
    blend.boolean("enabled", b.enable);
    blend.str("rgb_operation", constant(b.operationRGB));
    blend.str("alpha_operation", constant(b.operationA));
    blend.str("src_rgb", constant(b.srcFactorRGB));
    blend.str("src_alpha", constant(b.srcFactorA));
    blend.str("dst_rgb", constant(b.dstFactorRGB));
    blend.str("dst_alpha", constant(b.dstFactorA));
    r.set("blend", blend.value);
    auto s = g.getStencilState();
    Record stencil(c);
    stencil.str("action", constant(s.action));
    stencil.str("compare", constant(s.compare));
    stencil.integer("value", s.value);
    stencil.integer("read_mask", s.readMask);
    stencil.integer("write_mask", s.writeMask);
    r.set("stencil", stencil.value);
    r.done();
}
void configure_pipeline(scua_ctx *c)
{
    arity(c, 1);
    auto &g = graphics(c, false);
    Options o(c, 0);
    auto cull = g.getMeshCullMode();
    auto winding = g.getFrontFaceWinding();
    auto blend = g.getBlendState();
    optionEnum(o, "cull", cull);
    optionEnum(o, "winding", winding);
    if (o.has("blend"))
    {
        Options b(o.p, o.field("blend"));
        blend.enable = b.boolean("enabled", blend.enable);
        optionEnum(b, "rgb_operation", blend.operationRGB);
        optionEnum(b, "alpha_operation", blend.operationA);
        optionEnum(b, "src_rgb", blend.srcFactorRGB);
        optionEnum(b, "src_alpha", blend.srcFactorA);
        optionEnum(b, "dst_rgb", blend.dstFactorRGB);
        optionEnum(b, "dst_alpha", blend.dstFactorA);
    }
    g.setBlendState(blend);
    g.setMeshCullMode(cull);
    g.setFrontFaceWinding(winding);
}
void default_sampler(scua_ctx *c)
{
    arity(c, 0);
    auto s = graphics(c, false).getDefaultSamplerState();
    Record r(c);
    const char *n = "unknown";
    SamplerState::getConstant(s.minFilter, n);
    r.str("min_filter", n);
    SamplerState::getConstant(s.magFilter, n);
    r.str("mag_filter", n);
    SamplerState::getConstant(s.mipmapFilter, n);
    r.str("mipmap_filter", n);
    r.integer("anisotropy", s.maxAnisotropy);
    r.num("lod_bias", s.lodBias);
    r.done();
}
void set_default_sampler(scua_ctx *c)
{
    arity(c, 1);
    auto &g = graphics(c, false);
    auto s = g.getDefaultSamplerState();
    Options o(c, 0);
    auto filter = [&](const char *key, auto &out)
    {
        if (o.has(key) && !SamplerState::getConstant(o.str(key, "").c_str(), out))
            throw std::invalid_argument(std::string("invalid ") + key);
    };
    filter("min_filter", s.minFilter);
    filter("mag_filter", s.magFilter);
    filter("mipmap_filter", s.mipmapFilter);
    s.maxAnisotropy = o.integer("anisotropy", s.maxAnisotropy, 1, 255);
    s.lodBias = o.num("lod_bias", s.lodBias);
    g.setDefaultSamplerState(s);
}
void current_shader(scua_ctx *c)
{
    arity(c, 0);
    auto s = graphics(c, false).getShader();
    if (s)
    {
        s->retain();
        returnObject(c, ObjectKind::Shader, s);
    }
    else
        scua_return_value(c, scua_nil(scua_ctx_partition(c)));
}
void shader_debug_name(scua_ctx *c)
{
    arity(c, 1);
    auto s = objectArgument<Shader>(c, 0, ObjectKind::Shader);
    if (s)
        returnString(c, s->getDebugName());
}
void flush(scua_ctx *c)
{
    arity(c, 0);
    graphics(c).flushBatchedDraws();
}
void set_projection(scua_ctx *c)
{
    arity(c, 1);
    auto t = objectArgument<love::math::Transform>(c, 0, ObjectKind::Transform);
    if (t)
        graphics(c).setProjection(t->getMatrix());
}
void reset_projection(scua_ctx *c)
{
    arity(c, 0);
    graphics(c).resetProjection();
}
void discard(scua_ctx *c)
{
    arity(c, 2);
    auto p = scua_ctx_partition(c);
    int kind;
    size_t stride, count;
    if (scua_buffer_info(p, scua_arg(c, 0), &kind, &stride, &count) != SCUA_OK || count > 8)
        throw std::invalid_argument("discard needs up to 8 color flags");
    std::vector<bool> flags;
    for (size_t i = 0; i < count; ++i)
    {
        auto v = scua_index(p, scua_arg(c, 0), i);
        if (scua_typeof(p, v) != SCUA_TYPE_BOOL)
            throw std::invalid_argument("discard flags must be bool");
        flags.push_back(scua_to_bool(p, v));
    }
    bool depth = boolean(c, 1);
    graphics(c).discard(flags, depth);
}
void render_target(scua_ctx *c)
{
    arity(c, 1);
    int i = integer(c, 0, -1, 7);
    auto targets = graphics(c, false).getRenderTargets();
    auto t = i < 0 ? targets.depthStencil
                   : (size_t(i) < targets.colors.size() ? targets.colors[i] : Graphics::RenderTarget{});
    if (!t.texture)
    {
        scua_return_value(c, scua_nil(scua_ctx_partition(c)));
        return;
    }
    t.texture->retain();
    returnImage(c, t.texture);
}

} // namespace
void registerGraphicsExtras(scua_partition *p, Host &s)
{
    registerFunctions(
        p, s,
        {{"love_graphics", "batch_attach_attribute", guarded<batch_attach_attribute>,
          "batch_attach_attribute(batch: any, name: string, buffer: any)",
          "Attach a retained vertex buffer member; supply at least four vertices per sprite."},
         {"love_graphics", "pipeline_state", guarded<pipeline_state>, "pipeline_state() -> any",
          "Get blend/stencil/color mask, cull/winding and renderer policy."},
         {"love_graphics", "configure_pipeline", guarded<configure_pipeline>,
          "configure_pipeline(settings: any)", "Set checked cull/winding and custom blend state records."},
         {"love_graphics", "default_sampler", guarded<default_sampler>, "default_sampler() -> any",
          "Get default texture min/mag/mipmap filters, anisotropy and LOD bias."},
         {"love_graphics", "set_default_sampler", guarded<set_default_sampler>,
          "set_default_sampler(settings: any)", "Set default texture filtering for newly created textures."},
         {"love_graphics", "current_shader", guarded<current_shader>, "current_shader() -> any",
          "Get a retained custom shader, or nil for the engine default."},
         {"love_graphics", "shader_debug_name", guarded<shader_debug_name>,
          "shader_debug_name(shader: any) -> string", "Get shader debug label."},
         {"love_graphics", "flush", guarded<flush>, "flush()", "Flush batched drawing during draw()."},
         {"love_graphics", "set_projection", guarded<set_projection>, "set_projection(transform: any)",
          "Set a custom projection matrix during draw()."},
         {"love_graphics", "reset_projection", guarded<reset_projection>, "reset_projection()",
          "Restore the engine projection during draw()."},
         {"love_graphics", "discard", guarded<discard>, "discard(colors: any, depth_stencil: bool)",
          "Discard selected render attachments during draw(); their contents become undefined."},
         {"love_graphics", "render_target", guarded<render_target>, "render_target(index: int) -> any",
          "Get a retained active color target, -1 for depth/stencil, or nil. Pass index explicitly."},
         {"love_graphics", "mesh_info", guarded<mesh_info>, "mesh_info(mesh: any) -> any",
          "Get vertex format, enabled/attached attributes, draw mode/range and vertex map."},
         {"love_graphics", "mesh_configure", guarded<mesh_configure>,
          "mesh_configure(mesh: any, settings: any)",
          "Set a checked draw mode/range; clear_range resets the range."},
         {"love_graphics", "mesh_texture", guarded<mesh_texture>, "mesh_texture(mesh: any) -> any",
          "Get a retained texture or nil."},
         {"love_graphics", "mesh_vertex_bytes", guarded<mesh_vertex_bytes>,
          "mesh_vertex_bytes(mesh: any, index: int) -> bytes",
          "Get one CPU-owned vertex in its documented native format."},
         {"love_graphics", "batch_info", guarded<batch_info>, "batch_info(batch: any) -> any",
          "Get count/capacity, current tint and draw range."},
         {"love_graphics", "batch_set_range", guarded<batch_range>,
          "batch_set_range(batch: any, start: int = nil, count: int = nil)",
          "Set a zero-based draw range, or reset it by omitting both bounds."},
         {"love_graphics", "batch_texture", guarded<batch_texture>, "batch_texture(batch: any) -> any",
          "Get a retained batch texture."},
         {"love_graphics", "batch_set_texture", guarded<batch_set_texture>,
          "batch_set_texture(batch: any, texture: any)", "Replace the retained batch texture."},
         {"love_graphics", "batch_layer", guarded<batch_layer>,
          "batch_layer(batch: any, layer: int, position: vec2, rotation: float = 0, scale: "
          "vec2 = nil, index: int = nil) -> int",
          "Append or replace an array-texture sprite."}});
}
} // namespace lovescua
