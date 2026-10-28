// Remaining desktop platform adapters. Dialog callbacks never enter SCUA.
#include "options.h"
#include <SDL3/SDL.h>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
namespace lovescua
{
struct DialogInbox
{
    struct Result
    {
        bool ready = false, failed = false;
        std::string error;
        std::vector<std::string> files;
        int filter = -1;
        int kind = 0;
    };
    std::mutex mutex;
    uint64_t next = 1;
    std::map<uint64_t, Result> results;
};
namespace
{
using namespace binding;
using W = love::window::Window;
SDL_Window *native(scua_ctx *c)
{
    return static_cast<SDL_Window *>(host(c).window->getHandle());
}
void push(scua_partition *p, scua_Handle a, scua_Handle v)
{
    if (scua_array_push(p, a, v) != SCUA_OK)
        throw std::runtime_error("platform result allocation failed");
}
size_t arrayLength(scua_partition *p, scua_Handle a)
{
    int kind;
    size_t size, count;
    if (scua_buffer_info(p, a, &kind, &size, &count) != SCUA_OK)
        throw std::invalid_argument("expected an array");
    return count;
}
int display(scua_ctx *c, int i = 0)
{
    return supplied(c, i) ? integer(c, i, 0, host(c).window->getDisplayCount() - 1) : 0;
}
void settings(scua_ctx *c)
{
    arity(c, 1);
    Options o(c, 0);
    auto w = host(c).window;
    int width, height;
    love::window::WindowSettings s;
    w->getWindow(width, height, s);
    width = o.integer("width", width, 64, 8192);
    height = o.integer("height", height, 64, 8192);
    s.fullscreen = o.boolean("fullscreen", s.fullscreen);
    auto type = o.str("fullscreen_type", s.fstype == W::FULLSCREEN_EXCLUSIVE ? "exclusive" : "desktop");
    if (!W::getConstant(type.c_str(), s.fstype))
        throw std::invalid_argument("unknown fullscreen type");
    s.vsync = o.integer("vsync", s.vsync, -1, 4);
    s.msaa = o.integer("msaa", s.msaa, 0, 64);
    s.stencil = o.boolean("stencil", s.stencil);
    s.depth = o.boolean("depth", s.depth);
    s.resizable = o.boolean("resizable", s.resizable);
    s.borderless = o.boolean("borderless", s.borderless);
    s.centered = o.boolean("centered", s.centered);
    s.usedpiscale = o.boolean("use_dpi_scale", s.usedpiscale);
    s.minwidth = o.integer("min_width", s.minwidth, 1, 8192);
    s.minheight = o.integer("min_height", s.minheight, 1, 8192);
    s.displayindex = o.integer("display", s.displayindex, 0, w->getDisplayCount() - 1);
    s.refreshrate = o.num("refresh_rate", s.refreshrate);
    if (s.refreshrate < 0 || s.refreshrate > 1000)
        throw std::invalid_argument("invalid refresh rate");
    if (o.has("x") || o.has("y"))
    {
        s.x = o.integer("x", s.x, -100000, 100000);
        s.y = o.integer("y", s.y, -100000, 100000);
        s.useposition = true;
        s.centered = false;
    }
    if (host(c).ignoreInput && s.fullscreen)
        throw std::invalid_argument("fullscreen is unavailable in no-input checks");
    if (!w->setWindow(width, height, &s))
        throw std::runtime_error(SDL_GetError());
    if (host(c).ignoreInput && !SDL_SetWindowFocusable(native(c), false))
        throw std::runtime_error(SDL_GetError());
}
void display_modes(scua_ctx *c)
{
    if (scua_arg_count(c) > 1)
        throw std::invalid_argument("expected optional display");
    auto modes = host(c).window->getFullscreenModes(display(c));
    auto p = scua_ctx_partition(c);
    auto a = scua_new_array(p, 0);
    for (auto &m : modes)
    {
        Record r(c);
        r.integer("width", m.width);
        r.integer("height", m.height);
        r.num("refresh_rate", m.refreshRate);
        push(p, a, r.value);
    }
    scua_return_value(c, a);
}
void orientation(scua_ctx *c)
{
    if (scua_arg_count(c) > 1)
        throw std::invalid_argument("expected optional display");
    const char *name = "unknown";
    W::getConstant(host(c).window->getDisplayOrientation(display(c)), name);
    returnString(c, name);
}
void theme(scua_ctx *c)
{
    arity(c, 0);
    const char *n = "unknown";
    W::getConstant(host(c).window->getSystemTheme(), n);
    returnString(c, n);
}
void fullscreen(scua_ctx *c)
{
    arity(c, 0);
    int width, height;
    love::window::WindowSettings s;
    host(c).window->getWindow(width, height, s);
    Record r(c);
    r.boolean("enabled", s.fullscreen);
    r.str("type", s.fstype == W::FULLSCREEN_EXCLUSIVE ? "exclusive" : "desktop");
    r.done();
}
void set_icon(scua_ctx *c)
{
    arity(c, 1);
    auto d = objectArgument<love::image::ImageData>(c, 0, ObjectKind::ImageData);
    if (d)
        scua_return_bool(c, host(c).window->setIcon(d));
}
void icon(scua_ctx *c)
{
    arity(c, 0);
    auto d = host(c).window->getIcon();
    if (d)
    {
        d->retain();
        returnObject(c, ObjectKind::ImageData, d);
    }
    else
        scua_return_value(c, scua_nil(scua_ctx_partition(c)));
}
void attention(scua_ctx *c)
{
    if (scua_arg_count(c) > 1)
        throw std::invalid_argument("expected optional continuous flag");
    if (!host(c).ignoreInput)
        host(c).window->requestAttention(supplied(c, 0) ? boolean(c, 0) : false);
}
void close_window(scua_ctx *c)
{
    arity(c, 0);
    host(c).quitting = true;
}
void high_dpi(scua_ctx *c)
{
    arity(c, 0);
    scua_return_bool(c, love::window::isHighDPIAllowed());
}
void clipboard(scua_ctx *c)
{
    arity(c, 0);
    auto text = SDL_GetClipboardText();
    if (!text)
        throw std::runtime_error(SDL_GetError());
    auto owned = std::unique_ptr<char, void (*)(void *)>(text, SDL_free);
    if (std::strlen(text) > 1024 * 1024)
        throw std::invalid_argument("clipboard exceeds 1 MiB");
    returnString(c, text);
}
void set_clipboard(scua_ctx *c)
{
    arity(c, 1);
    auto text = string(c, 0);
    if (text.size() > 1024 * 1024)
        throw std::invalid_argument("clipboard limit is 1 MiB");
    if (!SDL_SetClipboardText(text.c_str()))
        throw std::runtime_error(SDL_GetError());
}
void open_url(scua_ctx *c)
{
    arity(c, 1);
    auto url = string(c, 0);
    if (url.size() > 8192 ||
        (url.rfind("https://", 0) != 0 && url.rfind("http://", 0) != 0 && url.rfind("mailto:", 0) != 0))
        throw std::invalid_argument("URL must use http, https or mailto");
    scua_return_bool(c, SDL_OpenURL(url.c_str()));
}
void locales(scua_ctx *c)
{
    arity(c, 0);
    auto p = scua_ctx_partition(c);
    auto a = scua_new_array(p, 0);
    int count = 0;
    auto list = SDL_GetPreferredLocales(&count);
    if (!list && count)
        throw std::runtime_error(SDL_GetError());
    for (int i = 0; i < count; ++i)
    {
        std::string name = list[i]->language ? list[i]->language : "";
        if (list[i]->country)
            name += "_" + std::string(list[i]->country);
        push(p, a, scua_string(p, name.data(), name.size()));
    }
    SDL_free(list);
    scua_return_value(c, a);
}
void is_scancode_down(scua_ctx *c)
{
    arity(c, 1);
    auto name = string(c, 0);
    auto code = SDL_GetScancodeFromName(name.c_str());
    if (code == SDL_SCANCODE_UNKNOWN)
        throw std::invalid_argument("unknown scancode");
    int count = 0;
    auto keys = SDL_GetKeyboardState(&count);
    scua_return_bool(c, !host(c).ignoreInput && code < count && keys[code]);
}
void scancode_from_key(scua_ctx *c)
{
    arity(c, 1);
    auto key = SDL_GetKeyFromName(string(c, 0).c_str());
    if (key == SDLK_UNKNOWN)
        throw std::invalid_argument("unknown key");
    auto code = SDL_GetScancodeFromKey(key, nullptr);
    returnString(c, SDL_GetScancodeName(code));
}
void key_from_scancode(scua_ctx *c)
{
    arity(c, 1);
    auto code = SDL_GetScancodeFromName(string(c, 0).c_str());
    if (code == SDL_SCANCODE_UNKNOWN)
        throw std::invalid_argument("unknown scancode");
    returnString(c, SDL_GetKeyName(SDL_GetKeyFromScancode(code, SDL_KMOD_NONE, false)));
}
void screen_keyboard_visible(scua_ctx *c)
{
    arity(c, 0);
    scua_return_bool(c, SDL_ScreenKeyboardShown(native(c)));
}
void global_mouse(scua_ctx *c)
{
    arity(c, 0);
    float x = 0, y = 0;
    if (!host(c).ignoreInput)
        SDL_GetGlobalMouseState(&x, &y);
    returnVec(c, x, y);
}
void cursor_supported(scua_ctx *c)
{
    arity(c, 0);
    scua_return_bool(c, true);
}
void hardware_info(scua_ctx *c)
{
    arity(c, 0);
    Uint32 wanted = SDL_INIT_AUDIO | SDL_INIT_JOYSTICK | SDL_INIT_GAMEPAD | SDL_INIT_SENSOR;
    Uint32 owned = wanted & ~SDL_WasInit(wanted);
    if (!SDL_InitSubSystem(owned))
        throw std::runtime_error(SDL_GetError());
    struct Quit
    {
        Uint32 flags;
        ~Quit()
        {
            SDL_QuitSubSystem(flags);
        }
    } quit{owned};
    int joysticks = 0, sensors = 0, outputs = 0, inputs = 0;
    SDL_free(SDL_GetJoysticks(&joysticks));
    SDL_free(SDL_GetSensors(&sensors));
    SDL_free(SDL_GetAudioPlaybackDevices(&outputs));
    SDL_free(SDL_GetAudioRecordingDevices(&inputs));
    Record r(c);
    r.integer("joysticks", joysticks);
    r.integer("sensors", sensors);
    r.integer("playback_devices", outputs);
    r.integer("recording_devices", inputs);
    r.boolean("capture_started", false);
    r.str("audio_driver", SDL_GetCurrentAudioDriver() ? SDL_GetCurrentAudioDriver() : "unknown");
    r.done();
}
void has_background_music(scua_ctx *c)
{
    arity(c, 0);
    // Upstream System::hasBackgroundMusic probes Android/iOS and returns false
    // on desktop platforms (src/modules/system/System.cpp).
    scua_return_bool(c, false);
}
void vibrate(scua_ctx *c)
{
    arity(c, 1);
    auto seconds = number(c, 0);
    if (seconds < 0 || seconds > 10)
        throw std::invalid_argument("vibration must be in [0,10] seconds");
    scua_return_bool(c, false);
}
struct DialogRequest
{
    std::shared_ptr<DialogInbox> inbox;
    uint64_t id;
    std::vector<std::string> names, patterns;
    std::vector<SDL_DialogFileFilter> filters;
    std::string location;
};
void SDLCALL dialogCallback(void *context, const char *const *files, int filter) noexcept
{
    std::unique_ptr<DialogRequest> request(static_cast<DialogRequest *>(context));
    auto box = request->inbox;
    std::lock_guard<std::mutex> lock(box->mutex);
    auto it = box->results.find(request->id);
    if (it == box->results.end())
        return;
    auto &r = it->second;
    try
    {
        r.filter = filter;
        if (!files)
        {
            r.failed = true;
            r.error = SDL_GetError();
        }
        else
            for (size_t i = 0; files[i]; ++i)
            {
                if (i >= 256 || std::strlen(files[i]) > 16384)
                    throw std::runtime_error("dialog selection exceeds limits");
                r.files.emplace_back(files[i]);
            }
    }
    catch (...)
    {
        r.failed = true;
        r.files.clear();
    }
    r.ready = true;
}
void file_dialog(scua_ctx *c)
{
    arity(c, 2);
    if (host(c).ignoreInput)
        throw std::invalid_argument("interactive dialogs are disabled in no-input checks");
    auto kind = string(c, 0);
    int k = kind == "open" ? 0 : kind == "folder" ? 1 : kind == "save" ? 2 : -1;
    if (k < 0)
        throw std::invalid_argument("dialog kind must be open, folder or save");
    Options o(c, 1);
    bool multiple = o.boolean("multiple", false), attach = o.boolean("attach", true);
    if (k == 2 && multiple)
        throw std::invalid_argument("save dialog cannot select multiple files");
    if (!host(c).dialogs)
        host(c).dialogs = std::make_shared<DialogInbox>();
    auto r = std::make_unique<DialogRequest>();
    r->inbox = host(c).dialogs;
    r->location = o.str("default_name", "");
    if (o.has("filters"))
    {
        auto p = scua_ctx_partition(c);
        auto a = o.field("filters");
        if (scua_typeof(p, a) != SCUA_TYPE_ARRAY || arrayLength(p, a) > 16)
            throw std::invalid_argument("dialog filters must be an array of at most 16 records");
        for (size_t i = 0; i < arrayLength(p, a); ++i)
        {
            Options f(p, scua_index(p, a, i));
            auto n = f.str("name", ""), pattern = f.str("pattern", "");
            if (n.empty() || pattern.empty() || n.size() > 256 || pattern.size() > 512)
                throw std::invalid_argument("invalid dialog filter");
            r->names.push_back(n);
            r->patterns.push_back(pattern);
        }
        for (size_t i = 0; i < r->names.size(); ++i)
            r->filters.push_back({r->names[i].c_str(), r->patterns[i].c_str()});
    }
    {
        std::lock_guard<std::mutex> lock(r->inbox->mutex);
        if (r->inbox->results.size() >= 32)
            throw std::invalid_argument("dialog request limit is 32; release completed requests");
        r->id = r->inbox->next++;
        r->inbox->results[r->id].kind = k;
    }
    auto id = r->id;
    auto request = r.release();
    auto window = attach ? native(c) : nullptr;
    auto filters = request->filters.empty() ? nullptr : request->filters.data();
    auto location = request->location.empty() ? nullptr : request->location.c_str();
    if (k == 0)
        SDL_ShowOpenFileDialog(dialogCallback, request, window, filters, request->filters.size(), location,
                               multiple);
    else if (k == 1)
        SDL_ShowOpenFolderDialog(dialogCallback, request, window, location, multiple);
    else
        SDL_ShowSaveFileDialog(dialogCallback, request, window, filters, request->filters.size(), location);
    scua_return_int(c, id);
}
DialogInbox::Result dialogResult(scua_ctx *c, bool requireReady)
{
    auto id = integer(c, 0, 1, INT_MAX);
    auto box = host(c).dialogs;
    if (!box)
        throw std::invalid_argument("unknown dialog request");
    std::lock_guard<std::mutex> lock(box->mutex);
    auto it = box->results.find(id);
    if (it == box->results.end())
        throw std::invalid_argument("unknown dialog request");
    if (requireReady && (!it->second.ready || it->second.failed || it->second.files.empty()))
        throw std::invalid_argument("dialog has no successful selection");
    return it->second;
}
void poll_dialog(scua_ctx *c)
{
    arity(c, 1);
    auto state = dialogResult(c, false);
    if (!state.ready)
    {
        scua_return_value(c, scua_nil(scua_ctx_partition(c)));
        return;
    }
    Record r(c);
    r.boolean("cancelled", !state.failed && state.files.empty());
    r.boolean("failed", state.failed);
    r.str("error", state.error);
    r.integer("filter", state.filter);
    auto a = scua_new_array(r.p, 0);
    for (auto &s : state.files)
        push(r.p, a, scua_string(r.p, s.data(), s.size()));
    r.set("files", a);
    r.done();
}
void release_dialog(scua_ctx *c)
{
    arity(c, 1);
    auto id = integer(c, 0, 1, INT_MAX);
    if (auto box = host(c).dialogs)
    {
        std::lock_guard<std::mutex> lock(box->mutex);
        box->results.erase(id);
    }
}
std::filesystem::path selectedPath(const DialogInbox::Result &r, const std::string &relative)
{
    if (r.kind != 1 || r.files.size() != 1)
        throw std::invalid_argument("expected one selected folder");
    auto root = std::filesystem::canonical(r.files[0]);
    std::filesystem::path path(relative);
    if (path.is_absolute() || relative.size() > 4096)
        throw std::invalid_argument("expected a relative selection path");
    auto resolved = std::filesystem::weakly_canonical(root / path);
    auto inside = resolved.lexically_relative(root);
    if (inside.empty() || *inside.begin() == "..")
        throw std::invalid_argument("selection path escapes the chosen folder");
    return resolved;
}
void read_selection(scua_ctx *c)
{
    if (scua_arg_count(c) < 2 || scua_arg_count(c) > 3)
        throw std::invalid_argument("expected request, selection index and optional relative path");
    auto result = dialogResult(c, true);
    auto index = integer(c, 1, 0, result.files.size() - 1);
    std::filesystem::path path;
    if (result.kind == 0)
    {
        if (supplied(c, 2))
            throw std::invalid_argument("an open-file selection takes no relative path");
        path = result.files[index];
    }
    else if (result.kind == 1)
    {
        if (index != 0 || !supplied(c, 2))
            throw std::invalid_argument("folder reads require index zero and a relative path");
        path = selectedPath(result, string(c, 2));
    }
    else
        throw std::invalid_argument("reading requires an open-file or folder selection");
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    auto size = file.tellg();
    if (!file || size < 0 || size > 32 * 1024 * 1024)
        throw std::runtime_error("cannot read selection or file exceeds 32 MiB");
    std::vector<uint8_t> data(size);
    file.seekg(0);
    file.read(reinterpret_cast<char *>(data.data()), data.size());
    if (!file)
        throw std::runtime_error("selected file read failed");
    scua_return_bytes(c, data.data(), data.size());
}
void list_selection(scua_ctx *c)
{
    if (scua_arg_count(c) < 1 || scua_arg_count(c) > 2)
        throw std::invalid_argument("expected folder request and optional relative path");
    auto result = dialogResult(c, true);
    auto path = selectedPath(result, supplied(c, 1) ? string(c, 1) : "");
    std::vector<std::string> names;
    for (auto &entry : std::filesystem::directory_iterator(path))
    {
        if (names.size() >= 4096)
            throw std::invalid_argument("selected folder listing exceeds 4096 entries");
        names.push_back(entry.path().filename().string());
    }
    std::sort(names.begin(), names.end());
    auto p = scua_ctx_partition(c);
    auto a = scua_new_array(p, 0);
    for (auto &name : names)
        push(p, a, scua_string(p, name.data(), name.size()));
    scua_return_value(c, a);
}
void write_selection(scua_ctx *c)
{
    arity(c, 2);
    auto result = dialogResult(c, true);
    if (result.kind != 2 || result.files.size() != 1)
        throw std::invalid_argument("writing requires one save-file selection");
    const uint8_t *b;
    size_t n;
    if (scua_arg_bytes(c, 1, &b, &n) != SCUA_OK || n > 32 * 1024 * 1024)
        throw std::invalid_argument("expected bytes up to 32 MiB");
    std::ofstream file(result.files[0], std::ios::binary | std::ios::trunc);
    file.write(reinterpret_cast<const char *>(b), n);
    file.close();
    if (!file)
        throw std::runtime_error("selected file write failed");
}
void message_box(scua_ctx *c)
{
    arity(c, 3);
    if (host(c).ignoreInput)
        throw std::invalid_argument("interactive dialogs are disabled in no-input checks");
    auto title = string(c, 0), message = string(c, 1);
    if (title.size() > 1024 || message.size() > 65536)
        throw std::invalid_argument("message box text exceeds limits");
    Options o(c, 2);
    W::MessageBoxData d;
    d.title = title;
    d.message = message;
    auto type = o.str("type", "info");
    if (!W::getConstant(type.c_str(), d.type))
        throw std::invalid_argument("unknown message box type");
    d.attachToWindow = o.boolean("attach", true);
    auto p = scua_ctx_partition(c);
    if (o.has("buttons"))
    {
        auto a = o.field("buttons");
        if (scua_typeof(p, a) != SCUA_TYPE_ARRAY || arrayLength(p, a) < 1 || arrayLength(p, a) > 16)
            throw std::invalid_argument("expected 1..16 buttons");
        for (size_t i = 0; i < arrayLength(p, a); ++i)
        {
            size_t n;
            auto text = scua_to_lstring(p, scua_index(p, a, i), &n);
            if (!text || n > 256 || std::memchr(text, 0, n))
                throw std::invalid_argument("invalid button label");
            d.buttons.emplace_back(text, n);
        }
    }
    else
        d.buttons = {"OK"};
    d.enterButtonIndex = o.integer("enter", 0, 0, d.buttons.size() - 1);
    d.escapeButtonIndex = o.integer("escape", d.buttons.size() - 1, 0, d.buttons.size() - 1);
    scua_return_int(c, host(c).window->showMessageBox(d));
}
} // namespace
namespace
{
void reload(scua_ctx *c)
{
    arity(c, 0);
    host(c).reloadRequested = true;
}
void reload_status(scua_ctx *c)
{
    arity(c, 0);
    Record r(c);
    r.boolean("requested", host(c).reloadRequested);
    r.str("error", host(c).reloadError);
    r.done();
}
} // namespace
void registerPlatformExtras(scua_partition *p, Host &s)
{
    registerFunctions(
        p, s,
        {{"love_keyboard", "is_scancode_down", guarded<is_scancode_down>,
          "is_scancode_down(scancode: string) -> bool",
          "Poll a physical key position independently of keyboard layout."},
         {"love_keyboard", "scancode_from_key", guarded<scancode_from_key>,
          "scancode_from_key(key: string) -> string", "Map an SDL key name to a scancode name."},
         {"love_keyboard", "key_from_scancode", guarded<key_from_scancode>,
          "key_from_scancode(scancode: string) -> string", "Map a scancode name to a key name."},
         {"love_keyboard", "screen_keyboard_visible", guarded<screen_keyboard_visible>,
          "screen_keyboard_visible() -> bool", "Check native on-screen keyboard visibility."},
         {"love_mouse", "global_position", guarded<global_mouse>, "global_position() -> vec2",
          "Get desktop pointer coordinates; no-input mode returns zero."},
         {"love_mouse", "is_cursor_supported", guarded<cursor_supported>, "is_cursor_supported() -> bool",
          "The macOS host supports native cursors."},
         {"love_system", "hardware_info", guarded<hardware_info>, "hardware_info() -> any",
          "Enumerate actual input/audio/sensor presence without starting microphone capture."},
         {"love_event", "reload", guarded<reload>, "reload()",
          "Request a staged script reload at the next frame boundary."},
         {"love_event", "reload_status", guarded<reload_status>, "reload_status() -> any",
          "Read reload request/error state."},
         {"love_window", "configure", guarded<settings>, "configure(settings: any)",
          "Update checked display, fullscreen, size, position, framebuffer and DPI settings."},
         {"love_window", "fullscreen_modes", guarded<display_modes>,
          "fullscreen_modes(display: int = 0) -> any", "Enumerate display resolutions and refresh rates."},
         {"love_window", "display_orientation", guarded<orientation>,
          "display_orientation(display: int = 0) -> string", "Read native display orientation."},
         {"love_window", "system_theme", guarded<theme>, "system_theme() -> string",
          "Read native light/dark/unknown theme."},
         {"love_window", "fullscreen", guarded<fullscreen>, "fullscreen() -> any",
          "Read fullscreen state and type."},
         {"love_window", "set_icon", guarded<set_icon>, "set_icon(data: any) -> bool",
          "Set a window icon from ImageData; support is platform-dependent."},
         {"love_window", "icon", guarded<icon>, "icon() -> any", "Get a retained icon image or nil."},
         {"love_window", "request_attention", guarded<attention>,
          "request_attention(continuous: bool = false)", "Request native user attention."},
         {"love_window", "close", guarded<close_window>, "close()", "Request orderly game/window shutdown."},
         {"love_window", "high_dpi_allowed", guarded<high_dpi>, "high_dpi_allowed() -> bool",
          "Check native high-DPI policy."},
         {"love_window", "show_message_box", guarded<message_box>,
          "show_message_box(title: string, message: string, settings: any) -> int",
          "Show a native message box with explicit button and default indices."},
         {"love_window", "show_file_dialog", guarded<file_dialog>,
          "show_file_dialog(kind: string, settings: any) -> int",
          "Open an asynchronous native chooser; poll by request id."},
         {"love_window", "poll_file_dialog", guarded<poll_dialog>, "poll_file_dialog(request: int) -> any",
          "Get nil while pending or a completed selection/error record."},
         {"love_window", "release_file_dialog", guarded<release_dialog>, "release_file_dialog(request: int)",
          "Release a request and its selected-file permissions."},
         {"love_window", "read_selected_file", guarded<read_selection>,
          "read_selected_file(request: int, index: int, relative: string = nil) -> bytes",
          "Read only an explicitly chosen open-file selection, up to 32 MiB."},
         {"love_window", "list_selected_folder", guarded<list_selection>,
          "list_selected_folder(request: int, relative: string = nil) -> any",
          "List only inside an explicitly selected folder."},
         {"love_window", "write_selected_file", guarded<write_selection>,
          "write_selected_file(request: int, encoded: bytes)",
          "Write only an explicitly chosen save-file selection, up to 32 MiB."},
         {"love_system", "clipboard_text", guarded<clipboard>, "clipboard_text() -> string",
          "Read text from the system clipboard."},
         {"love_system", "set_clipboard_text", guarded<set_clipboard>, "set_clipboard_text(text: string)",
          "Set text on the system clipboard."},
         {"love_system", "open_url", guarded<open_url>, "open_url(url: string) -> bool",
          "Open an http/https/mailto URL with the default application."},
         {"love_system", "preferred_locales", guarded<locales>, "preferred_locales() -> any",
          "Read preferred locale names."},
         {"love_system", "has_background_music", guarded<has_background_music>,
          "has_background_music() -> bool",
          "Return false, matching upstream desktop behavior; background-music detection is mobile-only."},
         {"love_system", "vibrate", guarded<vibrate>, "vibrate(seconds: float) -> bool",
          "Report unavailable system vibration on the macOS host; gamepad rumble is separate."}});
}
} // namespace lovescua
