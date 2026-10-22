// Native math operations identified by the method-level compatibility audit.
void curve_count(scua_ctx *c)
{
    arity(c, 1);
    if (auto v = curve(c))
        scua_return_int(c, v->getControlPointCount());
}
void curve_derivative(scua_ctx *c)
{
    arity(c, 1);
    if (auto v = curve(c))
        returnObject(c, ObjectKind::Curve, new love::math::BezierCurve(v->getDerivative()));
}
void curve_insert(scua_ctx *c)
{
    arity(c, 3);
    auto v = curve(c);
    if (!v)
        return;
    if (v->getControlPointCount() >= 64)
        throw std::invalid_argument("curve control point limit is 64");
    int i = integer(c, 1, 0, v->getControlPointCount());
    float xy[2];
    vector(c, 2, xy, 2);
    v->insertControlPoint({xy[0], xy[1]}, i);
}
void curve_remove(scua_ctx *c)
{
    arity(c, 2);
    auto v = curve(c);
    if (!v)
        return;
    if (v->getControlPointCount() < 3)
        throw std::invalid_argument("a curve needs at least two points");
    v->removeControlPoint(integer(c, 1, 0, v->getControlPointCount() - 1));
}
void curve_rotate(scua_ctx *c)
{
    arity(c, 3);
    float angle = number(c, 1), xy[2];
    vector(c, 2, xy, 2);
    if (auto v = curve(c))
        v->rotate(angle, {xy[0], xy[1]});
}
void curve_scale(scua_ctx *c)
{
    arity(c, 3);
    float scale = number(c, 1), xy[2];
    vector(c, 2, xy, 2);
    if (std::abs(scale) > 1e6)
        throw std::invalid_argument("curve scale exceeds supported range");
    if (auto v = curve(c))
        v->scale(scale, {xy[0], xy[1]});
}
void curve_render_segment(scua_ctx *c)
{
    arity(c, 4);
    float a = number(c, 1), b = number(c, 2);
    int accuracy = integer(c, 3, 0, 12);
    if (a < 0 || b > 1 || b < a)
        throw std::invalid_argument("invalid curve segment");
    if (auto v = curve(c))
        returnPoints(c, v->renderSegment(a, b, accuracy));
}
void random_seed(scua_ctx *c)
{
    arity(c, 1);
    if (auto r = rng(c))
    {
        auto seed = r->getSeed();
        Record result(c);
        result.integer("low", seed.b32.low);
        result.integer("high", seed.b32.high);
        result.done();
    }
}
void transform_matrix(scua_ctx *c)
{
    arity(c, 1);
    auto v = trans(c);
    if (!v)
        return;
    auto p = scua_ctx_partition(c);
    auto a = scua_new_array(p, 0);
    auto data = v->getMatrix().getElements();
    for (int row = 0; row < 4; ++row)
        for (int col = 0; col < 4; ++col)
            if (scua_array_push(p, a, scua_num(p, data[col * 4 + row])) != SCUA_OK)
                throw std::runtime_error("matrix allocation failed");
    scua_return_value(c, a);
}
void transform_set_matrix(scua_ctx *c)
{
    arity(c, 2);
    auto p = scua_ctx_partition(c);
    auto a = scua_arg(c, 1);
    int kind;
    size_t stride, count;
    if (scua_buffer_info(p, a, &kind, &stride, &count) != SCUA_OK || count != 16)
        throw std::invalid_argument("matrix must have 16 row-major numbers");
    float values[16];
    for (int row = 0; row < 4; ++row)
        for (int col = 0; col < 4; ++col)
        {
            auto x = scua_index(p, a, row * 4 + col);
            int t = scua_typeof(p, x);
            double n = scua_to_num(p, x);
            if ((t != SCUA_TYPE_INT && t != SCUA_TYPE_FLOAT) || !std::isfinite(n) || std::abs(n) > 1e6)
                throw std::invalid_argument("invalid matrix component");
            values[col * 4 + row] = n;
        }
    if (auto v = trans(c))
        v->setMatrix(love::Matrix4(values));
}
void transform_is_affine(scua_ctx *c)
{
    arity(c, 1);
    if (auto t = trans(c))
        scua_return_bool(c, t->getMatrix().isAffine2DTransform());
}
void transform_inverse_of(scua_ctx *c)
{
    arity(c, 2);
    auto t = trans(c);
    auto other = objectArgument<love::math::Transform>(c, 1, ObjectKind::Transform);
    if (t && other)
        t->setMatrix(other->getMatrix().inverse());
}
void transform_configure(scua_ctx *c)
{
    arity(c, 2);
    Options o(c, 1);
    auto vec = [&](const char *name, float fallback)
    {
        love::Vector2 v(fallback, fallback);
        if (o.has(name))
        {
            float xy[2];
            if (scua_to_vec(o.p, o.field(name), xy, 2) != 2 || !std::isfinite(xy[0]) ||
                !std::isfinite(xy[1]) || std::abs(xy[0]) > 1e6 || std::abs(xy[1]) > 1e6)
                throw std::invalid_argument(std::string("invalid ") + name);
            v = {xy[0], xy[1]};
        }
        return v;
    };
    auto p = vec("position", 0), s = vec("scale", 1), origin = vec("origin", 0), shear = vec("shear", 0);
    float rotation = o.num("rotation", 0);
    if (auto t = trans(c))
        t->setTransformation(p.x, p.y, rotation, s.x, s.y, origin.x, origin.y, shear.x, shear.y);
}
void registerMathExtras(scua_partition *p, Host &s)
{
    registerFunctions(
        p, s,
        {{"love_math", "transform_is_affine", guarded<transform_is_affine>,
          "transform_is_affine(transform: any) -> bool", "Test whether a matrix is an affine 2D transform."},
         {"love_math", "transform_inverse_of", guarded<transform_inverse_of>,
          "transform_inverse_of(transform: any, other: any)",
          "Replace a transform with another transform's inverse."},
         {"love_math", "transform_configure", guarded<transform_configure>,
          "transform_configure(transform: any, settings: any)",
          "Replace transformation from position/rotation/scale/origin/shear settings."},
         {"love_math", "curve_count", guarded<curve_count>, "curve_count(curve: any) -> int",
          "Count native curve control points."},
         {"love_math", "curve_derivative", guarded<curve_derivative>, "curve_derivative(curve: any) -> any",
          "Get a retained derivative curve."},
         {"love_math", "curve_insert", guarded<curve_insert>,
          "curve_insert(curve: any, index: int, point: vec2)", "Insert a zero-based curve control point."},
         {"love_math", "curve_remove", guarded<curve_remove>, "curve_remove(curve: any, index: int)",
          "Remove a zero-based curve control point."},
         {"love_math", "curve_rotate", guarded<curve_rotate>,
          "curve_rotate(curve: any, radians: float, center: vec2)", "Rotate curve points around a center."},
         {"love_math", "curve_scale", guarded<curve_scale>,
          "curve_scale(curve: any, factor: float, center: vec2)", "Scale curve points around a center."},
         {"love_math", "curve_render_segment", guarded<curve_render_segment>,
          "curve_render_segment(curve: any, start: float, finish: float, accuracy: int) -> any",
          "Render a bounded native curve segment."},
         {"love_math", "random_seed", guarded<random_seed>, "random_seed(generator: any) -> any",
          "Get exact low/high 32-bit seed words."},
         {"love_math", "transform_matrix", guarded<transform_matrix>,
          "transform_matrix(transform: any) -> any", "Get a flat row-major 4x4 matrix."},
         {"love_math", "transform_set_matrix", guarded<transform_set_matrix>,
          "transform_set_matrix(transform: any, matrix: any)", "Set a checked row-major 4x4 matrix."}});
}
