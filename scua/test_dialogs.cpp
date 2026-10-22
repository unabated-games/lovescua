// Exercise actual native dialog completion/selection logic without opening a chooser.
#include "platform_extra.cpp"
#include <iostream>
#include <thread>
#include <unistd.h>
static void require(bool ok)
{
    if (!ok)
        throw std::runtime_error("dialog assertion failed");
}
int main()
{
    try
    {
        char pattern[] = "/private/tmp/lovescua-dialogs-XXXXXX";
        auto root = mkdtemp(pattern);
        require(root);
        std::filesystem::path directory(root);
        struct Remove
        {
            std::filesystem::path p;
            ~Remove()
            {
                std::filesystem::remove_all(p);
            }
        } remove{directory};
        auto source = (directory / "source.txt").string(), output = (directory / "saved.txt").string();
        {
            std::ofstream file(source);
            file << "selected bytes";
        }
        lovescua::Host host;
        host.dialogs = std::make_shared<lovescua::DialogInbox>();
        auto box = host.dialogs;
        box->results[1].kind = 0;
        box->results[2].kind = 2;
        box->results[3].kind = 1;
        box->results[4].kind = 0;
        box->results[5].kind = 0;
        auto deliver = [&](int id, const char *path)
        {
            auto r = new lovescua::DialogRequest();
            r->inbox = box;
            r->id = id;
            const char *files[] = {path, nullptr};
            std::thread worker([&] { lovescua::dialogCallback(r, files, 0); });
            worker.join();
        };
        deliver(1, source.c_str());
        deliver(2, output.c_str());
        deliver(3, root);
        deliver(4, nullptr);
        auto failed = new lovescua::DialogRequest();
        failed->inbox = box;
        failed->id = 5;
        lovescua::dialogCallback(failed, nullptr, -1);
        auto p = scua_partition_new();
        require(p);
        struct Free
        {
            scua_partition *p;
            ~Free()
            {
                scua_partition_free(p);
            }
        } free{p};
        lovescua::registerPlatformExtras(p, host);
        const char *code = R"SCUA(
import love_window as window
import bytes
assert(window.poll_file_dialog(1).files.len()==1)
assert(window.poll_file_dialog(4).cancelled)
assert(window.poll_file_dialog(5).failed)
assert(window.read_selected_file(1,0)==bytes.from_string("selected bytes"))
assert(window.read_selected_file(3,0,"source.txt")==bytes.from_string("selected bytes"))
assert(window.list_selected_folder(3).contains("source.txt"))
window.write_selected_file(2,bytes.from_string("saved bytes"))
match pcall(fn() window.read_selected_file(3,0,"../escape.txt") end)
 Error(_) -> nil
 Ok(_) -> assert(false)
end
window.release_file_dialog(1)
match pcall(fn() window.read_selected_file(1,0) end)
 Error(_) -> nil
 Ok(_) -> assert(false)
end
)SCUA";
        char error[4096]{};
        int status =
            scua_load_script(p, code, std::strlen(code), 8 * 1024 * 1024, 100000, error, sizeof(error));
        if (status != SCUA_OK)
            throw std::runtime_error(error);
        std::ifstream file(output);
        std::string saved;
        std::getline(file, saved);
        require(saved == "saved bytes");
        auto detached = std::make_shared<lovescua::DialogInbox>();
        detached->results[1].kind = 0;
        auto pending = new lovescua::DialogRequest();
        pending->inbox = detached;
        pending->id = 1;
        std::weak_ptr<lovescua::DialogInbox> weak = detached;
        detached.reset();
        const char *cancel[] = {nullptr};
        lovescua::dialogCallback(pending, cancel, -1);
        require(weak.expired());
        std::cout << "PASS: native chooser completions, cancellation/errors, cross-thread copy, selected "
                     "file/folder bounds and teardown\n";
        return 0;
    }
    catch (const std::exception &e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
