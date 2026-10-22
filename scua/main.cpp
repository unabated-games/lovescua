// SCUA host for this altered LÖVE fork. See ../license.txt.
#include "audio/Audio.h"
#include "bindings.h"
#include "filesystem/FileData.h"
#include "font/freetype/Font.h"
#include "image/Image.h"
#include "joystick/sdl/JoystickModule.h"
#include "mouse/Mouse.h"
#include "sensor/Sensor.h"
#include "sensor_events.h"

#include <SDL3/SDL.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
struct Options
{
    std::filesystem::path script;
    std::filesystem::path declarations;
    std::string screenshot;
    std::string saveDirectory, userId;
    std::string renderer = "metal";
    uint64_t frames = 0;
    double fixedDt = 0;
    bool noInput = false, check = false, headless = false, watch = false, reportResources = false;
    std::vector<std::string> mounts;
};

std::filesystem::path runtimeModules()
{
    const char *base = SDL_GetBasePath();
    if (!base)
        throw std::runtime_error(SDL_GetError());
    auto root = std::filesystem::path(base);
    for (auto candidate : {root / "../share/lovescua", root / "lovescua-modules"})
        if (std::filesystem::is_regular_file(candidate / "love.scua"))
            return std::filesystem::canonical(candidate);
    throw std::runtime_error(
        "SCUA runtime modules are missing beside the executable; rebuild or reinstall lovescua");
}

void usage()
{
    std::cout << "Usage: lovescua [--frames N] [--fixed-dt SECONDS] [--renderer metal|opengl]\n"
                 "                [--screenshot FILE.png] [--verify] [--no-input] GAME.scua|DIRECTORY\n"
                 "                [--mount RESOURCE_DIRECTORY_OR_PACK] (repeatable)\n"
                 "                [--save-dir DIRECTORY] [--user-id APP_ID] (opt-in saves)\n"
                 "                [--watch] (reload changed SCUA files; preserve snapshot/restore state)\n"
                 "       lovescua --headless [--frames N] [--verify] GAME (non-graphical bindings)\n"
                 "       lovescua --check GAME.scua|DIRECTORY (no window; loads top-level definitions)\n"
                 "       lovescua --write-decls DIRECTORY\n"
                 "       lovescua --print-module-path\n"
                 "       lovescua --version\n";
}

template <typename T> struct Release
{
    void operator()(T *object) const
    {
        if (object)
            object->release();
    }
};
template <typename T> using EnginePtr = std::unique_ptr<T, Release<T>>;
struct DestroyPartition
{
    lovescua::Host *host;
    bool report;
    void operator()(scua_partition *partition) const noexcept
    {
        // Resources must be retired while the host and graphics context live,
        // including when loading, drawing or verification throws an exception.
        for (auto &event : host->pendingEvents)
        {
            scua_unpin_global(partition, event.first);
            scua_unpin_global(partition, event.second);
        }
        host->pendingEvents.clear();
        if (scua_partition_free_checked(partition) != SCUA_OK || host->liveImages || host->liveQuads ||
            host->liveObjects)
        {
            std::fputs("lovescua: managed resource teardown failed\n", stderr);
            std::abort();
        }
        if (report && host->imagesCreated)
            std::cout << "Resources released: " << host->imagesDestroyed << " images, "
                      << host->quadsDestroyed << " quads\n";
    }
};
using Partition = std::unique_ptr<scua_partition, DestroyPartition>;

struct Scope
{
    scua_partition *p;
    uint32_t token;
    explicit Scope(scua_partition *p) : p(p), token(scua_scope_enter(p)) {}
    ~Scope()
    {
        scua_scope_leave(p, token);
    }
};

class Callbacks
{
    scua_partition *p;

  public:
    scua_Handle load{}, update{}, draw{}, keypressed{}, keyreleased{}, mousepressed{}, mousereleased{},
        mousemoved{}, wheelmoved{}, textinput{}, textedited{}, focus{}, mousefocus{}, visible{},
        touchpressed{}, touchreleased{}, touchmoved{}, joystickadded{}, joystickremoved{}, joystickpressed{},
        joystickreleased{}, joystickaxis{}, gamepadpressed{}, gamepadreleased{}, gamepadaxis{}, event{},
        resize{}, quit{}, verify{}, sensorupdated{}, gamepadsensorupdated{}, snapshot{}, restore{};
    explicit Callbacks(scua_partition *p, bool requireDraw = true) : p(p)
    {
        Scope scope(p);
        load = lookup("load");
        snapshot = lookup("snapshot");
        restore = lookup("restore");
        update = lookup("update");
        draw = lookup("draw");
        keypressed = lookup("keypressed");
        resize = lookup("resize");
        keyreleased = lookup("keyreleased");
        mousepressed = lookup("mousepressed");
        mousereleased = lookup("mousereleased");
        mousemoved = lookup("mousemoved");
        wheelmoved = lookup("wheelmoved");
        textinput = lookup("textinput");
        textedited = lookup("textedited");
        focus = lookup("focus");
        mousefocus = lookup("mousefocus");
        visible = lookup("visible");
        touchpressed = lookup("touchpressed");
        touchreleased = lookup("touchreleased");
        touchmoved = lookup("touchmoved");
        joystickadded = lookup("joystickadded");
        joystickremoved = lookup("joystickremoved");
        joystickpressed = lookup("joystickpressed");
        joystickreleased = lookup("joystickreleased");
        joystickaxis = lookup("joystickaxis");
        gamepadpressed = lookup("gamepadpressed");
        gamepadreleased = lookup("gamepadreleased");
        gamepadaxis = lookup("gamepadaxis");
        event = lookup("event");
        quit = lookup("quit");
        verify = lookup("verify");
        sensorupdated = lookup("sensorupdated");
        gamepadsensorupdated = lookup("gamepadsensorupdated");
        if (requireDraw && !present(draw))
            throw std::runtime_error("game must define fn draw()");
    }
    scua_Handle lookup(const char *name)
    {
        auto value = scua_get_global(p, name);
        if (scua_typeof(p, value) == SCUA_TYPE_NIL || !scua_handle_valid(p, value))
            return {};
        return scua_pin_global(p, value);
    }
    bool present(scua_Handle fn) const
    {
        return scua_handle_valid(p, fn);
    }
    void call(scua_Handle fn, const char *name, const scua_Handle *args = nullptr, int count = 0)
    {
        if (!present(fn))
            return;
        char error[4096] = {};
        if (scua_pcall(p, fn, args, count, nullptr, error, sizeof(error)) != SCUA_OK)
            throw std::runtime_error(std::string(name) + ": " + error);
    }
    ~Callbacks()
    {
        for (auto value : {load,
                           update,
                           draw,
                           keypressed,
                           keyreleased,
                           mousepressed,
                           mousereleased,
                           mousemoved,
                           wheelmoved,
                           textinput,
                           textedited,
                           focus,
                           mousefocus,
                           visible,
                           touchpressed,
                           touchreleased,
                           touchmoved,
                           joystickadded,
                           joystickremoved,
                           joystickpressed,
                           joystickreleased,
                           joystickaxis,
                           gamepadpressed,
                           gamepadreleased,
                           gamepadaxis,
                           event,
                           resize,
                           quit,
                           verify,
                           sensorupdated,
                           gamepadsensorupdated,
                           snapshot,
                           restore})
            if (present(value))
                scua_unpin_global(p, value);
    }
};

struct Screenshot
{
    std::string path, error;
    bool captured = false;
    static void capture(const love::graphics::Graphics::ScreenshotInfo *info, love::image::ImageData *image,
                        void *) noexcept
    {
        auto &self = *static_cast<Screenshot *>(info->data);
        try
        {
            if (!image)
                throw std::runtime_error("renderer returned no screenshot");
            EnginePtr<love::filesystem::FileData> png(
                image->encode(love::image::FormatHandler::ENCODED_PNG, "capture.png", false));
            std::ofstream file(self.path, std::ios::binary);
            file.write(static_cast<const char *>(png->getData()),
                       static_cast<std::streamsize>(png->getSize()));
            if (!file)
                throw std::runtime_error("could not write screenshot: " + self.path);
            self.captured = true;
        }
        catch (const std::exception &error)
        {
            self.error = error.what();
        }
        catch (...)
        {
            self.error = "screenshot failed";
        }
    }
};
#include "game.inc"
} // namespace

int main(int argc, char **argv)
{
    try
    {
        if (scua_abi_version() != SCUA_ABI_VERSION || scua_version() != ((0u << 16) | (37u << 8)))
            throw std::runtime_error("this host requires the matching SCUA 0.37.0 SDK and ABI");
        Options options;
        bool verify = false;
        for (int i = 1; i < argc; ++i)
        {
            std::string arg = argv[i];
            auto value = [&]() -> std::string
            {
                if (++i >= argc)
                    throw std::invalid_argument("missing value after " + arg);
                return argv[i];
            };
            if (arg == "--help" || arg == "-h")
            {
                usage();
                return 0;
            }
            if (arg == "--print-module-path")
            {
                std::cout << runtimeModules().string() << "\n";
                return 0;
            }
            if (arg == "--version")
            {
                std::cout << "lovescua (LÖVE C++ engine + " << scua_version_string() << ")\n";
                return 0;
            }
            if (arg == "--frames")
            {
                auto text = value();
                size_t end = 0;
                if (text.empty() || text[0] == '-')
                    throw std::invalid_argument("--frames must be positive");
                options.frames = std::stoull(text, &end);
                if (end != text.size() || options.frames == 0)
                    throw std::invalid_argument("--frames must be positive");
            }
            else if (arg == "--fixed-dt")
            {
                auto text = value();
                size_t end = 0;
                options.fixedDt = std::stod(text, &end);
                if (end != text.size() || !std::isfinite(options.fixedDt) || options.fixedDt <= 0 ||
                    options.fixedDt > 0.25)
                    throw std::invalid_argument("--fixed-dt must be in (0, 0.25]");
            }
            else if (arg == "--renderer")
                options.renderer = value();
            else if (arg == "--screenshot")
                options.screenshot = value();
            else if (arg == "--write-decls")
                options.declarations = value();
            else if (arg == "--save-dir")
                options.saveDirectory = value();
            else if (arg == "--user-id")
                options.userId = value();
            else if (arg == "--mount")
                options.mounts.push_back(value());
            else if (arg == "--no-input")
                options.noInput = true;
            else if (arg == "--headless")
                options.headless = true;
            else if (arg == "--watch")
                options.watch = true;
            else if (arg == "--check")
                options.check = true;
            else if (arg == "--verify")
                verify = true;
            else if (arg[0] == '-')
                throw std::invalid_argument("unknown option: " + arg);
            else if (!options.script.empty())
                throw std::invalid_argument("expected one game path");
            else
                options.script = arg;
        }
        if (options.renderer != "metal" && options.renderer != "opengl")
            throw std::invalid_argument("--renderer must be metal or opengl");
        if (!options.screenshot.empty() && !options.frames)
            throw std::invalid_argument("--screenshot requires --frames");
        if (verify && !options.frames)
            throw std::invalid_argument("--verify requires --frames");

        options.reportResources = verify;
        lovescua::Host host;
        host.ignoreInput = options.noInput;
        if (!options.declarations.empty())
        {
            Partition partition(scua_partition_new(), DestroyPartition{&host, verify});
            if (!partition)
                throw std::runtime_error("could not create SCUA partition");
            lovescua::registerBindings(partition.get(), host);
            std::filesystem::create_directories(options.declarations);
            for (const auto &module : lovescua::bindingModules())
                if (scua_write_decls(partition.get(), module.c_str(),
                                     (options.declarations / (module + ".scua")).c_str()) != SCUA_OK)
                    throw std::runtime_error("could not write declarations");
            return 0;
        }
        if (options.script.empty())
        {
            usage();
            return 2;
        }
        if (std::filesystem::is_directory(options.script))
            options.script /= "main.scua";
        options.script = std::filesystem::canonical(options.script);
        if (options.check)
        {
            host.checking = true;
            Partition partition(scua_partition_new(), DestroyPartition{&host, false});
            if (!partition)
                throw std::runtime_error("could not create SCUA partition");
            lovescua::registerBindings(partition.get(), host);
            char error[4096] = {};
            if (scua_use_project(partition.get(), options.script.parent_path().c_str(), error,
                                 sizeof(error)) != SCUA_OK)
                throw std::runtime_error(error);
            if (scua_add_module_path(partition.get(), runtimeModules().c_str()) != SCUA_OK)
                throw std::runtime_error("could not add LÖVE module path");
            for (const auto &mount : options.mounts)
                if (scua_mount(partition.get(), mount.c_str(), error, sizeof(error)) != SCUA_OK)
                    throw std::runtime_error(error);
            if (scua_load_script_file(partition.get(), options.script.c_str(), 64 * 1024 * 1024, 1000000,
                                      error, sizeof(error)) != SCUA_OK)
                throw std::runtime_error(error);
            std::cout
                << "PASS: SCUA script definitions loaded without a window (callbacks were not executed)\n";
            return 0;
        }
        if (!options.mounts.empty())
        {
            Partition mounting(scua_partition_new(), DestroyPartition{&host, false});
            char error[4096]{};
            for (auto &mount : options.mounts)
                if (scua_mount(mounting.get(), mount.c_str(), error, sizeof(error)) != SCUA_OK)
                    throw std::runtime_error(error);
        }
        if (options.headless)
        {
            EnginePtr<love::image::Image> image(new love::image::Image());
            host.cpuOnly = true;
            auto game = Game::create(host, options);
            SourceWatch watch(options);
            uint64_t frames = options.frames ? options.frames : 1, executed = 0;
            for (uint64_t i = 0; i < frames && !game->host.quitting; ++i)
            {
                if (game->host.reloadRequested || (options.watch && watch.changed(options)))
                    reloadGame(game, options);
                Scope scope(game->partition.get());
                auto dt = options.fixedDt ? options.fixedDt : 1.0 / 60;
                game->host.delta = dt;
                game->host.elapsed += dt;
                ++game->host.frameCount;
                game->host.averageDelta = game->host.elapsed / game->host.frameCount;
                auto arg = scua_num(game->partition.get(), dt);
                game->callbacks->call(game->callbacks->update, "update", &arg, 1);
                ++executed;
            }
            if (verify)
            {
                if (executed != frames)
                    throw std::runtime_error("game quit before requested frame count");
                if (!game->callbacks->present(game->callbacks->verify))
                    throw std::runtime_error("--verify requires fn verify()");
                Scope scope(game->partition.get());
                game->callbacks->call(game->callbacks->verify, "verify");
            }
            {
                Scope scope(game->partition.get());
                game->callbacks->call(game->callbacks->quit, "quit");
            }
            return 0;
        }
        // Keep SDL and every SCUA callback on the main thread. The window retains
        // graphics; close and detach it before releasing the engine modules.
        EnginePtr<love::window::sdl::Window> window(new love::window::sdl::Window());
        EnginePtr<love::font::freetype::Font> font(new love::font::freetype::Font());
        EnginePtr<love::image::Image> image(new love::image::Image());
        love::graphics::setRenderers(
            {options.renderer == "metal" ? love::graphics::RENDERER_METAL : love::graphics::RENDERER_OPENGL});
        EnginePtr<love::graphics::Graphics> graphics(love::graphics::Graphics::createInstance());
        EnginePtr<love::joystick::sdl::JoystickModule> joysticks(new love::joystick::sdl::JoystickModule());
        host.joysticks = joysticks.get();
        host.window = window.get();
        host.graphics = graphics.get();
        struct CloseWindow
        {
            love::window::sdl::Window *window;
            ~CloseWindow()
            {
                window->close();
                window->setGraphics(nullptr);
            }
        } closeWindow{window.get()};
        if (!window->setWindow(960, 640))
            throw std::runtime_error(SDL_GetError());
        if (options.noInput && !SDL_SetWindowFocusable(static_cast<SDL_Window *>(window->getHandle()), false))
            throw std::runtime_error(SDL_GetError());
        window->setWindowTitle("LÖVE + SCUA");
        graphics->setBackgroundColor(love::Colorf(0.04, 0.05, 0.09, 1));

        // Declare the partition after the graphics context so every exit path
        // destroys script-owned GPU resources before closing the window.
        auto game = Game::create(host, options);
        SourceWatch watch(options);
        {
            Screenshot screenshot{options.screenshot, "", false};
            uint64_t frames = 0, previous = SDL_GetPerformanceCounter();
            while (!game->host.quitting && (!options.frames || frames < options.frames))
            {
                if (game->host.reloadRequested || (options.watch && watch.changed(options)))
                    reloadGame(game, options);
                Scope scope(game->partition.get());
                SDL_Event event;
                while (SDL_PollEvent(&event))
                {
                    window->handleSDLEvent(event);
                    if (event.type == SDL_EVENT_SENSOR_UPDATE ||
                        event.type == SDL_EVENT_GAMEPAD_SENSOR_UPDATE)
                    {
                        auto data =
                            lovescua::sensorEvent(event, event.type == SDL_EVENT_SENSOR_UPDATE
                                                             ? SDL_GetSensorTypeForID(event.sensor.which)
                                                             : SDL_SENSOR_UNKNOWN);
                        if (data.kind && !options.noInput)
                        {
                            auto p = game->partition.get();
                            scua_Handle args[] = {
                                scua_int(p, data.id), scua_string(p, data.kind, std::strlen(data.kind)),
                                scua_vec(p, data.data, 3), scua_num(p, double(data.timestamp) / 1e9)};
                            game->callbacks->call(data.gamepad ? game->callbacks->gamepadsensorupdated
                                                               : game->callbacks->sensorupdated,
                                                  data.gamepad ? "gamepadsensorupdated" : "sensorupdated",
                                                  args, 4);
                        }
                    }

                    // Verification windows must not react to desktop typing or
                    // pointer movement while their owner works in another app.
                    if (options.noInput &&
                        ((event.type >= SDL_EVENT_KEY_DOWN && event.type <= SDL_EVENT_KEYMAP_CHANGED) ||
                         (event.type >= SDL_EVENT_MOUSE_MOTION && event.type <= SDL_EVENT_MOUSE_WHEEL) ||
                         (event.type >= SDL_EVENT_FINGER_DOWN && event.type <= SDL_EVENT_FINGER_MOTION)))
                        continue;
                    if (event.type == SDL_EVENT_JOYSTICK_ADDED)
                    {
                        joysticks->addJoystick(event.jdevice.which);
                        auto id = scua_int(game->partition.get(), event.jdevice.which);
                        game->callbacks->call(game->callbacks->joystickadded, "joystickadded", &id, 1);
                    }
                    if (event.type == SDL_EVENT_JOYSTICK_REMOVED)
                    {
                        auto id = scua_int(game->partition.get(), event.jdevice.which);
                        game->callbacks->call(game->callbacks->joystickremoved, "joystickremoved", &id, 1);
                    }
                    if (event.type == SDL_EVENT_GAMEPAD_ADDED)
                    {
                        auto j = joysticks->getJoystickFromID(event.gdevice.which);
                        if (j)
                            j->openGamepad(event.gdevice.which);
                    }
                    if (event.type == SDL_EVENT_JOYSTICK_BUTTON_DOWN ||
                        event.type == SDL_EVENT_JOYSTICK_BUTTON_UP)
                    {
                        scua_Handle args[] = {scua_int(game->partition.get(), event.jbutton.which),
                                              scua_int(game->partition.get(), event.jbutton.button)};
                        if (event.type == SDL_EVENT_JOYSTICK_BUTTON_DOWN)
                            game->callbacks->call(game->callbacks->joystickpressed, "joystickpressed", args,
                                                  2);
                        else
                            game->callbacks->call(game->callbacks->joystickreleased, "joystickreleased", args,
                                                  2);
                    }
                    if (event.type == SDL_EVENT_JOYSTICK_AXIS_MOTION)
                    {
                        scua_Handle args[] = {scua_int(game->partition.get(), event.jaxis.which),
                                              scua_int(game->partition.get(), event.jaxis.axis),
                                              scua_num(game->partition.get(),
                                                       event.jaxis.value < 0 ? event.jaxis.value / 32768.0
                                                                             : event.jaxis.value / 32767.0)};
                        game->callbacks->call(game->callbacks->joystickaxis, "joystickaxis", args, 3);
                    }
                    if (event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN ||
                        event.type == SDL_EVENT_GAMEPAD_BUTTON_UP)
                    {
                        auto name = SDL_GetGamepadStringForButton(
                            static_cast<SDL_GamepadButton>(event.gbutton.button));
                        if (name)
                        {
                            scua_Handle args[] = {
                                scua_int(game->partition.get(), event.gbutton.which),
                                scua_string(game->partition.get(), name, std::strlen(name))};
                            if (event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN)
                                game->callbacks->call(game->callbacks->gamepadpressed, "gamepadpressed", args,
                                                      2);
                            else
                                game->callbacks->call(game->callbacks->gamepadreleased, "gamepadreleased",
                                                      args, 2);
                        }
                    }
                    if (event.type == SDL_EVENT_GAMEPAD_AXIS_MOTION)
                    {
                        auto name =
                            SDL_GetGamepadStringForAxis(static_cast<SDL_GamepadAxis>(event.gaxis.axis));
                        if (name)
                        {
                            scua_Handle args[] = {
                                scua_int(game->partition.get(), event.gaxis.which),
                                scua_string(game->partition.get(), name, std::strlen(name)),
                                scua_num(game->partition.get(), event.gaxis.value < 0
                                                                    ? event.gaxis.value / 32768.0
                                                                    : event.gaxis.value / 32767.0)};
                            game->callbacks->call(game->callbacks->gamepadaxis, "gamepadaxis", args, 3);
                        }
                    }
                    if (event.type == SDL_EVENT_JOYSTICK_REMOVED)
                    {
                        auto j = joysticks->getJoystickFromID(event.jdevice.which);
                        if (j)
                            joysticks->removeJoystick(j);
                    }
                    if (event.type == SDL_EVENT_QUIT || event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED)
                        game->host.quitting = true;
                    if ((event.type == SDL_EVENT_KEY_DOWN && (!event.key.repeat || game->host.keyRepeat)) ||
                        event.type == SDL_EVENT_KEY_UP)
                    {
                        const char *name = SDL_GetScancodeName(event.key.scancode);
                        auto key =
                            scua_string(game->partition.get(), name, std::char_traits<char>::length(name));
                        if (event.type == SDL_EVENT_KEY_DOWN)
                            game->callbacks->call(game->callbacks->keypressed, "keypressed", &key, 1);
                        else
                            game->callbacks->call(game->callbacks->keyreleased, "keyreleased", &key, 1);
                    }
                    auto point = [&](double x, double y)
                    {
                        window->windowToDPICoords(&x, &y);
                        float v[] = {static_cast<float>(x), static_cast<float>(y)};
                        return scua_vec(game->partition.get(), v, 2);
                    };
                    if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN || event.type == SDL_EVENT_MOUSE_BUTTON_UP)
                    {
                        int button = event.button.button == SDL_BUTTON_RIGHT    ? 2
                                     : event.button.button == SDL_BUTTON_MIDDLE ? 3
                                                                                : event.button.button;
                        scua_Handle args[] = {point(event.button.x, event.button.y),
                                              scua_int(game->partition.get(), button)};
                        if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN)
                            game->callbacks->call(game->callbacks->mousepressed, "mousepressed", args, 2);
                        else
                            game->callbacks->call(game->callbacks->mousereleased, "mousereleased", args, 2);
                    }
                    if (event.type == SDL_EVENT_MOUSE_MOTION)
                    {
                        scua_Handle args[] = {point(event.motion.x, event.motion.y),
                                              point(event.motion.xrel, event.motion.yrel)};
                        game->callbacks->call(game->callbacks->mousemoved, "mousemoved", args, 2);
                    }
                    if (event.type == SDL_EVENT_MOUSE_WHEEL)
                    {
                        float direction = event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -1 : 1;
                        float v[] = {event.wheel.x * direction, event.wheel.y * direction};
                        auto arg = scua_vec(game->partition.get(), v, 2);
                        game->callbacks->call(game->callbacks->wheelmoved, "wheelmoved", &arg, 1);
                    }
                    if (event.type == SDL_EVENT_TEXT_INPUT)
                    {
                        auto arg =
                            scua_string(game->partition.get(), event.text.text, std::strlen(event.text.text));
                        game->callbacks->call(game->callbacks->textinput, "textinput", &arg, 1);
                    }
                    if (event.type == SDL_EVENT_TEXT_EDITING)
                    {
                        scua_Handle args[] = {
                            scua_string(game->partition.get(), event.edit.text, std::strlen(event.edit.text)),
                            scua_int(game->partition.get(), event.edit.start),
                            scua_int(game->partition.get(), event.edit.length)};
                        game->callbacks->call(game->callbacks->textedited, "textedited", args, 3);
                    }
                    if (event.type == SDL_EVENT_WINDOW_FOCUS_GAINED ||
                        event.type == SDL_EVENT_WINDOW_FOCUS_LOST)
                    {
                        auto arg =
                            scua_bool(game->partition.get(), event.type == SDL_EVENT_WINDOW_FOCUS_GAINED);
                        game->callbacks->call(game->callbacks->focus, "focus", &arg, 1);
                    }
                    if (event.type == SDL_EVENT_WINDOW_MOUSE_ENTER ||
                        event.type == SDL_EVENT_WINDOW_MOUSE_LEAVE)
                    {
                        auto arg =
                            scua_bool(game->partition.get(), event.type == SDL_EVENT_WINDOW_MOUSE_ENTER);
                        game->callbacks->call(game->callbacks->mousefocus, "mousefocus", &arg, 1);
                    }
                    if (event.type == SDL_EVENT_WINDOW_SHOWN || event.type == SDL_EVENT_WINDOW_HIDDEN)
                    {
                        auto arg = scua_bool(game->partition.get(), event.type == SDL_EVENT_WINDOW_SHOWN);
                        game->callbacks->call(game->callbacks->visible, "visible", &arg, 1);
                    }
                    if (event.type == SDL_EVENT_FINGER_DOWN || event.type == SDL_EVENT_FINGER_UP ||
                        event.type == SDL_EVENT_FINGER_MOTION)
                    {
                        float xy[] = {event.tfinger.x * graphics->getWidth(),
                                      event.tfinger.y * graphics->getHeight()};
                        scua_Handle args[] = {scua_int(game->partition.get(), event.tfinger.fingerID),
                                              scua_vec(game->partition.get(), xy, 2),
                                              scua_num(game->partition.get(), event.tfinger.pressure)};
                        if (event.type == SDL_EVENT_FINGER_DOWN)
                            game->callbacks->call(game->callbacks->touchpressed, "touchpressed", args, 3);
                        else if (event.type == SDL_EVENT_FINGER_UP)
                            game->callbacks->call(game->callbacks->touchreleased, "touchreleased", args, 3);
                        else
                            game->callbacks->call(game->callbacks->touchmoved, "touchmoved", args, 3);
                    }
                    if (event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED)
                    {
                        window->onSizeChanged(event.window.data1, event.window.data2);
                        float size[] = {static_cast<float>(graphics->getWidth()),
                                        static_cast<float>(graphics->getHeight())};
                        auto dimensions = scua_vec(game->partition.get(), size, 2);
                        game->callbacks->call(game->callbacks->resize, "resize", &dimensions, 1);
                    }
                }
                std::vector<std::pair<scua_Handle, scua_Handle>> queued;
                queued.swap(game->host.pendingEvents);
                struct UnpinEvents
                {
                    scua_partition *p;
                    std::vector<std::pair<scua_Handle, scua_Handle>> &events;
                    ~UnpinEvents()
                    {
                        for (auto &e : events)
                        {
                            scua_unpin_global(p, e.first);
                            scua_unpin_global(p, e.second);
                        }
                    }
                } unpinEvents{game->partition.get(), queued};
                for (auto &item : queued)
                {
                    scua_Handle args[] = {item.first, item.second};
                    game->callbacks->call(game->callbacks->event, "event", args, 2);
                }
                if (game->host.quitting)
                    break;
                uint64_t now = SDL_GetPerformanceCounter();
                double dt =
                    options.fixedDt
                        ? options.fixedDt
                        : std::min(0.25, static_cast<double>(now - previous) / SDL_GetPerformanceFrequency());
                previous = now;
                game->host.delta = dt;
                game->host.elapsed += dt;
                ++game->host.frameCount;
                game->host.averageDelta = game->host.elapsed / game->host.frameCount;
                auto delta = scua_num(game->partition.get(), dt);
                game->callbacks->call(game->callbacks->update, "update", &delta, 1);
                if (game->host.quitting)
                    break;
                graphics->origin();
                const auto background = graphics->getBackgroundColor();
                graphics->clear(love::ColorD(background.r, background.g, background.b, background.a),
                                love::OptionalInt(0), love::OptionalDouble(1));
                graphics->setColor(love::Colorf(1, 1, 1, 1));
                game->host.drawing = true;
                game->callbacks->call(game->callbacks->draw, "draw");
                game->host.drawing = false;
                if (!options.screenshot.empty() && frames + 1 == options.frames)
                {
                    love::graphics::Graphics::ScreenshotInfo capture;
                    capture.callback = Screenshot::capture;
                    capture.data = &screenshot;
                    graphics->captureScreenshot(capture);
                }
                graphics->present(nullptr);
                ++frames;
                // GPU memory does not itself create arena pressure. Collect dropped
                // resource wrappers once per 60 frames, and drain after presentation.
                if (frames % 60 == 0 && scua_userdata_collect(game->partition.get()) != SCUA_OK)
                    throw std::runtime_error("could not collect managed resources");
                size_t destroyed = 0;
                if (scua_userdata_drain(game->partition.get(), 64, &destroyed) != SCUA_OK)
                    throw std::runtime_error("could not drain managed resources");
            }
            {
                Scope scope(game->partition.get());
                if (verify)
                {
                    if (frames != options.frames)
                        throw std::runtime_error("game quit before requested frame count");
                    if (!game->callbacks->present(game->callbacks->verify))
                        throw std::runtime_error("--verify requires fn verify()");
                    game->callbacks->call(game->callbacks->verify, "verify");
                }
                game->callbacks->call(game->callbacks->quit, "quit");
            }
            if (!screenshot.error.empty())
                throw std::runtime_error(screenshot.error);
            if (!options.screenshot.empty() && !screenshot.captured)
                throw std::runtime_error("screenshot was not captured");
            std::cout << "Rendered " << frames << " frames using " << options.renderer << " and "
                      << scua_version_string() << '\n';
        } // Release cached callbacks before checked partition teardown.
        if (verify && game->host.imagesCreated)
            std::cout << "Resources before teardown: " << game->host.liveImages << " images, "
                      << game->host.liveQuads << " quads\n";
        game.reset();
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << "lovescua: " << error.what() << '\n';
        return 1;
    }
}
