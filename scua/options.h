// Checked record fields shared by native APIs with structured settings.
#pragma once
#include "binding_util.h"

namespace lovescua::binding
{
struct Options
{
    scua_partition *p;
    scua_Handle value;
    Options(scua_ctx *ctx, int index)
        : p(scua_ctx_partition(ctx)), value(index < scua_arg_count(ctx) ? scua_arg(ctx, index) : scua_nil(p))
    {
        if (supplied(ctx, index) && scua_typeof(p, value) != SCUA_TYPE_TABLE)
            throw std::invalid_argument("settings must be a record");
    }
    Options(scua_partition *p, scua_Handle value) : p(p), value(value)
    {
        if (scua_typeof(p, value) != SCUA_TYPE_TABLE)
            throw std::invalid_argument("settings must be a record");
    }
    scua_Handle field(const char *name) const
    {
        return scua_field(p, value, name);
    }
    bool has(const char *name) const
    {
        auto h = field(name);
        return scua_handle_valid(p, h) && scua_typeof(p, h) != SCUA_TYPE_NIL;
    }
    double num(const char *name, double fallback) const
    {
        if (!has(name))
            return fallback;
        auto h = field(name);
        int type = scua_typeof(p, h);
        double n = scua_to_num(p, h);
        if ((type != SCUA_TYPE_INT && type != SCUA_TYPE_FLOAT) || !std::isfinite(n) ||
            std::abs(n) > std::numeric_limits<float>::max())
            throw std::invalid_argument(std::string(name) + " must be a finite number");
        return n;
    }
    int integer(const char *name, int fallback, int minimum, int maximum) const
    {
        double n = num(name, fallback);
        if (n < minimum || n > maximum || n != std::floor(n))
            throw std::invalid_argument(std::string(name) + " is outside its integer range");
        return int(n);
    }
    bool boolean(const char *name, bool fallback) const
    {
        if (!has(name))
            return fallback;
        auto h = field(name);
        if (scua_typeof(p, h) != SCUA_TYPE_BOOL)
            throw std::invalid_argument(std::string(name) + " must be boolean");
        return scua_to_bool(p, h);
    }
    std::string str(const char *name, const char *fallback) const
    {
        if (!has(name))
            return fallback;
        size_t size = 0;
        const char *s = scua_to_lstring(p, field(name), &size);
        if (!s || std::memchr(s, 0, size))
            throw std::invalid_argument(std::string(name) + " must be a string without NUL");
        return {s, size};
    }
};
} // namespace lovescua::binding
