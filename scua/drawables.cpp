// Managed meshes, cached text, texture mutation and readback.
#include "binding_util.h"
#include "graphics/Mesh.h"
#include "graphics/TextBatch.h"
#include "image/ImageData.h"
namespace lovescua
{
namespace
{
using namespace binding;
auto mesh(scua_ctx *ctx)
{
    return objectArgument<love::graphics::Mesh>(ctx, 0, ObjectKind::Mesh);
}
auto text(scua_ctx *ctx)
{
    return objectArgument<love::graphics::TextBatch>(ctx, 0, ObjectKind::Text);
}
love::graphics::Vertex vertex(scua_ctx *ctx, scua_Handle value)
{
    auto p = scua_ctx_partition(ctx);
    float position[2], uv[2], color[4];
    if (scua_to_vec(p, scua_field(p, value, "position"), position, 2) != 2 ||
        scua_to_vec(p, scua_field(p, value, "uv"), uv, 2) != 2 ||
        scua_to_vec(p, scua_field(p, value, "color"), color, 4) != 4)
        throw std::invalid_argument("mesh vertices need position:vec2, uv:vec2 and color:vec4");
    for (auto v : position)
        if (!std::isfinite(v))
            throw std::invalid_argument("nonfinite vertex position");
    for (auto v : uv)
        if (!std::isfinite(v))
            throw std::invalid_argument("nonfinite texture coordinate");
    for (auto v : color)
        if (!std::isfinite(v))
            throw std::invalid_argument("nonfinite vertex color");
    return {position[0],
            position[1],
            uv[0],
            uv[1],
            {static_cast<uint8_t>(std::clamp(color[0], 0.f, 1.f) * 255),
             static_cast<uint8_t>(std::clamp(color[1], 0.f, 1.f) * 255),
             static_cast<uint8_t>(std::clamp(color[2], 0.f, 1.f) * 255),
             static_cast<uint8_t>(std::clamp(color[3], 0.f, 1.f) * 255)}};
}
void new_mesh(scua_ctx *ctx)
{
    arity(ctx, 2);
    auto name = string(ctx, 1);
    love::graphics::PrimitiveType mode;
    if (!love::graphics::getConstant(name.c_str(), mode))
        throw std::invalid_argument("invalid mesh draw mode");
    auto p = scua_ctx_partition(ctx);
    auto array = scua_arg(ctx, 0);
    if (scua_typeof(p, array) != SCUA_TYPE_ARRAY)
        throw std::invalid_argument("expected a vertex array");
    std::vector<love::graphics::Vertex> vertices;
    for (int i = 0; i <= 65536; ++i)
    {
        auto value = scua_index(p, array, i);
        if (!scua_handle_valid(p, value))
            break;
        vertices.push_back(vertex(ctx, value));
    }
    if (vertices.empty() || vertices.size() > 65536)
        throw std::invalid_argument("mesh vertex count must be 1..65536");
    returnObject(ctx, ObjectKind::Mesh,
                 graphics(ctx, false)
                     .newMesh(love::graphics::Mesh::getDefaultVertexFormat(), vertices.data(),
                              vertices.size() * sizeof(vertices[0]), mode,
                              love::graphics::BUFFERDATAUSAGE_DYNAMIC));
}
void mesh_count(scua_ctx *ctx)
{
    arity(ctx, 1);
    auto m = mesh(ctx);
    if (m)
        scua_return_int(ctx, m->getVertexCount());
}
void mesh_set_vertex(scua_ctx *ctx)
{
    arity(ctx, 3);
    auto v = vertex(ctx, scua_arg(ctx, 2));
    auto m = mesh(ctx);
    if (m)
    {
        size_t index = integer(ctx, 1, 0, m->getVertexCount() - 1), offset = 0;
        auto dest = m->checkVertexDataOffset(index, &offset);
        std::memcpy(dest, &v, sizeof(v));
        m->setVertexDataModified(offset, sizeof(v));
    }
}
void mesh_set_texture(scua_ctx *ctx)
{
    arity(ctx, 2);
    auto m = mesh(ctx);
    if (!m)
        return;
    if (!supplied(ctx, 1))
    {
        m->setTexture();
        return;
    }
    auto t = imageArgument(ctx, 1);
    if (t)
        m->setTexture(t->texture);
}
void mesh_set_vertex_map(scua_ctx *ctx)
{
    arity(ctx, 2);
    auto m = mesh(ctx);
    if (!m)
        return;
    if (!supplied(ctx, 1))
    {
        m->setVertexMap();
        return;
    }
    auto p = scua_ctx_partition(ctx);
    auto array = scua_arg(ctx, 1);
    if (scua_typeof(p, array) != SCUA_TYPE_ARRAY)
        throw std::invalid_argument("expected an index array");
    std::vector<uint32_t> indices;
    for (int i = 0; i <= 196608; ++i)
    {
        auto value = scua_index(p, array, i);
        if (!scua_handle_valid(p, value))
            break;
        auto n = scua_to_num(p, value);
        if (scua_typeof(p, value) != SCUA_TYPE_INT || n < 0 || n >= m->getVertexCount())
            throw std::invalid_argument("mesh index must be a valid zero-based vertex");
        indices.push_back(n);
    }
    if (indices.size() > 196608)
        throw std::invalid_argument("mesh index limit is 196608");
    m->setVertexMap(indices);
}
void mesh_flush(scua_ctx *ctx)
{
    arity(ctx, 1);
    auto m = mesh(ctx);
    if (m)
        m->flush();
}
void draw_mesh(scua_ctx *ctx)
{
    auto matrix = transform(ctx);
    auto m = mesh(ctx);
    if (m)
        graphics(ctx).draw(m, matrix);
}
void new_text(scua_ctx *ctx)
{
    arity(ctx, 2);
    auto value = string(ctx, 1);
    auto f = objectArgument<love::graphics::Font>(ctx, 0, ObjectKind::Font);
    if (f)
        returnObject(ctx, ObjectKind::Text, graphics(ctx, false).newTextBatch(f, {{value, {1, 1, 1, 1}}}));
}
void text_set(scua_ctx *ctx)
{
    arity(ctx, 2);
    auto value = string(ctx, 1);
    auto t = text(ctx);
    if (t)
        t->set({{value, {1, 1, 1, 1}}});
}
void text_set_wrapped(scua_ctx *ctx)
{
    arity(ctx, 4);
    auto value = string(ctx, 1), alignment = string(ctx, 3);
    auto width = number(ctx, 2);
    love::graphics::Font::AlignMode align;
    if (width <= 0 || !love::graphics::Font::getConstant(alignment.c_str(), align))
        throw std::invalid_argument("invalid text width or alignment");
    auto t = text(ctx);
    if (t)
        t->set({{value, {1, 1, 1, 1}}}, width, align);
}
void text_add(scua_ctx *ctx)
{
    arity(ctx, 3);
    auto value = string(ctx, 1);
    float pos[2];
    vector(ctx, 2, pos, 2);
    auto t = text(ctx);
    if (t)
        scua_return_int(ctx,
                        t->add({{value, {1, 1, 1, 1}}}, love::Matrix4(pos[0], pos[1], 0, 1, 1, 0, 0, 0, 0)));
}
void text_dimensions(scua_ctx *ctx)
{
    arity(ctx, 1);
    auto t = text(ctx);
    if (t)
        returnVec(ctx, t->getWidth(), t->getHeight());
}
void text_clear(scua_ctx *ctx)
{
    arity(ctx, 1);
    auto t = text(ctx);
    if (t)
        t->clear();
}
void draw_text(scua_ctx *ctx)
{
    auto matrix = transform(ctx);
    auto t = text(ctx);
    if (t)
        graphics(ctx).draw(t, matrix);
}
void texture_replace_pixels(scua_ctx *ctx)
{
    arity(ctx, 4);
    auto t = imageArgument(ctx, 0);
    auto d = objectArgument<love::image::ImageData>(ctx, 1, ObjectKind::ImageData);
    if (t && d)
    {
        int x = integer(ctx, 2, 0, t->texture->getPixelWidth() - 1),
            y = integer(ctx, 3, 0, t->texture->getPixelHeight() - 1);
        t->texture->replacePixels(d, 0, 0, x, y, false);
    }
}
void texture_readback(scua_ctx *ctx)
{
    arity(ctx, 1);
    auto t = imageArgument(ctx, 0);
    if (t)
    {
        love::Rect rect{0, 0, t->texture->getPixelWidth(), t->texture->getPixelHeight()};
        returnObject(ctx, ObjectKind::ImageData,
                     graphics(ctx, false).readbackTexture(t->texture, 0, 0, rect, nullptr, 0, 0));
    }
}
void texture_set_wrap(scua_ctx *ctx)
{
    arity(ctx, 3);
    auto u = string(ctx, 1), v = string(ctx, 2);
    love::graphics::SamplerState::WrapMode um, vm;
    if (!love::graphics::SamplerState::getConstant(u.c_str(), um) ||
        !love::graphics::SamplerState::getConstant(v.c_str(), vm))
        throw std::invalid_argument("invalid texture wrap mode");
    auto t = imageArgument(ctx, 0);
    if (t)
    {
        auto state = t->texture->getSamplerState();
        state.wrapU = um;
        state.wrapV = vm;
        t->texture->setSamplerState(state);
    }
}
void quad_info(scua_ctx *ctx)
{
    arity(ctx, 1);
    auto q = quadArgument(ctx, 0);
    if (!q)
        return;
    auto v = q->quad->getViewport();
    Record r(ctx);
    float position[] = {float(v.x), float(v.y)}, size[] = {float(v.w), float(v.h)};
    float texture[] = {float(q->quad->getTextureWidth()), float(q->quad->getTextureHeight())};
    r.set("position", scua_vec(r.p, position, 2));
    r.set("size", scua_vec(r.p, size, 2));
    r.set("texture_dimensions", scua_vec(r.p, texture, 2));
    r.integer("layer", q->quad->getLayer());
    r.done();
}
void quad_set_layer(scua_ctx *ctx)
{
    arity(ctx, 2);
    auto q = quadArgument(ctx, 0);
    if (!q)
        return;
    int layer = integer(ctx, 1, 0, q->texture->getLayerCount() - 1);
    q->quad->setLayer(layer);
}
void quad_set_viewport(scua_ctx *ctx)
{
    arity(ctx, 3);
    float pos[2], size[2];
    vector(ctx, 1, pos, 2);
    vector(ctx, 2, size, 2);
    auto q = quadArgument(ctx, 0);
    if (q)
    {
        if (std::abs(pos[0]) > 1e6 || std::abs(pos[1]) > 1e6 || size[0] <= 0 || size[1] <= 0 ||
            size[0] > 1e6 || size[1] > 1e6)
            throw std::invalid_argument("quad position/size exceeds supported range");
        q->quad->setViewport({pos[0], pos[1], size[0], size[1]});
    }
}
} // namespace
void registerDrawables(scua_partition *p, Host &state)
{
    registerObjectType(p, state, ObjectKind::Mesh);
    registerObjectType(p, state, ObjectKind::Text);
    registerFunctions(
        p, state,
        {
            {"love_graphics", "new_mesh", guarded<new_mesh>, "new_mesh(vertices: any, mode: string) -> any",
             "new mesh."},
            {"love_graphics", "mesh_count", guarded<mesh_count>, "mesh_count(mesh: any) -> int",
             "mesh count."},
            {"love_graphics", "mesh_set_vertex", guarded<mesh_set_vertex>,
             "mesh_set_vertex(mesh: any, index: int, vertex: any)", "mesh set vertex."},
            {"love_graphics", "mesh_set_texture", guarded<mesh_set_texture>,
             "mesh_set_texture(mesh: any, texture: any)", "mesh set texture."},
            {"love_graphics", "mesh_set_vertex_map", guarded<mesh_set_vertex_map>,
             "mesh_set_vertex_map(mesh: any, indices: any)", "mesh set vertex map."},
            {"love_graphics", "mesh_flush", guarded<mesh_flush>, "mesh_flush(mesh: any)", "mesh flush."},
            {"love_graphics", "draw_mesh", guarded<draw_mesh>,
             "draw_mesh(mesh: any, position: vec2, rotation: float = 0, scale: vec2 = nil, origin: vec2 = "
             "nil)",
             "draw mesh."},
            {"love_graphics", "new_text", guarded<new_text>, "new_text(font: any, text: string) -> any",
             "new text."},
            {"love_graphics", "text_set", guarded<text_set>, "text_set(text: any, content: string)",
             "text set."},
            {"love_graphics", "text_set_wrapped", guarded<text_set_wrapped>,
             "text_set_wrapped(text: any, content: string, width: float, alignment: string)",
             "text set wrapped."},
            {"love_graphics", "text_add", guarded<text_add>,
             "text_add(text: any, content: string, position: vec2) -> int", "text add."},
            {"love_graphics", "text_dimensions", guarded<text_dimensions>,
             "text_dimensions(text: any) -> vec2", "text dimensions."},
            {"love_graphics", "text_clear", guarded<text_clear>, "text_clear(text: any)", "text clear."},
            {"love_graphics", "draw_text", guarded<draw_text>,
             "draw_text(text: any, position: vec2, rotation: float = 0, scale: vec2 = nil, origin: vec2 = "
             "nil)",
             "draw text."},
            {"love_graphics", "texture_replace_pixels", guarded<texture_replace_pixels>,
             "texture_replace_pixels(texture: any, pixels: any, x: int, y: int)", "texture replace pixels."},
            {"love_graphics", "texture_readback", guarded<texture_readback>,
             "texture_readback(texture: any) -> any", "texture readback."},
            {"love_graphics", "texture_set_wrap", guarded<texture_set_wrap>,
             "texture_set_wrap(texture: any, horizontal: string, vertical: string)", "texture set wrap."},
            {"love_graphics", "quad_info", guarded<quad_info>, "quad_info(quad: any) -> any",
             "Get viewport position/size, source dimensions and zero-based layer."},
            {"love_graphics", "quad_set_layer", guarded<quad_set_layer>,
             "quad_set_layer(quad: any, layer: int)",
             "Select a zero-based source array layer for draw_quad."},
            {"love_graphics", "quad_set_viewport", guarded<quad_set_viewport>,
             "quad_set_viewport(quad: any, position: vec2, size: vec2)", "quad set viewport."},
        });
}
} // namespace lovescua
