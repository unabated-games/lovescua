// Queue runtime captures; renderer callbacks only retain pixels, never enter SCUA.
#include "binding_util.h"
#include "image/ImageData.h"
namespace lovescua
{
struct ScreenshotRequest
{
    scua_partition *partition;
    scua_Handle callback{};
    uint64_t id;
    love::image::ImageData *image = nullptr;
    bool ready = false;
    ~ScreenshotRequest()
    {
        if (scua_handle_valid(partition, callback))
            scua_unpin_global(partition, callback);
        if (image)
            image->release();
    }
};
struct ScreenshotInbox
{
    std::vector<std::unique_ptr<ScreenshotRequest>> pending, active, completed;
    uint64_t nextId = 1;
    size_t count = 0;
};
namespace
{
using namespace binding;
void captured(const love::graphics::Graphics::ScreenshotInfo *info, love::image::ImageData *image,
              void *) noexcept
{
    auto &request = *static_cast<ScreenshotRequest *>(info->data);
    if (image)
        image->retain();
    request.image = image;
    request.ready = true;
}
void capture(scua_ctx *c)
{
    if (scua_arg_count(c) > 1)
        throw std::invalid_argument("capture_screenshot expects an optional callback");
    auto &h = host(c);
    auto &state = *h.screenshots;
    if (state.count >= 8)
        throw std::invalid_argument("screenshot queue limit is 8; drain results before capturing more");
    auto &g = graphics(c, false);
    if (uint64_t(g.getPixelWidth()) * g.getPixelHeight() > 16u * 1024 * 1024)
        throw std::invalid_argument("screenshot image exceeds the 64 MiB CPU image limit");
    auto p = scua_ctx_partition(c);
    auto request = std::make_unique<ScreenshotRequest>();
    request->partition = p;
    request->id = state.nextId++;
    if (supplied(c, 0))
    {
        if (scua_typeof(p, scua_arg(c, 0)) != SCUA_TYPE_OTHER)
            throw std::invalid_argument("screenshot callback must be a function");
        request->callback = scua_pin_global(p, scua_arg(c, 0));
        if (!scua_handle_valid(p, request->callback))
            throw std::runtime_error("could not retain screenshot callback");
    }
    auto id = request->id;
    state.pending.push_back(std::move(request));
    ++state.count;
    scua_return_int(c, id);
}
scua_Handle imageHandle(scua_partition *p, Host &h, love::image::ImageData *image)
{
    if (!image)
        return scua_nil(p);
    scua_Handle value{};
    if (scua_userdata_new(p, h.objectTypes[size_t(ObjectKind::ImageData)], sizeof(ObjectResource), image,
                          &value) != SCUA_OK)
        throw std::runtime_error("could not publish screenshot ImageData");
    ++h.liveObjects;
    return value;
}
struct ImageRoot
{
    scua_partition *partition;
    scua_Handle value;
    ImageRoot(scua_partition *p, Host &h, love::image::ImageData *image)
        : partition(p), value(imageHandle(p, h, image))
    {
    }
    ~ImageRoot()
    {
        // Host-created userdata is globally pinned. Once published into a script
        // record or passed to a callback, ordinary script reachability owns it.
        scua_unpin_global(partition, value);
    }
};
void results(scua_ctx *c)
{
    arity(c, 0);
    auto &h = host(c);
    auto &state = *h.screenshots;
    auto p = scua_ctx_partition(c);
    auto out = scua_new_array(p, 0);
    // Keep completed requests until the whole result array has been built.
    for (auto &request : state.completed)
    {
        Record item(c);
        item.integer("id", request->id);
        ImageRoot image(p, h, request->image);
        item.set("image", image.value);
        item.set("error",
                 request->image ? scua_nil(p) : scua_string(p, "renderer returned no screenshot", 31));
        if (scua_array_push(p, out, item.value) != SCUA_OK)
            throw std::runtime_error("could not publish screenshot result");
    }
    state.count -= state.completed.size();
    state.completed.clear();
    scua_return_value(c, out);
}
} // namespace
void registerScreenshots(scua_partition *p, Host &h)
{
    h.screenshots = std::make_shared<ScreenshotInbox>();
    binding::registerFunctions(
        p, h,
        {{"love_graphics", "capture_screenshot", binding::guarded<capture>,
          "capture_screenshot(callback: any = nil) -> int",
          "Capture the next presented frame. Callback receives ImageData after presentation; nil callback "
          "queues a polled result."},
         {"love_graphics", "screenshot_results", binding::guarded<results>, "screenshot_results() -> any",
          "Drain completed screenshot records {id,image,error}; at most 8 outstanding captures."}});
}
void submitScreenshots(Host &h)
{
    auto &s = *h.screenshots;
    // Inactive/minimized windows do not present; keep requests for a later frame.
    if (!h.graphics->isActive())
        return;
    s.active.swap(s.pending);
    for (auto &request : s.active)
    {
        love::graphics::Graphics::ScreenshotInfo info;
        info.callback = captured;
        info.data = request.get();
        h.graphics->captureScreenshot(info);
    }
}
void deliverScreenshots(Host &h)
{
    auto &s = *h.screenshots;
    std::vector<std::unique_ptr<ScreenshotRequest>> ready;
    ready.swap(s.active);
    for (auto &request : ready)
    {
        if (!request->ready)
            throw std::runtime_error("renderer did not complete a submitted screenshot");
        auto p = request->partition;
        if (!scua_handle_valid(p, request->callback))
        {
            s.completed.push_back(std::move(request));
            continue;
        }
        --s.count;
        auto token = scua_scope_enter(p);
        struct Leave
        {
            scua_partition *p;
            uint32_t token;
            ~Leave()
            {
                scua_scope_leave(p, token);
            }
        } leave{p, token};
        ImageRoot image(p, h, request->image);
        char error[4096]{};
        if (scua_pcall(p, request->callback, &image.value, 1, nullptr, error, sizeof(error)) != SCUA_OK)
            throw std::runtime_error(std::string("screenshot callback: ") + error);
    }
}
void cancelScreenshots(Host &h) noexcept
{
    h.screenshots.reset();
}
} // namespace lovescua
