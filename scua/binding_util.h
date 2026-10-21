// Shared checked conversions and resource ownership for the SCUA bindings.
#pragma once
#include "bindings.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <initializer_list>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace lovescua::binding
{
inline Host &host(scua_ctx *ctx)
{
    return *static_cast<Host *>(scua_userdata(ctx));
}

inline void arity(scua_ctx *ctx, int count)
{
    if (scua_arg_count(ctx) != count)
        throw std::invalid_argument("expected " + std::to_string(count) + " arguments");
}

inline double number(scua_ctx *ctx, int index)
{
    auto p = scua_ctx_partition(ctx);
    int type = scua_typeof(p, scua_arg(ctx, index));
    if (type != SCUA_TYPE_INT && type != SCUA_TYPE_FLOAT)
        throw std::invalid_argument("argument " + std::to_string(index + 1) + " must be a number");
    double value = scua_arg_float(ctx, index);
    if (!std::isfinite(value) || std::abs(value) > std::numeric_limits<float>::max())
        throw std::invalid_argument("argument must be a finite number in the float range");
    return value;
}

inline void vector(scua_ctx *ctx, int index, float *out, int count)
{
    if (scua_to_vec(scua_ctx_partition(ctx), scua_arg(ctx, index), out, count) != count)
        throw std::invalid_argument("argument " + std::to_string(index + 1) + " must be vec" +
                                    std::to_string(count));
    for (int i = 0; i < count; ++i)
        if (!std::isfinite(out[i]))
            throw std::invalid_argument("vector components must be finite");
}

inline std::string string(scua_ctx *ctx, int index)
{
    size_t length = 0;
    const char *value = scua_arg_str(ctx, index, &length);
    if (!value)
        throw std::invalid_argument("argument " + std::to_string(index + 1) + " must be a string");
    if (std::memchr(value, 0, length))
        throw std::invalid_argument("embedded NUL in string");
    return {value, length}; // Copy before making another allocating runtime call.
}

inline love::graphics::Graphics &graphics(scua_ctx *ctx, bool drawing = true)
{
    if (drawing && !host(ctx).drawing)
        throw std::invalid_argument("drawing functions must be called inside draw()");
    return *host(ctx).graphics;
}

inline love::graphics::Graphics::DrawMode mode(scua_ctx *ctx)
{
    auto value = string(ctx, 0);
    if (value == "fill")
        return love::graphics::Graphics::DRAW_FILL;
    if (value == "line")
        return love::graphics::Graphics::DRAW_LINE;
    throw std::invalid_argument("mode must be 'fill' or 'line'");
}

inline love::Matrix4 transform(scua_ctx *ctx, int positionIndex = 1)
{
    int count = scua_arg_count(ctx);
    if (count < positionIndex + 1 || count > positionIndex + 4)
        throw std::invalid_argument("draw expects " + std::to_string(positionIndex + 1) + " to " +
                                    std::to_string(positionIndex + 4) + " arguments");
    auto supplied = [&](int index)
    { return count > index && scua_typeof(scua_ctx_partition(ctx), scua_arg(ctx, index)) != SCUA_TYPE_NIL; };
    float position[2], scale[] = {1.0f, 1.0f}, origin[] = {0.0f, 0.0f};
    vector(ctx, positionIndex, position, 2);
    float rotation = supplied(positionIndex + 1) ? static_cast<float>(number(ctx, positionIndex + 1)) : 0.0f;
    if (supplied(positionIndex + 2))
        vector(ctx, positionIndex + 2, scale, 2);
    if (supplied(positionIndex + 3))
        vector(ctx, positionIndex + 3, origin, 2);
    return love::Matrix4(position[0], position[1], rotation, scale[0], scale[1], origin[0], origin[1], 0, 0);
}

// No C++ exception may cross the C ABI into Zig. Native argument and engine
// errors become located SCUA faults returned by the enclosing scua_pcall.
template <scua_fn Fn> void guarded(scua_ctx *ctx) noexcept
{
    try
    {
        if (host(ctx).checking)
            throw std::invalid_argument(
                "--check does not execute engine functions; initialize resources in load()");
        Fn(ctx);
    }
    catch (const std::exception &error)
    {
        scua_native_error_message(ctx, SCUA_ERR_OTHER, error.what(), std::strlen(error.what()));
    }
    catch (...)
    {
        scua_native_error(ctx, SCUA_ERR_OTHER);
    }
}

inline ImageResource *imageArgument(scua_ctx *ctx, int index)
{
    void *storage = nullptr;
    if (scua_arg_userdata(ctx, index, host(ctx).imageType, &storage) != SCUA_OK)
        return nullptr;
    return static_cast<ImageResource *>(storage);
}
inline QuadResource *quadArgument(scua_ctx *ctx, int index)
{
    void *storage = nullptr;
    if (scua_arg_userdata(ctx, index, host(ctx).quadType, &storage) != SCUA_OK)
        return nullptr;
    return static_cast<QuadResource *>(storage);
}
inline bool supplied(scua_ctx *ctx, int index)
{
    return scua_arg_count(ctx) > index &&
           scua_typeof(scua_ctx_partition(ctx), scua_arg(ctx, index)) != SCUA_TYPE_NIL;
}
inline double optionalNumber(scua_ctx *ctx, int index, double fallback)
{
    return supplied(ctx, index) ? number(ctx, index) : fallback;
}
inline int integer(scua_ctx *ctx, int index, int minimum, int maximum)
{
    double value = number(ctx, index);
    if (value < minimum || value > maximum || std::floor(value) != value)
        throw std::invalid_argument("argument " + std::to_string(index + 1) + " must be an integer in [" +
                                    std::to_string(minimum) + ", " + std::to_string(maximum) + "]");
    return static_cast<int>(value);
}
inline bool boolean(scua_ctx *ctx, int index)
{
    if (scua_typeof(scua_ctx_partition(ctx), scua_arg(ctx, index)) != SCUA_TYPE_BOOL)
        throw std::invalid_argument("argument " + std::to_string(index + 1) + " must be a boolean");
    return scua_arg_bool(ctx, index);
}
inline void returnString(scua_ctx *ctx, const std::string &value)
{
    scua_return_str(ctx, value.data(), value.size());
}
inline void returnVec(scua_ctx *ctx, float x, float y)
{
    float value[] = {x, y};
    scua_return_vec(ctx, value, 2);
}
struct Record
{
    scua_ctx *ctx;
    scua_partition *p;
    scua_Handle value;
    explicit Record(scua_ctx *ctx) : ctx(ctx), p(scua_ctx_partition(ctx)), value(scua_new_table(p)) {}
    void set(const char *key, scua_Handle field)
    {
        if (scua_table_set(p, value, key, field) != SCUA_OK)
            throw std::runtime_error("could not build result record");
    }
    void num(const char *key, double field)
    {
        set(key, scua_num(p, field));
    }
    void integer(const char *key, int64_t field)
    {
        set(key, scua_int(p, field));
    }
    void boolean(const char *key, bool field)
    {
        set(key, scua_bool(p, field));
    }
    void str(const char *key, const std::string &field)
    {
        set(key, scua_string(p, field.data(), field.size()));
    }
    void done()
    {
        scua_return_value(ctx, value);
    }
};
inline void returnStrings(scua_ctx *ctx, const std::vector<std::string> &strings)
{
    auto p = scua_ctx_partition(ctx);
    auto array = scua_new_array(p, 0);
    for (const auto &s : strings)
        if (scua_array_push(p, array, scua_string(p, s.data(), s.size())) != SCUA_OK)
            throw std::runtime_error("could not build string array");
    scua_return_value(ctx, array);
}
inline std::vector<love::Vector2> points(scua_ctx *ctx, int index, int minimum = 1)
{
    auto p = scua_ctx_partition(ctx);
    auto value = scua_arg(ctx, index);
    if (scua_typeof(p, value) != SCUA_TYPE_ARRAY)
        throw std::invalid_argument("expected an array of vec2 points");
    std::vector<love::Vector2> output;
    for (int i = 0; i < 65537; ++i)
    {
        auto item = scua_index(p, value, i);
        if (!scua_handle_valid(p, item))
            break;
        float v[2];
        if (scua_to_vec(p, item, v, 2) != 2 || !std::isfinite(v[0]) || !std::isfinite(v[1]))
            throw std::invalid_argument("points must contain finite vec2 values");
        output.emplace_back(v[0], v[1]);
    }
    if (output.size() < static_cast<size_t>(minimum) || output.size() > 65536)
        throw std::invalid_argument("point count is outside the supported range");
    return output;
}
struct Binding
{
    const char *module, *name;
    scua_fn fn;
    const char *signature, *doc;
};
inline void registerFunction(scua_partition *p, Host &state, const Binding &b)
{
    auto key = std::string(b.module) + "." + b.name;
    if (!state.registeredBindings.insert(key).second)
        throw std::runtime_error("duplicate native binding: " + key);
    if (scua_register_module_doc(p, b.module, b.name, b.fn, &state, b.signature, b.doc) != SCUA_OK)
        throw std::runtime_error("could not register " + key);
}
inline void registerFunctions(scua_partition *p, Host &state, std::initializer_list<Binding> bindings)
{
    for (const auto &b : bindings)
        registerFunction(p, state, b);
}
struct ObjectResource
{
    love::Object *object;
};
inline int initializeObject(void *storage, size_t, void *argument) noexcept
{
    auto object = static_cast<love::Object *>(argument);
    object->retain();
    static_cast<ObjectResource *>(storage)->object = object;
    return SCUA_OK;
}
inline void destroyObject(void *storage, size_t, void *context) noexcept
{
    static_cast<ObjectResource *>(storage)->object->release();
    --static_cast<Host *>(context)->liveObjects;
}
inline void registerObjectType(scua_partition *p, Host &state, ObjectKind kind)
{
    scua_UserdataTypeSpec type{};
    type.struct_size = sizeof(type);
    type.version = 1;
    type.automatic = 1;
    type.size = sizeof(ObjectResource);
    type.alignment = alignof(ObjectResource);
    type.initialize = initializeObject;
    type.destroy = destroyObject;
    type.destroy_context = &state;
    if (scua_userdata_register(p, &type, &state.objectTypes[static_cast<size_t>(kind)]) != SCUA_OK)
        throw std::runtime_error("could not register engine object type");
}
template <typename T> inline T *objectArgument(scua_ctx *ctx, int index, ObjectKind kind)
{
    void *storage = nullptr;
    if (scua_arg_userdata(ctx, index, host(ctx).objectTypes[static_cast<size_t>(kind)], &storage) != SCUA_OK)
        return nullptr;
    return static_cast<T *>(static_cast<ObjectResource *>(storage)->object);
}
inline bool returnObject(scua_ctx *ctx, ObjectKind kind, love::Object *object)
{
    struct Release
    {
        void operator()(love::Object *p) const noexcept
        {
            if (p)
                p->release();
        }
    };
    std::unique_ptr<love::Object, Release> owned(object);
    bool published = scua_return_userdata(ctx, host(ctx).objectTypes[static_cast<size_t>(kind)],
                                          sizeof(ObjectResource), object) == SCUA_OK;
    if (published)
        ++host(ctx).liveObjects;
    return published;
}
} // namespace lovescua::binding
