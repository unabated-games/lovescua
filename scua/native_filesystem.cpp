// Native paths are an explicit host grant; SCUA's fs owns streams and I/O.
#include "binding_util.h"
#include <filesystem>
namespace lovescua
{
namespace
{
using namespace binding;
void enabled(scua_ctx *c)
{
    arity(c, 0);
    scua_return_bool(c, host(c).nativePaths);
}
void resolve(scua_ctx *c)
{
    arity(c, 1);
    if (!host(c).nativePaths)
        throw std::invalid_argument("native paths require --allow-native-fs for trusted scripts");
    auto path = string(c, 0);
    if (path.empty())
        throw std::invalid_argument("native path must not be empty");
    // Resolve existing symlinks before '..', retaining a non-existing suffix so
    // creates work. Relative paths use the process working directory, not res:.
    auto absolute = std::filesystem::weakly_canonical(std::filesystem::absolute(path));
    Record r(c);
    r.str("path", absolute.string());
    r.str("rooted", absolute.relative_path().string());
    r.done();
}
} // namespace
void registerNativeFilesystem(scua_partition *p, Host &h)
{
    registerFunctions(p, h,
                      {{"love_nativefs", "enabled", guarded<enabled>, "enabled() -> bool",
                        "Whether the host explicitly enabled native-path access."},
                       {"love_nativefs", "resolve", guarded<resolve>, "resolve(path: string) -> any",
                        "Resolve a native path against the process working directory; requires "
                        "--allow-native-fs. Returns absolute path and SDK-root-relative path."}});
}
} // namespace lovescua
