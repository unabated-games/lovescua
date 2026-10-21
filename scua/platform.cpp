// SCUA adapters for windowing, input, timer and passive system information.
#include "binding_util.h"
#include "mouse/sdl/Mouse.h"
#include "timer/Timer.h"
#include <SDL3/SDL.h>
namespace lovescua
{
namespace
{
using namespace binding;
SDL_Window *window(scua_ctx *ctx)
{
    return static_cast<SDL_Window *>(host(ctx).window->getHandle());
}

love::mouse::Mouse &mouseModule(scua_ctx *ctx)
{
    auto &state = host(ctx);
    if (!state.mouse)
        state.mouse = new love::mouse::sdl::Mouse();
    return *state.mouse;
}
auto cursor(scua_ctx *ctx)
{
    return objectArgument<love::mouse::Cursor>(ctx, 0, ObjectKind::Cursor);
}
void new_cursor(scua_ctx *ctx)
{
    arity(ctx, 3);
    auto image = objectArgument<love::image::ImageData>(ctx, 0, ObjectKind::ImageData);
    if (!image)
        return;
    if (image->getWidth() > 256 || image->getHeight() > 256)
        throw std::invalid_argument("cursor dimensions must be at most 256x256");
    int x = integer(ctx, 1, 0, image->getWidth() - 1), y = integer(ctx, 2, 0, image->getHeight() - 1);
    returnObject(ctx, ObjectKind::Cursor, mouseModule(ctx).newCursor({image}, x, y));
}
void system_cursor(scua_ctx *ctx)
{
    arity(ctx, 1);
    auto name = string(ctx, 0);
    love::mouse::Cursor::SystemCursor type;
    if (!love::mouse::Cursor::getConstant(name.c_str(), type))
        throw std::invalid_argument("unknown system cursor");
    auto c = mouseModule(ctx).getSystemCursor(type);
    c->retain();
    returnObject(ctx, ObjectKind::Cursor, c);
}
void set_cursor(scua_ctx *ctx)
{
    arity(ctx, 1);
    if (!supplied(ctx, 0))
    {
        mouseModule(ctx).setCursor();
        return;
    }
    auto c = cursor(ctx);
    if (c)
        mouseModule(ctx).setCursor(c);
}
void get_cursor(scua_ctx *ctx)
{
    arity(ctx, 0);
    auto c = mouseModule(ctx).getCursor();
    if (c)
    {
        c->retain();
        returnObject(ctx, ObjectKind::Cursor, c);
    }
    else
        scua_return_value(ctx, scua_nil(scua_ctx_partition(ctx)));
}
void cursor_info(scua_ctx *ctx)
{
    arity(ctx, 1);
    auto c = cursor(ctx);
    if (c)
    {
        const char *name = "unknown";
        love::mouse::Cursor::getConstant(c->getType(), name);
        Record r(ctx);
        r.str("kind", name);
        if (c->getType() == love::mouse::Cursor::CURSORTYPE_SYSTEM)
        {
            love::mouse::Cursor::getConstant(c->getSystemType(), name);
            r.str("system", name);
        }
        else
            r.set("system", scua_nil(r.p));
        r.done();
    }
}

void event_post(scua_ctx *ctx)
{
    arity(ctx, 2);
    auto name = string(ctx, 0);
    if (name.empty() || name.size() > 128)
        throw std::invalid_argument("event name must contain 1..128 bytes");
    auto &state = host(ctx);
    if (state.pendingEvents.size() >= 256)
        throw std::invalid_argument("event queue limit is 256");
    auto p = scua_ctx_partition(ctx);
    auto key = scua_pin_global(p, scua_arg(ctx, 0)), value = scua_pin_global(p, scua_arg(ctx, 1));
    if (!scua_handle_valid(p, key) || !scua_handle_valid(p, value))
    {
        scua_unpin_global(p, key);
        scua_unpin_global(p, value);
        throw std::runtime_error("could not retain event");
    }
    try
    {
        state.pendingEvents.emplace_back(key, value);
    }
    catch (...)
    {
        scua_unpin_global(p, key);
        scua_unpin_global(p, value);
        throw;
    }
}
void window_title(scua_ctx *ctx)
{
    arity(ctx, 0);
    returnString(ctx, host(ctx).window->getWindowTitle());
}

void window_is_open(scua_ctx *ctx)
{
    arity(ctx, 0);
    scua_return_bool(ctx, host(ctx).window->isOpen());
}

void window_has_focus(scua_ctx *ctx)
{
    arity(ctx, 0);
    scua_return_bool(ctx, host(ctx).window->hasFocus());
}

void window_has_mouse_focus(scua_ctx *ctx)
{
    arity(ctx, 0);
    scua_return_bool(ctx, host(ctx).window->hasMouseFocus());
}

void window_is_visible(scua_ctx *ctx)
{
    arity(ctx, 0);
    scua_return_bool(ctx, host(ctx).window->isVisible());
}

void window_is_minimized(scua_ctx *ctx)
{
    arity(ctx, 0);
    scua_return_bool(ctx, host(ctx).window->isMinimized());
}

void window_is_maximized(scua_ctx *ctx)
{
    arity(ctx, 0);
    scua_return_bool(ctx, host(ctx).window->isMaximized());
}

void window_is_occluded(scua_ctx *ctx)
{
    arity(ctx, 0);
    scua_return_bool(ctx, host(ctx).window->isOccluded());
}

void window_display_sleep_enabled(scua_ctx *ctx)
{
    arity(ctx, 0);
    scua_return_bool(ctx, host(ctx).window->isDisplaySleepEnabled());
}

void window_minimize(scua_ctx *ctx)
{
    arity(ctx, 0);
    host(ctx).window->minimize();
}

void window_maximize(scua_ctx *ctx)
{
    arity(ctx, 0);
    host(ctx).window->maximize();
}

void window_restore(scua_ctx *ctx)
{
    arity(ctx, 0);
    host(ctx).window->restore();
}

void window_focus(scua_ctx *ctx)
{
    arity(ctx, 0);
    host(ctx).window->focus();
}

void window_vsync(scua_ctx *ctx)
{
    arity(ctx, 0);
    scua_return_int(ctx, host(ctx).window->getVSync());
}

void window_display_count(scua_ctx *ctx)
{
    arity(ctx, 0);
    scua_return_int(ctx, host(ctx).window->getDisplayCount());
}

void window_dpi_scale(scua_ctx *ctx)
{
    arity(ctx, 0);
    scua_return_float(ctx, host(ctx).window->getDPIScale());
}

void window_native_dpi_scale(scua_ctx *ctx)
{
    arity(ctx, 0);
    scua_return_float(ctx, host(ctx).window->getNativeDPIScale());
}

void window_set_vsync(scua_ctx *ctx)
{
    arity(ctx, 1);
    host(ctx).window->setVSync(integer(ctx, 0, -1, 4));
}

void window_set_display_sleep_enabled(scua_ctx *ctx)
{
    arity(ctx, 1);
    host(ctx).window->setDisplaySleepEnabled(boolean(ctx, 0));
}

void window_set_fullscreen(scua_ctx *ctx)
{
    arity(ctx, 1);
    scua_return_bool(ctx, host(ctx).window->setFullscreen(boolean(ctx, 0)));
}

void window_position(scua_ctx *ctx)
{
    arity(ctx, 0);
    int x, y, d;
    host(ctx).window->getPosition(x, y, d);
    returnVec(ctx, x, y);
}

void window_set_position(scua_ctx *ctx)
{
    arity(ctx, 1);
    float v[2];
    vector(ctx, 0, v, 2);
    if (std::abs(v[0]) > 100000 || std::abs(v[1]) > 100000)
        throw std::invalid_argument("window position out of range");
    host(ctx).window->setPosition(v[0], v[1], 0, false);
}

void window_mode(scua_ctx *ctx)
{
    arity(ctx, 0);
    int w, h;
    love::window::WindowSettings s;
    host(ctx).window->getWindow(w, h, s);
    Record r(ctx);
    r.integer("width", w);
    r.integer("height", h);
    r.boolean("resizable", s.resizable);
    r.boolean("fullscreen", s.fullscreen);
    r.boolean("borderless", s.borderless);
    r.integer("vsync", s.vsync);
    r.integer("msaa", s.msaa);
    r.done();
}

void window_display_name(scua_ctx *ctx)
{
    arity(ctx, 1);
    returnString(
        ctx, host(ctx).window->getDisplayName(integer(ctx, 0, 0, host(ctx).window->getDisplayCount() - 1)));
}

void window_desktop_dimensions(scua_ctx *ctx)
{
    if (scua_arg_count(ctx) > 1)
        throw std::invalid_argument("expected zero or one display index");
    int i = supplied(ctx, 0) ? integer(ctx, 0, 0, host(ctx).window->getDisplayCount() - 1) : 0;
    int w, h;
    host(ctx).window->getDesktopDimensions(i, w, h);
    returnVec(ctx, w, h);
}

void window_safe_area(scua_ctx *ctx)
{
    arity(ctx, 0);
    auto r = host(ctx).window->getSafeArea();
    float v[] = {float(r.x), float(r.y), float(r.w), float(r.h)};
    scua_return_vec(ctx, v, 4);
}

void window_to_pixels(scua_ctx *ctx)
{
    arity(ctx, 1);
    float v[2];
    vector(ctx, 0, v, 2);
    double x, y;
    host(ctx).window->toPixels(v[0], v[1], x, y);
    returnVec(ctx, x, y);
}

void window_from_pixels(scua_ctx *ctx)
{
    arity(ctx, 1);
    float v[2];
    vector(ctx, 0, v, 2);
    double x, y;
    host(ctx).window->fromPixels(v[0], v[1], x, y);
    returnVec(ctx, x, y);
}

void timer_get_time(scua_ctx *ctx)
{
    arity(ctx, 0);
    scua_return_float(ctx, love::timer::Timer::getTime());
}

void timer_get_delta(scua_ctx *ctx)
{
    arity(ctx, 0);
    scua_return_float(ctx, host(ctx).delta);
}

void timer_get_average_delta(scua_ctx *ctx)
{
    arity(ctx, 0);
    scua_return_float(ctx, host(ctx).averageDelta);
}

void timer_get_elapsed(scua_ctx *ctx)
{
    arity(ctx, 0);
    scua_return_float(ctx, host(ctx).elapsed);
}

void timer_get_fps(scua_ctx *ctx)
{
    arity(ctx, 0);
    auto delta = host(ctx).averageDelta;
    scua_return_int(ctx, delta > 0 ? std::llround(std::min(1.0 / delta, static_cast<double>(INT_MAX))) : 0);
}

void timer_sleep(scua_ctx *ctx)
{
    arity(ctx, 1);
    auto seconds = number(ctx, 0);
    if (seconds < 0 || seconds > 60)
        throw std::invalid_argument("sleep must be between 0 and 60 seconds");
    SDL_DelayNS(static_cast<Uint64>(seconds * 1e9));
}

void keyboard_set_key_repeat(scua_ctx *ctx)
{
    arity(ctx, 1);
    host(ctx).keyRepeat = boolean(ctx, 0);
}

void keyboard_has_key_repeat(scua_ctx *ctx)
{
    arity(ctx, 0);
    scua_return_bool(ctx, host(ctx).keyRepeat);
}

void keyboard_set_text_input(scua_ctx *ctx)
{
    arity(ctx, 1);
    bool enabled = boolean(ctx, 0);
    bool ok = enabled ? SDL_StartTextInput(window(ctx)) : SDL_StopTextInput(window(ctx));
    if (!ok)
        throw std::runtime_error(SDL_GetError());
}

void keyboard_has_text_input(scua_ctx *ctx)
{
    arity(ctx, 0);
    scua_return_bool(ctx, SDL_TextInputActive(window(ctx)));
}

void keyboard_has_screen_keyboard(scua_ctx *ctx)
{
    arity(ctx, 0);
    scua_return_bool(ctx, SDL_HasScreenKeyboardSupport());
}

void keyboard_is_modifier_active(scua_ctx *ctx)
{
    arity(ctx, 1);
    auto s = string(ctx, 0);
    SDL_Keymod mask;
    if (s == "shift")
        mask = SDL_KMOD_SHIFT;
    else if (s == "ctrl")
        mask = SDL_KMOD_CTRL;
    else if (s == "alt")
        mask = SDL_KMOD_ALT;
    else if (s == "gui")
        mask = SDL_KMOD_GUI;
    else if (s == "capslock")
        mask = SDL_KMOD_CAPS;
    else if (s == "numlock")
        mask = SDL_KMOD_NUM;
    else
        throw std::invalid_argument("unknown keyboard modifier");
    scua_return_bool(ctx, !host(ctx).ignoreInput && (SDL_GetModState() & mask) != 0);
}

void mouse_is_down(scua_ctx *ctx)
{
    arity(ctx, 1);
    int b = integer(ctx, 0, 1, 32);
    if (b == 2)
        b = SDL_BUTTON_RIGHT;
    else if (b == 3)
        b = SDL_BUTTON_MIDDLE;
    scua_return_bool(ctx, !host(ctx).ignoreInput &&
                              (SDL_GetMouseState(nullptr, nullptr) & SDL_BUTTON_MASK(b)) != 0);
}

void mouse_set_position(scua_ctx *ctx)
{
    arity(ctx, 1);
    float v[2];
    vector(ctx, 0, v, 2);
    double x = v[0], y = v[1];
    host(ctx).window->DPIToWindowCoords(&x, &y);
    SDL_WarpMouseInWindow(window(ctx), x, y);
}

void mouse_set_visible(scua_ctx *ctx)
{
    arity(ctx, 1);
    bool visible = boolean(ctx, 0);
    if (!(visible ? SDL_ShowCursor() : SDL_HideCursor()))
        throw std::runtime_error(SDL_GetError());
}

void mouse_is_visible(scua_ctx *ctx)
{
    arity(ctx, 0);
    scua_return_bool(ctx, SDL_CursorVisible());
}

void mouse_set_grabbed(scua_ctx *ctx)
{
    arity(ctx, 1);
    host(ctx).window->setMouseGrab(boolean(ctx, 0));
}

void mouse_is_grabbed(scua_ctx *ctx)
{
    arity(ctx, 0);
    scua_return_bool(ctx, host(ctx).window->isMouseGrabbed());
}

void mouse_set_relative_mode(scua_ctx *ctx)
{
    arity(ctx, 1);
    if (!SDL_SetWindowRelativeMouseMode(window(ctx), boolean(ctx, 0)))
        throw std::runtime_error(SDL_GetError());
}

void mouse_get_relative_mode(scua_ctx *ctx)
{
    arity(ctx, 0);
    scua_return_bool(ctx, SDL_GetWindowRelativeMouseMode(window(ctx)));
}

void mouse_has_cursor(scua_ctx *ctx)
{
    arity(ctx, 0);
    scua_return_bool(ctx, SDL_GetDefaultCursor() != nullptr);
}

void system_get_os(scua_ctx *ctx)
{
    arity(ctx, 0);
    returnString(ctx, SDL_GetPlatform());
}

void system_processor_count(scua_ctx *ctx)
{
    arity(ctx, 0);
    scua_return_int(ctx, SDL_GetNumLogicalCPUCores());
}

void system_memory_size(scua_ctx *ctx)
{
    arity(ctx, 0);
    scua_return_int(ctx, SDL_GetSystemRAM());
}

void system_power_info(scua_ctx *ctx)
{
    arity(ctx, 0);
    int seconds, percent;
    auto state = SDL_GetPowerInfo(&seconds, &percent);
    const char *s = "unknown";
    switch (state)
    {
    case SDL_POWERSTATE_ON_BATTERY:
        s = "battery";
        break;
    case SDL_POWERSTATE_NO_BATTERY:
        s = "nobattery";
        break;
    case SDL_POWERSTATE_CHARGING:
        s = "charging";
        break;
    case SDL_POWERSTATE_CHARGED:
        s = "charged";
        break;
    default:
        break;
    }
    Record r(ctx);
    r.str("state", s);
    r.integer("seconds", seconds);
    r.integer("percent", percent);
    r.done();
}

} // namespace
void registerPlatform(scua_partition *p, Host &state)
{
    registerObjectType(p, state, ObjectKind::Cursor);
    registerFunctions(
        p, state,
        {
            {"love_event", "post", guarded<event_post>, "post(name: string, payload: any)",
             "Queue a custom event for event(name, payload) on the next frame."},
            {"love_window", "title", guarded<window_title>, "title() -> string", "title."},
            {"love_window", "is_open", guarded<window_is_open>, "is_open() -> bool", "is open."},
            {"love_window", "has_focus", guarded<window_has_focus>, "has_focus() -> bool", "has focus."},
            {"love_window", "has_mouse_focus", guarded<window_has_mouse_focus>, "has_mouse_focus() -> bool",
             "has mouse focus."},
            {"love_window", "is_visible", guarded<window_is_visible>, "is_visible() -> bool", "is visible."},
            {"love_window", "is_minimized", guarded<window_is_minimized>, "is_minimized() -> bool",
             "is minimized."},
            {"love_window", "is_maximized", guarded<window_is_maximized>, "is_maximized() -> bool",
             "is maximized."},
            {"love_window", "is_occluded", guarded<window_is_occluded>, "is_occluded() -> bool",
             "is occluded."},
            {"love_window", "display_sleep_enabled", guarded<window_display_sleep_enabled>,
             "display_sleep_enabled() -> bool", "display sleep enabled."},
            {"love_window", "minimize", guarded<window_minimize>, "minimize()", "minimize."},
            {"love_window", "maximize", guarded<window_maximize>, "maximize()", "maximize."},
            {"love_window", "restore", guarded<window_restore>, "restore()", "restore."},
            {"love_window", "focus", guarded<window_focus>, "focus()", "focus."},
            {"love_window", "vsync", guarded<window_vsync>, "vsync() -> int", "vsync."},
            {"love_window", "display_count", guarded<window_display_count>, "display_count() -> int",
             "display count."},
            {"love_window", "dpi_scale", guarded<window_dpi_scale>, "dpi_scale() -> float", "dpi scale."},
            {"love_window", "native_dpi_scale", guarded<window_native_dpi_scale>,
             "native_dpi_scale() -> float", "native dpi scale."},
            {"love_window", "set_vsync", guarded<window_set_vsync>, "set_vsync(interval: int)", "set vsync."},
            {"love_window", "set_display_sleep_enabled", guarded<window_set_display_sleep_enabled>,
             "set_display_sleep_enabled(enabled: bool)", "set display sleep enabled."},
            {"love_window", "set_fullscreen", guarded<window_set_fullscreen>, "set_fullscreen(enabled: bool)",
             "set fullscreen."},
            {"love_window", "position", guarded<window_position>, "position() -> vec2", "position."},
            {"love_window", "set_position", guarded<window_set_position>, "set_position(position: vec2)",
             "set position."},
            {"love_window", "mode", guarded<window_mode>, "mode() -> any", "mode."},
            {"love_window", "display_name", guarded<window_display_name>,
             "display_name(index: int) -> string", "display name."},
            {"love_window", "desktop_dimensions", guarded<window_desktop_dimensions>,
             "desktop_dimensions(index: int = 0) -> vec2", "desktop dimensions."},
            {"love_window", "safe_area", guarded<window_safe_area>, "safe_area() -> vec4", "safe area."},
            {"love_window", "to_pixels", guarded<window_to_pixels>, "to_pixels(position: vec2) -> vec2",
             "to pixels."},
            {"love_window", "from_pixels", guarded<window_from_pixels>, "from_pixels(position: vec2) -> vec2",
             "from pixels."},
            {"love_timer", "get_time", guarded<timer_get_time>, "get_time() -> float", "get time."},
            {"love_timer", "get_delta", guarded<timer_get_delta>, "get_delta() -> float", "get delta."},
            {"love_timer", "get_average_delta", guarded<timer_get_average_delta>,
             "get_average_delta() -> float", "get average delta."},
            {"love_timer", "get_elapsed", guarded<timer_get_elapsed>, "get_elapsed() -> float",
             "get elapsed."},
            {"love_timer", "get_fps", guarded<timer_get_fps>, "get_fps() -> int", "get fps."},
            {"love_timer", "sleep", guarded<timer_sleep>, "sleep(seconds: float)", "sleep."},
            {"love_keyboard", "set_key_repeat", guarded<keyboard_set_key_repeat>,
             "set_key_repeat(enabled: bool)", "set key repeat."},
            {"love_keyboard", "has_key_repeat", guarded<keyboard_has_key_repeat>, "has_key_repeat() -> bool",
             "has key repeat."},
            {"love_keyboard", "set_text_input", guarded<keyboard_set_text_input>,
             "set_text_input(enabled: bool)", "set text input."},
            {"love_keyboard", "has_text_input", guarded<keyboard_has_text_input>, "has_text_input() -> bool",
             "has text input."},
            {"love_keyboard", "has_screen_keyboard", guarded<keyboard_has_screen_keyboard>,
             "has_screen_keyboard() -> bool", "has screen keyboard."},
            {"love_keyboard", "is_modifier_active", guarded<keyboard_is_modifier_active>,
             "is_modifier_active(modifier: string) -> bool", "is modifier active."},
            {"love_mouse", "is_down", guarded<mouse_is_down>, "is_down(button: int) -> bool", "is down."},
            {"love_mouse", "set_position", guarded<mouse_set_position>, "set_position(position: vec2)",
             "set position."},
            {"love_mouse", "set_visible", guarded<mouse_set_visible>, "set_visible(visible: bool)",
             "set visible."},
            {"love_mouse", "is_visible", guarded<mouse_is_visible>, "is_visible() -> bool", "is visible."},
            {"love_mouse", "set_grabbed", guarded<mouse_set_grabbed>, "set_grabbed(grabbed: bool)",
             "set grabbed."},
            {"love_mouse", "is_grabbed", guarded<mouse_is_grabbed>, "is_grabbed() -> bool", "is grabbed."},
            {"love_mouse", "set_relative_mode", guarded<mouse_set_relative_mode>,
             "set_relative_mode(relative: bool)", "set relative mode."},
            {"love_mouse", "get_relative_mode", guarded<mouse_get_relative_mode>,
             "get_relative_mode() -> bool", "get relative mode."},
            {"love_mouse", "has_cursor", guarded<mouse_has_cursor>, "has_cursor() -> bool", "has cursor."},
            {"love_system", "get_os", guarded<system_get_os>, "get_os() -> string", "get os."},
            {"love_system", "processor_count", guarded<system_processor_count>, "processor_count() -> int",
             "processor count."},
            {"love_system", "memory_size", guarded<system_memory_size>, "memory_size() -> int",
             "memory size."},
            {"love_system", "power_info", guarded<system_power_info>, "power_info() -> any", "power info."},
            {"love_mouse", "new_cursor", guarded<new_cursor>,
             "new_cursor(image: any, hot_x: int, hot_y: int) -> any",
             "Create a managed image cursor, at most 256x256."},
            {"love_mouse", "system_cursor", guarded<system_cursor>, "system_cursor(name: string) -> any",
             "Retain an upstream system cursor."},
            {"love_mouse", "set_cursor", guarded<set_cursor>, "set_cursor(cursor: any)",
             "Select and retain a cursor; nil restores the default."},
            {"love_mouse", "get_cursor", guarded<get_cursor>, "get_cursor() -> any",
             "Retain the selected cursor, or nil for the default."},
            {"love_mouse", "cursor_info", guarded<cursor_info>, "cursor_info(cursor: any) -> any",
             "Get cursor kind and system name."},
        });
}
} // namespace lovescua
