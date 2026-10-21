// Native sprite batches and particle systems, with retained textures.
#include "binding_util.h"
#include "graphics/ParticleSystem.h"
#include "graphics/SpriteBatch.h"
namespace lovescua
{
namespace
{
using namespace binding;
auto batch(scua_ctx *ctx)
{
    return objectArgument<love::graphics::SpriteBatch>(ctx, 0, ObjectKind::SpriteBatch);
}
auto particles(scua_ctx *ctx)
{
    return objectArgument<love::graphics::ParticleSystem>(ctx, 0, ObjectKind::Particles);
}
void new_sprite_batch(scua_ctx *ctx)
{
    arity(ctx, 2);
    int n = integer(ctx, 1, 1, 65536);
    auto image = imageArgument(ctx, 0);
    if (image)
        returnObject(
            ctx, ObjectKind::SpriteBatch,
            graphics(ctx, false).newSpriteBatch(image->texture, n, love::graphics::BUFFERDATAUSAGE_DYNAMIC));
}
void batch_add(scua_ctx *ctx)
{
    auto m = transform(ctx);
    auto b = batch(ctx);
    if (b)
    {
        if (b->getCount() >= 65536)
            throw std::invalid_argument("sprite limit is 65536");
        scua_return_int(ctx, b->add(m));
    }
}
void batch_set(scua_ctx *ctx)
{
    arity(ctx, 5);
    int i = integer(ctx, 1, 0, 65535);
    float p[2], s[2];
    vector(ctx, 2, p, 2);
    vector(ctx, 4, s, 2);
    float angle = number(ctx, 3);
    auto b = batch(ctx);
    if (b)
    {
        if (i >= b->getCount())
            throw std::invalid_argument("sprite index is outside the batch");
        b->add(love::Matrix4(p[0], p[1], angle, s[0], s[1], 0, 0, 0, 0), i);
    }
}
void batch_add_quad(scua_ctx *ctx)
{
    arity(ctx, 5);
    float p[2], s[2];
    vector(ctx, 2, p, 2);
    vector(ctx, 4, s, 2);
    auto angle = number(ctx, 3);
    auto q = quadArgument(ctx, 1);
    auto b = batch(ctx);
    if (b && q)
    {
        if (b->getCount() >= 65536)
            throw std::invalid_argument("sprite limit is 65536");
        if (q->texture != b->getTexture())
            throw std::invalid_argument("quad and batch must use the same texture");
        scua_return_int(ctx, b->add(q->quad, love::Matrix4(p[0], p[1], angle, s[0], s[1], 0, 0, 0, 0)));
    }
}
void batch_clear(scua_ctx *ctx)
{
    arity(ctx, 1);
    auto b = batch(ctx);
    if (b)
        b->clear();
}
void batch_flush(scua_ctx *ctx)
{
    arity(ctx, 1);
    auto b = batch(ctx);
    if (b)
        b->flush();
}
void batch_count(scua_ctx *ctx)
{
    arity(ctx, 1);
    auto b = batch(ctx);
    if (b)
        scua_return_int(ctx, b->getCount());
}
void batch_capacity(scua_ctx *ctx)
{
    arity(ctx, 1);
    auto b = batch(ctx);
    if (b)
        scua_return_int(ctx, b->getBufferSize());
}
void batch_set_color(scua_ctx *ctx)
{
    arity(ctx, 2);
    float c[4];
    vector(ctx, 1, c, 4);
    auto b = batch(ctx);
    if (b)
        b->setColor({c[0], c[1], c[2], c[3]});
}
void draw_batch(scua_ctx *ctx)
{
    auto m = transform(ctx);
    auto b = batch(ctx);
    if (b)
        graphics(ctx).draw(b, m);
}
void new_particles(scua_ctx *ctx)
{
    arity(ctx, 2);
    int n = integer(ctx, 1, 1, 65536);
    auto image = imageArgument(ctx, 0);
    if (image)
        returnObject(ctx, ObjectKind::Particles, graphics(ctx, false).newParticleSystem(image->texture, n));
}
void draw_particles(scua_ctx *ctx)
{
    auto m = transform(ctx);
    auto p = particles(ctx);
    if (p)
        graphics(ctx).draw(p, m);
}
void particles_clone(scua_ctx *ctx)
{
    arity(ctx, 1);
    auto p = particles(ctx);
    if (p)
        returnObject(ctx, ObjectKind::Particles, p->clone());
}
void particles_update(scua_ctx *ctx)
{
    arity(ctx, 2);
    auto dt = number(ctx, 1);
    if (dt < 0 || dt > 1)
        throw std::invalid_argument("particle dt must be in [0,1]");
    auto p = particles(ctx);
    if (p)
        p->update(dt);
}
void particles_emit(scua_ctx *ctx)
{
    arity(ctx, 2);
    int n = integer(ctx, 1, 0, 65536);
    auto p = particles(ctx);
    if (p)
        p->emit(n);
}
void particles_start(scua_ctx *ctx)
{
    arity(ctx, 1);
    auto p = particles(ctx);
    if (p)
        p->start();
}
void particles_stop(scua_ctx *ctx)
{
    arity(ctx, 1);
    auto p = particles(ctx);
    if (p)
        p->stop();
}
void particles_pause(scua_ctx *ctx)
{
    arity(ctx, 1);
    auto p = particles(ctx);
    if (p)
        p->pause();
}
void particles_reset(scua_ctx *ctx)
{
    arity(ctx, 1);
    auto p = particles(ctx);
    if (p)
        p->reset();
}
void particles_count(scua_ctx *ctx)
{
    arity(ctx, 1);
    auto p = particles(ctx);
    if (p)
        scua_return_int(ctx, p->getCount());
}
void particles_is_active(scua_ctx *ctx)
{
    arity(ctx, 1);
    auto p = particles(ctx);
    if (p)
        scua_return_bool(ctx, p->isActive());
}
void particles_is_empty(scua_ctx *ctx)
{
    arity(ctx, 1);
    auto p = particles(ctx);
    if (p)
        scua_return_bool(ctx, p->isEmpty());
}
void particles_is_full(scua_ctx *ctx)
{
    arity(ctx, 1);
    auto p = particles(ctx);
    if (p)
        scua_return_bool(ctx, p->isFull());
}
void particles_set_rate(scua_ctx *ctx)
{
    arity(ctx, 2);
    auto v = number(ctx, 1);
    if (v < 0 || v > 65536)
        throw std::invalid_argument("particle parameter outside supported range");
    auto p = particles(ctx);
    if (p)
        p->setEmissionRate(v);
}
void particles_set_direction(scua_ctx *ctx)
{
    arity(ctx, 2);
    auto v = number(ctx, 1);
    if (v < -1000000000.0 || v > 1000000000.0)
        throw std::invalid_argument("particle parameter outside supported range");
    auto p = particles(ctx);
    if (p)
        p->setDirection(v);
}
void particles_set_spread(scua_ctx *ctx)
{
    arity(ctx, 2);
    auto v = number(ctx, 1);
    if (v < 0 || v > 1000000000.0)
        throw std::invalid_argument("particle parameter outside supported range");
    auto p = particles(ctx);
    if (p)
        p->setSpread(v);
}
void particles_set_size(scua_ctx *ctx)
{
    arity(ctx, 2);
    auto v = number(ctx, 1);
    if (v < 0 || v > 10000)
        throw std::invalid_argument("particle parameter outside supported range");
    auto p = particles(ctx);
    if (p)
        p->setSize(v);
}
void particles_set_size_variation(scua_ctx *ctx)
{
    arity(ctx, 2);
    auto v = number(ctx, 1);
    if (v < 0 || v > 1)
        throw std::invalid_argument("particle parameter outside supported range");
    auto p = particles(ctx);
    if (p)
        p->setSizeVariation(v);
}
void particles_set_emitter_lifetime(scua_ctx *ctx)
{
    arity(ctx, 2);
    auto v = number(ctx, 1);
    if (v < -1 || v > 3600)
        throw std::invalid_argument("particle parameter outside supported range");
    auto p = particles(ctx);
    if (p)
        p->setEmitterLifetime(v);
}
void particles_set_lifetime(scua_ctx *ctx)
{
    arity(ctx, 3);
    auto lo = number(ctx, 1), hi = number(ctx, 2);
    if (hi < lo || std::abs(lo) > 1e6 || std::abs(hi) > 1e6 || lo <= 0)
        throw std::invalid_argument("invalid particle range");
    auto p = particles(ctx);
    if (p)
        p->setParticleLifetime(lo, hi);
}
void particles_set_speed(scua_ctx *ctx)
{
    arity(ctx, 3);
    auto lo = number(ctx, 1), hi = number(ctx, 2);
    if (hi < lo || std::abs(lo) > 1e6 || std::abs(hi) > 1e6)
        throw std::invalid_argument("invalid particle range");
    auto p = particles(ctx);
    if (p)
        p->setSpeed(lo, hi);
}
void particles_set_rotation(scua_ctx *ctx)
{
    arity(ctx, 3);
    auto lo = number(ctx, 1), hi = number(ctx, 2);
    if (hi < lo || std::abs(lo) > 1e6 || std::abs(hi) > 1e6)
        throw std::invalid_argument("invalid particle range");
    auto p = particles(ctx);
    if (p)
        p->setRotation(lo, hi);
}
void particles_set_spin(scua_ctx *ctx)
{
    arity(ctx, 3);
    auto lo = number(ctx, 1), hi = number(ctx, 2);
    if (std::abs(lo) > 1e6 || std::abs(hi) > 1e6)
        throw std::invalid_argument("invalid particle range");
    auto p = particles(ctx);
    if (p)
        p->setSpin(lo, hi);
}
void particles_set_damping(scua_ctx *ctx)
{
    arity(ctx, 3);
    auto lo = number(ctx, 1), hi = number(ctx, 2);
    if (hi < lo || std::abs(lo) > 1e6 || std::abs(hi) > 1e6)
        throw std::invalid_argument("invalid particle range");
    auto p = particles(ctx);
    if (p)
        p->setLinearDamping(lo, hi);
}
void particles_set_radial_acceleration(scua_ctx *ctx)
{
    arity(ctx, 3);
    auto lo = number(ctx, 1), hi = number(ctx, 2);
    if (hi < lo || std::abs(lo) > 1e6 || std::abs(hi) > 1e6)
        throw std::invalid_argument("invalid particle range");
    auto p = particles(ctx);
    if (p)
        p->setRadialAcceleration(lo, hi);
}
void particles_set_tangential_acceleration(scua_ctx *ctx)
{
    arity(ctx, 3);
    auto lo = number(ctx, 1), hi = number(ctx, 2);
    if (hi < lo || std::abs(lo) > 1e6 || std::abs(hi) > 1e6)
        throw std::invalid_argument("invalid particle range");
    auto p = particles(ctx);
    if (p)
        p->setTangentialAcceleration(lo, hi);
}
void particles_set_position(scua_ctx *ctx)
{
    arity(ctx, 2);
    float v[2];
    vector(ctx, 1, v, 2);
    auto p = particles(ctx);
    if (p)
        p->setPosition(v[0], v[1]);
}
void particles_move_to(scua_ctx *ctx)
{
    arity(ctx, 2);
    float v[2];
    vector(ctx, 1, v, 2);
    auto p = particles(ctx);
    if (p)
        p->moveTo(v[0], v[1]);
}
void particles_set_acceleration(scua_ctx *ctx)
{
    arity(ctx, 2);
    float v[2];
    vector(ctx, 1, v, 2);
    auto p = particles(ctx);
    if (p)
        p->setLinearAcceleration(v[0], v[1]);
}
void particles_set_offset(scua_ctx *ctx)
{
    arity(ctx, 2);
    float v[2];
    vector(ctx, 1, v, 2);
    auto p = particles(ctx);
    if (p)
        p->setOffset(v[0], v[1]);
}
void particles_set_colors(scua_ctx *ctx)
{
    arity(ctx, 3);
    float a[4], b[4];
    vector(ctx, 1, a, 4);
    vector(ctx, 2, b, 4);
    auto p = particles(ctx);
    if (p)
        p->setColor({{a[0], a[1], a[2], a[3]}, {b[0], b[1], b[2], b[3]}});
}
} // namespace
void registerBatches(scua_partition *p, Host &state)
{
    registerObjectType(p, state, ObjectKind::SpriteBatch);
    registerObjectType(p, state, ObjectKind::Particles);
    registerFunctions(
        p, state,
        {
            {"love_graphics", "new_sprite_batch", guarded<new_sprite_batch>,
             "new_sprite_batch(image: any, capacity: int) -> any",
             "Create a managed batch retaining its texture."},
            {"love_graphics", "batch_add", guarded<batch_add>,
             "batch_add(batch: any, position: vec2, rotation: float = 0, scale: vec2 = nil, origin: vec2 = "
             "nil) -> int",
             "Add a sprite and return its zero-based index."},
            {"love_graphics", "batch_set", guarded<batch_set>,
             "batch_set(batch: any, index: int, position: vec2, rotation: float, scale: vec2)",
             "Replace one sprite transform."},
            {"love_graphics", "batch_add_quad", guarded<batch_add_quad>,
             "batch_add_quad(batch: any, quad: any, position: vec2, rotation: float, scale: vec2) -> int",
             "Add a sprite using an atlas quad."},
            {"love_graphics", "batch_clear", guarded<batch_clear>, "batch_clear(batch: any)", "Batch clear."},
            {"love_graphics", "batch_flush", guarded<batch_flush>, "batch_flush(batch: any)", "Batch flush."},
            {"love_graphics", "batch_count", guarded<batch_count>, "batch_count(batch: any) -> int",
             "Batch count."},
            {"love_graphics", "batch_capacity", guarded<batch_capacity>, "batch_capacity(batch: any) -> int",
             "Batch capacity."},
            {"love_graphics", "batch_set_color", guarded<batch_set_color>,
             "batch_set_color(batch: any, color: vec4)", "Tint subsequently added sprites."},
            {"love_graphics", "draw_batch", guarded<draw_batch>,
             "draw_batch(batch: any, position: vec2, rotation: float = 0, scale: vec2 = nil, origin: vec2 = "
             "nil)",
             "Draw a sprite batch."},
            {"love_graphics", "new_particles", guarded<new_particles>,
             "new_particles(image: any, capacity: int) -> any",
             "Create a managed particle emitter retaining its texture."},
            {"love_graphics", "draw_particles", guarded<draw_particles>,
             "draw_particles(particles: any, position: vec2, rotation: float = 0, scale: vec2 = nil, origin: "
             "vec2 = nil)",
             "Draw particles."},
            {"love_graphics", "particles_clone", guarded<particles_clone>,
             "particles_clone(particles: any) -> any", "Clone emitter settings without existing particles."},
            {"love_graphics", "particles_update", guarded<particles_update>,
             "particles_update(particles: any, dt: float)", "Advance particles with a bounded time step."},
            {"love_graphics", "particles_emit", guarded<particles_emit>,
             "particles_emit(particles: any, count: int)", "Emit a burst."},
            {"love_graphics", "particles_start", guarded<particles_start>, "particles_start(particles: any)",
             "Particle start."},
            {"love_graphics", "particles_stop", guarded<particles_stop>, "particles_stop(particles: any)",
             "Particle stop."},
            {"love_graphics", "particles_pause", guarded<particles_pause>, "particles_pause(particles: any)",
             "Particle pause."},
            {"love_graphics", "particles_reset", guarded<particles_reset>, "particles_reset(particles: any)",
             "Particle reset."},
            {"love_graphics", "particles_count", guarded<particles_count>,
             "particles_count(particles: any) -> int", "Particle count."},
            {"love_graphics", "particles_is_active", guarded<particles_is_active>,
             "particles_is_active(particles: any) -> bool", "Particle is active."},
            {"love_graphics", "particles_is_empty", guarded<particles_is_empty>,
             "particles_is_empty(particles: any) -> bool", "Particle is empty."},
            {"love_graphics", "particles_is_full", guarded<particles_is_full>,
             "particles_is_full(particles: any) -> bool", "Particle is full."},
            {"love_graphics", "particles_set_rate", guarded<particles_set_rate>,
             "particles_set_rate(particles: any, value: float)", "Configure rate."},
            {"love_graphics", "particles_set_direction", guarded<particles_set_direction>,
             "particles_set_direction(particles: any, value: float)", "Configure direction."},
            {"love_graphics", "particles_set_spread", guarded<particles_set_spread>,
             "particles_set_spread(particles: any, value: float)", "Configure spread."},
            {"love_graphics", "particles_set_size", guarded<particles_set_size>,
             "particles_set_size(particles: any, value: float)", "Configure size."},
            {"love_graphics", "particles_set_size_variation", guarded<particles_set_size_variation>,
             "particles_set_size_variation(particles: any, value: float)", "Configure size variation."},
            {"love_graphics", "particles_set_emitter_lifetime", guarded<particles_set_emitter_lifetime>,
             "particles_set_emitter_lifetime(particles: any, value: float)", "Configure emitter lifetime."},
            {"love_graphics", "particles_set_lifetime", guarded<particles_set_lifetime>,
             "particles_set_lifetime(particles: any, minimum: float, maximum: float)",
             "Configure a particle parameter range."},
            {"love_graphics", "particles_set_speed", guarded<particles_set_speed>,
             "particles_set_speed(particles: any, minimum: float, maximum: float)",
             "Configure a particle parameter range."},
            {"love_graphics", "particles_set_rotation", guarded<particles_set_rotation>,
             "particles_set_rotation(particles: any, minimum: float, maximum: float)",
             "Configure a particle parameter range."},
            {"love_graphics", "particles_set_spin", guarded<particles_set_spin>,
             "particles_set_spin(particles: any, minimum: float, maximum: float)",
             "Configure a particle parameter range."},
            {"love_graphics", "particles_set_damping", guarded<particles_set_damping>,
             "particles_set_damping(particles: any, minimum: float, maximum: float)",
             "Configure a particle parameter range."},
            {"love_graphics", "particles_set_radial_acceleration", guarded<particles_set_radial_acceleration>,
             "particles_set_radial_acceleration(particles: any, minimum: float, maximum: float)",
             "Configure a particle parameter range."},
            {"love_graphics", "particles_set_tangential_acceleration",
             guarded<particles_set_tangential_acceleration>,
             "particles_set_tangential_acceleration(particles: any, minimum: float, maximum: float)",
             "Configure a particle parameter range."},
            {"love_graphics", "particles_set_position", guarded<particles_set_position>,
             "particles_set_position(particles: any, value: vec2)", "Configure a particle vector."},
            {"love_graphics", "particles_move_to", guarded<particles_move_to>,
             "particles_move_to(particles: any, value: vec2)", "Configure a particle vector."},
            {"love_graphics", "particles_set_acceleration", guarded<particles_set_acceleration>,
             "particles_set_acceleration(particles: any, value: vec2)", "Configure a particle vector."},
            {"love_graphics", "particles_set_offset", guarded<particles_set_offset>,
             "particles_set_offset(particles: any, value: vec2)", "Configure a particle vector."},
            {"love_graphics", "particles_set_colors", guarded<particles_set_colors>,
             "particles_set_colors(particles: any, start: vec4, finish: vec4)",
             "Interpolate particle colors over their lifetime."},
        });
}
} // namespace lovescua
