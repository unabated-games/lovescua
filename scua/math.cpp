// Native LÖVE math with managed generators, curves and transforms.
#include "binding_util.h"
#include "math/BezierCurve.h"
#include "math/MathModule.h"
#include "math/RandomGenerator.h"
#include "math/Transform.h"
namespace lovescua
{
namespace
{
using namespace binding;
auto rng(scua_ctx *ctx)
{
    return objectArgument<love::math::RandomGenerator>(ctx, 0, ObjectKind::Random);
}
auto curve(scua_ctx *ctx)
{
    return objectArgument<love::math::BezierCurve>(ctx, 0, ObjectKind::Curve);
}
auto trans(scua_ctx *ctx)
{
    return objectArgument<love::math::Transform>(ctx, 0, ObjectKind::Transform);
}
void returnPoints(scua_ctx *ctx, const std::vector<love::Vector2> &points)
{
    auto p = scua_ctx_partition(ctx);
    auto a = scua_new_array(p, 0);
    for (auto &point : points)
    {
        float v[] = {point.x, point.y};
        if (scua_array_push(p, a, scua_vec(p, v, 2)) != SCUA_OK)
            throw std::runtime_error("could not create points array");
    }
    scua_return_value(ctx, a);
}
void new_random(scua_ctx *ctx)
{
    arity(ctx, 1);
    if (scua_typeof(scua_ctx_partition(ctx), scua_arg(ctx, 0)) != SCUA_TYPE_INT)
        throw std::invalid_argument("seed must be an integer");
    auto r = new love::math::RandomGenerator();
    r->setSeed({static_cast<uint64_t>(scua_arg_int(ctx, 0))});
    returnObject(ctx, ObjectKind::Random, r);
}
void random(scua_ctx *ctx)
{
    arity(ctx, 1);
    auto r = rng(ctx);
    if (r)
        scua_return_float(ctx, r->random());
}
void random_int(scua_ctx *ctx)
{
    arity(ctx, 3);
    int lo = integer(ctx, 1, INT_MIN, INT_MAX), hi = integer(ctx, 2, INT_MIN, INT_MAX);
    if (hi < lo)
        throw std::invalid_argument("maximum must be >= minimum");
    auto r = rng(ctx);
    if (r)
        scua_return_int(ctx, static_cast<int64_t>(lo) +
                                 static_cast<int64_t>(r->random() * (static_cast<double>(hi) - lo + 1)));
}
void random_normal(scua_ctx *ctx)
{
    arity(ctx, 3);
    auto deviation = number(ctx, 1), mean = number(ctx, 2);
    if (deviation < 0)
        throw std::invalid_argument("deviation must be nonnegative");
    auto r = rng(ctx);
    if (r)
        scua_return_float(ctx, r->randomNormal(deviation) + mean);
}
void random_state(scua_ctx *ctx)
{
    arity(ctx, 1);
    auto r = rng(ctx);
    if (r)
        returnString(ctx, r->getState());
}
void random_set_state(scua_ctx *ctx)
{
    arity(ctx, 2);
    auto state = string(ctx, 1);
    auto r = rng(ctx);
    if (r)
        r->setState(state);
}
void random_set_seed(scua_ctx *ctx)
{
    arity(ctx, 2);
    if (scua_typeof(scua_ctx_partition(ctx), scua_arg(ctx, 1)) != SCUA_TYPE_INT)
        throw std::invalid_argument("seed must be an integer");
    auto r = rng(ctx);
    if (r)
        r->setSeed({static_cast<uint64_t>(scua_arg_int(ctx, 1))});
}
void new_curve(scua_ctx *ctx)
{
    arity(ctx, 1);
    auto pts = points(ctx, 0, 2);
    if (pts.size() > 64)
        throw std::invalid_argument("maximum 64 control points");
    returnObject(ctx, ObjectKind::Curve, new love::math::BezierCurve(pts));
}
void curve_evaluate(scua_ctx *ctx)
{
    arity(ctx, 2);
    auto t = number(ctx, 1);
    auto c = curve(ctx);
    if (c)
    {
        auto v = c->evaluate(t);
        returnVec(ctx, v.x, v.y);
    }
}
void curve_render(scua_ctx *ctx)
{
    arity(ctx, 2);
    int accuracy = integer(ctx, 1, 1, 10);
    auto c = curve(ctx);
    if (c)
        returnPoints(ctx, c->render(accuracy));
}
void curve_degree(scua_ctx *ctx)
{
    arity(ctx, 1);
    auto c = curve(ctx);
    if (c)
        scua_return_int(ctx, c->getDegree());
}
void curve_point(scua_ctx *ctx)
{
    arity(ctx, 2);
    auto c = curve(ctx);
    if (c)
    {
        auto v = c->getControlPoint(integer(ctx, 1, 0, c->getControlPointCount() - 1));
        returnVec(ctx, v.x, v.y);
    }
}
void curve_set_point(scua_ctx *ctx)
{
    arity(ctx, 3);
    float v[2];
    vector(ctx, 2, v, 2);
    auto c = curve(ctx);
    if (c)
        c->setControlPoint(integer(ctx, 1, 0, c->getControlPointCount() - 1), {v[0], v[1]});
}
void curve_segment(scua_ctx *ctx)
{
    arity(ctx, 3);
    auto a = number(ctx, 1), b = number(ctx, 2);
    auto c = curve(ctx);
    if (c)
        returnObject(ctx, ObjectKind::Curve, c->getSegment(a, b));
}
void curve_translate(scua_ctx *ctx)
{
    arity(ctx, 2);
    float v[2];
    vector(ctx, 1, v, 2);
    auto c = curve(ctx);
    if (c)
        c->translate({v[0], v[1]});
}
void gamma_to_linear(scua_ctx *ctx)
{
    arity(ctx, 1);
    scua_return_float(ctx, love::math::gammaToLinear(number(ctx, 0)));
}
void linear_to_gamma(scua_ctx *ctx)
{
    arity(ctx, 1);
    scua_return_float(ctx, love::math::linearToGamma(number(ctx, 0)));
}
void noise(scua_ctx *ctx)
{
    arity(ctx, 1);
    float v[2];
    vector(ctx, 0, v, 2);
    scua_return_float(ctx, love::math::simplexNoise2(v[0], v[1]));
}
void perlin_noise(scua_ctx *ctx)
{
    arity(ctx, 1);
    float v[2];
    vector(ctx, 0, v, 2);
    scua_return_float(ctx, love::math::perlinNoise2(v[0], v[1]));
}
void is_convex(scua_ctx *ctx)
{
    arity(ctx, 1);
    scua_return_bool(ctx, love::math::isConvex(points(ctx, 0, 3)));
}
void triangulate(scua_ctx *ctx)
{
    arity(ctx, 1);
    auto input = points(ctx, 0, 3);
    if (input.size() > 4096)
        throw std::invalid_argument("triangulation limit is 4096 points");
    auto triangles = love::math::triangulate(input);
    std::vector<love::Vector2> vertices;
    for (auto &t : triangles)
    {
        vertices.push_back(t.a);
        vertices.push_back(t.b);
        vertices.push_back(t.c);
    }
    returnPoints(ctx, vertices);
}
void new_transform(scua_ctx *ctx)
{
    arity(ctx, 0);
    returnObject(ctx, ObjectKind::Transform, new love::math::Transform());
}
void transform_clone(scua_ctx *ctx)
{
    arity(ctx, 1);
    auto t = trans(ctx);
    if (t)
        returnObject(ctx, ObjectKind::Transform, t->clone());
}
void transform_inverse(scua_ctx *ctx)
{
    arity(ctx, 1);
    auto t = trans(ctx);
    if (t)
        returnObject(ctx, ObjectKind::Transform, t->inverse());
}
void transform_translate(scua_ctx *ctx)
{
    arity(ctx, 2);
    float v[2];
    vector(ctx, 1, v, 2);
    auto t = trans(ctx);
    if (t)
        t->translate(v[0], v[1]);
}
void transform_scale(scua_ctx *ctx)
{
    arity(ctx, 2);
    float v[2];
    vector(ctx, 1, v, 2);
    auto t = trans(ctx);
    if (t)
        t->scale(v[0], v[1]);
}
void transform_shear(scua_ctx *ctx)
{
    arity(ctx, 2);
    float v[2];
    vector(ctx, 1, v, 2);
    auto t = trans(ctx);
    if (t)
        t->shear(v[0], v[1]);
}
void transform_rotate(scua_ctx *ctx)
{
    arity(ctx, 2);
    auto a = number(ctx, 1);
    auto t = trans(ctx);
    if (t)
        t->rotate(a);
}
void transform_reset(scua_ctx *ctx)
{
    arity(ctx, 1);
    auto t = trans(ctx);
    if (t)
        t->reset();
}
void transform_apply(scua_ctx *ctx)
{
    arity(ctx, 2);
    auto t = trans(ctx), o = objectArgument<love::math::Transform>(ctx, 1, ObjectKind::Transform);
    if (t && o)
        t->apply(o);
}
void transform_point(scua_ctx *ctx)
{
    arity(ctx, 2);
    float v[2];
    vector(ctx, 1, v, 2);
    auto t = trans(ctx);
    if (t)
    {
        auto p = t->transformPoint({v[0], v[1]});
        returnVec(ctx, p.x, p.y);
    }
}
void transform_inverse_point(scua_ctx *ctx)
{
    arity(ctx, 2);
    float v[2];
    vector(ctx, 1, v, 2);
    auto t = trans(ctx);
    if (t)
    {
        auto p = t->inverseTransformPoint({v[0], v[1]});
        returnVec(ctx, p.x, p.y);
    }
}
void apply_transform(scua_ctx *ctx)
{
    arity(ctx, 1);
    auto t = trans(ctx);
    if (t)
        graphics(ctx).applyTransform(t->getMatrix());
}
void replace_transform(scua_ctx *ctx)
{
    arity(ctx, 1);
    auto t = trans(ctx);
    if (t)
        graphics(ctx).replaceTransform(t->getMatrix());
}
} // namespace
void registerMath(scua_partition *p, Host &state)
{
    for (auto kind : {ObjectKind::Random, ObjectKind::Curve, ObjectKind::Transform})
        registerObjectType(p, state, kind);
    registerFunctions(
        p, state,
        {
            {"love_math", "new_random", guarded<new_random>, "new_random(seed: int) -> any",
             "Create a managed seeded random generator."},
            {"love_math", "random", guarded<random>, "random(generator: any) -> float",
             "Uniform random value in [0,1)."},
            {"love_math", "random_int", guarded<random_int>,
             "random_int(generator: any, minimum: int, maximum: int) -> int",
             "Uniform integer in an inclusive range."},
            {"love_math", "random_normal", guarded<random_normal>,
             "random_normal(generator: any, deviation: float, mean: float) -> float",
             "Normally distributed random value."},
            {"love_math", "random_state", guarded<random_state>, "random_state(generator: any) -> string",
             "Read generator state."},
            {"love_math", "random_set_state", guarded<random_set_state>,
             "random_set_state(generator: any, state: string)", "Restore generator state."},
            {"love_math", "random_set_seed", guarded<random_set_seed>,
             "random_set_seed(generator: any, seed: int)", "Reset the seed."},
            {"love_math", "new_curve", guarded<new_curve>, "new_curve(points: any) -> any",
             "Create a managed Bezier curve from vec2 control points."},
            {"love_math", "curve_evaluate", guarded<curve_evaluate>,
             "curve_evaluate(curve: any, t: float) -> vec2", "Evaluate a curve at t in [0,1]."},
            {"love_math", "curve_render", guarded<curve_render>,
             "curve_render(curve: any, accuracy: int) -> any", "Subdivide the curve into drawable points."},
            {"love_math", "curve_degree", guarded<curve_degree>, "curve_degree(curve: any) -> int",
             "Get polynomial degree."},
            {"love_math", "curve_point", guarded<curve_point>, "curve_point(curve: any, index: int) -> vec2",
             "Get a zero-based control point."},
            {"love_math", "curve_set_point", guarded<curve_set_point>,
             "curve_set_point(curve: any, index: int, position: vec2)", "Set a zero-based control point."},
            {"love_math", "curve_segment", guarded<curve_segment>,
             "curve_segment(curve: any, start: float, finish: float) -> any", "Extract a curve segment."},
            {"love_math", "curve_translate", guarded<curve_translate>,
             "curve_translate(curve: any, offset: vec2)", "Translate control points."},
            {"love_math", "gamma_to_linear", guarded<gamma_to_linear>,
             "gamma_to_linear(value: float) -> float", "Convert a color component."},
            {"love_math", "linear_to_gamma", guarded<linear_to_gamma>,
             "linear_to_gamma(value: float) -> float", "Convert a color component."},
            {"love_math", "noise", guarded<noise>, "noise(position: vec2) -> float",
             "Two dimensional simplex noise."},
            {"love_math", "perlin_noise", guarded<perlin_noise>, "perlin_noise(position: vec2) -> float",
             "Two dimensional Perlin noise."},
            {"love_math", "is_convex", guarded<is_convex>, "is_convex(points: any) -> bool",
             "Test polygon convexity."},
            {"love_math", "triangulate", guarded<triangulate>, "triangulate(points: any) -> any",
             "Triangulate a simple polygon into consecutive triples of vertices."},
            {"love_math", "new_transform", guarded<new_transform>, "new_transform() -> any",
             "Create an identity transform."},
            {"love_math", "transform_clone", guarded<transform_clone>,
             "transform_clone(transform: any) -> any", "Create a managed transform."},
            {"love_math", "transform_inverse", guarded<transform_inverse>,
             "transform_inverse(transform: any) -> any", "Create a managed transform."},
            {"love_math", "transform_translate", guarded<transform_translate>,
             "transform_translate(transform: any, value: vec2)", "Modify the transform."},
            {"love_math", "transform_scale", guarded<transform_scale>,
             "transform_scale(transform: any, value: vec2)", "Modify the transform."},
            {"love_math", "transform_shear", guarded<transform_shear>,
             "transform_shear(transform: any, value: vec2)", "Modify the transform."},
            {"love_math", "transform_rotate", guarded<transform_rotate>,
             "transform_rotate(transform: any, radians: float)", "Rotate the transform."},
            {"love_math", "transform_reset", guarded<transform_reset>, "transform_reset(transform: any)",
             "Reset to identity."},
            {"love_math", "transform_apply", guarded<transform_apply>,
             "transform_apply(transform: any, other: any)", "Compose transforms."},
            {"love_math", "transform_point", guarded<transform_point>,
             "transform_point(transform: any, point: vec2) -> vec2", "Map a point through the transform."},
            {"love_math", "transform_inverse_point", guarded<transform_inverse_point>,
             "transform_inverse_point(transform: any, point: vec2) -> vec2",
             "Map a point through the transform."},
            {"love_graphics", "apply_transform", guarded<apply_transform>, "apply_transform(transform: any)",
             "Compose a transform into the drawing state."},
            {"love_graphics", "replace_transform", guarded<replace_transform>,
             "replace_transform(transform: any)", "Replace the drawing transform."},
        });
}
} // namespace lovescua
