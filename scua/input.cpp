// Bind the upstream SDL joystick implementation and DPI-aware touch polling.
#include "binding_util.h"
#include "joystick/Joystick.h"
#include "joystick/sdl/JoystickModule.h"
#include "mouse/Mouse.h"
#include "sensor/sdl/Sensor.h"
#include <SDL3/SDL.h>
namespace lovescua
{
namespace
{
using namespace binding;
auto joystick(scua_ctx *ctx)
{
    return objectArgument<love::joystick::Joystick>(ctx, 0, ObjectKind::Joystick);
}

love::sensor::Sensor::SensorType sensorType(scua_ctx *ctx, int index)
{
    auto name = string(ctx, index);
    love::sensor::Sensor::SensorType type;
    if (!love::sensor::Sensor::getConstant(name.c_str(), type))
        throw std::invalid_argument("sensor must be accelerometer or gyroscope");
    return type;
}
love::sensor::Sensor &sensors(scua_ctx *ctx)
{
    auto &state = host(ctx);
    if (!state.sensors)
        state.sensors = new love::sensor::sdl::Sensor();
    return *state.sensors;
}
void sensor_has(scua_ctx *ctx)
{
    arity(ctx, 1);
    auto type = sensorType(ctx, 0);
    scua_return_bool(ctx, sensors(ctx).hasSensor(type));
}
void sensor_enabled(scua_ctx *ctx)
{
    arity(ctx, 1);
    auto type = sensorType(ctx, 0);
    scua_return_bool(ctx, sensors(ctx).isEnabled(type));
}
void sensor_enable(scua_ctx *ctx)
{
    arity(ctx, 2);
    auto type = sensorType(ctx, 0);
    bool enable = boolean(ctx, 1);
    auto &s = sensors(ctx);
    if (enable && !s.hasSensor(type))
        throw std::invalid_argument("requested sensor is unavailable");
    s.setEnabled(type, enable);
}
void sensor_data(scua_ctx *ctx)
{
    arity(ctx, 1);
    auto type = sensorType(ctx, 0);
    auto values = sensors(ctx).getData(type);
    if (values.size() != 3)
        throw std::runtime_error("invalid sensor data");
    scua_return_vec(ctx, values.data(), 3);
}
void sensor_name(scua_ctx *ctx)
{
    arity(ctx, 1);
    auto type = sensorType(ctx, 0);
    returnString(ctx, sensors(ctx).getSensorName(type));
}
void gamepad_has_sensor(scua_ctx *ctx)
{
    arity(ctx, 2);
    auto type = sensorType(ctx, 1);
    auto j = joystick(ctx);
    if (j)
        scua_return_bool(ctx, j->hasSensor(type));
}
void gamepad_sensor_enabled(scua_ctx *ctx)
{
    arity(ctx, 2);
    auto type = sensorType(ctx, 1);
    auto j = joystick(ctx);
    if (j)
        scua_return_bool(ctx, j->isSensorEnabled(type));
}
void gamepad_enable_sensor(scua_ctx *ctx)
{
    arity(ctx, 3);
    auto type = sensorType(ctx, 1);
    bool enabled = boolean(ctx, 2);
    auto j = joystick(ctx);
    if (j)
    {
        if (enabled && !j->hasSensor(type))
            throw std::invalid_argument("requested gamepad sensor is unavailable");
        j->setSensorEnabled(type, enabled);
    }
}
void gamepad_sensor_data(scua_ctx *ctx)
{
    arity(ctx, 2);
    auto type = sensorType(ctx, 1);
    auto j = joystick(ctx);
    if (j)
    {
        auto values = j->getSensorData(type);
        if (values.size() != 3)
            throw std::runtime_error("invalid gamepad sensor data");
        scua_return_vec(ctx, values.data(), 3);
    }
}

void count(scua_ctx *ctx)
{
    arity(ctx, 0);
    scua_return_int(ctx, host(ctx).joysticks->getJoystickCount());
}
void get(scua_ctx *ctx)
{
    arity(ctx, 1);
    int index = integer(ctx, 0, 0, 255);
    auto j = host(ctx).joysticks->getJoystick(index);
    if (!j)
    {
        scua_return_value(ctx, scua_nil(scua_ctx_partition(ctx)));
        return;
    }
    j->retain();
    returnObject(ctx, ObjectKind::Joystick, j);
}
void info(scua_ctx *ctx)
{
    arity(ctx, 1);
    auto j = joystick(ctx);
    if (!j)
        return;
    Record r(ctx);
    r.str("name", j->getName());
    r.str("guid", j->getGUID());
    r.integer("id", j->getID());
    r.integer("instance_id", j->getInstanceID());
    r.integer("axes", j->getAxisCount());
    r.integer("buttons", j->getButtonCount());
    r.integer("hats", j->getHatCount());
    r.integer("player_index", j->getPlayerIndex());
    r.boolean("connected", j->isConnected());
    r.boolean("gamepad", j->isGamepad());
    int vendor = 0, product = 0, version = 0, battery = 0;
    j->getDeviceInfo(vendor, product, version);
    r.integer("vendor", vendor);
    r.integer("product", product);
    r.integer("product_version", version);
    r.integer("connected_index", host(ctx).joysticks->getIndex(j));
    const char *kind = "unknown";
    love::joystick::Joystick::getConstant(j->getJoystickType(), kind);
    r.str("joystick_type", kind);
    kind = "unknown";
    love::joystick::Joystick::getConstant(j->getGamepadType(), kind);
    r.str("gamepad_type", kind);
    kind = "unknown";
    love::joystick::Joystick::getConstant(j->getPowerInfo(battery), kind);
    r.str("power", kind);
    r.integer("battery_percent", battery);
    kind = "unknown";
    love::joystick::Joystick::getConstant(j->getConnectionState(), kind);
    r.str("connection", kind);
    r.str("mapping", j->getGamepadMappingString());
    float left = 0, right = 0;
    j->getVibration(left, right);
    float vibration[] = {left, right};
    r.set("vibration", scua_vec(r.p, vibration, 2));
    auto axes = scua_new_array(r.p, 0);
    for (auto value : j->getAxes())
        if (scua_array_push(r.p, axes, scua_num(r.p, value)) != SCUA_OK)
            throw std::runtime_error("axes allocation failed");
    r.set("axis_values", axes);

    r.done();
}
void is_connected(scua_ctx *ctx)
{
    arity(ctx, 1);
    auto j = joystick(ctx);
    if (j)
        scua_return_bool(ctx, j->isConnected());
}
void axis(scua_ctx *ctx)
{
    arity(ctx, 2);
    auto j = joystick(ctx);
    if (j)
        scua_return_float(ctx, j->getAxis(integer(ctx, 1, 0, j->getAxisCount() - 1)));
}
void is_down(scua_ctx *ctx)
{
    arity(ctx, 2);
    auto j = joystick(ctx);
    if (j)
        scua_return_bool(ctx, j->isDown({integer(ctx, 1, 0, j->getButtonCount() - 1)}));
}
void hat(scua_ctx *ctx)
{
    arity(ctx, 2);
    auto j = joystick(ctx);
    if (j)
    {
        auto h = j->getHat(integer(ctx, 1, 0, j->getHatCount() - 1));
        const char *name = "invalid";
        love::joystick::Joystick::getConstant(h, name);
        returnString(ctx, name);
    }
}
void gamepad_axis(scua_ctx *ctx)
{
    arity(ctx, 2);
    auto name = string(ctx, 1);
    love::joystick::Joystick::GamepadAxis axis;
    if (!love::joystick::Joystick::getConstant(name.c_str(), axis))
        throw std::invalid_argument("invalid gamepad axis");
    auto j = joystick(ctx);
    if (j)
        scua_return_float(ctx, j->getGamepadAxis(axis));
}
void gamepad_down(scua_ctx *ctx)
{
    arity(ctx, 2);
    auto name = string(ctx, 1);
    love::joystick::Joystick::GamepadButton button;
    if (!love::joystick::Joystick::getConstant(name.c_str(), button))
        throw std::invalid_argument("invalid gamepad button");
    auto j = joystick(ctx);
    if (j)
        scua_return_bool(ctx, j->isGamepadDown({button}));
}
void mapping(scua_ctx *c)
{
    arity(c, 3);
    auto kind = string(c, 1), name = string(c, 2);
    love::joystick::Joystick::GamepadInput in{};
    if (kind == "axis")
    {
        in.type = love::joystick::Joystick::INPUT_TYPE_AXIS;
        if (!love::joystick::Joystick::getConstant(name.c_str(), in.axis))
            throw std::invalid_argument("unknown gamepad axis");
    }
    else if (kind == "button")
    {
        in.type = love::joystick::Joystick::INPUT_TYPE_BUTTON;
        if (!love::joystick::Joystick::getConstant(name.c_str(), in.button))
            throw std::invalid_argument("unknown gamepad button");
    }
    else
        throw std::invalid_argument("mapping kind must be axis or button");
    auto j = joystick(c);
    if (!j)
        return;
    auto out = j->getGamepadMapping(in);
    const char *type = "unknown";
    love::joystick::Joystick::getConstant(out.type, type);
    Record r(c);
    r.str("type", type);
    if (out.type == love::joystick::Joystick::INPUT_TYPE_AXIS)
        r.integer("index", out.axis);
    else if (out.type == love::joystick::Joystick::INPUT_TYPE_BUTTON)
        r.integer("index", out.button);
    else if (out.type == love::joystick::Joystick::INPUT_TYPE_HAT)
    {
        r.integer("index", out.hat.index);
        const char *hat = "invalid";
        love::joystick::Joystick::getConstant(out.hat.value, hat);
        r.str("hat", hat);
    }
    r.done();
}
void set_player_index(scua_ctx *ctx)
{
    arity(ctx, 2);
    int index = integer(ctx, 1, -1, 255);
    auto j = joystick(ctx);
    if (j)
        j->setPlayerIndex(index);
}
void vibration_supported(scua_ctx *ctx)
{
    arity(ctx, 1);
    auto j = joystick(ctx);
    if (j)
        scua_return_bool(ctx, j->isVibrationSupported());
}
void vibrate(scua_ctx *ctx)
{
    arity(ctx, 4);
    float left = number(ctx, 1), right = number(ctx, 2), duration = number(ctx, 3);
    if (left < 0 || left > 1 || right < 0 || right > 1 || duration < 0 || duration > 60)
        throw std::invalid_argument("vibration strengths must be in [0,1], duration in [0,60]");
    auto j = joystick(ctx);
    if (j)
        scua_return_bool(ctx, j->setVibration(left, right, duration));
}
void set_background_events(scua_ctx *ctx)
{
    arity(ctx, 1);
    host(ctx).joysticks->setBackgroundEvents(boolean(ctx, 0));
}
void background_events(scua_ctx *ctx)
{
    arity(ctx, 0);
    scua_return_bool(ctx, host(ctx).joysticks->hasBackgroundEvents());
}
void load_mappings(scua_ctx *ctx)
{
    arity(ctx, 1);
    auto mappings = string(ctx, 0);
    if (mappings.size() > 1024 * 1024)
        throw std::invalid_argument("mappings exceed 1 MiB");
    host(ctx).joysticks->loadGamepadMappings(mappings);
}
void save_mappings(scua_ctx *ctx)
{
    arity(ctx, 0);
    returnString(ctx, host(ctx).joysticks->saveGamepadMappings());
}
void touches(scua_ctx *ctx)
{
    arity(ctx, 0);
    auto p = scua_ctx_partition(ctx);
    auto output = scua_new_array(p, 0);
    if (host(ctx).ignoreInput)
    {
        scua_return_value(ctx, output);
        return;
    }
    int devices = 0;
    auto ids = SDL_GetTouchDevices(&devices);
    struct Free
    {
        void operator()(SDL_TouchID *p)
        {
            SDL_free(p);
        }
    };
    std::unique_ptr<SDL_TouchID, Free> owned(ids);
    for (int i = 0; i < devices; ++i)
    {
        int count = 0;
        auto fingers = SDL_GetTouchFingers(ids[i], &count);
        struct FreeFingers
        {
            void operator()(SDL_Finger **p)
            {
                SDL_free(p);
            }
        };
        std::unique_ptr<SDL_Finger *, FreeFingers> active(fingers);
        for (int j = 0; j < count; ++j)
        {
            Record r(ctx);
            r.integer("id", fingers[j]->id);
            r.integer("device", ids[i]);
            auto type = SDL_GetTouchDeviceType(ids[i]);
            r.str("device_type", type == SDL_TOUCH_DEVICE_DIRECT              ? "direct"
                                 : type == SDL_TOUCH_DEVICE_INDIRECT_ABSOLUTE ? "indirectabsolute"
                                 : type == SDL_TOUCH_DEVICE_INDIRECT_RELATIVE ? "indirectrelative"
                                                                              : "unknown");
            r.boolean("is_mouse", ids[i] == SDL_MOUSE_TOUCHID);

            r.num("pressure", fingers[j]->pressure);
            float xy[] = {fingers[j]->x * host(ctx).graphics->getWidth(),
                          fingers[j]->y * host(ctx).graphics->getHeight()};
            r.set("position", scua_vec(p, xy, 2));
            if (scua_array_push(p, output, r.value) != SCUA_OK)
                throw std::runtime_error("could not build touches array");
        }
    }
    scua_return_value(ctx, output);
}
} // namespace
void registerInput(scua_partition *p, Host &state)
{
    registerObjectType(p, state, ObjectKind::Joystick);
    registerFunctions(
        p, state,
        {
            {"love_joystick", "count", guarded<count>, "count() -> int",
             "count. Joystick indices are zero-based."},
            {"love_joystick", "get", guarded<get>, "get(index: int) -> any",
             "get. Joystick indices are zero-based."},
            {"love_joystick", "info", guarded<info>, "info(joystick: any) -> any",
             "info. Joystick indices are zero-based."},
            {"love_joystick", "is_connected", guarded<is_connected>, "is_connected(joystick: any) -> bool",
             "is connected. Joystick indices are zero-based."},
            {"love_joystick", "axis", guarded<axis>, "axis(joystick: any, index: int) -> float",
             "axis. Joystick indices are zero-based."},
            {"love_joystick", "is_down", guarded<is_down>, "is_down(joystick: any, index: int) -> bool",
             "is down. Joystick indices are zero-based."},
            {"love_joystick", "hat", guarded<hat>, "hat(joystick: any, index: int) -> string",
             "hat. Joystick indices are zero-based."},
            {"love_joystick", "gamepad_axis", guarded<gamepad_axis>,
             "gamepad_axis(joystick: any, name: string) -> float",
             "gamepad axis. Joystick indices are zero-based."},
            {"love_joystick", "gamepad_down", guarded<gamepad_down>,
             "gamepad_down(joystick: any, name: string) -> bool",
             "gamepad down. Joystick indices are zero-based."},
            {"love_joystick", "mapping", guarded<mapping>,
             "mapping(joystick: any, kind: string, input: string) -> any",
             "Get the native axis/button/hat binding of a gamepad input."},
            {"love_joystick", "set_player_index", guarded<set_player_index>,
             "set_player_index(joystick: any, index: int)",
             "set player index. Joystick indices are zero-based."},
            {"love_joystick", "vibration_supported", guarded<vibration_supported>,
             "vibration_supported(joystick: any) -> bool",
             "vibration supported. Joystick indices are zero-based."},
            {"love_joystick", "vibrate", guarded<vibrate>,
             "vibrate(joystick: any, left: float, right: float, seconds: float) -> bool",
             "vibrate. Joystick indices are zero-based."},
            {"love_joystick", "set_background_events", guarded<set_background_events>,
             "set_background_events(enabled: bool)",
             "set background events. Joystick indices are zero-based."},
            {"love_joystick", "background_events", guarded<background_events>, "background_events() -> bool",
             "background events. Joystick indices are zero-based."},
            {"love_joystick", "load_mappings", guarded<load_mappings>, "load_mappings(mappings: string)",
             "load mappings. Joystick indices are zero-based."},
            {"love_joystick", "save_mappings", guarded<save_mappings>, "save_mappings() -> string",
             "save mappings. Joystick indices are zero-based."},
            {"love_touch", "touches", guarded<touches>, "touches() -> any",
             "Get active touches as id, device, pressure and pixel-position records."},
            {"love_sensor", "has", guarded<sensor_has>, "has(kind: string) -> bool",
             "Check accelerometer or gyroscope availability."},
            {"love_sensor", "is_enabled", guarded<sensor_enabled>, "is_enabled(kind: string) -> bool",
             "Check sensor state."},
            {"love_sensor", "set_enabled", guarded<sensor_enable>, "set_enabled(kind: string, enabled: bool)",
             "Enable a sensor explicitly; unavailable devices fault."},
            {"love_sensor", "data", guarded<sensor_data>, "data(kind: string) -> vec3",
             "Read an enabled sensor."},
            {"love_sensor", "name", guarded<sensor_name>, "name(kind: string) -> string",
             "Get an enabled sensor name."},
            {"love_joystick", "has_sensor", guarded<gamepad_has_sensor>,
             "has_sensor(joystick: any, kind: string) -> bool", "Check gamepad sensor support."},
            {"love_joystick", "sensor_enabled", guarded<gamepad_sensor_enabled>,
             "sensor_enabled(joystick: any, kind: string) -> bool", "Check gamepad sensor state."},
            {"love_joystick", "set_sensor_enabled", guarded<gamepad_enable_sensor>,
             "set_sensor_enabled(joystick: any, kind: string, enabled: bool)",
             "Enable or disable a gamepad sensor."},
            {"love_joystick", "sensor_data", guarded<gamepad_sensor_data>,
             "sensor_data(joystick: any, kind: string) -> vec3", "Read an enabled gamepad sensor."},
        });
}
void shutdownInput(Host &state) noexcept
{
    if (state.mouse)
    {
        state.mouse->setCursor();
        state.mouse->release();
        state.mouse = nullptr;
    }
    if (!state.sensors)
        return;
    for (auto type : {love::sensor::Sensor::SENSOR_ACCELEROMETER, love::sensor::Sensor::SENSOR_GYROSCOPE})
        state.sensors->setEnabled(type, false);
    state.sensors->release();
    state.sensors = nullptr;
}
} // namespace lovescua
