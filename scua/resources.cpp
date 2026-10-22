// Inspect and close managed resources without exposing pointers or integer handles.
#include "binding_util.h"
namespace lovescua
{
namespace
{
using namespace binding;
const char *kind(scua_ctx *c, scua_Handle value)
{
    auto &h = host(c);
    auto p = scua_ctx_partition(c);
    auto matches = [&](scua_UserdataType type)
    {
        void *storage = nullptr;
        scua_UserdataLease lease{};
        if (scua_userdata_borrow(p, value, type, &storage, &lease) != SCUA_OK)
            return false;
        scua_userdata_release(p, lease);
        return true;
    };
    if (matches(h.imageType))
        return "Texture";
    if (matches(h.quadType))
        return "Quad";
    const char *names[] = {
        "Font",        "ImageData", "Shader",    "SpriteBatch", "Particles", "SoundData",
        "AudioSource", "Decoder",   "Recording", "Random",      "Curve",     "Transform",
        "World",       "Body",      "Collider",  "Joint",       "Mesh",      "Text",
        "Joystick",    "Video",     "Readback",  "Cursor",      "Buffer",    "CompressedImageData",
        "Rasterizer",  "GlyphData"};
    static_assert(sizeof(names) / sizeof(*names) == size_t(ObjectKind::Count));
    for (size_t i = 0; i < size_t(ObjectKind::Count); ++i)
        if (matches(h.objectTypes[i]))
            return names[i];
    return nullptr;
}
void object_kind(scua_ctx *c)
{
    arity(c, 1);
    auto name = kind(c, scua_arg(c, 0));
    if (name)
        returnString(c, name);
    else
        scua_return_value(c, scua_nil(scua_ctx_partition(c)));
}
void is_open(scua_ctx *c)
{
    arity(c, 1);
    scua_return_bool(c, kind(c, scua_arg(c, 0)) != nullptr);
}
void assert_kind(scua_ctx *c)
{
    arity(c, 2);
    auto expected = string(c, 1);
    auto name = kind(c, scua_arg(c, 0));
    if (!name || expected != name)
        throw std::invalid_argument("expected an open " + expected + " resource");
    scua_return_value(c, scua_arg(c, 0));
}
void close(scua_ctx *c)
{
    arity(c, 1);
    if (scua_userdata_close(scua_ctx_partition(c), scua_arg(c, 0)) != SCUA_OK)
        throw std::invalid_argument(
            "resource cannot be closed while retained by a dependent object, or is invalid");
}
} // namespace
void registerResources(scua_partition *p, Host &s)
{
    registerFunctions(p, s,
                      {{"love_object", "kind", guarded<object_kind>, "kind(resource: any) -> any",
                        "Get the checked native kind of an open resource, or nil."},
                       {"love_object", "is_open", guarded<is_open>, "is_open(resource: any) -> bool",
                        "Check whether a managed resource handle is open."},
                       {"love_object", "assert_kind", guarded<assert_kind>,
                        "assert_kind(resource: any, kind: string) -> any",
                        "Check a native kind and return the same resource."},
                       {"love_object", "close", guarded<close>, "close(resource: any)",
                        "Close a managed resource with the runtime's dependency checks."}});
}
} // namespace lovescua
