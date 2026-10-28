// Included inside physics.cpp's private namespace: shared state and checked aliases.
ContactState &contactState(World *w, b2Contact *c)
{
    auto old = w->contactIds.find(c);
    if (old != w->contactIds.end())
        return w->contacts.at(old->second);
    if (w->contacts.size() >= 4096)
        throw std::invalid_argument("tracked contact limit is 4096");
    ContactState state;
    state.id = w->nextContactId++;
    state.contact = c;
    state.colliderA = c->GetFixtureA()->GetUserData().pointer;
    state.colliderB = c->GetFixtureB()->GetUserData().pointer;
    state.childA = c->GetChildIndexA();
    state.childB = c->GetChildIndexB();
    auto entry = w->contacts.emplace(state.id, state);
    w->contactIds.emplace(c, state.id);
    return entry.first->second;
}
void retireContact(World *w, b2Contact *c)
{
    auto entry = w->contactIds.find(c);
    if (entry != w->contactIds.end())
    {
        w->contacts.erase(entry->second);
        w->contactIds.erase(entry);
    }
}
void clearContacts(World *w)
{
    w->contactIds.clear();
    w->contacts.clear();
}
b2Contact *liveContact(World *w, const ContactState &s)
{
    for (auto c = w->world.GetContactList(); c; c = c->GetNext())
        if (c == s.contact && c->GetFixtureA()->GetUserData().pointer == s.colliderA &&
            c->GetFixtureB()->GetUserData().pointer == s.colliderB && c->GetChildIndexA() == s.childA &&
            c->GetChildIndexB() == s.childB)
            return c;
    return nullptr;
}
void prepareContacts(World *w)
{
    for (auto it = w->contacts.begin(); it != w->contacts.end();)
    {
        auto live = liveContact(w, it->second);
        if (!live || !live->IsTouching())
        {
            w->contactIds.erase(it->second.contact);
            it = w->contacts.erase(it);
        }
        else
            ++it;
    }
}
void physicsVec(Record &r, const char *name, b2Vec2 value, float scale = 1)
{
    float v[] = {value.x * scale, value.y * scale};
    r.set(name, scua_vec(r.p, v, 2));
}
void physicsPush(scua_partition *p, scua_Handle array, scua_Handle item)
{
    if (scua_array_push(p, array, item) != SCUA_OK)
        throw std::runtime_error("could not construct physics results");
}
Record contactSnapshot(scua_ctx *ctx, World *w, b2Contact *c)
{
    auto &state = contactState(w, c);
    Record r(ctx);
    r.integer("id", state.id);
    r.integer("world", w->id);
    r.integer("body_a", c->GetFixtureA()->GetBody()->GetUserData().pointer);
    r.integer("body_b", c->GetFixtureB()->GetBody()->GetUserData().pointer);
    r.integer("collider_a", state.colliderA);
    r.integer("collider_b", state.colliderB);
    r.integer("child_a", state.childA);
    r.integer("child_b", state.childB);
    r.boolean("touching", c->IsTouching());
    r.boolean("enabled", c->IsEnabled());
    r.num("friction", c->GetFriction());
    r.num("restitution", c->GetRestitution());
    r.num("tangent_speed", c->GetTangentSpeed() * w->meter);
    b2WorldManifold manifold{};
    manifold.normal.SetZero();
    c->GetWorldManifold(&manifold);
    physicsVec(r, "normal", manifold.normal);
    auto points = scua_new_array(r.p, 0);
    for (int i = 0; i < c->GetManifold()->pointCount; i++)
    {
        float v[] = {manifold.points[i].x * w->meter, manifold.points[i].y * w->meter};
        physicsPush(r.p, points, scua_vec(r.p, v, 2));
    }
    r.set("points", points);
    return r;
}
bool ContactFilter::ShouldCollide(b2Fixture *a, b2Fixture *b)
{
    auto fa = a->GetFilterData(), fb = b->GetFilterData();
    if (fa.groupIndex != 0 && fa.groupIndex == fb.groupIndex)
        return fa.groupIndex > 0;
    if (!(fa.maskBits & fb.categoryBits) || !(fa.categoryBits & fb.maskBits))
        return false;
    auto ctx = owner->stepContext;
    if (!ctx || !owner->callbackError.empty() || !owner->callbacksEnabled)
        return true;
    auto p = scua_ctx_partition(ctx);
    auto fn = owner->filterCallback;
    if (!scua_handle_valid(p, fn) || scua_typeof(p, fn) == SCUA_TYPE_NIL)
        return true;
    auto scope = scua_scope_enter(p);
    bool collide = false;
    try
    {
        if (++owner->callbackCount > 4096)
            throw std::invalid_argument("physics callback limit is 4096 per step");
        Record r(ctx);
        r.integer("world", owner->id);
        r.integer("body_a", a->GetBody()->GetUserData().pointer);
        r.integer("body_b", b->GetBody()->GetUserData().pointer);
        r.integer("collider_a", a->GetUserData().pointer);
        r.integer("collider_b", b->GetUserData().pointer);
        r.boolean("sensor_a", a->IsSensor());
        r.boolean("sensor_b", b->IsSensor());
        scua_Handle result{};
        char error[2048]{};
        if (scua_pcall(p, fn, &r.value, 1, &result, error, sizeof(error)) != SCUA_OK)
            throw std::runtime_error(std::string("contactfilter: ") + error);
        if (scua_typeof(p, result) != SCUA_TYPE_BOOL)
            throw std::invalid_argument("contactfilter must return boolean");
        collide = scua_to_bool(p, result);
    }
    catch (const std::exception &e)
    {
        owner->callbackError = e.what();
    }
    catch (...)
    {
        owner->callbackError = "unknown contact filter failure";
    }
    scua_scope_leave(p, scope);
    return collide;
}
template <class T> auto physicsOwn(T *p)
{
    return std::unique_ptr<T, void (*)(T *)>(p,
                                             [](T *v)
                                             {
                                                 if (v)
                                                     v->release();
                                             });
}
uint64_t physicsId(scua_ctx *ctx, int index)
{
    auto p = scua_ctx_partition(ctx);
    if (scua_typeof(p, scua_arg(ctx, index)) != SCUA_TYPE_INT || scua_arg_int(ctx, index) < 1)
        throw std::invalid_argument("physics id must be a positive integer");
    return uint64_t(scua_arg_int(ctx, index));
}
void physicsArrayObject(scua_ctx *ctx, scua_Handle array, ObjectKind kind, love::Object *object)
{
    auto owned = physicsOwn(object);
    auto p = scua_ctx_partition(ctx);
    scua_Handle handle{};
    if (scua_userdata_new(p, host(ctx).objectTypes[size_t(kind)], sizeof(ObjectResource), object, &handle) !=
        SCUA_OK)
        throw std::runtime_error("could not create physics alias");
    ++host(ctx).liveObjects;
    int status = scua_array_push(p, array, handle);
    scua_unpin_global(p, handle);
    if (status != SCUA_OK)
        throw std::runtime_error("could not append physics alias");
}
Body *bodyAlias(World *w, uint64_t id)
{
    auto it = w->bodyStates.find(id);
    return it == w->bodyStates.end() || !it->second->body ? nullptr : new Body(w, it->second);
}
Collider *colliderAlias(World *w, uint64_t id)
{
    auto it = w->colliderStates.find(id);
    if (it == w->colliderStates.end() || !it->second->fixture)
        return nullptr;
    auto b = physicsOwn(bodyAlias(w, it->second->body));
    return b ? new Collider(b.get(), it->second) : nullptr;
}
Joint *jointAlias(World *w, const std::shared_ptr<JointState> &state)
{
    if (!state->joint)
        return nullptr;
    auto a = physicsOwn(bodyAlias(w, state->bodyA)), b = physicsOwn(bodyAlias(w, state->bodyB));
    return a && b ? new Joint(a.get(), b.get(), state) : nullptr;
}
void world_bodies(scua_ctx *ctx)
{
    arity(ctx, 1);
    auto w = world(ctx);
    if (!w)
        return;
    auto p = scua_ctx_partition(ctx);
    auto array = scua_new_array(p, 0);
    for (auto &entry : w->bodyStates)
        if (entry.second->body)
            physicsArrayObject(ctx, array, ObjectKind::Body, new Body(w, entry.second));
    scua_return_value(ctx, array);
}
void world_body(scua_ctx *ctx)
{
    arity(ctx, 2);
    auto id = physicsId(ctx, 1);
    auto w = world(ctx);
    if (!w)
        return;
    auto b = bodyAlias(w, id);
    if (b)
        returnObject(ctx, ObjectKind::Body, b);
    else
        scua_return_value(ctx, scua_nil(scua_ctx_partition(ctx)));
}
void world_collider(scua_ctx *ctx)
{
    arity(ctx, 2);
    auto id = physicsId(ctx, 1);
    auto w = world(ctx);
    if (!w)
        return;
    auto c = colliderAlias(w, id);
    if (c)
        returnObject(ctx, ObjectKind::Collider, c);
    else
        scua_return_value(ctx, scua_nil(scua_ctx_partition(ctx)));
}
void world_joints(scua_ctx *ctx)
{
    arity(ctx, 1);
    auto w = world(ctx);
    if (!w)
        return;
    auto p = scua_ctx_partition(ctx);
    auto array = scua_new_array(p, 0);
    for (auto &state : w->jointStates)
        if (auto j = jointAlias(w, state))
            physicsArrayObject(ctx, array, ObjectKind::Joint, j);
    scua_return_value(ctx, array);
}
void world_joint(scua_ctx *ctx)
{
    arity(ctx, 2);
    auto id = physicsId(ctx, 1);
    auto w = world(ctx);
    if (!w)
        return;
    for (auto &state : w->jointStates)
        if (state->id == id)
            if (auto j = jointAlias(w, state))
            {
                returnObject(ctx, ObjectKind::Joint, j);
                return;
            }
    scua_return_value(ctx, scua_nil(scua_ctx_partition(ctx)));
}
void body_world(scua_ctx *ctx)
{
    arity(ctx, 1);
    auto b = body(ctx);
    if (b)
    {
        b->owner->retain();
        returnObject(ctx, ObjectKind::World, b->owner);
    }
}
void body_colliders(scua_ctx *ctx)
{
    arity(ctx, 1);
    auto b = body(ctx);
    if (!b)
        return;
    auto p = scua_ctx_partition(ctx);
    auto array = scua_new_array(p, 0);
    for (auto &entry : b->owner->colliderStates)
        if (entry.second->body == b->id && entry.second->fixture)
            physicsArrayObject(ctx, array, ObjectKind::Collider, new Collider(b, entry.second));
    scua_return_value(ctx, array);
}
void body_joints(scua_ctx *ctx)
{
    arity(ctx, 1);
    auto b = body(ctx);
    if (!b)
        return;
    auto p = scua_ctx_partition(ctx);
    auto array = scua_new_array(p, 0);
    for (auto &state : b->owner->jointStates)
        if (state->bodyA == b->id || state->bodyB == b->id)
            if (auto j = jointAlias(b->owner, state))
                physicsArrayObject(ctx, array, ObjectKind::Joint, j);
    scua_return_value(ctx, array);
}
void collider_body(scua_ctx *ctx)
{
    arity(ctx, 1);
    auto c = collider(ctx);
    if (c)
    {
        c->owner->retain();
        returnObject(ctx, ObjectKind::Body, c->owner);
    }
}
void joint_bodies(scua_ctx *ctx)
{
    arity(ctx, 1);
    auto j = joint(ctx);
    if (!j)
        return;
    auto p = scua_ctx_partition(ctx);
    auto array = scua_new_array(p, 0);
    j->a->retain();
    physicsArrayObject(ctx, array, ObjectKind::Body, j->a);
    j->b->retain();
    physicsArrayObject(ctx, array, ObjectKind::Body, j->b);
    scua_return_value(ctx, array);
}
void joint_id(scua_ctx *ctx)
{
    arity(ctx, 1);
    auto j = joint(ctx);
    if (j)
        scua_return_int(ctx, j->state->id);
}
void world_destroy(scua_ctx *ctx)
{
    arity(ctx, 1);
    auto w = objectArgument<World>(ctx, 0, ObjectKind::World);
    if (!w || w->destroyed)
        return;
    if (w->world.IsLocked() || w->querying || w->stepping)
        throw std::invalid_argument("physics world is locked during callbacks");
    w->callbacksEnabled = false;
    clearContacts(w);
    for (auto &state : w->jointStates)
        if (state->joint)
            destroyJoint(w, state);
    for (auto &entry : w->colliderStates)
        entry.second->fixture = nullptr;
    while (auto b = w->world.GetBodyList())
        w->world.DestroyBody(b);
    for (auto &entry : w->bodyStates)
        entry.second->body = nullptr;
    w->colliderStates.clear();
    w->bodyStates.clear();
    w->destroyed = true;
}
void world_is_destroyed(scua_ctx *ctx)
{
    arity(ctx, 1);
    auto w = objectArgument<World>(ctx, 0, ObjectKind::World);
    if (w)
        scua_return_bool(ctx, w->destroyed);
}
void world_is_locked(scua_ctx *ctx)
{
    arity(ctx, 1);
    auto w = objectArgument<World>(ctx, 0, ObjectKind::World);
    if (w)
        scua_return_bool(ctx, w->stepping || w->querying || w->world.IsLocked());
}
void world_configure(scua_ctx *ctx)
{
    arity(ctx, 2);
    auto w = world(ctx);
    if (!w)
        return;
    Options o(ctx, 1);
    bool sleeping = o.boolean("sleeping_allowed", w->world.GetAllowSleeping()),
         warm = o.boolean("warm_starting", w->world.GetWarmStarting()),
         continuous = o.boolean("continuous", w->world.GetContinuousPhysics()),
         sub = o.boolean("sub_stepping", w->world.GetSubStepping()),
         clear = o.boolean("auto_clear_forces", w->world.GetAutoClearForces());
    w->world.SetAllowSleeping(sleeping);
    w->world.SetWarmStarting(warm);
    w->world.SetContinuousPhysics(continuous);
    w->world.SetSubStepping(sub);
    w->world.SetAutoClearForces(clear);
}
void world_settings(scua_ctx *ctx)
{
    arity(ctx, 1);
    auto w = world(ctx);
    if (!w)
        return;
    Record r(ctx);
    r.boolean("sleeping_allowed", w->world.GetAllowSleeping());
    r.boolean("warm_starting", w->world.GetWarmStarting());
    r.boolean("continuous", w->world.GetContinuousPhysics());
    r.boolean("sub_stepping", w->world.GetSubStepping());
    r.boolean("auto_clear_forces", w->world.GetAutoClearForces());
    r.integer("proxies", w->world.GetProxyCount());
    r.integer("tree_height", w->world.GetTreeHeight());
    r.integer("tree_balance", w->world.GetTreeBalance());
    r.num("tree_quality", w->world.GetTreeQuality());
    r.done();
}
void world_clear_forces(scua_ctx *ctx)
{
    arity(ctx, 1);
    auto w = world(ctx);
    if (w)
        w->world.ClearForces();
}
void world_shift_origin(scua_ctx *ctx)
{
    arity(ctx, 2);
    auto w = world(ctx);
    if (w)
        w->world.ShiftOrigin(vec(ctx, 1, w->meter));
}
void world_profile(scua_ctx *ctx)
{
    arity(ctx, 1);
    auto w = world(ctx);
    if (!w)
        return;
    auto v = w->world.GetProfile();
    Record r(ctx);
    r.num("step", v.step);
    r.num("collide", v.collide);
    r.num("solve", v.solve);
    r.num("solve_init", v.solveInit);
    r.num("solve_velocity", v.solveVelocity);
    r.num("solve_position", v.solvePosition);
    r.num("broadphase", v.broadphase);
    r.num("solve_toi", v.solveTOI);
    r.done();
}
void body_set_sleeping_allowed(scua_ctx *ctx)
{
    arity(ctx, 2);
    bool enabled = boolean(ctx, 1);
    auto b = body(ctx);
    if (b)
        b->body->SetSleepingAllowed(enabled);
}
void body_set_transform(scua_ctx *ctx)
{
    arity(ctx, 3);
    float angle = scalar(ctx, 2);
    auto b = body(ctx);
    if (b)
        b->body->SetTransform(vec(ctx, 1, b->owner->meter), angle);
}
void body_set_mass_data(scua_ctx *ctx)
{
    arity(ctx, 4);
    float mass = scalar(ctx, 1, 0.000001, 1e6), inertia = scalar(ctx, 3, 0, 1e12);
    auto b = body(ctx);
    if (!b)
        return;
    b2MassData data;
    data.mass = mass;
    data.center = vec(ctx, 2, b->owner->meter);
    data.I = inertia / (double(b->owner->meter) * b->owner->meter);
    if (data.I > 0 && !b->body->IsFixedRotation() && data.I <= mass * b2Dot(data.center, data.center))
        throw std::invalid_argument("inertia must exceed mass times squared local center distance");
    b->body->SetMassData(&data);
    b->state->customMass = b->body->GetType() == b2_dynamicBody;
}
void body_reset_mass_data(scua_ctx *ctx)
{
    arity(ctx, 1);
    auto b = body(ctx);
    if (b)
    {
        b->body->ResetMassData();
        b->state->customMass = false;
    }
}
void body_mass_data(scua_ctx *ctx)
{
    arity(ctx, 1);
    auto b = body(ctx);
    if (!b)
        return;
    b2MassData m;
    b->body->GetMassData(&m);
    Record r(ctx);
    r.num("mass", m.mass);
    r.num("inertia", m.I * double(b->owner->meter) * b->owner->meter);
    physicsVec(r, "center", m.center, b->owner->meter);
    r.boolean("custom", b->state->customMass);
    r.done();
}
template <bool Local> void body_vector(scua_ctx *ctx)
{
    arity(ctx, 2);
    auto b = body(ctx);
    if (!b)
        return;
    auto v = vec(ctx, 1);
    auto out = Local ? b->body->GetLocalVector(v) : b->body->GetWorldVector(v);
    returnVec(ctx, out.x, out.y);
}
void body_local_velocity(scua_ctx *ctx)
{
    arity(ctx, 2);
    auto b = body(ctx);
    if (b)
    {
        auto v = b->body->GetLinearVelocityFromLocalPoint(vec(ctx, 1, b->owner->meter));
        returnVec(ctx, v.x * b->owner->meter, v.y * b->owner->meter);
    }
}
void body_transform_points(scua_ctx *ctx)
{
    if (scua_arg_count(ctx) < 2 || scua_arg_count(ctx) > 3)
        throw std::invalid_argument("expected body, point array and optional local flag");
    auto pointsIn = points(ctx, 1, 0);
    bool local = supplied(ctx, 2) ? boolean(ctx, 2) : false;
    auto b = body(ctx);
    if (!b)
        return;
    auto p = scua_ctx_partition(ctx);
    auto array = scua_new_array(p, 0);
    for (auto point : pointsIn)
    {
        if (std::abs(point.x) > 1e6 || std::abs(point.y) > 1e6)
            throw std::invalid_argument("point exceeds supported range");
        b2Vec2 v(point.x / b->owner->meter, point.y / b->owner->meter);
        auto out = local ? b->body->GetLocalPoint(v) : b->body->GetWorldPoint(v);
        float value[] = {out.x * b->owner->meter, out.y * b->owner->meter};
        physicsPush(p, array, scua_vec(p, value, 2));
    }
    scua_return_value(ctx, array);
}
void body_is_touching(scua_ctx *ctx)
{
    arity(ctx, 2);
    auto a = body(ctx), b = body(ctx, 1);
    if (!a || !b)
        return;
    bool touching = false;
    if (a->owner == b->owner)
        for (auto edge = a->body->GetContactList(); edge; edge = edge->next)
            if (edge->other == b->body && edge->contact->IsTouching())
            {
                touching = true;
                break;
            }
    scua_return_bool(ctx, touching);
}

b2Vec2 physicsVectorField(const Options &o, const char *name, b2Vec2 fallback = {}, float scale = 1)
{
    if (!o.has(name))
        return fallback;
    float v[2];
    if (scua_to_vec(o.p, o.field(name), v, 2) != 2)
        throw std::invalid_argument(std::string(name) + " must be vec2");
    if (!std::isfinite(v[0]) || !std::isfinite(v[1]) || std::abs(v[0]) > 1e6 || std::abs(v[1]) > 1e6)
        throw std::invalid_argument(std::string(name) + " exceeds physics coordinate bounds");
    return {v[0] / scale, v[1] / scale};
}
Record shapeSnapshot(scua_ctx *ctx, World *w, b2Fixture *f)
{
    Record r(ctx);
    auto s = f->GetShape();
    const char *names[] = {"circle", "edge", "polygon", "chain"};
    r.str("kind", names[s->GetType()]);
    r.integer("id", f->GetUserData().pointer);
    r.integer("body", f->GetBody()->GetUserData().pointer);
    r.integer("world", w->id);
    r.num("radius", s->m_radius * w->meter);
    r.integer("children", s->GetChildCount());
    r.num("density", f->GetDensity());
    r.num("friction", f->GetFriction());
    r.num("restitution", f->GetRestitution());
    r.boolean("sensor", f->IsSensor());
    auto filter = f->GetFilterData();
    r.integer("category", filter.categoryBits);
    r.integer("mask", filter.maskBits);
    r.integer("group", filter.groupIndex);
    auto points = scua_new_array(r.p, 0);
    auto push = [&](b2Vec2 point)
    {
        float v[] = {point.x * w->meter, point.y * w->meter};
        physicsPush(r.p, points, scua_vec(r.p, v, 2));
    };
    switch (s->GetType())
    {
    case b2Shape::e_circle:
    {
        auto circle = static_cast<const b2CircleShape *>(s);
        physicsVec(r, "center", circle->m_p, w->meter);
        push(circle->m_p);
        break;
    }
    case b2Shape::e_edge:
    {
        auto edge = static_cast<const b2EdgeShape *>(s);
        push(edge->m_vertex1);
        push(edge->m_vertex2);
        physicsVec(r, "previous", edge->m_vertex0, w->meter);
        physicsVec(r, "next", edge->m_vertex3, w->meter);
        r.boolean("one_sided", edge->m_oneSided);
        break;
    }
    case b2Shape::e_polygon:
    {
        auto polygon = static_cast<const b2PolygonShape *>(s);
        for (int i = 0; i < polygon->m_count; i++)
            push(polygon->m_vertices[i]);
        physicsVec(r, "center", polygon->m_centroid, w->meter);
        r.boolean("valid", polygon->Validate());
        break;
    }
    case b2Shape::e_chain:
    {
        auto chain = static_cast<const b2ChainShape *>(s);
        for (int i = 0; i < chain->m_count; i++)
            push(chain->m_vertices[i]);
        physicsVec(r, "previous", chain->m_prevVertex, w->meter);
        physicsVec(r, "next", chain->m_nextVertex, w->meter);
        r.boolean("loop",
                  chain->m_count > 2 &&
                      (chain->m_vertices[0] - chain->m_vertices[chain->m_count - 1]).LengthSquared() < 1e-12);
        break;
    }
    default:
        break;
    }
    r.set("points", points);
    return r;
}
void body_configure(scua_ctx *ctx)
{
    arity(ctx, 2);
    auto b = body(ctx);
    if (!b)
        return;
    Options o(ctx, 1);
    auto native = b->body;
    float m = b->owner->meter;
    auto position = physicsVectorField(o, "position", native->GetPosition(), m);
    auto velocity = physicsVectorField(o, "velocity", native->GetLinearVelocity(), m);
    double angle = o.num("angle", native->GetAngle());
    double angular = o.num("angular_velocity", native->GetAngularVelocity());
    double linearDamping = o.num("linear_damping", native->GetLinearDamping());
    double angularDamping = o.num("angular_damping", native->GetAngularDamping());
    if (std::abs(angle) > 1e6 || std::abs(angular) > 1e6 || linearDamping < 0 || linearDamping > 1e6 ||
        angularDamping < 0 || angularDamping > 1e6)
        throw std::invalid_argument("invalid body state or damping");
    b2MassData mass;
    native->GetMassData(&mass);
    mass.mass = o.num("mass", mass.mass);
    mass.I = o.num("inertia", mass.I * double(m) * m) / (double(m) * m);
    mass.center = physicsVectorField(o, "local_center", mass.center, m);
    bool custom = o.has("mass") || o.has("inertia") || o.has("local_center");
    if (custom &&
        (mass.mass <= 0 || mass.mass > 1e6 || mass.I < 0 || mass.I * double(m) * m > 1e12 ||
         (mass.I > 0 && !native->IsFixedRotation() && mass.I <= mass.mass * b2Dot(mass.center, mass.center))))
        throw std::invalid_argument("invalid body mass/inertia/local center");
    // Validate every requested field before applying any mutation.
    if (o.has("position") || o.has("angle"))
        native->SetTransform(position, angle);
    if (custom)
    {
        native->SetMassData(&mass);
        b->state->customMass = native->GetType() == b2_dynamicBody;
    }
    if (o.has("velocity"))
        native->SetLinearVelocity(velocity);
    if (o.has("angular_velocity"))
        native->SetAngularVelocity(angular);
    if (o.has("linear_damping"))
        native->SetLinearDamping(linearDamping);
    if (o.has("angular_damping"))
        native->SetAngularDamping(angularDamping);
}
void collider_shape(scua_ctx *ctx)
{
    arity(ctx, 1);
    auto c = collider(ctx);
    if (c)
        shapeSnapshot(ctx, c->owner->owner, c->fixture).done();
}
void collider_configure(scua_ctx *ctx)
{
    arity(ctx, 2);
    auto c = collider(ctx);
    if (!c)
        return;
    Options o(ctx, 1);
    auto f = c->fixture;
    auto shape = f->GetShape();
    auto w = c->owner->owner;
    float friction = o.num("friction", f->GetFriction()),
          restitution = o.num("restitution", f->GetRestitution()),
          density = o.num("density", f->GetDensity());
    bool sensor = o.boolean("sensor", f->IsSensor());
    if (friction < 0 || friction > 1e6 || restitution < 0 || restitution > 1e6 || density < 0 ||
        density > 10000)
        throw std::invalid_argument("invalid collider material");
    auto filter = f->GetFilterData();
    filter.categoryBits = o.integer("category", filter.categoryBits, 0, 65535);
    filter.maskBits = o.integer("mask", filter.maskBits, 0, 65535);
    filter.groupIndex = o.integer("group", filter.groupIndex, -32768, 32767);
    bool geometry = o.has("radius") || o.has("center") || o.has("previous") || o.has("next");
    float radius = shape->m_radius;
    b2Vec2 center{}, previous{}, next{};
    if (o.has("radius") || o.has("center"))
    {
        if (shape->GetType() != b2Shape::e_circle)
            throw std::invalid_argument("radius/center changes require a circle collider");
        auto circle = static_cast<b2CircleShape *>(shape);
        radius = o.num("radius", circle->m_radius * w->meter) / w->meter;
        center = physicsVectorField(o, "center", circle->m_p, w->meter);
        if (radius < b2_linearSlop || radius * w->meter > 1e6)
            throw std::invalid_argument("circle radius exceeds supported bounds/tolerance");
    }
    if (o.has("previous") || o.has("next"))
    {
        if (shape->GetType() == b2Shape::e_edge)
        {
            auto edge = static_cast<b2EdgeShape *>(shape);
            previous = physicsVectorField(o, "previous", edge->m_vertex0, w->meter);
            next = physicsVectorField(o, "next", edge->m_vertex3, w->meter);
        }
        else if (shape->GetType() == b2Shape::e_chain)
        {
            auto chain = static_cast<b2ChainShape *>(shape);
            previous = physicsVectorField(o, "previous", chain->m_prevVertex, w->meter);
            next = physicsVectorField(o, "next", chain->m_nextVertex, w->meter);
        }
        else
            throw std::invalid_argument("adjacent vertices require an edge or chain collider");
    }
    f->SetFriction(friction);
    f->SetRestitution(restitution);
    f->SetSensor(sensor);
    f->SetFilterData(filter);
    if (o.has("density"))
    {
        f->SetDensity(density);
        c->owner->body->ResetMassData();
        c->owner->state->customMass = false;
    }
    if (geometry)
    {
        clearContacts(w);
        if (shape->GetType() == b2Shape::e_circle)
        {
            auto circle = static_cast<b2CircleShape *>(shape);
            circle->m_radius = radius;
            circle->m_p = center;
        }
        else if (shape->GetType() == b2Shape::e_edge)
        {
            auto edge = static_cast<b2EdgeShape *>(shape);
            edge->m_vertex0 = previous;
            edge->m_vertex3 = next;
            edge->m_oneSided = true;
        }
        else
        {
            auto chain = static_cast<b2ChainShape *>(shape);
            chain->m_prevVertex = previous;
            chain->m_nextVertex = next;
        }
        c->owner->body->SetTransform(c->owner->body->GetPosition(), c->owner->body->GetAngle());
        c->owner->body->ResetMassData();
        c->owner->state->customMass = false;
    }
}
void collider_mass(scua_ctx *ctx)
{
    if (scua_arg_count(ctx) < 1 || scua_arg_count(ctx) > 2)
        throw std::invalid_argument("expected collider and optional density");
    auto c = collider(ctx);
    if (!c)
        return;
    float density = supplied(ctx, 1) ? scalar(ctx, 1, 0, 10000) : c->fixture->GetDensity();
    b2MassData data;
    c->fixture->GetShape()->ComputeMass(&data, density);
    Record r(ctx);
    float m = c->owner->owner->meter;
    r.num("mass", data.mass);
    r.num("inertia", data.I * double(m) * m);
    physicsVec(r, "center", data.center, m);
    r.done();
}
int shapeChild(scua_ctx *ctx, b2Shape *s, int index)
{
    return supplied(ctx, index) ? integer(ctx, index, 0, s->GetChildCount() - 1) : 0;
}
void collider_bounds(scua_ctx *ctx)
{
    if (scua_arg_count(ctx) < 1 || scua_arg_count(ctx) > 2)
        throw std::invalid_argument("expected collider and optional child index");
    auto c = collider(ctx);
    if (!c)
        return;
    b2AABB bounds;
    c->fixture->GetShape()->ComputeAABB(&bounds, c->owner->body->GetTransform(),
                                        shapeChild(ctx, c->fixture->GetShape(), 1));
    float m = c->owner->owner->meter;
    float v[] = {bounds.lowerBound.x * m, bounds.lowerBound.y * m, bounds.upperBound.x * m,
                 bounds.upperBound.y * m};
    scua_return_vec(ctx, v, 4);
}
void collider_compute_bounds(scua_ctx *ctx)
{
    if (scua_arg_count(ctx) < 3 || scua_arg_count(ctx) > 4)
        throw std::invalid_argument("expected collider, position, angle and optional child index");
    auto c = collider(ctx);
    if (!c)
        return;
    float m = c->owner->owner->meter;
    b2Transform transform(vec(ctx, 1, m), b2Rot(scalar(ctx, 2)));
    b2AABB bounds;
    c->fixture->GetShape()->ComputeAABB(&bounds, transform, shapeChild(ctx, c->fixture->GetShape(), 3));
    float v[] = {bounds.lowerBound.x * m, bounds.lowerBound.y * m, bounds.upperBound.x * m,
                 bounds.upperBound.y * m};
    scua_return_vec(ctx, v, 4);
}
void collider_raycast(scua_ctx *ctx)
{
    if (scua_arg_count(ctx) < 3 || scua_arg_count(ctx) > 5)
        throw std::invalid_argument("expected collider, ray endpoints, optional max fraction and child");
    auto c = collider(ctx);
    if (!c)
        return;
    float m = c->owner->owner->meter;
    b2RayCastInput input;
    input.p1 = vec(ctx, 1, m);
    input.p2 = vec(ctx, 2, m);
    input.maxFraction = supplied(ctx, 3) ? scalar(ctx, 3, 0, 1) : 1;
    if ((input.p2 - input.p1).LengthSquared() < 1e-12)
        throw std::invalid_argument("ray endpoints must differ");
    b2RayCastOutput hit;
    if (!c->fixture->GetShape()->RayCast(&hit, input, c->owner->body->GetTransform(),
                                         shapeChild(ctx, c->fixture->GetShape(), 4)))
    {
        scua_return_value(ctx, scua_nil(scua_ctx_partition(ctx)));
        return;
    }
    Record r(ctx);
    r.num("fraction", hit.fraction);
    physicsVec(r, "normal", hit.normal);
    physicsVec(r, "point", input.p1 + hit.fraction * (input.p2 - input.p1), m);
    r.done();
}
void collider_distance(scua_ctx *ctx)
{
    if (scua_arg_count(ctx) < 2 || scua_arg_count(ctx) > 4)
        throw std::invalid_argument("expected two colliders and optional child indices");
    auto a = collider(ctx);
    auto b = objectArgument<Collider>(ctx, 1, ObjectKind::Collider);
    if (!a || !b)
        return;
    if (!b->fixture || !b->owner->body)
        throw std::invalid_argument("physics collider is destroyed");
    if (a->owner->owner != b->owner->owner)
        throw std::invalid_argument("distance colliders must belong to the same world");
    b2DistanceInput input;
    input.proxyA.Set(a->fixture->GetShape(), shapeChild(ctx, a->fixture->GetShape(), 2));
    input.proxyB.Set(b->fixture->GetShape(), shapeChild(ctx, b->fixture->GetShape(), 3));
    input.transformA = a->owner->body->GetTransform();
    input.transformB = b->owner->body->GetTransform();
    input.useRadii = true;
    b2SimplexCache cache{};
    b2DistanceOutput result;
    b2Distance(&result, &cache, &input);
    Record r(ctx);
    float m = a->owner->owner->meter;
    r.num("distance", result.distance * m);
    physicsVec(r, "point_a", result.pointA, m);
    physicsVec(r, "point_b", result.pointB, m);
    r.integer("iterations", result.iterations);
    r.done();
}
void collider_child_edge(scua_ctx *ctx)
{
    arity(ctx, 2);
    auto c = collider(ctx);
    if (!c)
        return;
    auto shape = c->fixture->GetShape();
    if (shape->GetType() != b2Shape::e_chain)
        throw std::invalid_argument("child edges require a chain");
    b2EdgeShape edge;
    static_cast<b2ChainShape *>(shape)->GetChildEdge(&edge, integer(ctx, 1, 0, shape->GetChildCount() - 1));
    Record r(ctx);
    float m = c->owner->owner->meter;
    physicsVec(r, "start", edge.m_vertex1, m);
    physicsVec(r, "finish", edge.m_vertex2, m);
    physicsVec(r, "previous", edge.m_vertex0, m);
    physicsVec(r, "next", edge.m_vertex3, m);
    r.boolean("one_sided", edge.m_oneSided);
    r.done();
}

void world_contact_list(scua_ctx *ctx)
{
    if (scua_arg_count(ctx) < 1 || scua_arg_count(ctx) > 2)
        throw std::invalid_argument("expected world and optional non-touching flag");
    bool all = supplied(ctx, 1) ? boolean(ctx, 1) : false;
    auto w = world(ctx);
    if (!w)
        return;
    auto p = scua_ctx_partition(ctx);
    auto array = scua_new_array(p, 0);
    int count = 0;
    for (auto c = w->world.GetContactList(); c; c = c->GetNext())
        if (all || c->IsTouching())
        {
            if (++count > 4096)
                throw std::invalid_argument("contact result limit is 4096");
            auto record = contactSnapshot(ctx, w, c);
            physicsPush(p, array, record.value);
        }
    scua_return_value(ctx, array);
}
void body_contact_list(scua_ctx *ctx)
{
    arity(ctx, 1);
    auto b = body(ctx);
    if (!b)
        return;
    auto p = scua_ctx_partition(ctx);
    auto array = scua_new_array(p, 0);
    int count = 0;
    for (auto edge = b->body->GetContactList(); edge; edge = edge->next)
    {
        if (++count > 4096)
            throw std::invalid_argument("contact result limit is 4096");
        auto record = contactSnapshot(ctx, b->owner, edge->contact);
        physicsPush(p, array, record.value);
    }
    scua_return_value(ctx, array);
}
World *contactWorld(scua_ctx *ctx)
{
    auto w = objectArgument<World>(ctx, 0, ObjectKind::World);
    if (w && w->destroyed)
        throw std::invalid_argument("physics world is destroyed");
    if (w && w->querying)
        throw std::invalid_argument("physics world is locked during query callbacks");
    return w;
}
ContactState *findContact(World *w, uint64_t id)
{
    auto it = w->contacts.find(id);
    if (it == w->contacts.end())
        return nullptr;
    auto live = liveContact(w, it->second);
    if (!live)
    {
        w->contactIds.erase(it->second.contact);
        w->contacts.erase(it);
        return nullptr;
    }
    return &it->second;
}
void contact_info(scua_ctx *ctx)
{
    arity(ctx, 2);
    auto id = physicsId(ctx, 1);
    auto w = contactWorld(ctx);
    if (!w)
        return;
    auto state = findContact(w, id);
    if (state)
    {
        auto r = contactSnapshot(ctx, w, state->contact);
        r.set("enabled_override",
              state->enabled.hasValue ? scua_bool(r.p, state->enabled.value) : scua_nil(r.p));
        r.done();
    }
    else
        scua_return_value(ctx, scua_nil(scua_ctx_partition(ctx)));
}
void contact_set(scua_ctx *ctx)
{
    arity(ctx, 3);
    auto id = physicsId(ctx, 1);
    auto w = contactWorld(ctx);
    if (!w)
        return;
    auto state = findContact(w, id);
    if (!state)
        throw std::invalid_argument("physics contact is no longer alive");
    Options o(ctx, 2);
    float friction = o.num("friction", state->contact->GetFriction()),
          restitution = o.num("restitution", state->contact->GetRestitution()),
          tangent = o.num("tangent_speed", state->contact->GetTangentSpeed() * w->meter);
    if (friction < 0 || friction > 1e6 || restitution < 0 || restitution > 1e6 || std::abs(tangent) > 1e6)
        throw std::invalid_argument("invalid contact material");
    bool resetFriction = o.boolean("reset_friction", false),
         resetRestitution = o.boolean("reset_restitution", false),
         resetEnabled = o.boolean("reset_enabled", false);
    bool enabled = o.boolean("enabled", state->contact->IsEnabled());
    if (resetEnabled)
        state->enabled.hasValue = false;
    if (o.has("enabled"))
    {
        state->enabled = love::OptionalBool(enabled);
        state->contact->SetEnabled(enabled);
    }
    if (o.has("friction"))
        state->contact->SetFriction(friction);
    if (o.has("restitution"))
        state->contact->SetRestitution(restitution);
    if (o.has("tangent_speed"))
        state->contact->SetTangentSpeed(tangent / w->meter);
    if (resetFriction)
        state->contact->ResetFriction();
    if (resetRestitution)
        state->contact->ResetRestitution();
}
void contact_reset(scua_ctx *ctx)
{
    arity(ctx, 2);
    auto id = physicsId(ctx, 1);
    auto w = contactWorld(ctx);
    if (!w)
        return;
    auto state = findContact(w, id);
    if (!state)
        throw std::invalid_argument("physics contact is no longer alive");
    state->enabled.hasValue = false;
    state->contact->SetEnabled(true);
    state->contact->ResetFriction();
    state->contact->ResetRestitution();
    state->contact->SetTangentSpeed(0);
}
void world_set_callbacks_enabled(scua_ctx *ctx)
{
    arity(ctx, 2);
    bool enabled = boolean(ctx, 1);
    auto w = world(ctx);
    if (w)
        w->callbacksEnabled = enabled;
}
struct PhysicsQueryCallback
{
    scua_ctx *ctx;
    World *world;
    scua_Handle fn;
    std::string error;
    int count = 0;
    PhysicsQueryCallback(scua_ctx *c, World *w, int index) : ctx(c), world(w), fn(scua_arg(c, index)) {}
    template <class Build> double invoke(Build build, bool ray)
    {
        auto p = scua_ctx_partition(ctx);
        auto scope = scua_scope_enter(p);
        struct Leave
        {
            scua_partition *p;
            uint32_t scope;
            ~Leave()
            {
                scua_scope_leave(p, scope);
            }
        } leave{p, scope};
        if (++count > 4096)
            throw std::invalid_argument("physics query callback limit is 4096");
        auto record = build();
        scua_Handle result{};
        char message[2048]{};
        if (scua_pcall(p, fn, &record.value, 1, &result, message, sizeof(message)) != SCUA_OK)
            throw std::runtime_error(std::string("physics query callback: ") +
                                     (message[0] ? message : "expected a callable"));
        if (!ray)
        {
            if (scua_typeof(p, result) != SCUA_TYPE_BOOL)
                throw std::invalid_argument("area callback must return boolean");
            return scua_to_bool(p, result) ? 1 : 0;
        }
        if (scua_typeof(p, result) == SCUA_TYPE_NIL)
            return 1;
        int type = scua_typeof(p, result);
        double n = scua_to_num(p, result);
        if ((type != SCUA_TYPE_INT && type != SCUA_TYPE_FLOAT) || !std::isfinite(n) ||
            (n != -1 && (n < 0 || n > 1)))
            throw std::invalid_argument("ray callback must return -1, a fraction in [0,1], or nil");
        return n;
    }
};
struct QueryLock
{
    World *w;
    explicit QueryLock(World *w) : w(w)
    {
        w->querying = true;
    }
    ~QueryLock()
    {
        w->querying = false;
    }
};
void world_query_each(scua_ctx *ctx)
{
    arity(ctx, 4);
    auto w = world(ctx);
    if (!w)
        return;
    auto lo = vec(ctx, 1, w->meter), hi = vec(ctx, 2, w->meter);
    if (lo.x > hi.x || lo.y > hi.y)
        throw std::invalid_argument("query bounds must be ordered");
    struct Query final : b2QueryCallback
    {
        PhysicsQueryCallback callback;
        explicit Query(scua_ctx *c, World *w) : callback(c, w, 3) {}
        bool ReportFixture(b2Fixture *f) override
        {
            try
            {
                return callback.invoke([&] { return shapeSnapshot(callback.ctx, callback.world, f); },
                                       false) != 0;
            }
            catch (const std::exception &e)
            {
                callback.error = e.what();
                return false;
            }
            catch (...)
            {
                callback.error = "unknown area query callback failure";
                return false;
            }
        }
    } query(ctx, w);
    {
        QueryLock lock(w);
        b2AABB bounds;
        bounds.lowerBound = lo;
        bounds.upperBound = hi;
        w->world.QueryAABB(&query, bounds);
    }
    if (!query.callback.error.empty())
        throw std::runtime_error(query.callback.error);
    scua_return_int(ctx, query.callback.count);
}
void world_raycast_each(scua_ctx *ctx)
{
    arity(ctx, 4);
    auto w = world(ctx);
    if (!w)
        return;
    auto from = vec(ctx, 1, w->meter), to = vec(ctx, 2, w->meter);
    if ((to - from).LengthSquared() < 1e-12)
        throw std::invalid_argument("ray endpoints must differ");
    struct Ray final : b2RayCastCallback
    {
        PhysicsQueryCallback callback;
        explicit Ray(scua_ctx *c, World *w) : callback(c, w, 3) {}
        float ReportFixture(b2Fixture *f, const b2Vec2 &point, const b2Vec2 &normal, float fraction) override
        {
            try
            {
                return callback.invoke(
                    [&]
                    {
                        auto r = shapeSnapshot(callback.ctx, callback.world, f);
                        r.num("fraction", fraction);
                        physicsVec(r, "point", point, callback.world->meter);
                        physicsVec(r, "normal", normal);
                        return r;
                    },
                    true);
            }
            catch (const std::exception &e)
            {
                callback.error = e.what();
                return 0;
            }
            catch (...)
            {
                callback.error = "unknown ray query callback failure";
                return 0;
            }
        }
    } ray(ctx, w);
    {
        QueryLock lock(w);
        w->world.RayCast(&ray, from, to);
    }
    if (!ray.callback.error.empty())
        throw std::runtime_error(ray.callback.error);
    scua_return_int(ctx, ray.callback.count);
}

void registerPhysicsExtra(scua_partition *p, Host &state)
{
    registerFunctions(
        p, state,
        {
            {"love_physics", "collider_shape", guarded<collider_shape>,
             "collider_shape(collider: any) -> any",
             "Get shape geometry, adjacency, child count, material and filter data in local pixels."},
            {"love_physics", "collider_configure", guarded<collider_configure>,
             "collider_configure(collider: any, settings: any)",
             "Set checked material/filter fields, circle geometry and edge/chain adjacency."},
            {"love_physics", "collider_mass", guarded<collider_mass>,
             "collider_mass(collider: any, density: float = nil) -> any",
             "Compute shape mass, local center and inertia."},
            {"love_physics", "collider_bounds", guarded<collider_bounds>,
             "collider_bounds(collider: any, child: int = 0) -> vec4", "Compute world-space shape bounds."},
            {"love_physics", "collider_compute_bounds", guarded<collider_compute_bounds>,
             "collider_compute_bounds(collider: any, position: vec2, radians: float, child: int = 0) -> vec4",
             "Compute shape bounds at an explicit transform."},
            {"love_physics", "collider_raycast", guarded<collider_raycast>,
             "collider_raycast(collider: any, start: vec2, finish: vec2, fraction: float = 1, child: int = "
             "0) -> any",
             "Intersect a ray with one shape child."},
            {"love_physics", "collider_distance", guarded<collider_distance>,
             "collider_distance(collider_a: any, collider_b: any, child_a: int = 0, child_b: int = 0) -> any",
             "Get closest shape points and separation in pixels."},
            {"love_physics", "collider_child_edge", guarded<collider_child_edge>,
             "collider_child_edge(collider: any, child: int) -> any",
             "Get a chain child edge as geometry, without creating a fixture."},
            {"love_physics", "world_contact_list", guarded<world_contact_list>,
             "world_contact_list(world: any, non_touching: bool = false) -> any",
             "Get full contact snapshots with stable IDs while touching."},
            {"love_physics", "body_contact_list", guarded<body_contact_list>,
             "body_contact_list(body: any) -> any", "Get full contact snapshots for a body."},
            {"love_physics", "contact_info", guarded<contact_info>,
             "contact_info(world: any, id: int) -> any",
             "Get a live contact snapshot or nil after retirement."},
            {"love_physics", "contact_set", guarded<contact_set>,
             "contact_set(world: any, id: int, settings: any)",
             "Edit contact material and persistent enabled state; valid across steps until retirement."},
            {"love_physics", "contact_reset", guarded<contact_reset>, "contact_reset(world: any, id: int)",
             "Clear contact overrides and restore mixed materials."},
            {"love_physics", "world_set_callbacks_enabled", guarded<world_set_callbacks_enabled>,
             "world_set_callbacks_enabled(world: any, enabled: bool)",
             "Enable/disable this world contact callbacks."},
            {"love_physics", "world_query_each", guarded<world_query_each>,
             "world_query_each(world: any, lower: vec2, upper: vec2, callback: any) -> int",
             "Visit area-query shape snapshots; boolean callback controls continuation."},
            {"love_physics", "world_raycast_each", guarded<world_raycast_each>,
             "world_raycast_each(world: any, start: vec2, finish: vec2, callback: any) -> int",
             "Visit ray-hit snapshots; return -1 to ignore, 0 to stop, a fraction to clip or nil/1 to "
             "continue."},
            {"love_physics", "world_bodies", guarded<world_bodies>, "world_bodies(world: any) -> any",
             "Get retained body aliases in ID order."},
            {"love_physics", "world_body", guarded<world_body>, "world_body(world: any, id: int) -> any",
             "Find a body by world-local ID, or nil."},
            {"love_physics", "world_collider", guarded<world_collider>,
             "world_collider(world: any, id: int) -> any", "Find a collider by world-local ID, or nil."},
            {"love_physics", "world_joints", guarded<world_joints>, "world_joints(world: any) -> any",
             "Get retained joint aliases in ID order."},
            {"love_physics", "world_joint", guarded<world_joint>, "world_joint(world: any, id: int) -> any",
             "Find a joint by world-local ID, or nil."},
            {"love_physics", "body_world", guarded<body_world>, "body_world(body: any) -> any",
             "Get a retained owner-world alias."},
            {"love_physics", "body_colliders", guarded<body_colliders>, "body_colliders(body: any) -> any",
             "Get retained collider aliases."},
            {"love_physics", "body_joints", guarded<body_joints>, "body_joints(body: any) -> any",
             "Get retained attached-joint aliases."},
            {"love_physics", "collider_body", guarded<collider_body>, "collider_body(collider: any) -> any",
             "Get a retained owner-body alias."},
            {"love_physics", "joint_bodies", guarded<joint_bodies>, "joint_bodies(joint: any) -> any",
             "Get retained body A/B aliases."},
            {"love_physics", "joint_id", guarded<joint_id>, "joint_id(joint: any) -> int",
             "Get the world-local joint ID."},
            {"love_physics", "world_destroy", guarded<world_destroy>, "world_destroy(world: any)",
             "Destroy all native children and invalidate every retained alias."},
            {"love_physics", "world_is_destroyed", guarded<world_is_destroyed>,
             "world_is_destroyed(world: any) -> bool", "Check explicit world destruction."},
            {"love_physics", "world_is_locked", guarded<world_is_locked>,
             "world_is_locked(world: any) -> bool",
             "Check step/query callback locking, including early contact filtering."},
            {"love_physics", "world_configure", guarded<world_configure>,
             "world_configure(world: any, settings: any)",
             "Set sleeping, warm starting, continuous physics, sub-stepping and automatic force clearing."},
            {"love_physics", "world_settings", guarded<world_settings>, "world_settings(world: any) -> any",
             "Get solver settings and broadphase tree statistics."},
            {"love_physics", "world_clear_forces", guarded<world_clear_forces>,
             "world_clear_forces(world: any)", "Clear accumulated forces manually."},
            {"love_physics", "world_shift_origin", guarded<world_shift_origin>,
             "world_shift_origin(world: any, offset: vec2)", "Shift the physics origin in pixels."},
            {"love_physics", "world_profile", guarded<world_profile>, "world_profile(world: any) -> any",
             "Get native per-step timings in milliseconds."},
            {"love_physics", "body_set_sleeping_allowed", guarded<body_set_sleeping_allowed>,
             "body_set_sleeping_allowed(body: any, enabled: bool)", "Set per-body sleeping permission."},
            {"love_physics", "body_set_transform", guarded<body_set_transform>,
             "body_set_transform(body: any, position: vec2, radians: float)",
             "Set position and angle together."},
            {"love_physics", "body_set_mass_data", guarded<body_set_mass_data>,
             "body_set_mass_data(body: any, mass: float, center: vec2, inertia: float)",
             "Set checked mass, local center and inertia in pixel units."},
            {"love_physics", "body_reset_mass_data", guarded<body_reset_mass_data>,
             "body_reset_mass_data(body: any)", "Restore mass data from collider densities."},
            {"love_physics", "body_configure", guarded<body_configure>,
             "body_configure(body: any, settings: any)",
             "Validate and set position/angle/velocities, damping and custom mass fields as one serial "
             "operation."},
            {"love_physics", "body_mass_data", guarded<body_mass_data>, "body_mass_data(body: any) -> any",
             "Get mass, local center, inertia and custom-mass state."},
            {"love_physics", "body_local_vector", guarded<body_vector<true>>,
             "body_local_vector(body: any, vector: vec2) -> vec2", "Rotate a world vector into local space."},
            {"love_physics", "body_world_vector", guarded<body_vector<false>>,
             "body_world_vector(body: any, vector: vec2) -> vec2", "Rotate a local vector into world space."},
            {"love_physics", "body_local_velocity", guarded<body_local_velocity>,
             "body_local_velocity(body: any, point: vec2) -> vec2", "Get velocity at a local-space point."},
            {"love_physics", "body_transform_points", guarded<body_transform_points>,
             "body_transform_points(body: any, points: any, local: bool = false) -> any",
             "Transform an array of points between local and world spaces."},
            {"love_physics", "body_is_touching", guarded<body_is_touching>,
             "body_is_touching(body_a: any, body_b: any) -> bool",
             "Check touching contacts between two bodies."},
        });
}
