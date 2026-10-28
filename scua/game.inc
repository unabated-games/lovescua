// Game instances own partition-local registrations and resources independently.
struct Game
{
    lovescua::Host host;
    Partition partition{nullptr, DestroyPartition{&host, false}};
    std::unique_ptr<Callbacks> callbacks;
    bool preserveInput = false;
    ~Game()
    {
        callbacks.reset();
        partition.reset();
        lovescua::shutdownAudio(host);
        if (!preserveInput)
            lovescua::shutdownInput(host);
        else
        {
            if (host.mouse)
                host.mouse->release();
            if (host.sensors)
                host.sensors->release();
            host.mouse = nullptr;
            host.sensors = nullptr;
        }
    }
    static std::unique_ptr<Game> create(const lovescua::Host &base, const Options &options,
                                        bool validateOnly = false)
    {
        auto game = std::make_unique<Game>();
        auto &h = game->host;
        h.window = base.window;
        h.graphics = base.graphics;
        h.joysticks = base.joysticks;
        h.ignoreInput = base.ignoreInput;
        h.keyRepeat = base.keyRepeat;
        h.cpuOnly = base.cpuOnly;
        h.nativePaths = options.nativePaths;
        h.delta = base.delta;
        h.elapsed = base.elapsed;
        h.averageDelta = base.averageDelta;
        h.frameCount = base.frameCount;
        h.audio = base.audio;
        if (h.audio)
            h.audio->retain();
        h.mouse = base.mouse;
        if (h.mouse)
            h.mouse->retain();
        h.sensors = base.sensors;
        if (h.sensors)
            h.sensors->retain();
        game->preserveInput = validateOnly;
        h.checking = true;
        game->partition.get_deleter().report = options.reportResources;
        game->partition.reset(scua_partition_new());
        auto p = game->partition.get();
        if (!p)
            throw std::runtime_error("could not allocate game partition");
        lovescua::configureJobs(p, h, options.jobs, base);
        lovescua::registerBindings(p, h);
        char error[4096]{};
        if (scua_use_project(p, options.script.parent_path().c_str(), error, sizeof(error)) != SCUA_OK)
            throw std::runtime_error(error);
        if (options.nativePaths && scua_grant_fs(p, "/") != SCUA_OK)
            throw std::runtime_error("could not enable native filesystem paths");
        if (!options.saveDirectory.empty() || !options.userId.empty())
            if (scua_grant_user(p, options.userId.empty() ? nullptr : options.userId.c_str(),
                                options.saveDirectory.empty() ? nullptr : options.saveDirectory.c_str(), 0,
                                error, sizeof(error)) != SCUA_OK)
                throw std::runtime_error(error);
        if (scua_add_module_path(p, runtimeModules().c_str()) != SCUA_OK)
            throw std::runtime_error("could not add runtime modules");
        // Resources are process-wide and are already mounted before loading game actors.
        if (scua_load_script_file(p, options.script.c_str(), 64 * 1024 * 1024, 1000000, error,
                                  sizeof(error)) != SCUA_OK)
            throw std::runtime_error(error);
        h.checking = false;
        game->callbacks = std::make_unique<Callbacks>(p, !h.cpuOnly);
        if (!validateOnly)
        {
            Scope scope(p);
            game->callbacks->call(game->callbacks->load, "load");
        }
        return game;
    }
    std::string snapshot()
    {
        auto p = partition.get();
        Scope scope(p);
        auto fn = callbacks->snapshot;
        if (!callbacks->present(fn))
            return {};
        char error[4096]{};
        scua_Handle result{};
        if (scua_pcall(p, fn, nullptr, 0, &result, error, sizeof(error)) != SCUA_OK)
            throw std::runtime_error(std::string("snapshot: ") + error);
        size_t n = 0;
        auto text = scua_to_lstring(p, result, &n);
        if (!text || n > 4 * 1024 * 1024)
            throw std::runtime_error("snapshot() must return encoded logical state up to 4 MiB");
        return {text, n};
    }
    void restore(const std::string &state)
    {
        if (state.empty())
            return;
        auto p = partition.get();
        Scope scope(p);
        auto fn = callbacks->restore;
        if (!callbacks->present(fn))
            throw std::runtime_error("restoring a snapshot requires fn restore(encoded: string)");
        auto arg = scua_string(p, state.data(), state.size());
        char error[4096]{};
        if (scua_pcall(p, fn, &arg, 1, nullptr, error, sizeof(error)) != SCUA_OK)
            throw std::runtime_error(std::string("restore: ") + error);
    }
};
struct SourceWatch
{
    std::map<std::string, std::filesystem::file_time_type> previous;
    static auto scan(const Options &o)
    {
        std::map<std::string, std::filesystem::file_time_type> files;
        std::error_code error;
        for (std::filesystem::recursive_directory_iterator it(
                 o.script.parent_path(), std::filesystem::directory_options::skip_permission_denied, error),
             end;
             it != end && !error; it.increment(error))
        {
            if (it.depth() > 8)
            {
                it.disable_recursion_pending();
                continue;
            }
            if (it->is_directory() && (it->path().filename() == "build" || it->path().filename() == ".git"))
            {
                it.disable_recursion_pending();
                continue;
            }
            if (it->is_regular_file() && it->path().extension() == ".scua")
            {
                if (files.size() >= 2048)
                    throw std::runtime_error("watch limit is 2048 SCUA source files");
                files.emplace(it->path().string(), it->last_write_time());
            }
        }
        return files;
    }
    explicit SourceWatch(const Options &o) : previous(o.watch ? scan(o) : decltype(previous){}) {}
    bool changed(const Options &o)
    {
        auto now = scan(o);
        bool changed = now != previous;
        previous = std::move(now);
        return changed;
    }
};
void reloadGame(std::unique_ptr<Game> &game, const Options &options)
{
    game->host.reloadRequested = false;
    auto graphics = game->host.graphics;
    auto window = game->host.window;
    std::string title;
    int width = 0, height = 0;
    love::window::WindowSettings settings;
    bool pushed = false, committed = false;
    try
    {
        // Syntax/top-level validation runs without touching the working game's resources.
        auto next = Game::create(game->host, options, true);
        auto state = game->snapshot();
        if (window)
        {
            window->getWindow(width, height, settings);
            title = window->getWindowTitle();
        }
        if (graphics)
        {
            graphics->push(love::graphics::Graphics::STACK_ALL);
            pushed = true;
        }
        next->host.staging = true;
        {
            Scope scope(next->partition.get());
            next->callbacks->call(next->callbacks->load, "load");
        }
        next->restore(state);
        next->host.staging = false;
        if (graphics)
        {
            graphics->commitPushedState();
            pushed = false;
        }
#ifdef LOVE_SCUA_AUDIO
        if (game->host.audio)
            game->host.audio->stop();
#endif
        game->preserveInput = true;
        next->preserveInput = false;
        game.swap(next);
        next.reset();
        committed = true;
        game->host.reloadError.clear();
        // Reapply external effects after commit; staging leaves input state untouched.
        {
            Scope scope(game->partition.get());
            auto fn = game->callbacks->lookup("reloaded");
            if (game->callbacks->present(fn))
            {
                struct Unpin
                {
                    scua_partition *p;
                    scua_Handle h;
                    ~Unpin()
                    {
                        scua_unpin_global(p, h);
                    }
                } unpin{game->partition.get(), fn};
                game->callbacks->call(fn, "reloaded");
            }
        }
        std::cout << "Reloaded game; logical state restored\n";
    }
    catch (const std::exception &e)
    {
        if (pushed && graphics)
            graphics->pop();
        if (!committed && window && width)
        {
            window->setWindow(width, height, &settings);
            window->setWindowTitle(title);
        }
        game->host.reloadError = e.what();
        std::cerr << (committed ? "lovescua: reload committed; reloaded callback failed: "
                                : "lovescua: reload rejected; current game continues: ")
                  << e.what() << '\n';
    }
}
