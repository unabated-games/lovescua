// Complete particle configuration via records, plus retained texture/quad access.
#include "graphics/ParticleSystem.h"
#include "options.h"
#include <functional>
namespace lovescua
{
namespace
{
using namespace binding;
using Particle = love::graphics::ParticleSystem;
auto particles(scua_ctx *ctx)
{
    return objectArgument<Particle>(ctx, 0, ObjectKind::Particles);
}
void vec(Record &r, const char *name, float x, float y)
{
    float v[] = {x, y};
    r.set(name, scua_vec(r.p, v, 2));
}
void range(Record &r, const char *name, const Particle *p, void (Particle::*get)(float &, float &) const)
{
    float x, y;
    (p->*get)(x, y);
    vec(r, name, x, y);
}
love::Vector2 vectorField(const Options &o, const char *name, love::Vector2 fallback = {}, float limit = 1e6)
{
    if (!o.has(name))
        return fallback;
    float v[2];
    if (scua_to_vec(o.p, o.field(name), v, 2) != 2)
        throw std::invalid_argument(std::string(name) + " must be vec2");
    for (float n : v)
        if (!std::isfinite(n) || std::abs(n) > limit)
            throw std::invalid_argument(std::string(name) + " is outside the finite supported range");
    return {v[0], v[1]};
}
void append(scua_partition *p, scua_Handle array, scua_Handle value)
{
    if (scua_array_push(p, array, value) != SCUA_OK)
        throw std::runtime_error("could not build particle result");
}
void particles_state(scua_ctx *ctx)
{
    arity(ctx, 1);
    auto p = particles(ctx);
    if (!p)
        return;
    Record r(ctx);
    r.integer("capacity", p->getBufferSize());
    r.integer("count", p->getCount());
    r.boolean("active", p->isActive());
    r.boolean("paused", p->isPaused());
    r.boolean("stopped", p->isStopped());
    r.boolean("empty", p->isEmpty());
    r.boolean("full", p->isFull());
    r.num("rate", p->getEmissionRate());
    r.num("emitter_lifetime", p->getEmitterLifetime());
    range(r, "lifetime", p, &Particle::getParticleLifetime);
    auto position = p->getPosition();
    vec(r, "position", position.x, position.y);
    auto offset = p->getOffset();
    vec(r, "offset", offset.x, offset.y);
    r.num("direction", p->getDirection());
    r.num("spread", p->getSpread());
    range(r, "speed", p, &Particle::getSpeed);
    range(r, "rotation", p, &Particle::getRotation);
    range(r, "spin", p, &Particle::getSpin);
    range(r, "damping", p, &Particle::getLinearDamping);
    range(r, "radial_acceleration", p, &Particle::getRadialAcceleration);
    range(r, "tangential_acceleration", p, &Particle::getTangentialAcceleration);
    love::Vector2 lo, hi;
    p->getLinearAcceleration(lo, hi);
    vec(r, "acceleration_min", lo.x, lo.y);
    vec(r, "acceleration_max", hi.x, hi.y);
    r.num("size_variation", p->getSizeVariation());
    r.num("spin_variation", p->getSpinVariation());
    r.boolean("relative_rotation", p->hasRelativeRotation());
    const char *name = "unknown";
    Particle::getConstant(p->getInsertMode(), name);
    r.str("insert_mode", name);
    Record area(ctx);
    love::Vector2 size;
    float angle;
    bool relative;
    auto distribution = p->getEmissionArea(size, angle, relative);
    Particle::getConstant(distribution, name);
    area.str("distribution", name);
    vec(area, "size", size.x, size.y);
    area.num("angle", angle);
    area.boolean("relative", relative);
    r.set("area", area.value);
    auto sizes = scua_new_array(r.p, 0);
    for (float n : p->getSizes())
        append(r.p, sizes, scua_num(r.p, n));
    r.set("sizes", sizes);
    auto colors = scua_new_array(r.p, 0);
    for (auto c : p->getColor())
    {
        float v[] = {c.r, c.g, c.b, c.a};
        append(r.p, colors, scua_vec(r.p, v, 4));
    }
    r.set("colors", colors);
    r.integer("quads", p->getQuads().size());
    r.done();
}
void particles_configure(scua_ctx *ctx)
{
    arity(ctx, 2);
    auto p = particles(ctx);
    if (!p)
        return;
    Options o(ctx, 1);
    std::vector<std::function<void()>> apply;
    if (o.has("capacity"))
    {
        auto n = o.integer("capacity", 1, 1, 65536);
        apply.push_back([=] { p->setBufferSize(n); });
    }
    auto scalar = [&](const char *name, void (Particle::*set)(float), float minimum, float maximum)
    {
        if (!o.has(name))
            return;
        float n = o.num(name, 0);
        if (n < minimum || n > maximum)
            throw std::invalid_argument(std::string("invalid particle ") + name);
        apply.push_back([=] { (p->*set)(n); });
    };
    scalar("rate", &Particle::setEmissionRate, 0, 65536);
    scalar("direction", &Particle::setDirection, -1e6, 1e6);
    scalar("spread", &Particle::setSpread, 0, 1e6);
    scalar("size_variation", &Particle::setSizeVariation, 0, 1);
    scalar("spin_variation", &Particle::setSpinVariation, 0, 1);
    if (o.has("emitter_lifetime"))
    {
        float n = o.num("emitter_lifetime", -1);
        if (n != -1 && (n < 0 || n > 3600))
            throw std::invalid_argument("emitter lifetime must be -1 or in [0,3600]");
        apply.push_back([=] { p->setEmitterLifetime(n); });
    }
    auto pair = [&](const char *name, void (Particle::*set)(float, float), float minimum, bool ordered = true)
    {
        if (!o.has(name))
            return;
        auto v = vectorField(o, name);
        if (v.x < minimum || v.y < minimum || (ordered && v.x > v.y))
            throw std::invalid_argument(std::string("invalid particle range ") + name);
        apply.push_back([=] { (p->*set)(v.x, v.y); });
    };
    pair("lifetime", &Particle::setParticleLifetime, 0.000001f);
    pair("speed", static_cast<void (Particle::*)(float, float)>(&Particle::setSpeed), -1e6);
    pair("rotation", static_cast<void (Particle::*)(float, float)>(&Particle::setRotation), -1e6);
    pair("spin", static_cast<void (Particle::*)(float, float)>(&Particle::setSpin), -1e6, false);
    pair("damping", &Particle::setLinearDamping, 0);
    pair("radial_acceleration",
         static_cast<void (Particle::*)(float, float)>(&Particle::setRadialAcceleration), -1e6);
    pair("tangential_acceleration",
         static_cast<void (Particle::*)(float, float)>(&Particle::setTangentialAcceleration), -1e6);
    if (o.has("position"))
    {
        auto v = vectorField(o, "position");
        apply.push_back([=] { p->setPosition(v.x, v.y); });
    }
    if (o.has("offset"))
    {
        auto v = vectorField(o, "offset");
        apply.push_back([=] { p->setOffset(v.x, v.y); });
    }
    if (o.has("acceleration_min") || o.has("acceleration_max"))
    {
        love::Vector2 a, b;
        p->getLinearAcceleration(a, b);
        auto low = vectorField(o, "acceleration_min", a);
        auto high = vectorField(o, "acceleration_max", b);
        if (low.x > high.x || low.y > high.y)
            throw std::invalid_argument("invalid acceleration range");
        apply.push_back([=] { p->setLinearAcceleration(low.x, low.y, high.x, high.y); });
    }
    if (o.has("area"))
    {
        Options a(o.p, o.field("area"));
        Particle::AreaSpreadDistribution mode;
        auto n = a.str("distribution", "none");
        if (!Particle::getConstant(n.c_str(), mode))
            throw std::invalid_argument("invalid particle distribution");
        auto size = vectorField(a, "size");
        if (size.x < 0 || size.y < 0)
            throw std::invalid_argument("area size must be nonnegative");
        float angle = a.num("angle", 0);
        bool relative = a.boolean("relative", false);
        apply.push_back([=] { p->setEmissionArea(mode, size.x, size.y, angle, relative); });
    }
    if (o.has("insert_mode"))
    {
        auto n = o.str("insert_mode", "top");
        Particle::InsertMode mode;
        if (!Particle::getConstant(n.c_str(), mode))
            throw std::invalid_argument("invalid particle insertion mode");
        apply.push_back([=] { p->setInsertMode(mode); });
    }
    if (o.has("relative_rotation"))
    {
        bool n = o.boolean("relative_rotation", false);
        apply.push_back([=] { p->setRelativeRotation(n); });
    }
    if (o.has("sizes"))
    {
        auto array = o.field("sizes");
        if (scua_typeof(o.p, array) != SCUA_TYPE_ARRAY)
            throw std::invalid_argument("sizes must be an array");
        std::vector<float> sizes;
        for (int i = 0; i <= 8; i++)
        {
            auto v = scua_index(o.p, array, i);
            if (!scua_handle_valid(o.p, v))
                break;
            auto type = scua_typeof(o.p, v);
            double n = scua_to_num(o.p, v);
            if (i == 8 || (type != SCUA_TYPE_INT && type != SCUA_TYPE_FLOAT) || !std::isfinite(n) || n < 0 ||
                n > 10000)
                throw std::invalid_argument("particle sizes require 1..8 finite values in [0,10000]");
            sizes.push_back(n);
        }
        if (sizes.empty())
            throw std::invalid_argument("particle sizes must not be empty");
        apply.push_back([=] { p->setSizes(sizes); });
    }
    if (o.has("colors"))
    {
        auto array = o.field("colors");
        if (scua_typeof(o.p, array) != SCUA_TYPE_ARRAY)
            throw std::invalid_argument("colors must be an array");
        std::vector<love::Colorf> colors;
        for (int i = 0; i <= 8; i++)
        {
            auto v = scua_index(o.p, array, i);
            if (!scua_handle_valid(o.p, v))
                break;
            float c[4];
            if (i == 8 || scua_to_vec(o.p, v, c, 4) != 4)
                throw std::invalid_argument("particle colors require 1..8 vec4 values");
            for (float n : c)
                if (!std::isfinite(n) || n < 0 || n > 1)
                    throw std::invalid_argument("particle colors must be in [0,1]");
            colors.emplace_back(c[0], c[1], c[2], c[3]);
        }
        if (colors.empty())
            throw std::invalid_argument("particle colors must not be empty");
        apply.push_back([=] { p->setColor(colors); });
    }
    for (auto &set : apply)
        set();
}
void particles_set_texture(scua_ctx *ctx)
{
    arity(ctx, 2);
    auto p = particles(ctx);
    auto t = imageArgument(ctx, 1);
    if (!p || !t)
        return;
    if (t->texture->getTextureType() != love::graphics::TEXTURE_2D)
        throw std::invalid_argument("particles require a 2d texture");
    p->setTexture(t->texture);
}
void particles_texture(scua_ctx *ctx)
{
    arity(ctx, 1);
    auto p = particles(ctx);
    if (p)
    {
        auto t = p->getTexture();
        t->retain();
        returnImage(ctx, t);
    }
}
void particles_set_quads(scua_ctx *ctx)
{
    arity(ctx, 2);
    auto p = particles(ctx);
    if (!p)
        return;
    auto part = scua_ctx_partition(ctx);
    auto array = scua_arg(ctx, 1);
    if (scua_typeof(part, array) != SCUA_TYPE_ARRAY)
        throw std::invalid_argument("particle quads must be an array");
    std::vector<love::StrongRef<love::graphics::Quad>> refs;
    std::vector<love::graphics::Quad *> quads;
    for (int i = 0; i <= 64; i++)
    {
        auto v = scua_index(part, array, i);
        if (!scua_handle_valid(part, v))
            break;
        if (i == 64)
            throw std::invalid_argument("maximum 64 particle quads");
        void *storage = nullptr;
        scua_UserdataLease lease{};
        if (scua_userdata_borrow(part, v, host(ctx).quadType, &storage, &lease) != SCUA_OK)
            throw std::invalid_argument("particle quads must be live managed quads");
        auto q = static_cast<QuadResource *>(storage);
        bool compatible = q->texture == p->getTexture();
        refs.emplace_back(q->quad);
        if (scua_userdata_release(part, lease) != SCUA_OK)
            throw std::runtime_error("could not release quad lease");
        if (!compatible)
            throw std::invalid_argument("particle quads must use the emitter texture");
        quads.push_back(refs.back().get());
    }
    // The vector overload also restores the automatic full-texture offset when empty.
    p->setQuads(quads);
}
struct Root
{
    scua_partition *p;
    scua_Handle value{};
    ~Root()
    {
        if (scua_handle_valid(p, value))
            scua_unpin_global(p, value);
    }
};
void particles_quads(scua_ctx *ctx)
{
    arity(ctx, 1);
    auto p = particles(ctx);
    if (!p)
        return;
    auto &h = host(ctx);
    auto part = scua_ctx_partition(ctx);
    auto array = scua_new_array(part, 0);
    auto quads = p->getQuads();
    if (!quads.empty())
    {
        Root provider{part};
        auto texture = p->getTexture();
        if (scua_userdata_new(part, h.imageType, sizeof(ImageResource), texture, &provider.value) != SCUA_OK)
            throw std::runtime_error("could not retain quad texture");
        ++h.liveImages;
        ++h.imagesCreated;
        for (auto quad : quads)
        {
            QuadResource resource{texture, quad};
            Root item{part};
            if (scua_userdata_new_with_dependencies(part, h.quadType, sizeof(QuadResource), &resource,
                                                    &provider.value, 1, &item.value) != SCUA_OK)
                throw std::runtime_error("could not retain particle quad");
            ++h.liveQuads;
            ++h.quadsCreated;
            append(part, array, item.value);
        }
    }
    scua_return_value(ctx, array);
}
} // namespace
void registerParticleSettings(scua_partition *p, Host &state)
{
    registerFunctions(p, state,
                      {
                          {"love_graphics", "particles_configure", guarded<particles_configure>,
                           "particles_configure(particles: any, settings: any)",
                           "Apply checked particle ranges, area, colors/sizes, spin variation, capacity and "
                           "emitter settings from a record."},
                          {"love_graphics", "particles_state", guarded<particles_state>,
                           "particles_state(particles: any) -> any",
                           "Snapshot all particle settings, state flags, counters and quad count."},
                          {"love_graphics", "particles_set_texture", guarded<particles_set_texture>,
                           "particles_set_texture(particles: any, texture: any)",
                           "Change the retained 2d particle texture."},
                          {"love_graphics", "particles_texture", guarded<particles_texture>,
                           "particles_texture(particles: any) -> any", "Get a retained texture alias."},
                          {"love_graphics", "particles_set_quads", guarded<particles_set_quads>,
                           "particles_set_quads(particles: any, quads: any)",
                           "Retain up to 64 atlas quads; [] restores the full texture."},
                          {"love_graphics", "particles_quads", guarded<particles_quads>,
                           "particles_quads(particles: any) -> any",
                           "Get independent managed quad aliases retaining their texture."},
                      });
}
} // namespace lovescua
