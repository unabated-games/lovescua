// SCUA physics adapter over the same bundled Box2D used by LÖVE. The upstream
// physics wrappers embed Lua references, so ownership is expressed with retained
// C++ objects here rather than importing those Lua-dependent wrappers.
#include "libraries/box2d/Box2D.h"
#include "libraries/box2d/b2_distance.h"
#include "options.h"
#include <functional>
#include <map>
#include <set>
namespace lovescua
{
namespace
{
using namespace binding;
struct BodyState
{
    b2Body *body = nullptr;
    uint64_t id = 0;
    bool customMass = false;
};
struct ColliderState
{
    b2Fixture *fixture = nullptr;
    uint64_t id = 0, body = 0;
};
struct JointState
{
    uint64_t id = 0, bodyA = 0, bodyB = 0;
    b2Joint *joint = nullptr;
    std::array<std::weak_ptr<JointState>, 2> sources;
};
struct ContactState
{
    uint64_t id = 0, colliderA = 0, colliderB = 0;
    int childA = 0, childB = 0;
    b2Contact *contact = nullptr;
    love::OptionalBool enabled;
};
struct World;
struct ContactFilter final : b2ContactFilter
{
    World *owner;
    explicit ContactFilter(World *w) : owner(w) {}
    bool ShouldCollide(b2Fixture *, b2Fixture *) override;
};
struct ContactListener final : b2ContactListener
{
    World *owner;
    explicit ContactListener(World *world) : owner(world) {}
    void BeginContact(b2Contact *) override;
    void EndContact(b2Contact *) override;
    void PreSolve(b2Contact *, const b2Manifold *) override;
    void PostSolve(b2Contact *, const b2ContactImpulse *) override;
    void call(int, b2Contact *, const b2ContactImpulse * = nullptr) noexcept;
};
struct World final : love::Object
{
    std::vector<std::shared_ptr<JointState>> jointStates;
    std::map<uint64_t, std::shared_ptr<BodyState>> bodyStates;
    std::map<uint64_t, std::shared_ptr<ColliderState>> colliderStates;
    bool destroyed = false, querying = false, stepping = false;
    uint64_t nextJointId = 1, nextContactId = 1;
    std::map<uint64_t, ContactState> contacts;
    std::map<b2Contact *, uint64_t> contactIds;
    scua_Handle filterCallback{};
    ContactListener listener;
    ContactFilter filter;
    b2World world;
    scua_ctx *stepContext = nullptr;
    std::array<scua_Handle, 4> callbacks{};
    std::string callbackError;
    size_t callbackCount = 0;
    bool callbacksEnabled = true;
    float meter;
    uint64_t id;
    uint64_t nextId = 1, nextColliderId = 1;
    World(b2Vec2 gravity, float meter, uint64_t id)
        : listener(this), filter(this), world(gravity), meter(meter), id(id)
    {
        world.SetContactListener(&listener);
        world.SetContactFilter(&filter);
    }
    ~World()
    {
        world.SetContactListener(nullptr);
    }
};
Record contactSnapshot(scua_ctx *, World *, b2Contact *);
void retireContact(World *, b2Contact *);
void prepareContacts(World *);
void ContactListener::call(int phase, b2Contact *contact, const b2ContactImpulse *impulse) noexcept
{
    auto ctx = owner->stepContext;
    if (!ctx || !owner->callbackError.empty() || !owner->callbacksEnabled)
        return;
    auto p = scua_ctx_partition(ctx);
    auto fn = owner->callbacks[phase];
    if (!scua_handle_valid(p, fn))
        return;
    if (++owner->callbackCount > 4096)
    {
        owner->callbackError = "physics callback limit is 4096 per step";
        return;
    }
    auto scope = scua_scope_enter(p);
    try
    {
        const char *names[] = {"begincontact", "endcontact", "presolve", "postsolve"};
        auto r = contactSnapshot(ctx, owner, contact);
        r.str("phase", names[phase]);
        auto impulses = scua_new_array(p, 0);
        if (impulse)
            for (int i = 0; i < impulse->count; ++i)
            {
                float v[] = {impulse->normalImpulses[i] * owner->meter,
                             impulse->tangentImpulses[i] * owner->meter};
                if (scua_array_push(p, impulses, scua_vec(p, v, 2)) != SCUA_OK)
                    throw std::runtime_error("could not construct contact impulses");
            }
        r.set("impulses", impulses);
        scua_Handle result{};
        char error[2048]{};
        if (scua_pcall(p, fn, &r.value, 1, &result, error, sizeof(error)) != SCUA_OK)
            throw std::runtime_error(std::string(names[phase]) + ": " + error);
        if (phase == 2 && scua_handle_valid(p, result) && scua_typeof(p, result) != SCUA_TYPE_NIL)
        {
            if (scua_typeof(p, result) != SCUA_TYPE_TABLE)
                throw std::invalid_argument("presolve must return nil or a contact-options record");
            auto enabled = scua_field(p, result, "enabled");
            if (scua_handle_valid(p, enabled) && scua_typeof(p, enabled) != SCUA_TYPE_NIL)
            {
                if (scua_typeof(p, enabled) != SCUA_TYPE_BOOL)
                    throw std::invalid_argument("contact enabled must be boolean");
                contact->SetEnabled(scua_to_bool(p, enabled));
            }
            for (auto field : {"friction", "restitution", "tangent_speed"})
            {
                auto value = scua_field(p, result, field);
                if (!scua_handle_valid(p, value) || scua_typeof(p, value) == SCUA_TYPE_NIL)
                    continue;
                int type = scua_typeof(p, value);
                double n = scua_to_num(p, value);
                if ((type != SCUA_TYPE_INT && type != SCUA_TYPE_FLOAT) || !std::isfinite(n) ||
                    std::abs(n) > 1e6 || ((std::strcmp(field, "tangent_speed") != 0) && n < 0))
                    throw std::invalid_argument("invalid contact material option");
                if (std::strcmp(field, "friction") == 0)
                    contact->SetFriction(n);
                else if (std::strcmp(field, "restitution") == 0)
                    contact->SetRestitution(n);
                else
                    contact->SetTangentSpeed(n / owner->meter);
            }
        }
    }
    catch (const std::exception &error)
    {
        try
        {
            owner->callbackError = error.what();
        }
        catch (...)
        {
            std::terminate();
        }
    }
    catch (...)
    {
        try
        {
            owner->callbackError = "unknown physics callback failure";
        }
        catch (...)
        {
            std::terminate();
        }
    }
    scua_scope_leave(p, scope);
}
void ContactListener::BeginContact(b2Contact *c)
{
    call(0, c);
}
void ContactListener::EndContact(b2Contact *c)
{
    call(1, c);
    retireContact(owner, c);
}
void ContactListener::PreSolve(b2Contact *c, const b2Manifold *)
{
    auto id = owner->contactIds.find(c);
    if (id != owner->contactIds.end())
    {
        auto state = owner->contacts.find(id->second);
        if (state != owner->contacts.end() && state->second.enabled.hasValue)
            c->SetEnabled(state->second.enabled.value);
    }
    call(2, c);
}
void ContactListener::PostSolve(b2Contact *c, const b2ContactImpulse *i)
{
    call(3, c, i);
}
struct Body final : love::Object
{
    World *owner;
    std::shared_ptr<BodyState> state;
    b2Body *&body;
    uint64_t id;
    bool published = false;
    Body(World *w, const b2BodyDef &def)
        : owner(w), state(std::make_shared<BodyState>()), body(state->body), id(w->nextId++)
    {
        body = owner->world.CreateBody(&def);
        state->id = id;
        body->GetUserData().pointer = id;
        try
        {
            owner->bodyStates.emplace(id, state);
        }
        catch (...)
        {
            owner->world.DestroyBody(body);
            throw;
        }
        owner->retain();
    }
    Body(World *w, std::shared_ptr<BodyState> existing)
        : owner(w), state(std::move(existing)), body(state->body), id(state->id), published(true)
    {
        owner->retain();
    }
    ~Body()
    {
        if (!published && body)
        {
            owner->world.DestroyBody(body);
            body = nullptr;
            owner->bodyStates.erase(id);
        }
        owner->release();
    }
};
struct Collider final : love::Object
{
    Body *owner;
    std::shared_ptr<ColliderState> state;
    b2Fixture *&fixture;
    uint64_t id;
    bool published = false;
    Collider(Body *b, const b2Shape *shape, float density)
        : owner(b), state(std::make_shared<ColliderState>()), fixture(state->fixture),
          id(b->owner->nextColliderId++)
    {
        fixture = b->body->CreateFixture(shape, density);
        state->id = id;
        state->body = b->id;
        fixture->GetUserData().pointer = id;
        try
        {
            b->owner->colliderStates.emplace(id, state);
        }
        catch (...)
        {
            b->body->DestroyFixture(fixture);
            throw;
        }
        b->state->customMass = false;
        owner->retain();
    }
    Collider(Body *b, std::shared_ptr<ColliderState> existing)
        : owner(b), state(std::move(existing)), fixture(state->fixture), id(state->id), published(true)
    {
        owner->retain();
    }
    ~Collider()
    {
        if (!published && fixture)
        {
            if (owner->body)
                owner->body->DestroyFixture(fixture);
            fixture = nullptr;
            owner->owner->colliderStates.erase(id);
        }
        owner->release();
    }
};
void destroyJoint(World *world, const std::shared_ptr<JointState> &state)
{
    if (!state->joint)
        return;
    for (auto &candidate : world->jointStates)
        if (candidate->joint &&
            (candidate->sources[0].lock() == state || candidate->sources[1].lock() == state))
            destroyJoint(world, candidate);
    world->world.DestroyJoint(state->joint);
    state->joint = nullptr;
}
struct Joint final : love::Object
{
    Body *a, *b;
    std::shared_ptr<JointState> state;
    bool published = false;
    Joint(Body *a, Body *b, const b2JointDef &def) : a(a), b(b), state(std::make_shared<JointState>())
    {
        auto &slots = a->owner->jointStates;
        slots.erase(std::remove_if(slots.begin(), slots.end(), [](auto &s) { return !s->joint; }),
                    slots.end());
        if (slots.size() >= 4096)
            throw std::invalid_argument("joint limit is 4096 per world");
        state->id = a->owner->nextJointId++;
        state->bodyA = a->id;
        state->bodyB = b->id;
        slots.push_back(state);
        state->joint = a->owner->world.CreateJoint(&def);
        a->retain();
        b->retain();
    }
    Joint(Body *a, Body *b, std::shared_ptr<JointState> existing)
        : a(a), b(b), state(std::move(existing)), published(true)
    {
        a->retain();
        b->retain();
    }
    ~Joint()
    {
        if (!published)
        {
            destroyJoint(a->owner, state);
            auto &slots = a->owner->jointStates;
            slots.erase(std::remove(slots.begin(), slots.end(), state), slots.end());
        }
        a->release();
        b->release();
    }
};
auto world(scua_ctx *ctx)
{
    auto value = objectArgument<World>(ctx, 0, ObjectKind::World);
    if (value && value->destroyed)
        throw std::invalid_argument("physics world is destroyed");
    if (value && (value->world.IsLocked() || value->querying || value->stepping))
        throw std::invalid_argument(
            "physics world is locked during contact callbacks; defer mutations until world_step returns");
    return value;
}
auto body(scua_ctx *ctx, int i = 0)
{
    auto value = objectArgument<Body>(ctx, i, ObjectKind::Body);
    if (value && (value->owner->world.IsLocked() || value->owner->querying || value->owner->stepping))
        throw std::invalid_argument("physics world is locked during contact callbacks");
    if (value && !value->body)
        throw std::invalid_argument("physics body is destroyed");
    return value;
}
auto collider(scua_ctx *ctx)
{
    auto value = objectArgument<Collider>(ctx, 0, ObjectKind::Collider);
    if (value && (value->owner->owner->world.IsLocked() || value->owner->owner->querying ||
                  value->owner->owner->stepping))
        throw std::invalid_argument("physics world is locked during contact callbacks");
    if (value && (!value->owner->body || !value->fixture))
        throw std::invalid_argument("physics collider is destroyed");
    return value;
}
float scalar(scua_ctx *ctx, int i, float minimum = -1e6, float maximum = 1e6)
{
    auto n = number(ctx, i);
    if (n < minimum || n > maximum)
        throw std::invalid_argument("physics value outside supported range");
    return n;
}
b2Vec2 vec(scua_ctx *ctx, int i, float meter = 1)
{
    float v[2];
    vector(ctx, i, v, 2);
    if (std::abs(v[0]) > 1e6 || std::abs(v[1]) > 1e6)
        throw std::invalid_argument("physics vector exceeds supported range");
    return {v[0] / meter, v[1] / meter};
}
void new_world(scua_ctx *ctx)
{
    if (scua_arg_count(ctx) < 1 || scua_arg_count(ctx) > 2)
        throw std::invalid_argument("new_world expects gravity and optional meter");
    float meter = supplied(ctx, 1) ? scalar(ctx, 1, 1, 10000) : 30;
    returnObject(ctx, ObjectKind::World,
                 new World(vec(ctx, 0, meter), meter, host(ctx).nextPhysicsWorldId++));
}
void world_step(scua_ctx *ctx)
{
    if (scua_arg_count(ctx) < 2 || scua_arg_count(ctx) > 4)
        throw std::invalid_argument("world_step expects 2 to 4 arguments");
    float dt = scalar(ctx, 1, 0, 0.25);
    int velocityIterations = supplied(ctx, 2) ? integer(ctx, 2, 1, 100) : 8;
    int positionIterations = supplied(ctx, 3) ? integer(ctx, 3, 1, 100) : 3;
    auto w = world(ctx);
    if (w)
    {
        w->callbackError.clear();
        w->callbackCount = 0;
        const char *names[] = {"begincontact", "endcontact", "presolve", "postsolve"};
        auto p = scua_ctx_partition(ctx);
        for (int i = 0; i < 4; ++i)
        {
            auto fn = scua_get_global(p, names[i]);
            w->callbacks[i] =
                scua_handle_valid(p, fn) && scua_typeof(p, fn) != SCUA_TYPE_NIL ? fn : scua_Handle{};
        }
        struct Reset
        {
            World *w;
            ~Reset()
            {
                w->stepContext = nullptr;
                w->stepping = false;
                w->filterCallback = {};
                w->callbacks = {};
            }
        } reset{w};
        prepareContacts(w);
        w->filterCallback = scua_get_global(p, "contactfilter");
        w->stepContext = ctx;
        w->stepping = true;
        w->world.Step(dt, velocityIterations, positionIterations);
        if (!w->callbackError.empty())
            throw std::runtime_error(w->callbackError);
    }
}
void world_stats(scua_ctx *ctx)
{
    arity(ctx, 1);
    auto w = world(ctx);
    if (w)
    {
        Record r(ctx);
        r.integer("id", w->id);
        r.integer("bodies", w->world.GetBodyCount());
        r.integer("joints", w->world.GetJointCount());
        r.integer("contacts", w->world.GetContactCount());
        int fixtures = 0;
        for (auto b = w->world.GetBodyList(); b; b = b->GetNext())
            for (auto f = b->GetFixtureList(); f; f = f->GetNext())
                ++fixtures;
        r.integer("fixtures", fixtures);
        r.num("meter", w->meter);
        r.done();
    }
}
void world_set_gravity(scua_ctx *ctx)
{
    arity(ctx, 2);
    auto w = world(ctx);
    if (w)
        w->world.SetGravity(vec(ctx, 1, w->meter));
}
void world_gravity(scua_ctx *ctx)
{
    arity(ctx, 1);
    auto w = world(ctx);
    if (w)
    {
        auto v = w->world.GetGravity();
        returnVec(ctx, v.x * w->meter, v.y * w->meter);
    }
}
void new_body(scua_ctx *ctx)
{
    arity(ctx, 3);
    auto type = string(ctx, 2);
    b2BodyDef def;
    if (type == "dynamic")
        def.type = b2_dynamicBody;
    else if (type == "kinematic")
        def.type = b2_kinematicBody;
    else if (type != "static")
        throw std::invalid_argument("body type must be static, dynamic or kinematic");
    auto w = world(ctx);
    if (w)
    {
        if (w->world.GetBodyCount() >= 4096)
            throw std::invalid_argument("body limit is 4096");
        def.position = vec(ctx, 1, w->meter);
        auto created = new Body(w, def);
        if (returnObject(ctx, ObjectKind::Body, created))
            created->published = true;
    }
}
void body_id(scua_ctx *ctx)
{
    arity(ctx, 1);
    auto b = body(ctx);
    if (b)
        scua_return_int(ctx, b->id);
}
void body_position(scua_ctx *ctx)
{
    arity(ctx, 1);
    auto b = body(ctx);
    if (b)
    {
        auto v = b->body->GetPosition();
        returnVec(ctx, v.x * b->owner->meter, v.y * b->owner->meter);
    }
}
void body_set_position(scua_ctx *ctx)
{
    arity(ctx, 2);
    auto b = body(ctx);
    if (b)
        b->body->SetTransform(vec(ctx, 1, b->owner->meter), b->body->GetAngle());
}
void body_angle(scua_ctx *ctx)
{
    arity(ctx, 1);
    auto b = body(ctx);
    if (b)
        scua_return_float(ctx, b->body->GetAngle());
}
void body_set_angle(scua_ctx *ctx)
{
    arity(ctx, 2);
    float a = scalar(ctx, 1);
    auto b = body(ctx);
    if (b)
        b->body->SetTransform(b->body->GetPosition(), a);
}
void body_velocity(scua_ctx *ctx)
{
    arity(ctx, 1);
    auto b = body(ctx);
    if (b)
    {
        auto v = b->body->GetLinearVelocity();
        returnVec(ctx, v.x * b->owner->meter, v.y * b->owner->meter);
    }
}
void body_set_velocity(scua_ctx *ctx)
{
    arity(ctx, 2);
    auto b = body(ctx);
    if (b)
        b->body->SetLinearVelocity(vec(ctx, 1, b->owner->meter));
}
void body_angular_velocity(scua_ctx *ctx)
{
    arity(ctx, 1);
    auto b = body(ctx);
    if (b)
        scua_return_float(ctx, b->body->GetAngularVelocity());
}
void body_set_angular_velocity(scua_ctx *ctx)
{
    arity(ctx, 2);
    float v = scalar(ctx, 1);
    auto b = body(ctx);
    if (b)
        b->body->SetAngularVelocity(v);
}
void body_mass(scua_ctx *ctx)
{
    arity(ctx, 1);
    auto b = body(ctx);
    if (b)
        scua_return_float(ctx, b->body->GetMass());
}
void body_apply_force(scua_ctx *ctx)
{
    arity(ctx, 2);
    auto b = body(ctx);
    if (b)
        b->body->ApplyForceToCenter(vec(ctx, 1, b->owner->meter), true);
}
void body_apply_impulse(scua_ctx *ctx)
{
    arity(ctx, 2);
    auto b = body(ctx);
    if (b)
        b->body->ApplyLinearImpulseToCenter(vec(ctx, 1, b->owner->meter), true);
}
void body_set_damping(scua_ctx *ctx)
{
    arity(ctx, 3);
    float linear = scalar(ctx, 1, 0), angular = scalar(ctx, 2, 0);
    auto b = body(ctx);
    if (b)
    {
        b->body->SetLinearDamping(linear);
        b->body->SetAngularDamping(angular);
    }
}
void body_set_fixed_rotation(scua_ctx *ctx)
{
    arity(ctx, 2);
    bool enabled = boolean(ctx, 1);
    auto b = body(ctx);
    if (b)
    {
        bool changed = b->body->IsFixedRotation() != enabled;
        b->body->SetFixedRotation(enabled);
        if (changed)
            b->state->customMass = false;
    }
}
void body_set_bullet(scua_ctx *ctx)
{
    arity(ctx, 2);
    bool enabled = boolean(ctx, 1);
    auto b = body(ctx);
    if (b)
        b->body->SetBullet(enabled);
}
void body_set_awake(scua_ctx *ctx)
{
    arity(ctx, 2);
    bool enabled = boolean(ctx, 1);
    auto b = body(ctx);
    if (b)
        b->body->SetAwake(enabled);
}
void body_is_awake(scua_ctx *ctx)
{
    arity(ctx, 1);
    auto b = body(ctx);
    if (b)
        scua_return_bool(ctx, b->body->IsAwake());
}
void body_world_point(scua_ctx *ctx)
{
    arity(ctx, 2);
    auto b = body(ctx);
    if (b)
    {
        auto p = b->body->GetWorldPoint(vec(ctx, 1, b->owner->meter));
        returnVec(ctx, p.x * b->owner->meter, p.y * b->owner->meter);
    }
}
void new_circle(scua_ctx *ctx)
{
    arity(ctx, 3);
    float radius = scalar(ctx, 1, 0.01), density = scalar(ctx, 2, 0, 10000);
    auto b = body(ctx);
    if (b)
    {
        if (radius / b->owner->meter < b2_linearSlop)
            throw std::invalid_argument("circle is smaller than Box2D tolerance");
        b2CircleShape shape;
        shape.m_radius = radius / b->owner->meter;
        auto created = new Collider(b, &shape, density);
        if (returnObject(ctx, ObjectKind::Collider, created))
            created->published = true;
    }
}
void new_box(scua_ctx *ctx)
{
    arity(ctx, 3);
    float density = scalar(ctx, 2, 0, 10000);
    auto b = body(ctx);
    if (b)
    {
        auto size = vec(ctx, 1, b->owner->meter);
        if (size.x < 2 * b2_linearSlop || size.y < 2 * b2_linearSlop)
            throw std::invalid_argument("box dimensions must exceed Box2D tolerance");
        b2PolygonShape shape;
        shape.SetAsBox(size.x / 2, size.y / 2);
        auto created = new Collider(b, &shape, density);
        if (returnObject(ctx, ObjectKind::Collider, created))
            created->published = true;
    }
}
void new_polygon(scua_ctx *ctx)
{
    arity(ctx, 3);
    auto vertices = points(ctx, 1, 3);
    if (vertices.size() > b2_maxPolygonVertices)
        throw std::invalid_argument("polygon supports at most 8 vertices");
    float density = scalar(ctx, 2, 0, 10000);
    auto b = body(ctx);
    if (!b)
        return;
    std::vector<b2Vec2> converted;
    for (auto p : vertices)
    {
        if (std::abs(p.x) > 1e6 || std::abs(p.y) > 1e6)
            throw std::invalid_argument("polygon coordinates exceed supported range");
        converted.push_back({p.x / b->owner->meter, p.y / b->owner->meter});
    }
    float direction = 0;
    for (size_t i = 0; i < converted.size(); ++i)
    {
        auto edge = converted[(i + 1) % converted.size()] - converted[i];
        auto next = converted[(i + 2) % converted.size()] - converted[(i + 1) % converted.size()];
        float cross = b2Cross(edge, next);
        if (edge.LengthSquared() < 4 * b2_linearSlop * b2_linearSlop ||
            std::abs(cross) < 4 * b2_linearSlop * b2_linearSlop || (direction != 0 && cross * direction < 0))
            throw std::invalid_argument("polygon must be strictly convex with distinct, separated vertices");
        direction = cross;
    }
    for (size_t i = 0; i < converted.size(); ++i)
        for (size_t j = 0; j < converted.size(); ++j)
            if (j != i && j != (i + 1) % converted.size() &&
                b2Cross(converted[(i + 1) % converted.size()] - converted[i], converted[j] - converted[i]) *
                        direction <=
                    0)
                throw std::invalid_argument("polygon must be convex without intersecting edges");
    b2PolygonShape shape;
    shape.Set(converted.data(), converted.size());
    auto created = new Collider(b, &shape, density);
    if (returnObject(ctx, ObjectKind::Collider, created))
        created->published = true;
}
void new_edge(scua_ctx *ctx)
{
    arity(ctx, 3);
    auto b = body(ctx);
    if (!b)
        return;
    auto a = vec(ctx, 1, b->owner->meter), end = vec(ctx, 2, b->owner->meter);
    if ((end - a).LengthSquared() < 4 * b2_linearSlop * b2_linearSlop)
        throw std::invalid_argument("edge endpoints must be separated");
    b2EdgeShape shape;
    shape.SetTwoSided(a, end);
    auto created = new Collider(b, &shape, 0);
    if (returnObject(ctx, ObjectKind::Collider, created))
        created->published = true;
}
void new_chain(scua_ctx *ctx)
{
    arity(ctx, 3);
    auto input = points(ctx, 1, 2);
    if (input.size() > 2048)
        throw std::invalid_argument("chain limit is 2048 vertices");
    bool loop = boolean(ctx, 2);
    if (loop && input.size() < 3)
        throw std::invalid_argument("a loop requires three vertices");
    auto b = body(ctx);
    if (!b)
        return;
    std::vector<b2Vec2> vertices;
    for (auto p : input)
    {
        if (std::abs(p.x) > 1e6 || std::abs(p.y) > 1e6)
            throw std::invalid_argument("chain coordinates exceed supported range");
        vertices.push_back({p.x / b->owner->meter, p.y / b->owner->meter});
    }
    for (size_t i = 0; i < vertices.size() - (loop ? 0 : 1); ++i)
        if ((vertices[(i + 1) % vertices.size()] - vertices[i]).LengthSquared() <
            4 * b2_linearSlop * b2_linearSlop)
            throw std::invalid_argument("chain vertices must be separated");
    b2ChainShape shape;
    if (loop)
        shape.CreateLoop(vertices.data(), vertices.size());
    else
        shape.CreateChain(vertices.data(), vertices.size(), 2 * vertices.front() - vertices[1],
                          2 * vertices.back() - vertices[vertices.size() - 2]);
    auto created = new Collider(b, &shape, 0);
    if (returnObject(ctx, ObjectKind::Collider, created))
        created->published = true;
}
void collider_set_density(scua_ctx *ctx)
{
    arity(ctx, 2);
    float density = scalar(ctx, 1, 0, 10000);
    auto c = collider(ctx);
    if (c)
    {
        c->fixture->SetDensity(density);
        c->owner->body->ResetMassData();
        c->owner->state->customMass = false;
    }
}
void collider_info(scua_ctx *ctx)
{
    arity(ctx, 1);
    auto c = collider(ctx);
    if (c)
    {
        Record r(ctx);
        r.num("density", c->fixture->GetDensity());
        r.num("friction", c->fixture->GetFriction());
        r.num("restitution", c->fixture->GetRestitution());
        r.boolean("sensor", c->fixture->IsSensor());
        auto f = c->fixture->GetFilterData();
        r.integer("category", f.categoryBits);
        r.integer("mask", f.maskBits);
        r.integer("group", f.groupIndex);
        r.done();
    }
}
void body_set_type(scua_ctx *ctx)
{
    arity(ctx, 2);
    auto type = string(ctx, 1);
    auto value = type == "dynamic" ? b2_dynamicBody : type == "kinematic" ? b2_kinematicBody : b2_staticBody;
    if (type != "dynamic" && type != "kinematic" && type != "static")
        throw std::invalid_argument("body type must be static, dynamic or kinematic");
    auto b = body(ctx);
    if (b)
    {
        if (b->body->GetType() != value)
            b->state->customMass = false;
        b->body->SetType(value);
    }
}
void body_type(scua_ctx *ctx)
{
    arity(ctx, 1);
    auto b = body(ctx);
    if (b)
        returnString(ctx, b->body->GetType() == b2_dynamicBody     ? "dynamic"
                          : b->body->GetType() == b2_kinematicBody ? "kinematic"
                                                                   : "static");
}
void body_set_enabled(scua_ctx *ctx)
{
    arity(ctx, 2);
    bool value = boolean(ctx, 1);
    auto b = body(ctx);
    if (b)
        b->body->SetEnabled(value);
}
void body_is_enabled(scua_ctx *ctx)
{
    arity(ctx, 1);
    auto b = body(ctx);
    if (b)
        scua_return_bool(ctx, b->body->IsEnabled());
}
void collider_set_friction(scua_ctx *ctx)
{
    arity(ctx, 2);
    float v = scalar(ctx, 1, 0, 100);
    auto c = collider(ctx);
    if (c)
        c->fixture->SetFriction(v);
}
void collider_set_restitution(scua_ctx *ctx)
{
    arity(ctx, 2);
    float v = scalar(ctx, 1, 0, 1);
    auto c = collider(ctx);
    if (c)
        c->fixture->SetRestitution(v);
}
void collider_set_sensor(scua_ctx *ctx)
{
    arity(ctx, 2);
    bool enabled = boolean(ctx, 1);
    auto c = collider(ctx);
    if (c)
        c->fixture->SetSensor(enabled);
}
void collider_test_point(scua_ctx *ctx)
{
    arity(ctx, 2);
    auto c = collider(ctx);
    if (c)
        scua_return_bool(ctx, c->fixture->TestPoint(vec(ctx, 1, c->owner->owner->meter)));
}
void collider_set_filter(scua_ctx *ctx)
{
    arity(ctx, 4);
    b2Filter f;
    f.categoryBits = integer(ctx, 1, 0, 65535);
    f.maskBits = integer(ctx, 2, 0, 65535);
    f.groupIndex = integer(ctx, 3, -32768, 32767);
    auto c = collider(ctx);
    if (c)
        c->fixture->SetFilterData(f);
}
void new_distance_joint(scua_ctx *ctx)
{
    arity(ctx, 4);
    auto a = body(ctx), b = body(ctx, 1);
    if (!a || !b)
        return;
    if (a->body == b->body || a->owner != b->owner)
        throw std::invalid_argument("joint requires different bodies from the same world");
    b2DistanceJointDef def;
    def.Initialize(a->body, b->body, vec(ctx, 2, a->owner->meter), vec(ctx, 3, a->owner->meter));
    auto created = new Joint(a, b, def);
    if (returnObject(ctx, ObjectKind::Joint, created))
        created->published = true;
}
void new_revolute_joint(scua_ctx *ctx)
{
    arity(ctx, 3);
    auto a = body(ctx), b = body(ctx, 1);
    if (!a || !b)
        return;
    if (a->body == b->body || a->owner != b->owner)
        throw std::invalid_argument("joint requires different bodies from the same world");
    b2RevoluteJointDef def;
    def.Initialize(a->body, b->body, vec(ctx, 2, a->owner->meter));
    auto created = new Joint(a, b, def);
    if (returnObject(ctx, ObjectKind::Joint, created))
        created->published = true;
}
auto joint(scua_ctx *ctx, int index = 0)
{
    auto j = objectArgument<Joint>(ctx, index, ObjectKind::Joint);
    if (j && (!j->state->joint || !j->a->body || !j->b->body))
        throw std::invalid_argument("physics joint is destroyed");
    if (j && (j->a->owner->world.IsLocked() || j->a->owner->querying || j->a->owner->stepping))
        throw std::invalid_argument("physics world is locked during contact callbacks");
    return j;
}
void jointBodies(Body *a, Body *b)
{
    if (a->body == b->body || a->owner != b->owner)
        throw std::invalid_argument("joint requires different bodies from the same world");
}
void publishJoint(scua_ctx *ctx, Body *a, Body *b, const b2JointDef &def)
{
    auto value = new Joint(a, b, def);
    if (returnObject(ctx, ObjectKind::Joint, value))
        value->published = true;
}
void new_prismatic_joint(scua_ctx *ctx)
{
    arity(ctx, 4);
    auto a = body(ctx), b = body(ctx, 1);
    if (!a || !b)
        return;
    jointBodies(a, b);
    auto axis = vec(ctx, 3);
    if (axis.LengthSquared() < 1e-12)
        throw std::invalid_argument("joint axis must be nonzero");
    axis.Normalize();
    b2PrismaticJointDef def;
    def.Initialize(a->body, b->body, vec(ctx, 2, a->owner->meter), axis);
    publishJoint(ctx, a, b, def);
}
void new_wheel_joint(scua_ctx *ctx)
{
    arity(ctx, 4);
    auto a = body(ctx), b = body(ctx, 1);
    if (!a || !b)
        return;
    jointBodies(a, b);
    auto axis = vec(ctx, 3);
    if (axis.LengthSquared() < 1e-12)
        throw std::invalid_argument("joint axis must be nonzero");
    axis.Normalize();
    b2WheelJointDef def;
    def.Initialize(a->body, b->body, vec(ctx, 2, a->owner->meter), axis);
    b2LinearStiffness(def.stiffness, def.damping, 4, 0.7, a->body, b->body);
    publishJoint(ctx, a, b, def);
}
void new_weld_joint(scua_ctx *ctx)
{
    arity(ctx, 3);
    auto a = body(ctx), b = body(ctx, 1);
    if (!a || !b)
        return;
    jointBodies(a, b);
    b2WeldJointDef def;
    def.Initialize(a->body, b->body, vec(ctx, 2, a->owner->meter));
    publishJoint(ctx, a, b, def);
}
void new_friction_joint(scua_ctx *ctx)
{
    arity(ctx, 3);
    auto a = body(ctx), b = body(ctx, 1);
    if (!a || !b)
        return;
    jointBodies(a, b);
    b2FrictionJointDef def;
    def.Initialize(a->body, b->body, vec(ctx, 2, a->owner->meter));
    publishJoint(ctx, a, b, def);
}
void new_motor_joint(scua_ctx *ctx)
{
    arity(ctx, 3);
    float correction = scalar(ctx, 2, 0, 1);
    auto a = body(ctx), b = body(ctx, 1);
    if (!a || !b)
        return;
    jointBodies(a, b);
    b2MotorJointDef def;
    def.Initialize(a->body, b->body);
    def.correctionFactor = correction;
    publishJoint(ctx, a, b, def);
}
void new_mouse_joint(scua_ctx *ctx)
{
    arity(ctx, 4);
    float force = scalar(ctx, 3, 0);
    auto a = body(ctx), b = body(ctx, 1);
    if (!a || !b)
        return;
    jointBodies(a, b);
    if (b->body->GetType() != b2_dynamicBody)
        throw std::invalid_argument("mouse joint body must be dynamic");
    b2MouseJointDef def;
    def.bodyA = a->body;
    def.bodyB = b->body;
    def.target = vec(ctx, 2, a->owner->meter);
    def.maxForce = force / a->owner->meter;
    b2LinearStiffness(def.stiffness, def.damping, 5, 0.7, a->body, b->body);
    publishJoint(ctx, a, b, def);
}
void new_pulley_joint(scua_ctx *ctx)
{
    arity(ctx, 7);
    float ratio = scalar(ctx, 6, 0.0001, 10000);
    auto a = body(ctx), b = body(ctx, 1);
    if (!a || !b)
        return;
    jointBodies(a, b);
    float m = a->owner->meter;
    auto ga = vec(ctx, 2, m), gb = vec(ctx, 3, m), aa = vec(ctx, 4, m), ab = vec(ctx, 5, m);
    if ((ga - aa).LengthSquared() < b2_linearSlop * b2_linearSlop ||
        (gb - ab).LengthSquared() < b2_linearSlop * b2_linearSlop)
        throw std::invalid_argument("pulley segments must have positive length");
    b2PulleyJointDef def;
    def.Initialize(a->body, b->body, ga, gb, aa, ab, ratio);
    publishJoint(ctx, a, b, def);
}
void new_rope_joint(scua_ctx *ctx)
{
    arity(ctx, 5);
    auto a = body(ctx), b = body(ctx, 1);
    if (!a || !b)
        return;
    jointBodies(a, b);
    float length = scalar(ctx, 4, 0.01) / a->owner->meter;
    b2DistanceJointDef def;
    def.Initialize(a->body, b->body, vec(ctx, 2, a->owner->meter), vec(ctx, 3, a->owner->meter));
    def.minLength = b2_linearSlop;
    def.maxLength = std::max(length, b2_linearSlop);
    def.length = def.maxLength;
    publishJoint(ctx, a, b, def);
}
void new_gear_joint(scua_ctx *ctx)
{
    if (scua_arg_count(ctx) < 3 || scua_arg_count(ctx) > 4)
        throw std::invalid_argument("new_gear_joint expects sources, ratio and optional collision flag");
    bool collide = supplied(ctx, 3) ? boolean(ctx, 3) : false;
    float ratio = scalar(ctx, 2);
    if (std::abs(ratio) < 0.0001)
        throw std::invalid_argument("gear ratio must be nonzero");
    auto first = joint(ctx), second = joint(ctx, 1);
    if (!first || !second)
        return;
    if (first->state == second->state || first->a->owner != second->a->owner)
        throw std::invalid_argument("gear joints require different source joints in the same world");
    auto valid = [](b2Joint *j)
    { return j->GetType() == e_revoluteJoint || j->GetType() == e_prismaticJoint; };
    if (!valid(first->state->joint) || !valid(second->state->joint))
        throw std::invalid_argument("gear sources must be revolute or prismatic joints");
    auto a = first->b, b = second->b;
    if (a->body == b->body || a->body->GetType() != b2_dynamicBody || b->body->GetType() != b2_dynamicBody)
        throw std::invalid_argument("gear source bodyB objects must be different dynamic bodies");
    b2GearJointDef def;
    def.bodyA = a->body;
    def.bodyB = b->body;
    def.collideConnected = collide;
    def.joint1 = first->state->joint;
    def.joint2 = second->state->joint;
    def.ratio = ratio;
    auto created = new Joint(a, b, def);
    created->state->sources = {first->state, second->state};
    if (returnObject(ctx, ObjectKind::Joint, created))
        created->published = true;
}
void jointFields(Joint *, Record &, float);
void joint_info(scua_ctx *ctx)
{
    arity(ctx, 1);
    auto j = joint(ctx);
    if (!j)
        return;
    auto value = j->state->joint;
    const char *names[] = {"unknown", "revolute", "prismatic", "distance", "pulley", "mouse",
                           "gear",    "wheel",    "weld",      "friction", "rope",   "motor"};
    int type = value->GetType();
    Record r(ctx);
    r.str("kind", type >= 0 && type < 12 ? names[type] : "unknown");
    r.integer("body_a", j->a->id);
    r.integer("body_b", j->b->id);
    auto a = value->GetAnchorA(), b = value->GetAnchorB();
    float av[] = {a.x * j->a->owner->meter, a.y * j->a->owner->meter},
          bv[] = {b.x * j->a->owner->meter, b.y * j->a->owner->meter};
    r.set("anchor_a", scua_vec(r.p, av, 2));
    r.set("anchor_b", scua_vec(r.p, bv, 2));
    r.boolean("collide_connected", value->GetCollideConnected());
    jointFields(j, r, 0);
    r.done();
}
void joint_reaction(scua_ctx *ctx)
{
    arity(ctx, 2);
    float dt = scalar(ctx, 1, 0.000001, 1);
    auto j = joint(ctx);
    if (j)
    {
        auto v = j->state->joint->GetReactionForce(1 / dt);
        float m = j->a->owner->meter;
        float result[] = {v.x * m, v.y * m, j->state->joint->GetReactionTorque(1 / dt) * m * m};
        scua_return_vec(ctx, result, 3);
    }
}
void joint_set_motor(scua_ctx *ctx)
{
    arity(ctx, 4);
    bool enabled = boolean(ctx, 1);
    float speed = scalar(ctx, 2), maximum = scalar(ctx, 3, 0);
    auto j = joint(ctx);
    if (!j)
        return;
    auto p = j->state->joint;
    float m = j->a->owner->meter;
    switch (p->GetType())
    {
    case e_revoluteJoint:
    {
        auto v = static_cast<b2RevoluteJoint *>(p);
        v->EnableMotor(enabled);
        v->SetMotorSpeed(speed);
        v->SetMaxMotorTorque(maximum / (m * m));
        break;
    }
    case e_prismaticJoint:
    {
        auto v = static_cast<b2PrismaticJoint *>(p);
        v->EnableMotor(enabled);
        v->SetMotorSpeed(speed / m);
        v->SetMaxMotorForce(maximum / m);
        break;
    }
    case e_wheelJoint:
    {
        auto v = static_cast<b2WheelJoint *>(p);
        v->EnableMotor(enabled);
        v->SetMotorSpeed(speed);
        v->SetMaxMotorTorque(maximum / (m * m));
        break;
    }
    default:
        throw std::invalid_argument("joint does not support a motor");
    }
}
void joint_set_limits(scua_ctx *ctx)
{
    arity(ctx, 4);
    bool enabled = boolean(ctx, 1);
    float lo = scalar(ctx, 2), hi = scalar(ctx, 3);
    if (hi < lo)
        throw std::invalid_argument("joint limits must be ordered");
    auto j = joint(ctx);
    if (!j)
        return;
    auto p = j->state->joint;
    float m = j->a->owner->meter;
    switch (p->GetType())
    {
    case e_revoluteJoint:
    {
        auto v = static_cast<b2RevoluteJoint *>(p);
        v->EnableLimit(enabled);
        v->SetLimits(lo, hi);
        break;
    }
    case e_prismaticJoint:
    {
        auto v = static_cast<b2PrismaticJoint *>(p);
        v->EnableLimit(enabled);
        v->SetLimits(lo / m, hi / m);
        break;
    }
    case e_wheelJoint:
    {
        auto v = static_cast<b2WheelJoint *>(p);
        v->EnableLimit(enabled);
        v->SetLimits(lo / m, hi / m);
        break;
    }
    case e_distanceJoint:
    {
        if (lo < 0)
            throw std::invalid_argument("distance limit must be nonnegative");
        auto v = static_cast<b2DistanceJoint *>(p);
        // Widen the old interval first so Box2D does not clamp a new lower
        // bound against the previous maximum.
        v->SetMinLength(b2_linearSlop);
        v->SetMaxLength(enabled ? hi / m : 1e6);
        v->SetMinLength(enabled ? lo / m : b2_linearSlop);
        break;
    }
    default:
        throw std::invalid_argument("joint does not support limits");
    }
}
void joint_set_spring(scua_ctx *ctx)
{
    arity(ctx, 3);
    float frequency = scalar(ctx, 1, 0, 1000), ratio = scalar(ctx, 2, 0, 100);
    auto j = joint(ctx);
    if (!j)
        return;
    float stiffness = 0, damping = 0;
    auto p = j->state->joint;
    if (p->GetType() == e_weldJoint)
        b2AngularStiffness(stiffness, damping, frequency, ratio, j->a->body, j->b->body);
    else
        b2LinearStiffness(stiffness, damping, frequency, ratio, j->a->body, j->b->body);
    switch (p->GetType())
    {
    case e_distanceJoint:
    {
        auto v = static_cast<b2DistanceJoint *>(p);
        v->SetStiffness(stiffness);
        v->SetDamping(damping);
        break;
    }
    case e_weldJoint:
    {
        auto v = static_cast<b2WeldJoint *>(p);
        v->SetStiffness(stiffness);
        v->SetDamping(damping);
        break;
    }
    case e_wheelJoint:
    {
        auto v = static_cast<b2WheelJoint *>(p);
        v->SetStiffness(stiffness);
        v->SetDamping(damping);
        break;
    }
    case e_mouseJoint:
    {
        auto v = static_cast<b2MouseJoint *>(p);
        v->SetStiffness(stiffness);
        v->SetDamping(damping);
        break;
    }
    default:
        throw std::invalid_argument("joint does not support spring settings");
    }
}
void joint_set_target(scua_ctx *ctx)
{
    arity(ctx, 2);
    auto j = joint(ctx);
    if (!j)
        return;
    auto p = j->state->joint;
    if (p->GetType() != e_mouseJoint)
        throw std::invalid_argument("target requires a mouse joint");
    static_cast<b2MouseJoint *>(p)->SetTarget(vec(ctx, 1, j->a->owner->meter));
    j->b->body->SetAwake(true);
}
void joint_set_force_limits(scua_ctx *ctx)
{
    arity(ctx, 3);
    float force = scalar(ctx, 1, 0), torque = scalar(ctx, 2, 0);
    auto j = joint(ctx);
    if (!j)
        return;
    float m = j->a->owner->meter;
    auto p = j->state->joint;
    switch (p->GetType())
    {
    case e_frictionJoint:
    {
        auto v = static_cast<b2FrictionJoint *>(p);
        v->SetMaxForce(force / m);
        v->SetMaxTorque(torque / (m * m));
        break;
    }
    case e_motorJoint:
    {
        auto v = static_cast<b2MotorJoint *>(p);
        v->SetMaxForce(force / m);
        v->SetMaxTorque(torque / (m * m));
        break;
    }
    case e_mouseJoint:
        static_cast<b2MouseJoint *>(p)->SetMaxForce(force / m);
        break;
    default:
        throw std::invalid_argument("force limits require friction, motor or mouse joints");
    }
}
void joint_set_offsets(scua_ctx *ctx)
{
    arity(ctx, 3);
    float angle = scalar(ctx, 2);
    auto j = joint(ctx);
    if (!j)
        return;
    if (j->state->joint->GetType() != e_motorJoint)
        throw std::invalid_argument("offsets require a motor joint");
    auto v = static_cast<b2MotorJoint *>(j->state->joint);
    v->SetLinearOffset(vec(ctx, 1, j->a->owner->meter));
    v->SetAngularOffset(angle);
}
void joint_set_ratio(scua_ctx *ctx)
{
    arity(ctx, 2);
    float ratio = scalar(ctx, 1);
    if (std::abs(ratio) < 0.0001)
        throw std::invalid_argument("gear ratio must be nonzero");
    auto j = joint(ctx);
    if (!j)
        return;
    if (j->state->joint->GetType() != e_gearJoint)
        throw std::invalid_argument("ratio requires a gear joint");
    static_cast<b2GearJoint *>(j->state->joint)->SetRatio(ratio);
}
// Box2D automatically removes a body's fixtures and joints. Retained dependent
// wrappers check that shared Body before accessing their native pointers.
void body_destroy(scua_ctx *ctx)
{
    arity(ctx, 1);
    auto b = objectArgument<Body>(ctx, 0, ObjectKind::Body);
    if (b && (b->owner->world.IsLocked() || b->owner->querying || b->owner->stepping))
        throw std::invalid_argument("physics world is locked during contact callbacks");
    if (b && b->body)
    {
        for (auto &slot : b->owner->jointStates)
            if (slot->joint && (slot->joint->GetBodyA() == b->body || slot->joint->GetBodyB() == b->body))
                destroyJoint(b->owner, slot);
        for (auto it = b->owner->colliderStates.begin(); it != b->owner->colliderStates.end();)
            if (it->second->body == b->id)
            {
                it->second->fixture = nullptr;
                it = b->owner->colliderStates.erase(it);
            }
            else
                ++it;
        b->owner->world.DestroyBody(b->body);
        b->body = nullptr;
        b->owner->bodyStates.erase(b->id);
    }
}
void collider_destroy(scua_ctx *ctx)
{
    arity(ctx, 1);
    auto c = objectArgument<Collider>(ctx, 0, ObjectKind::Collider);
    if (c && (c->owner->owner->world.IsLocked() || c->owner->owner->querying || c->owner->owner->stepping))
        throw std::invalid_argument("physics world is locked during contact callbacks");
    if (c && c->fixture)
    {
        if (c->owner->body)
            c->owner->body->DestroyFixture(c->fixture);
        c->fixture = nullptr;
        c->owner->owner->colliderStates.erase(c->id);
        c->owner->state->customMass = false;
    }
}
void joint_destroy(scua_ctx *ctx)
{
    arity(ctx, 1);
    auto j = objectArgument<Joint>(ctx, 0, ObjectKind::Joint);
    if (j && (j->a->owner->world.IsLocked() || j->a->owner->querying || j->a->owner->stepping))
        throw std::invalid_argument("physics world is locked during contact callbacks");
    if (j && j->state->joint)
    {
        destroyJoint(j->a->owner, j->state);
    }
}
void body_is_destroyed(scua_ctx *ctx)
{
    arity(ctx, 1);
    auto b = objectArgument<Body>(ctx, 0, ObjectKind::Body);
    if (b)
        scua_return_bool(ctx, b->body == nullptr);
}
void collider_is_destroyed(scua_ctx *ctx)
{
    arity(ctx, 1);
    auto c = objectArgument<Collider>(ctx, 0, ObjectKind::Collider);
    if (c)
        scua_return_bool(ctx, !c->fixture || !c->owner->body);
}
void joint_is_destroyed(scua_ctx *ctx)
{
    arity(ctx, 1);
    auto j = objectArgument<Joint>(ctx, 0, ObjectKind::Joint);
    if (j)
        scua_return_bool(ctx, !j->state->joint || !j->a->body || !j->b->body);
}
void world_contacts(scua_ctx *ctx)
{
    arity(ctx, 1);
    auto w = world(ctx);
    if (!w)
        return;
    auto p = scua_ctx_partition(ctx);
    auto array = scua_new_array(p, 0);
    int count = 0;
    for (auto c = w->world.GetContactList(); c; c = c->GetNext())
    {
        if (!c->IsTouching())
            continue;
        if (++count > 4096)
            throw std::invalid_argument("contact result limit is 4096");
        auto a = c->GetFixtureA()->GetBody()->GetUserData().pointer;
        auto b = c->GetFixtureB()->GetBody()->GetUserData().pointer;
        Record r(ctx);
        r.integer("body_a", a);
        r.integer("body_b", b);
        r.integer("collider_a", c->GetFixtureA()->GetUserData().pointer);
        r.integer("collider_b", c->GetFixtureB()->GetUserData().pointer);
        r.boolean("touching", true);
        if (scua_array_push(p, array, r.value) != SCUA_OK)
            throw std::runtime_error("could not create contacts");
    }
    scua_return_value(ctx, array);
}
void world_raycast(scua_ctx *ctx)
{
    arity(ctx, 3);
    auto w = world(ctx);
    if (!w)
        return;
    auto from = vec(ctx, 1, w->meter), to = vec(ctx, 2, w->meter);
    if ((to - from).LengthSquared() < 1e-12)
        throw std::invalid_argument("ray endpoints must differ");
    struct Ray : b2RayCastCallback
    {
        b2Fixture *fixture = nullptr;
        b2Vec2 point, normal;
        float fraction = 1;
        float ReportFixture(b2Fixture *f, const b2Vec2 &p, const b2Vec2 &n, float t) override
        {
            fixture = f;
            point = p;
            normal = n;
            fraction = t;
            return t;
        }
    } ray;
    w->world.RayCast(&ray, from, to);
    if (!ray.fixture)
    {
        scua_return_value(ctx, scua_nil(scua_ctx_partition(ctx)));
        return;
    }
    Record r(ctx);
    r.integer("body", ray.fixture->GetBody()->GetUserData().pointer);
    r.integer("collider", ray.fixture->GetUserData().pointer);
    r.boolean("sensor", ray.fixture->IsSensor());
    r.num("fraction", ray.fraction);
    float point[] = {ray.point.x * w->meter, ray.point.y * w->meter}, normal[] = {ray.normal.x, ray.normal.y};
    r.set("point", scua_vec(r.p, point, 2));
    r.set("normal", scua_vec(r.p, normal, 2));
    r.done();
}

void collider_id(scua_ctx *ctx)
{
    arity(ctx, 1);
    auto c = collider(ctx);
    if (c)
        scua_return_int(ctx, c->id);
}
void body_info(scua_ctx *ctx)
{
    arity(ctx, 1);
    auto b = body(ctx);
    if (!b)
        return;
    auto native = b->body;
    float meter = b->owner->meter;
    Record r(ctx);
    r.integer("id", b->id);
    r.integer("world", b->owner->id);
    r.str("kind", native->GetType() == b2_dynamicBody     ? "dynamic"
                  : native->GetType() == b2_kinematicBody ? "kinematic"
                                                          : "static");
    r.boolean("custom_mass", b->state->customMass);
    r.num("angle", native->GetAngle());
    r.num("angular_velocity", native->GetAngularVelocity());
    for (auto field : {"position", "velocity", "local_center"})
    {
        auto v = std::strcmp(field, "position") == 0   ? native->GetPosition()
                 : std::strcmp(field, "velocity") == 0 ? native->GetLinearVelocity()
                                                       : native->GetLocalCenter();
        float xy[] = {v.x * meter, v.y * meter};
        r.set(field, scua_vec(r.p, xy, 2));
    }
    r.num("mass", native->GetMass());
    r.num("inertia", native->GetInertia() * double(meter) * meter);
    r.num("gravity_scale", native->GetGravityScale());
    r.num("linear_damping", native->GetLinearDamping());
    r.num("angular_damping", native->GetAngularDamping());
    r.boolean("awake", native->IsAwake());
    r.boolean("enabled", native->IsEnabled());
    r.boolean("bullet", native->IsBullet());
    r.boolean("fixed_rotation", native->IsFixedRotation());
    r.boolean("sleeping_allowed", native->IsSleepingAllowed());
    auto center = native->GetWorldCenter();
    float v[] = {center.x * meter, center.y * meter};
    r.set("center", scua_vec(r.p, v, 2));
    r.done();
}
void body_set_gravity_scale(scua_ctx *ctx)
{
    arity(ctx, 2);
    float scale = scalar(ctx, 1, -100, 100);
    auto b = body(ctx);
    if (b)
        b->body->SetGravityScale(scale);
}
void body_local_point(scua_ctx *ctx)
{
    arity(ctx, 2);
    auto b = body(ctx);
    if (b)
    {
        auto v = b->body->GetLocalPoint(vec(ctx, 1, b->owner->meter));
        returnVec(ctx, v.x * b->owner->meter, v.y * b->owner->meter);
    }
}
void body_velocity_at_point(scua_ctx *ctx)
{
    arity(ctx, 2);
    auto b = body(ctx);
    if (b)
    {
        auto v = b->body->GetLinearVelocityFromWorldPoint(vec(ctx, 1, b->owner->meter));
        returnVec(ctx, v.x * b->owner->meter, v.y * b->owner->meter);
    }
}
void body_apply_force_at(scua_ctx *ctx)
{
    arity(ctx, 3);
    auto b = body(ctx);
    if (b)
    {
        auto force = vec(ctx, 1, b->owner->meter), point = vec(ctx, 2, b->owner->meter);
        b->body->ApplyForce(force, point, true);
    }
}
void body_apply_impulse_at(scua_ctx *ctx)
{
    arity(ctx, 3);
    auto b = body(ctx);
    if (b)
    {
        auto impulse = vec(ctx, 1, b->owner->meter), point = vec(ctx, 2, b->owner->meter);
        b->body->ApplyLinearImpulse(impulse, point, true);
    }
}
void body_apply_torque(scua_ctx *ctx)
{
    arity(ctx, 2);
    float torque = scalar(ctx, 1);
    auto b = body(ctx);
    if (b)
        b->body->ApplyTorque(torque / (b->owner->meter * b->owner->meter), true);
}
void body_apply_angular_impulse(scua_ctx *ctx)
{
    arity(ctx, 2);
    float impulse = scalar(ctx, 1);
    auto b = body(ctx);
    if (b)
        b->body->ApplyAngularImpulse(impulse / (b->owner->meter * b->owner->meter), true);
}
void world_query(scua_ctx *ctx)
{
    arity(ctx, 3);
    auto w = world(ctx);
    if (!w)
        return;
    auto lower = vec(ctx, 1, w->meter), upper = vec(ctx, 2, w->meter);
    if (lower.x > upper.x || lower.y > upper.y)
        throw std::invalid_argument("query lower bounds must not exceed upper bounds");
    struct Query : b2QueryCallback
    {
        std::vector<b2Fixture *> fixtures;
        bool overflow = false;
        bool ReportFixture(b2Fixture *f) override
        {
            if (std::find(fixtures.begin(), fixtures.end(), f) != fixtures.end())
                return true;
            if (fixtures.size() == 4096)
            {
                overflow = true;
                return false;
            }
            fixtures.push_back(f);
            return true;
        }
    } query;
    b2AABB bounds;
    bounds.lowerBound = lower;
    bounds.upperBound = upper;
    w->world.QueryAABB(&query, bounds);
    if (query.overflow)
        throw std::invalid_argument("query result limit is 4096");
    std::sort(query.fixtures.begin(), query.fixtures.end(),
              [](auto a, auto b) { return a->GetUserData().pointer < b->GetUserData().pointer; });
    auto p = scua_ctx_partition(ctx);
    auto results = scua_new_array(p, 0);
    for (auto f : query.fixtures)
    {
        Record r(ctx);
        r.integer("body", f->GetBody()->GetUserData().pointer);
        r.integer("collider", f->GetUserData().pointer);
        r.boolean("sensor", f->IsSensor());
        if (scua_array_push(p, results, r.value) != SCUA_OK)
            throw std::runtime_error("could not build query results");
    }
    scua_return_value(ctx, results);
}
void world_raycast_all(scua_ctx *ctx)
{
    arity(ctx, 3);
    auto w = world(ctx);
    if (!w)
        return;
    auto from = vec(ctx, 1, w->meter), to = vec(ctx, 2, w->meter);
    if ((to - from).LengthSquared() < 1e-12)
        throw std::invalid_argument("ray endpoints must differ");
    struct Hit
    {
        b2Fixture *fixture;
        b2Vec2 point, normal;
        float fraction;
    };
    struct Ray : b2RayCastCallback
    {
        std::vector<Hit> hits;
        bool overflow = false;
        float ReportFixture(b2Fixture *f, const b2Vec2 &p, const b2Vec2 &n, float t) override
        {
            if (hits.size() == 4096)
            {
                overflow = true;
                return 0;
            }
            hits.push_back({f, p, n, t});
            return 1;
        }
    } ray;
    w->world.RayCast(&ray, from, to);
    if (ray.overflow)
        throw std::invalid_argument("ray result limit is 4096");
    std::sort(ray.hits.begin(), ray.hits.end(),
              [](const Hit &a, const Hit &b)
              {
                  if (a.fraction != b.fraction)
                      return a.fraction < b.fraction;
                  return a.fixture->GetUserData().pointer < b.fixture->GetUserData().pointer;
              });
    auto p = scua_ctx_partition(ctx);
    auto results = scua_new_array(p, 0);
    for (auto &hit : ray.hits)
    {
        Record r(ctx);
        r.integer("body", hit.fixture->GetBody()->GetUserData().pointer);
        r.integer("collider", hit.fixture->GetUserData().pointer);
        r.boolean("sensor", hit.fixture->IsSensor());
        r.num("fraction", hit.fraction);
        float point[] = {hit.point.x * w->meter, hit.point.y * w->meter},
              normal[] = {hit.normal.x, hit.normal.y};
        r.set("point", scua_vec(p, point, 2));
        r.set("normal", scua_vec(p, normal, 2));
        if (scua_array_push(p, results, r.value) != SCUA_OK)
            throw std::runtime_error("could not build ray results");
    }
    scua_return_value(ctx, results);
}

// Spring conversion uses reduced mass/inertia, matching the bundled Box2D.
// Angular coefficients use pixel-squared units, like joint_state/configure.
void spring_parameters(scua_ctx *c)
{
    if (scua_arg_count(c) < 2 || scua_arg_count(c) > 3)
        throw std::invalid_argument("expected body, settings and optional second body");
    auto a = body(c);
    auto b = supplied(c, 2) ? body(c, 2) : nullptr;
    if (!a || (supplied(c, 2) && !b))
        return;
    if (b && a->owner != b->owner)
        throw std::invalid_argument("spring bodies must share a world");
    Options o(c, 1);
    bool angular = o.boolean("angular", false);
    double meter = a->owner->meter;
    double ma = angular ? a->body->GetInertia() * meter * meter : a->body->GetMass();
    double mb = b ? (angular ? b->body->GetInertia() * meter * meter : b->body->GetMass()) : 0;
    double mass = ma > 0 && mb > 0 ? ma * mb / (ma + mb) : (ma > 0 ? ma : mb);
    double frequency = 0, ratio = 0, stiffness = 0, damping = 0;
    bool inverse = o.has("stiffness") || o.has("damping");
    if (inverse && (o.has("frequency") || o.has("damping_ratio")))
        throw std::invalid_argument("choose frequency/ratio or stiffness/damping");
    if (inverse)
    {
        stiffness = o.num("stiffness", 0);
        damping = o.num("damping", 0);
        if (stiffness < 0 || damping < 0)
            throw std::invalid_argument("spring coefficients must be nonnegative");
        if (mass > 0 && stiffness > 0)
        {
            double omega = std::sqrt(stiffness / mass);
            frequency = omega / (2 * b2_pi);
            ratio = damping / (2 * mass * omega);
        }
    }
    else
    {
        frequency = o.num("frequency", 0);
        ratio = o.num("damping_ratio", 0);
        if (frequency < 0 || frequency > 100000 || ratio < 0 || ratio > 100000)
            throw std::invalid_argument("spring frequency/ratio outside supported range");
        double omega = 2 * b2_pi * frequency;
        stiffness = mass * omega * omega;
        damping = 2 * mass * ratio * omega;
    }
    if (!std::isfinite(stiffness) || !std::isfinite(damping) || !std::isfinite(frequency) ||
        !std::isfinite(ratio) || stiffness > std::numeric_limits<float>::max() ||
        damping > std::numeric_limits<float>::max())
        throw std::invalid_argument("spring parameters overflow");
    Record r(c);
    r.num("frequency", frequency);
    r.num("damping_ratio", ratio);
    r.num("stiffness", stiffness);
    r.num("damping", damping);
    r.boolean("angular", angular);
    r.done();
}

#include "physics_extra.inc"
#include "physics_joints.inc"
} // namespace
void registerPhysics(scua_partition *p, Host &state)
{
    for (auto kind : {ObjectKind::World, ObjectKind::Body, ObjectKind::Collider, ObjectKind::Joint})
        registerObjectType(p, state, kind);
    registerPhysicsExtra(p, state);
    registerPhysicsJoints(p, state);
    registerFunctions(
        p, state,
        {
            {"love_physics", "spring_parameters", guarded<spring_parameters>,
             "spring_parameters(body: any, settings: any, other_body: any = nil) -> any",
             "Convert frequency/damping_ratio to stiffness/damping, or the inverse; angular=true uses "
             "pixel-squared inertia."},
            {"love_physics", "new_prismatic_joint", guarded<new_prismatic_joint>,
             "new_prismatic_joint(body_a: any, body_b: any, anchor: vec2, axis: vec2) -> any",
             "new prismatic joint. Angles use radians and linear quantities use drawing pixels."},
            {"love_physics", "new_wheel_joint", guarded<new_wheel_joint>,
             "new_wheel_joint(body_a: any, body_b: any, anchor: vec2, axis: vec2) -> any",
             "new wheel joint. Angles use radians and linear quantities use drawing pixels."},
            {"love_physics", "new_weld_joint", guarded<new_weld_joint>,
             "new_weld_joint(body_a: any, body_b: any, anchor: vec2) -> any",
             "new weld joint. Angles use radians and linear quantities use drawing pixels."},
            {"love_physics", "new_friction_joint", guarded<new_friction_joint>,
             "new_friction_joint(body_a: any, body_b: any, anchor: vec2) -> any",
             "new friction joint. Angles use radians and linear quantities use drawing pixels."},
            {"love_physics", "new_motor_joint", guarded<new_motor_joint>,
             "new_motor_joint(body_a: any, body_b: any, correction: float) -> any",
             "new motor joint. Angles use radians and linear quantities use drawing pixels."},
            {"love_physics", "new_mouse_joint", guarded<new_mouse_joint>,
             "new_mouse_joint(body_a: any, body_b: any, target: vec2, force: float) -> any",
             "new mouse joint. Angles use radians and linear quantities use drawing pixels."},
            {"love_physics", "new_pulley_joint", guarded<new_pulley_joint>,
             "new_pulley_joint(body_a: any, body_b: any, ground_a: vec2, ground_b: vec2, anchor_a: vec2, "
             "anchor_b: vec2, ratio: float) -> any",
             "new pulley joint. Angles use radians and linear quantities use drawing pixels."},
            {"love_physics", "new_rope_joint", guarded<new_rope_joint>,
             "new_rope_joint(body_a: any, body_b: any, anchor_a: vec2, anchor_b: vec2, length: float) -> any",
             "new rope joint. Angles use radians and linear quantities use drawing pixels."},
            {"love_physics", "new_gear_joint", guarded<new_gear_joint>,
             "new_gear_joint(joint_a: any, joint_b: any, ratio: float, collide_connected: bool = false) -> "
             "any",
             "new gear joint. Angles use radians and linear quantities use drawing pixels."},
            {"love_physics", "joint_info", guarded<joint_info>, "joint_info(joint: any) -> any",
             "joint info. Angles use radians and linear quantities use drawing pixels."},
            {"love_physics", "joint_reaction", guarded<joint_reaction>,
             "joint_reaction(joint: any, dt: float) -> vec3",
             "joint reaction. Angles use radians and linear quantities use drawing pixels."},
            {"love_physics", "joint_set_motor", guarded<joint_set_motor>,
             "joint_set_motor(joint: any, enabled: bool, speed: float, maximum: float)",
             "joint set motor. Angles use radians and linear quantities use drawing pixels."},
            {"love_physics", "joint_set_limits", guarded<joint_set_limits>,
             "joint_set_limits(joint: any, enabled: bool, minimum: float, maximum: float)",
             "joint set limits. Angles use radians and linear quantities use drawing pixels."},
            {"love_physics", "joint_set_spring", guarded<joint_set_spring>,
             "joint_set_spring(joint: any, frequency: float, damping: float)",
             "joint set spring. Angles use radians and linear quantities use drawing pixels."},
            {"love_physics", "joint_set_target", guarded<joint_set_target>,
             "joint_set_target(joint: any, target: vec2)",
             "joint set target. Angles use radians and linear quantities use drawing pixels."},
            {"love_physics", "joint_set_force_limits", guarded<joint_set_force_limits>,
             "joint_set_force_limits(joint: any, force: float, torque: float)",
             "joint set force limits. Angles use radians and linear quantities use drawing pixels."},
            {"love_physics", "joint_set_offsets", guarded<joint_set_offsets>,
             "joint_set_offsets(joint: any, offset: vec2, angle: float)",
             "joint set offsets. Angles use radians and linear quantities use drawing pixels."},
            {"love_physics", "joint_set_ratio", guarded<joint_set_ratio>,
             "joint_set_ratio(joint: any, ratio: float)",
             "joint set ratio. Angles use radians and linear quantities use drawing pixels."},
            {"love_physics", "new_polygon", guarded<new_polygon>,
             "new_polygon(body: any, vertices: any, density: float) -> any", "new polygon."},
            {"love_physics", "new_edge", guarded<new_edge>,
             "new_edge(body: any, start: vec2, finish: vec2) -> any", "new edge."},
            {"love_physics", "new_chain", guarded<new_chain>,
             "new_chain(body: any, vertices: any, loop: bool) -> any", "new chain."},
            {"love_physics", "collider_set_density", guarded<collider_set_density>,
             "collider_set_density(collider: any, density: float)", "collider set density."},
            {"love_physics", "collider_info", guarded<collider_info>, "collider_info(collider: any) -> any",
             "collider info."},
            {"love_physics", "body_set_type", guarded<body_set_type>,
             "body_set_type(body: any, kind: string)", "body set type."},
            {"love_physics", "body_type", guarded<body_type>, "body_type(body: any) -> string", "body type."},
            {"love_physics", "body_set_enabled", guarded<body_set_enabled>,
             "body_set_enabled(body: any, enabled: bool)", "body set enabled."},
            {"love_physics", "body_is_enabled", guarded<body_is_enabled>,
             "body_is_enabled(body: any) -> bool", "body is enabled."},
            {"love_physics", "body_destroy", guarded<body_destroy>, "body_destroy(body: any)",
             "Destroy a native physics object or query its state; closing a handle alone leaves it in the "
             "world."},
            {"love_physics", "collider_destroy", guarded<collider_destroy>, "collider_destroy(collider: any)",
             "Destroy a native physics object or query its state; closing a handle alone leaves it in the "
             "world."},
            {"love_physics", "joint_destroy", guarded<joint_destroy>, "joint_destroy(joint: any)",
             "Destroy a native physics object or query its state; closing a handle alone leaves it in the "
             "world."},
            {"love_physics", "body_is_destroyed", guarded<body_is_destroyed>,
             "body_is_destroyed(body: any) -> bool",
             "Destroy a native physics object or query its state; closing a handle alone leaves it in the "
             "world."},
            {"love_physics", "collider_is_destroyed", guarded<collider_is_destroyed>,
             "collider_is_destroyed(collider: any) -> bool",
             "Destroy a native physics object or query its state; closing a handle alone leaves it in the "
             "world."},
            {"love_physics", "joint_is_destroyed", guarded<joint_is_destroyed>,
             "joint_is_destroyed(joint: any) -> bool",
             "Destroy a native physics object or query its state; closing a handle alone leaves it in the "
             "world."},
            {"love_physics", "new_world", guarded<new_world>,
             "new_world(gravity: vec2, meter: float = 30) -> any",
             "new world. Positions use pixels; one meter defaults to 30 pixels."},
            {"love_physics", "world_step", guarded<world_step>,
             "world_step(world: any, dt: float, velocity_iterations: int = 8, position_iterations: int = 3)",
             "world step. Positions use pixels; one meter defaults to 30 pixels."},
            {"love_physics", "world_stats", guarded<world_stats>, "world_stats(world: any) -> any",
             "world stats. Positions use pixels; one meter defaults to 30 pixels."},
            {"love_physics", "world_gravity", guarded<world_gravity>, "world_gravity(world: any) -> vec2",
             "world gravity. Positions use pixels; one meter defaults to 30 pixels."},
            {"love_physics", "world_set_gravity", guarded<world_set_gravity>,
             "world_set_gravity(world: any, gravity: vec2)",
             "world set gravity. Positions use pixels; one meter defaults to 30 pixels."},
            {"love_physics", "new_body", guarded<new_body>,
             "new_body(world: any, position: vec2, kind: string) -> any",
             "new body. Positions use pixels; one meter defaults to 30 pixels."},
            {"love_physics", "body_id", guarded<body_id>, "body_id(body: any) -> int",
             "body id. Positions use pixels; one meter defaults to 30 pixels."},
            {"love_physics", "body_position", guarded<body_position>, "body_position(body: any) -> vec2",
             "body position. Positions use pixels; one meter defaults to 30 pixels."},
            {"love_physics", "body_set_position", guarded<body_set_position>,
             "body_set_position(body: any, position: vec2)",
             "body set position. Positions use pixels; one meter defaults to 30 pixels."},
            {"love_physics", "body_angle", guarded<body_angle>, "body_angle(body: any) -> float",
             "body angle. Positions use pixels; one meter defaults to 30 pixels."},
            {"love_physics", "body_set_angle", guarded<body_set_angle>,
             "body_set_angle(body: any, radians: float)",
             "body set angle. Positions use pixels; one meter defaults to 30 pixels."},
            {"love_physics", "body_velocity", guarded<body_velocity>, "body_velocity(body: any) -> vec2",
             "body velocity. Positions use pixels; one meter defaults to 30 pixels."},
            {"love_physics", "body_set_velocity", guarded<body_set_velocity>,
             "body_set_velocity(body: any, velocity: vec2)",
             "body set velocity. Positions use pixels; one meter defaults to 30 pixels."},
            {"love_physics", "body_angular_velocity", guarded<body_angular_velocity>,
             "body_angular_velocity(body: any) -> float",
             "body angular velocity. Positions use pixels; one meter defaults to 30 pixels."},
            {"love_physics", "body_set_angular_velocity", guarded<body_set_angular_velocity>,
             "body_set_angular_velocity(body: any, velocity: float)",
             "body set angular velocity. Positions use pixels; one meter defaults to 30 pixels."},
            {"love_physics", "body_mass", guarded<body_mass>, "body_mass(body: any) -> float",
             "body mass. Positions use pixels; one meter defaults to 30 pixels."},
            {"love_physics", "body_apply_force", guarded<body_apply_force>,
             "body_apply_force(body: any, force: vec2)",
             "body apply force. Positions use pixels; one meter defaults to 30 pixels."},
            {"love_physics", "body_apply_impulse", guarded<body_apply_impulse>,
             "body_apply_impulse(body: any, impulse: vec2)",
             "body apply impulse. Positions use pixels; one meter defaults to 30 pixels."},
            {"love_physics", "body_set_damping", guarded<body_set_damping>,
             "body_set_damping(body: any, linear: float, angular: float)",
             "body set damping. Positions use pixels; one meter defaults to 30 pixels."},
            {"love_physics", "body_set_fixed_rotation", guarded<body_set_fixed_rotation>,
             "body_set_fixed_rotation(body: any, enabled: bool)",
             "body set fixed rotation. Positions use pixels; one meter defaults to 30 pixels."},
            {"love_physics", "body_set_bullet", guarded<body_set_bullet>,
             "body_set_bullet(body: any, enabled: bool)",
             "body set bullet. Positions use pixels; one meter defaults to 30 pixels."},
            {"love_physics", "body_set_awake", guarded<body_set_awake>,
             "body_set_awake(body: any, enabled: bool)",
             "body set awake. Positions use pixels; one meter defaults to 30 pixels."},
            {"love_physics", "body_is_awake", guarded<body_is_awake>, "body_is_awake(body: any) -> bool",
             "body is awake. Positions use pixels; one meter defaults to 30 pixels."},
            {"love_physics", "body_world_point", guarded<body_world_point>,
             "body_world_point(body: any, point: vec2) -> vec2",
             "body world point. Positions use pixels; one meter defaults to 30 pixels."},
            {"love_physics", "new_circle", guarded<new_circle>,
             "new_circle(body: any, radius: float, density: float) -> any",
             "new circle. Positions use pixels; one meter defaults to 30 pixels."},
            {"love_physics", "new_box", guarded<new_box>,
             "new_box(body: any, size: vec2, density: float) -> any",
             "new box. Positions use pixels; one meter defaults to 30 pixels."},
            {"love_physics", "collider_set_friction", guarded<collider_set_friction>,
             "collider_set_friction(collider: any, value: float)",
             "collider set friction. Positions use pixels; one meter defaults to 30 pixels."},
            {"love_physics", "collider_set_restitution", guarded<collider_set_restitution>,
             "collider_set_restitution(collider: any, value: float)",
             "collider set restitution. Positions use pixels; one meter defaults to 30 pixels."},
            {"love_physics", "collider_set_sensor", guarded<collider_set_sensor>,
             "collider_set_sensor(collider: any, enabled: bool)",
             "collider set sensor. Positions use pixels; one meter defaults to 30 pixels."},
            {"love_physics", "collider_test_point", guarded<collider_test_point>,
             "collider_test_point(collider: any, point: vec2) -> bool",
             "collider test point. Positions use pixels; one meter defaults to 30 pixels."},
            {"love_physics", "collider_set_filter", guarded<collider_set_filter>,
             "collider_set_filter(collider: any, category: int, mask: int, group: int)",
             "collider set filter. Positions use pixels; one meter defaults to 30 pixels."},
            {"love_physics", "new_distance_joint", guarded<new_distance_joint>,
             "new_distance_joint(body_a: any, body_b: any, anchor_a: vec2, anchor_b: vec2) -> any",
             "new distance joint. Positions use pixels; one meter defaults to 30 pixels."},
            {"love_physics", "new_revolute_joint", guarded<new_revolute_joint>,
             "new_revolute_joint(body_a: any, body_b: any, anchor: vec2) -> any",
             "new revolute joint. Positions use pixels; one meter defaults to 30 pixels."},
            {"love_physics", "world_contacts", guarded<world_contacts>, "world_contacts(world: any) -> any",
             "world contacts. Positions use pixels; one meter defaults to 30 pixels."},
            {"love_physics", "world_raycast", guarded<world_raycast>,
             "world_raycast(world: any, start: vec2, finish: vec2) -> any",
             "world raycast. Positions use pixels; one meter defaults to 30 pixels."},
            {"love_physics", "collider_id", guarded<collider_id>, "collider_id(collider: any) -> int",
             "Get a stable world-local collider ID."},
            {"love_physics", "body_info", guarded<body_info>, "body_info(body: any) -> any",
             "Get body mass, inertia in kg pixels squared, center, damping and flags."},
            {"love_physics", "body_set_gravity_scale", guarded<body_set_gravity_scale>,
             "body_set_gravity_scale(body: any, scale: float)", "Set gravity multiplier in [-100,100]."},
            {"love_physics", "body_local_point", guarded<body_local_point>,
             "body_local_point(body: any, world_point: vec2) -> vec2",
             "Convert world pixels to local coordinates."},
            {"love_physics", "body_velocity_at_point", guarded<body_velocity_at_point>,
             "body_velocity_at_point(body: any, world_point: vec2) -> vec2",
             "Get linear plus rotational point velocity."},
            {"love_physics", "body_apply_force_at", guarded<body_apply_force_at>,
             "body_apply_force_at(body: any, force: vec2, world_point: vec2)",
             "Apply pixel-scaled force at a world point."},
            {"love_physics", "body_apply_impulse_at", guarded<body_apply_impulse_at>,
             "body_apply_impulse_at(body: any, impulse: vec2, world_point: vec2)",
             "Apply pixel-scaled impulse at a world point."},
            {"love_physics", "body_apply_torque", guarded<body_apply_torque>,
             "body_apply_torque(body: any, torque: float)",
             "Apply torque in kg pixels squared per second squared."},
            {"love_physics", "body_apply_angular_impulse", guarded<body_apply_angular_impulse>,
             "body_apply_angular_impulse(body: any, impulse: float)",
             "Apply angular impulse in kg pixels squared per second."},
            {"love_physics", "world_query", guarded<world_query>,
             "world_query(world: any, lower: vec2, upper: vec2) -> any",
             "Snapshot broad-phase AABB candidates sorted by collider ID, maximum 4096."},
            {"love_physics", "world_raycast_all", guarded<world_raycast_all>,
             "world_raycast_all(world: any, start: vec2, finish: vec2) -> any",
             "Snapshot all ray hits sorted by fraction, maximum 4096."},
        });
}
} // namespace lovescua
