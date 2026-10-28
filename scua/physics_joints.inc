// Included in physics.cpp: checked joint settings and snapshots.
void jointFields(Joint *j, Record &r, float inverseDt)
{
    auto p = j->state->joint;
    float m = j->a->owner->meter;
    r.integer("id", j->state->id);
    r.integer("world", j->a->owner->id);
    r.boolean("enabled", p->IsEnabled());
    switch (p->GetType())
    {
    case e_distanceJoint:
    {
        auto v = static_cast<b2DistanceJoint *>(p);
        physicsVec(r, "local_anchor_a", v->GetLocalAnchorA(), m);
        physicsVec(r, "local_anchor_b", v->GetLocalAnchorB(), m);
        r.num("length", v->GetLength() * (m));
        r.num("min_length", v->GetMinLength() * (m));
        r.num("max_length", v->GetMaxLength() * (m));
        r.num("current_length", v->GetCurrentLength() * (m));
        r.num("stiffness", v->GetStiffness() * (1));
        r.num("damping", v->GetDamping() * (1));
        break;
    }
    case e_revoluteJoint:
    {
        auto v = static_cast<b2RevoluteJoint *>(p);
        physicsVec(r, "local_anchor_a", v->GetLocalAnchorA(), m);
        physicsVec(r, "local_anchor_b", v->GetLocalAnchorB(), m);
        r.num("reference_angle", v->GetReferenceAngle() * (1));
        r.num("angle", v->GetJointAngle() * (1));
        r.num("speed", v->GetJointSpeed() * (1));
        r.boolean("motor_enabled", v->IsMotorEnabled());
        r.num("motor_speed", v->GetMotorSpeed() * (1));
        r.num("max_motor_torque", v->GetMaxMotorTorque() * (m * m));
        r.boolean("limit_enabled", v->IsLimitEnabled());
        r.num("lower_limit", v->GetLowerLimit() * (1));
        r.num("upper_limit", v->GetUpperLimit() * (1));
        r.num("motor_torque", v->GetMotorTorque(inverseDt) * (m * m));
        break;
    }
    case e_prismaticJoint:
    {
        auto v = static_cast<b2PrismaticJoint *>(p);
        physicsVec(r, "local_anchor_a", v->GetLocalAnchorA(), m);
        physicsVec(r, "local_anchor_b", v->GetLocalAnchorB(), m);
        physicsVec(r, "local_axis", v->GetLocalAxisA(), 1);
        physicsVec(r, "axis", j->a->body->GetWorldVector(v->GetLocalAxisA()), 1);
        r.num("reference_angle", v->GetReferenceAngle() * (1));
        r.num("translation", v->GetJointTranslation() * (m));
        r.num("speed", v->GetJointSpeed() * (m));
        r.boolean("motor_enabled", v->IsMotorEnabled());
        r.num("motor_speed", v->GetMotorSpeed() * (m));
        r.num("max_motor_force", v->GetMaxMotorForce() * (m));
        r.boolean("limit_enabled", v->IsLimitEnabled());
        r.num("lower_limit", v->GetLowerLimit() * (m));
        r.num("upper_limit", v->GetUpperLimit() * (m));
        r.num("motor_force", v->GetMotorForce(inverseDt) * (m));
        break;
    }
    case e_wheelJoint:
    {
        auto v = static_cast<b2WheelJoint *>(p);
        physicsVec(r, "local_anchor_a", v->GetLocalAnchorA(), m);
        physicsVec(r, "local_anchor_b", v->GetLocalAnchorB(), m);
        physicsVec(r, "local_axis", v->GetLocalAxisA(), 1);
        physicsVec(r, "axis", j->a->body->GetWorldVector(v->GetLocalAxisA()), 1);
        r.num("translation", v->GetJointTranslation() * (m));
        r.num("linear_speed", v->GetJointLinearSpeed() * (m));
        r.num("angle", v->GetJointAngle() * (1));
        r.num("angular_speed", v->GetJointAngularSpeed() * (1));
        r.boolean("motor_enabled", v->IsMotorEnabled());
        r.num("motor_speed", v->GetMotorSpeed() * (1));
        r.num("max_motor_torque", v->GetMaxMotorTorque() * (m * m));
        r.boolean("limit_enabled", v->IsLimitEnabled());
        r.num("lower_limit", v->GetLowerLimit() * (m));
        r.num("upper_limit", v->GetUpperLimit() * (m));
        r.num("stiffness", v->GetStiffness() * (1));
        r.num("damping", v->GetDamping() * (1));
        r.num("motor_torque", v->GetMotorTorque(inverseDt) * (m * m));
        break;
    }
    case e_weldJoint:
    {
        auto v = static_cast<b2WeldJoint *>(p);
        physicsVec(r, "local_anchor_a", v->GetLocalAnchorA(), m);
        physicsVec(r, "local_anchor_b", v->GetLocalAnchorB(), m);
        r.num("reference_angle", v->GetReferenceAngle() * (1));
        r.num("stiffness", v->GetStiffness() * (m * m));
        r.num("damping", v->GetDamping() * (m * m));
        break;
    }
    case e_mouseJoint:
    {
        auto v = static_cast<b2MouseJoint *>(p);
        physicsVec(r, "target", v->GetTarget(), m);
        r.num("max_force", v->GetMaxForce() * (m));
        r.num("stiffness", v->GetStiffness() * (1));
        r.num("damping", v->GetDamping() * (1));
        break;
    }
    case e_frictionJoint:
    {
        auto v = static_cast<b2FrictionJoint *>(p);
        physicsVec(r, "local_anchor_a", v->GetLocalAnchorA(), m);
        physicsVec(r, "local_anchor_b", v->GetLocalAnchorB(), m);
        r.num("max_force", v->GetMaxForce() * (m));
        r.num("max_torque", v->GetMaxTorque() * (m * m));
        break;
    }
    case e_motorJoint:
    {
        auto v = static_cast<b2MotorJoint *>(p);
        physicsVec(r, "linear_offset", v->GetLinearOffset(), m);
        r.num("angular_offset", v->GetAngularOffset() * (1));
        r.num("correction_factor", v->GetCorrectionFactor() * (1));
        r.num("max_force", v->GetMaxForce() * (m));
        r.num("max_torque", v->GetMaxTorque() * (m * m));
        break;
    }
    case e_pulleyJoint:
    {
        auto v = static_cast<b2PulleyJoint *>(p);
        physicsVec(r, "local_anchor_a", j->a->body->GetLocalPoint(v->GetAnchorA()), m);
        physicsVec(r, "local_anchor_b", j->b->body->GetLocalPoint(v->GetAnchorB()), m);
        physicsVec(r, "ground_anchor_a", v->GetGroundAnchorA(), m);
        physicsVec(r, "ground_anchor_b", v->GetGroundAnchorB(), m);
        r.num("length_a", v->GetLengthA() * (m));
        r.num("length_b", v->GetLengthB() * (m));
        r.num("current_length_a", v->GetCurrentLengthA() * (m));
        r.num("current_length_b", v->GetCurrentLengthB() * (m));
        r.num("ratio", v->GetRatio() * (1));
        break;
    }
    case e_gearJoint:
    {
        auto v = static_cast<b2GearJoint *>(p);
        r.num("ratio", v->GetRatio() * (1));
        auto source0 = j->state->sources[0].lock();
        r.integer("source_a", source0 ? source0->id : 0);
        auto source1 = j->state->sources[1].lock();
        r.integer("source_b", source1 ? source1->id : 0);
        break;
    }
    default:
        break;
    }
}
void joint_state(scua_ctx *ctx)
{
    if (scua_arg_count(ctx) < 1 || scua_arg_count(ctx) > 2)
        throw std::invalid_argument("joint_state expects a joint and optional dt");
    auto j = joint(ctx);
    if (!j)
        return;
    float inv = supplied(ctx, 1) ? 1 / scalar(ctx, 1, 0.000001, 1) : 0;
    Record r(ctx);
    jointFields(j, r, inv);
    r.done();
}
float jointNumber(const Options &o, const char *name, double fallback, double lo = -1e6, double hi = 1e6)
{
    double n = o.num(name, fallback);
    if (n < lo || n > hi)
        throw std::invalid_argument(std::string(name) + " is outside the supported range");
    return n;
}
void configureJoint(Joint *j, const Options &o, bool creating = false)
{
    auto p = j->state->joint;
    float m = j->a->owner->meter;
    std::vector<std::function<void()>> changes;
    std::set<std::string> allowed;
    switch (p->GetType())
    {
    case e_distanceJoint:
    {
        auto v = static_cast<b2DistanceJoint *>(p);
        allowed.insert("length");
        if (o.has("length"))
        {
            float x = jointNumber(o, "length", 0, 0, 1e6) / (m);
            changes.push_back([=] { v->SetLength(x); });
        }
        allowed.insert("stiffness");
        if (o.has("stiffness"))
        {
            float x = jointNumber(o, "stiffness", 0, 0, 1e6) / (1);
            changes.push_back([=] { v->SetStiffness(x); });
        }
        allowed.insert("damping");
        if (o.has("damping"))
        {
            float x = jointNumber(o, "damping", 0, 0, 1e6) / (1);
            changes.push_back([=] { v->SetDamping(x); });
        }
        allowed.insert("min_length");
        allowed.insert("max_length");
        if (o.has("min_length") || o.has("max_length"))
        {
            float lo = jointNumber(o, "min_length", v->GetMinLength() * (m), 0) / (m);
            float hi = jointNumber(o, "max_length", v->GetMaxLength() * (m), 0) / (m);
            if (hi < lo)
                throw std::invalid_argument("joint limits must be ordered");
            changes.push_back(
                [=]
                {
                    v->SetMinLength(b2_linearSlop);
                    v->SetMaxLength(hi);
                    v->SetMinLength(lo);
                });
        }
        break;
    }
    case e_revoluteJoint:
    {
        auto v = static_cast<b2RevoluteJoint *>(p);
        allowed.insert("motor_enabled");
        if (o.has("motor_enabled"))
        {
            bool x = o.boolean("motor_enabled", false);
            changes.push_back([=] { v->EnableMotor(x); });
        }
        allowed.insert("motor_speed");
        if (o.has("motor_speed"))
        {
            float x = jointNumber(o, "motor_speed", 0, -1e6, 1e6) / (1);
            changes.push_back([=] { v->SetMotorSpeed(x); });
        }
        allowed.insert("max_motor_torque");
        if (o.has("max_motor_torque"))
        {
            float x = jointNumber(o, "max_motor_torque", 0, 0, 1e6) / (m * m);
            changes.push_back([=] { v->SetMaxMotorTorque(x); });
        }
        allowed.insert("limit_enabled");
        if (o.has("limit_enabled"))
        {
            bool x = o.boolean("limit_enabled", false);
            changes.push_back([=] { v->EnableLimit(x); });
        }
        allowed.insert("lower_limit");
        allowed.insert("upper_limit");
        if (o.has("lower_limit") || o.has("upper_limit"))
        {
            float lo = jointNumber(o, "lower_limit", v->GetLowerLimit() * (1), -1e6) / (1);
            float hi = jointNumber(o, "upper_limit", v->GetUpperLimit() * (1), -1e6) / (1);
            if (hi < lo)
                throw std::invalid_argument("joint limits must be ordered");
            changes.push_back([=] { v->SetLimits(lo, hi); });
        }
        break;
    }
    case e_prismaticJoint:
    {
        auto v = static_cast<b2PrismaticJoint *>(p);
        allowed.insert("motor_enabled");
        if (o.has("motor_enabled"))
        {
            bool x = o.boolean("motor_enabled", false);
            changes.push_back([=] { v->EnableMotor(x); });
        }
        allowed.insert("motor_speed");
        if (o.has("motor_speed"))
        {
            float x = jointNumber(o, "motor_speed", 0, -1e6, 1e6) / (m);
            changes.push_back([=] { v->SetMotorSpeed(x); });
        }
        allowed.insert("max_motor_force");
        if (o.has("max_motor_force"))
        {
            float x = jointNumber(o, "max_motor_force", 0, 0, 1e6) / (m);
            changes.push_back([=] { v->SetMaxMotorForce(x); });
        }
        allowed.insert("limit_enabled");
        if (o.has("limit_enabled"))
        {
            bool x = o.boolean("limit_enabled", false);
            changes.push_back([=] { v->EnableLimit(x); });
        }
        allowed.insert("lower_limit");
        allowed.insert("upper_limit");
        if (o.has("lower_limit") || o.has("upper_limit"))
        {
            float lo = jointNumber(o, "lower_limit", v->GetLowerLimit() * (m), -1e6) / (m);
            float hi = jointNumber(o, "upper_limit", v->GetUpperLimit() * (m), -1e6) / (m);
            if (hi < lo)
                throw std::invalid_argument("joint limits must be ordered");
            changes.push_back([=] { v->SetLimits(lo, hi); });
        }
        break;
    }
    case e_wheelJoint:
    {
        auto v = static_cast<b2WheelJoint *>(p);
        allowed.insert("motor_enabled");
        if (o.has("motor_enabled"))
        {
            bool x = o.boolean("motor_enabled", false);
            changes.push_back([=] { v->EnableMotor(x); });
        }
        allowed.insert("motor_speed");
        if (o.has("motor_speed"))
        {
            float x = jointNumber(o, "motor_speed", 0, -1e6, 1e6) / (1);
            changes.push_back([=] { v->SetMotorSpeed(x); });
        }
        allowed.insert("max_motor_torque");
        if (o.has("max_motor_torque"))
        {
            float x = jointNumber(o, "max_motor_torque", 0, 0, 1e6) / (m * m);
            changes.push_back([=] { v->SetMaxMotorTorque(x); });
        }
        allowed.insert("limit_enabled");
        if (o.has("limit_enabled"))
        {
            bool x = o.boolean("limit_enabled", false);
            changes.push_back([=] { v->EnableLimit(x); });
        }
        allowed.insert("stiffness");
        if (o.has("stiffness"))
        {
            float x = jointNumber(o, "stiffness", 0, 0, 1e6) / (1);
            changes.push_back([=] { v->SetStiffness(x); });
        }
        allowed.insert("damping");
        if (o.has("damping"))
        {
            float x = jointNumber(o, "damping", 0, 0, 1e6) / (1);
            changes.push_back([=] { v->SetDamping(x); });
        }
        allowed.insert("lower_limit");
        allowed.insert("upper_limit");
        if (o.has("lower_limit") || o.has("upper_limit"))
        {
            float lo = jointNumber(o, "lower_limit", v->GetLowerLimit() * (m), -1e6) / (m);
            float hi = jointNumber(o, "upper_limit", v->GetUpperLimit() * (m), -1e6) / (m);
            if (hi < lo)
                throw std::invalid_argument("joint limits must be ordered");
            changes.push_back([=] { v->SetLimits(lo, hi); });
        }
        break;
    }
    case e_weldJoint:
    {
        auto v = static_cast<b2WeldJoint *>(p);
        allowed.insert("stiffness");
        if (o.has("stiffness"))
        {
            float x = jointNumber(o, "stiffness", 0, 0, 1e6) / (m * m);
            changes.push_back([=] { v->SetStiffness(x); });
        }
        allowed.insert("damping");
        if (o.has("damping"))
        {
            float x = jointNumber(o, "damping", 0, 0, 1e6) / (m * m);
            changes.push_back([=] { v->SetDamping(x); });
        }
        break;
    }
    case e_mouseJoint:
    {
        auto v = static_cast<b2MouseJoint *>(p);
        allowed.insert("target");
        if (o.has("target"))
        {
            auto x = physicsVectorField(o, "target", v->GetTarget(), m);
            changes.push_back([=] { v->SetTarget(x); });
        }
        allowed.insert("max_force");
        if (o.has("max_force"))
        {
            float x = jointNumber(o, "max_force", 0, 0, 1e6) / (m);
            changes.push_back([=] { v->SetMaxForce(x); });
        }
        allowed.insert("stiffness");
        if (o.has("stiffness"))
        {
            float x = jointNumber(o, "stiffness", 0, 0, 1e6) / (1);
            changes.push_back([=] { v->SetStiffness(x); });
        }
        allowed.insert("damping");
        if (o.has("damping"))
        {
            float x = jointNumber(o, "damping", 0, 0, 1e6) / (1);
            changes.push_back([=] { v->SetDamping(x); });
        }
        break;
    }
    case e_frictionJoint:
    {
        auto v = static_cast<b2FrictionJoint *>(p);
        allowed.insert("max_force");
        if (o.has("max_force"))
        {
            float x = jointNumber(o, "max_force", 0, 0, 1e6) / (m);
            changes.push_back([=] { v->SetMaxForce(x); });
        }
        allowed.insert("max_torque");
        if (o.has("max_torque"))
        {
            float x = jointNumber(o, "max_torque", 0, 0, 1e6) / (m * m);
            changes.push_back([=] { v->SetMaxTorque(x); });
        }
        break;
    }
    case e_motorJoint:
    {
        auto v = static_cast<b2MotorJoint *>(p);
        allowed.insert("linear_offset");
        if (o.has("linear_offset"))
        {
            auto x = physicsVectorField(o, "linear_offset", v->GetLinearOffset(), m);
            changes.push_back([=] { v->SetLinearOffset(x); });
        }
        allowed.insert("angular_offset");
        if (o.has("angular_offset"))
        {
            float x = jointNumber(o, "angular_offset", 0, -1e6, 1e6) / (1);
            changes.push_back([=] { v->SetAngularOffset(x); });
        }
        allowed.insert("correction_factor");
        if (o.has("correction_factor"))
        {
            float x = jointNumber(o, "correction_factor", 0, 0, 1) / (1);
            changes.push_back([=] { v->SetCorrectionFactor(x); });
        }
        allowed.insert("max_force");
        if (o.has("max_force"))
        {
            float x = jointNumber(o, "max_force", 0, 0, 1e6) / (m);
            changes.push_back([=] { v->SetMaxForce(x); });
        }
        allowed.insert("max_torque");
        if (o.has("max_torque"))
        {
            float x = jointNumber(o, "max_torque", 0, 0, 1e6) / (m * m);
            changes.push_back([=] { v->SetMaxTorque(x); });
        }
        break;
    }
    case e_pulleyJoint:
    {
        if (creating)
            allowed.insert("ratio");
        break;
    }
    case e_gearJoint:
    {
        auto v = static_cast<b2GearJoint *>(p);
        allowed.insert("ratio");
        if (o.has("ratio"))
        {
            float x = jointNumber(o, "ratio", 0, -1e6, 1e6) / (1);
            if (std::abs(x) < 0.0001)
                throw std::invalid_argument("gear ratio must be nonzero");
            changes.push_back([=] { v->SetRatio(x); });
        }
        break;
    }
    default:
        break;
    }
    // Known fields for another family are rejected before applying any changes.
    for (auto name :
         {"length", "min_length", "max_length", "stiffness", "damping", "motor_enabled", "motor_speed",
          "max_motor_force", "max_motor_torque", "limit_enabled", "lower_limit", "upper_limit", "target",
          "max_force", "max_torque", "linear_offset", "angular_offset", "correction_factor", "ratio"})
        if (o.has(name) && !allowed.count(name))
            throw std::invalid_argument(std::string(name) + " is not configurable for this joint family");
    for (auto &change : changes)
        change();
    j->a->body->SetAwake(true);
    j->b->body->SetAwake(true);
}
void joint_configure(scua_ctx *ctx)
{
    arity(ctx, 2);
    auto j = joint(ctx);
    if (j)
        configureJoint(j, Options(ctx, 1));
}
void new_joint(scua_ctx *ctx)
{
    arity(ctx, 4);
    auto kind = string(ctx, 0);
    auto a = body(ctx, 1), b = body(ctx, 2);
    if (!a || !b)
        return;
    jointBodies(a, b);
    Options o(ctx, 3);
    float m = a->owner->meter;
    auto anchor = physicsVectorField(o, "anchor", b->body->GetPosition(), m);
    auto aa = physicsVectorField(o, "anchor_a", a->body->GetPosition(), m),
         ab = physicsVectorField(o, "anchor_b", b->body->GetPosition(), m);
    auto axis = physicsVectorField(o, "axis", b2Vec2(1, 0));
    if (axis.LengthSquared() < 1e-12)
        throw std::invalid_argument("joint axis must be nonzero");
    axis.Normalize();
    bool collide = o.boolean("collide_connected", false);
    auto finish = [&](auto &def)
    {
        def.collideConnected = collide;
        auto created = physicsOwn(new Joint(a, b, def));
        // Unpublished objects remove their native joint if option validation fails.
        configureJoint(created.get(), o, true);
        auto raw = created.release();
        if (returnObject(ctx, ObjectKind::Joint, raw))
            raw->published = true;
    };

    if (kind == "distance")
    {
        b2DistanceJointDef def;
        def.Initialize(a->body, b->body, aa, ab);
        def.localAnchorA = physicsVectorField(o, "local_anchor_a", def.localAnchorA, m);
        def.localAnchorB = physicsVectorField(o, "local_anchor_b", def.localAnchorB, m);
        finish(def);
        return;
    }
    if (kind == "rope")
    {
        b2DistanceJointDef def;
        def.Initialize(a->body, b->body, aa, ab);
        def.localAnchorA = physicsVectorField(o, "local_anchor_a", def.localAnchorA, m);
        def.localAnchorB = physicsVectorField(o, "local_anchor_b", def.localAnchorB, m);
        def.minLength = b2_linearSlop;
        def.maxLength = std::max(jointNumber(o, "max_length", (ab - aa).Length() * m, 0) / m, b2_linearSlop);
        def.length = def.maxLength;
        finish(def);
        return;
    }
    if (kind == "revolute")
    {
        b2RevoluteJointDef def;
        def.Initialize(a->body, b->body, anchor);
        def.localAnchorA = physicsVectorField(o, "local_anchor_a", def.localAnchorA, m);
        def.localAnchorB = physicsVectorField(o, "local_anchor_b", def.localAnchorB, m);
        def.referenceAngle = jointNumber(o, "reference_angle", def.referenceAngle);
        finish(def);
        return;
    }
    if (kind == "prismatic")
    {
        b2PrismaticJointDef def;
        def.Initialize(a->body, b->body, anchor, axis);
        def.localAnchorA = physicsVectorField(o, "local_anchor_a", def.localAnchorA, m);
        def.localAnchorB = physicsVectorField(o, "local_anchor_b", def.localAnchorB, m);
        def.referenceAngle = jointNumber(o, "reference_angle", def.referenceAngle);
        finish(def);
        return;
    }
    if (kind == "wheel")
    {
        b2WheelJointDef def;
        def.Initialize(a->body, b->body, anchor, axis);
        def.localAnchorA = physicsVectorField(o, "local_anchor_a", def.localAnchorA, m);
        def.localAnchorB = physicsVectorField(o, "local_anchor_b", def.localAnchorB, m);
        b2LinearStiffness(def.stiffness, def.damping, 5, 0.7, a->body, b->body);
        finish(def);
        return;
    }
    if (kind == "weld")
    {
        b2WeldJointDef def;
        def.Initialize(a->body, b->body, anchor);
        def.localAnchorA = physicsVectorField(o, "local_anchor_a", def.localAnchorA, m);
        def.localAnchorB = physicsVectorField(o, "local_anchor_b", def.localAnchorB, m);
        def.referenceAngle = jointNumber(o, "reference_angle", def.referenceAngle);
        finish(def);
        return;
    }
    if (kind == "friction")
    {
        b2FrictionJointDef def;
        def.Initialize(a->body, b->body, anchor);
        def.localAnchorA = physicsVectorField(o, "local_anchor_a", def.localAnchorA, m);
        def.localAnchorB = physicsVectorField(o, "local_anchor_b", def.localAnchorB, m);
        finish(def);
        return;
    }
    if (kind == "motor")
    {
        b2MotorJointDef def;
        def.Initialize(a->body, b->body);
        finish(def);
        return;
    }
    if (kind == "mouse")
    {
        b2MouseJointDef def;
        if (b->body->GetType() != b2_dynamicBody)
            throw std::invalid_argument("mouse joint body must be dynamic");
        def.bodyA = a->body;
        def.bodyB = b->body;
        def.target = physicsVectorField(o, "target", anchor, m);
        b2LinearStiffness(def.stiffness, def.damping, 5, 0.7, a->body, b->body);
        finish(def);
        return;
    }
    if (kind == "pulley")
    {
        b2PulleyJointDef def;
        auto ga = physicsVectorField(o, "ground_a", aa + b2Vec2(0, -1), m),
             gb = physicsVectorField(o, "ground_b", ab + b2Vec2(0, -1), m);
        if ((aa - ga).LengthSquared() < b2_linearSlop * b2_linearSlop ||
            (ab - gb).LengthSquared() < b2_linearSlop * b2_linearSlop)
            throw std::invalid_argument("pulley segments must have positive length");
        def.Initialize(a->body, b->body, ga, gb, aa, ab, jointNumber(o, "ratio", 1, 0.0001, 10000));
        def.localAnchorA = physicsVectorField(o, "local_anchor_a", def.localAnchorA, m);
        def.localAnchorB = physicsVectorField(o, "local_anchor_b", def.localAnchorB, m);
        finish(def);
        return;
    }
    throw std::invalid_argument("unknown joint family; gear joints use new_gear_joint");
}
void registerPhysicsJoints(scua_partition *p, Host &state)
{
    registerFunctions(p, state,
                      {{"love_physics", "new_joint", guarded<new_joint>,
                        "new_joint(kind: string, body_a: any, body_b: any, settings: any) -> any",
                        "Create a joint from checked anchors, collision, motor, limits and spring settings. "
                        "Gear sources use new_gear_joint."},
                       {"love_physics", "joint_state", guarded<joint_state>,
                        "joint_state(joint: any, dt: float = nil) -> any",
                        "Get family-specific joint properties; dt enables measured motor force/torque."},
                       {"love_physics", "joint_configure", guarded<joint_configure>,
                        "joint_configure(joint: any, settings: any)",
                        "Set family-specific joint properties after validating the entire record."}});
}
