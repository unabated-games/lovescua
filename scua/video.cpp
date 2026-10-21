// In-memory resource adapter for LÖVE's actual Theora decoder and YUV drawable.
#include "graphics/Video.h"
#include "binding_util.h"
#include "filesystem/File.h"
#include "ogg_audio.h"
#include "video/theora/TheoraVideoStream.h"
namespace lovescua
{
namespace
{
using namespace binding;
using love::int64;
class MemoryFile final : public love::filesystem::File
{
    std::shared_ptr<const std::vector<uint8_t>> data;
    int64_t position = 0;
    bool opened = true;
    std::string name = "resource.ogv";

  public:
    using love::filesystem::File::read;
    MemoryFile(const uint8_t *p, size_t n) : data(std::make_shared<std::vector<uint8_t>>(p, p + n)) {}
    MemoryFile(std::shared_ptr<const std::vector<uint8_t>> data) : data(std::move(data)) {}
    love::Stream *clone() override
    {
        return new MemoryFile(data);
    }
    bool open(Mode mode) override
    {
        if (mode != MODE_READ)
            return false;
        opened = true;
        position = 0;
        return true;
    }
    bool close() override
    {
        opened = false;
        return true;
    }
    bool isOpen() const override
    {
        return opened;
    }
    bool isEOF() override
    {
        return position >= static_cast<int64_t>(data->size());
    }
    bool setBuffer(BufferMode, int64) override
    {
        return true;
    }
    BufferMode getBuffer(int64 &size) const override
    {
        size = 0;
        return BUFFER_NONE;
    }
    Mode getMode() const override
    {
        return opened ? MODE_READ : MODE_CLOSED;
    }
    const std::string &getFilename() const override
    {
        return name;
    }
    int64 read(void *out, int64 size) override
    {
        if (!opened || size < 0)
            throw std::invalid_argument("invalid video read");
        size = std::min<int64>(size, data->size() - position);
        if (size)
            std::memcpy(out, data->data() + position, size);
        position += size;
        return size;
    }
    bool write(const void *, int64) override
    {
        return false;
    }
    bool flush() override
    {
        return true;
    }
    int64 getSize() override
    {
        return data->size();
    }
    int64 tell() override
    {
        return position;
    }
    bool seek(int64 offset, SeekOrigin origin = SEEKORIGIN_BEGIN) override
    {
        int64 base = origin == SEEKORIGIN_CURRENT ? position : origin == SEEKORIGIN_END ? data->size() : 0;
        if (offset < -base || offset > static_cast<int64>(data->size()) - base)
            return false;
        position = base + offset;
        return true;
    }
};
template <class T> struct Release
{
    void operator()(T *p)
    {
        if (p)
            p->release();
    }
};
template <class T> using Owned = std::unique_ptr<T, Release<T>>;
struct Video final : love::Object
{
    love::StrongRef<love::video::theora::TheoraVideoStream> stream;
    love::StrongRef<love::graphics::Video> drawable;
    Video(love::video::theora::TheoraVideoStream *s, love::graphics::Video *v) : stream(s), drawable(v) {}
};
auto video(scua_ctx *ctx)
{
    return objectArgument<Video>(ctx, 0, ObjectKind::Video);
}
void new_video(scua_ctx *ctx)
{
    if (scua_arg_count(ctx) < 1 || scua_arg_count(ctx) > 2)
        throw std::invalid_argument("expected encoded video and optional audio flag");
    bool useAudio = supplied(ctx, 1) ? boolean(ctx, 1) : true;
#ifndef LOVE_SCUA_AUDIO
    if (supplied(ctx, 1) && useAudio)
        throw std::invalid_argument("video audio is disabled in this build");
    useAudio = false;
#endif
    const uint8_t *p = nullptr;
    size_t n = 0;
    if (scua_arg_bytes(ctx, 0, &p, &n) != SCUA_OK || n < 27 || n > 32 * 1024 * 1024 ||
        std::memcmp(p, "OggS", 4) != 0)
        throw std::invalid_argument("expected Ogg Theora bytes (27 bytes to 32 MiB)");
    auto audioBytes = useAudio ? oggVorbisStream(p, n) : std::vector<uint8_t>{};
    Owned<MemoryFile> file(new MemoryFile(p, n));
    Owned<love::video::theora::TheoraVideoStream> stream(
        new love::video::theora::TheoraVideoStream(file.get()));
    stream->threadedFillBackBuffer(0);
    stream->swapBuffers();
    Owned<love::graphics::Video> drawable(graphics(ctx, false).newVideo(stream.get(), 1));
#ifdef LOVE_SCUA_AUDIO
    if (!audioBytes.empty())
    {
        Owned<love::audio::Source> source(newAudioStream(ctx, audioBytes.data(), audioBytes.size()));
        Owned<love::video::VideoStream::FrameSync> sync(
            new love::video::VideoStream::SourceSync(source.get()));
        sync->copyState(stream->getSync());
        stream->setSync(sync.get());
        drawable->setSource(source.get());
    }
#endif
    returnObject(ctx, ObjectKind::Video, new Video(stream.get(), drawable.get()));
}
void play(scua_ctx *ctx)
{
    arity(ctx, 1);
    auto v = video(ctx);
    if (v)
        v->stream->play();
}
void pause(scua_ctx *ctx)
{
    arity(ctx, 1);
    auto v = video(ctx);
    if (v)
        v->stream->pause();
}
void seek(scua_ctx *ctx)
{
    arity(ctx, 2);
    auto t = number(ctx, 1);
    if (t < 0 || t > 86400)
        throw std::invalid_argument("video seek must be in [0,86400] seconds");
    auto v = video(ctx);
    if (v)
    {
        v->stream->seek(t);
        v->stream->threadedFillBackBuffer(0);
    }
}
void tell(scua_ctx *ctx)
{
    arity(ctx, 1);
    auto v = video(ctx);
    if (v)
        scua_return_float(ctx, v->stream->tell());
}
void is_playing(scua_ctx *ctx)
{
    arity(ctx, 1);
    auto v = video(ctx);
    if (v)
        scua_return_bool(ctx, v->stream->isPlaying());
}
void update(scua_ctx *ctx)
{
    arity(ctx, 2);
    auto dt = number(ctx, 1);
    if (dt < 0 || dt > 0.25)
        throw std::invalid_argument("video dt must be in [0,0.25]");
    auto v = video(ctx);
    if (v)
        v->stream->threadedFillBackBuffer(dt);
}
void dimensions(scua_ctx *ctx)
{
    arity(ctx, 1);
    auto v = video(ctx);
    if (v)
        returnVec(ctx, v->drawable->getWidth(), v->drawable->getHeight());
}
void draw(scua_ctx *ctx)
{
    auto matrix = transform(ctx);
    auto v = video(ctx);
    if (v)
        graphics(ctx).draw(v->drawable.get(), matrix);
}
void set_filter(scua_ctx *ctx)
{
    arity(ctx, 2);
    auto name = string(ctx, 1);
    if (name != "linear" && name != "nearest")
        throw std::invalid_argument("video filter must be nearest or linear");
    auto v = video(ctx);
    if (v)
    {
        auto state = v->drawable->getSamplerState();
        state.minFilter = state.magFilter = name == "nearest" ? love::graphics::SamplerState::FILTER_NEAREST
                                                              : love::graphics::SamplerState::FILTER_LINEAR;
        v->drawable->setSamplerState(state);
    }
}
void get_source(scua_ctx *ctx)
{
    arity(ctx, 1);
    auto v = video(ctx);
    if (!v)
        return;
#ifdef LOVE_SCUA_AUDIO
    auto source = v->drawable->getSource();
    if (source)
    {
        source->retain();
        returnObject(ctx, ObjectKind::AudioSource, source);
        return;
    }
#endif
    scua_return_value(ctx, scua_nil(scua_ctx_partition(ctx)));
}
void get_filter(scua_ctx *ctx)
{
    arity(ctx, 1);
    auto v = video(ctx);
    if (!v)
        return;
    auto s = v->drawable->getSamplerState();
    const char *min = "unknown", *mag = "unknown";
    love::graphics::SamplerState::getConstant(s.minFilter, min);
    love::graphics::SamplerState::getConstant(s.magFilter, mag);
    Record r(ctx);
    r.str("min", min);
    r.str("mag", mag);
    r.integer("anisotropy", s.maxAnisotropy);
    r.done();
}
void set_source(scua_ctx *ctx)
{
    arity(ctx, 2);
#ifdef LOVE_SCUA_AUDIO
    auto v = video(ctx);
    if (!v)
        return;
    love::audio::Source *source = nullptr;
    if (supplied(ctx, 1))
    {
        source = objectArgument<love::audio::Source>(ctx, 1, ObjectKind::AudioSource);
        if (!source)
            return;
    }
    Owned<love::video::VideoStream::FrameSync> sync(
        source ? static_cast<love::video::VideoStream::FrameSync *>(
                     new love::video::VideoStream::SourceSync(source))
               : new love::video::VideoStream::DeltaSync());
    sync->copyState(v->stream->getSync());
    v->stream->setSync(sync.get());
    v->drawable->setSource(source);
#else
    throw std::invalid_argument("video audio synchronization is disabled in this build");
#endif
}
} // namespace
void registerVideo(scua_partition *p, Host &state)
{
    registerObjectType(p, state, ObjectKind::Video);
    registerFunctions(
        p, state,
        {
            {"love_video", "new", guarded<new_video>, "new(encoded: bytes, audio: bool = true) -> any",
             "Decode Ogg Theora and optionally its first embedded Vorbis track; audio-disabled builds "
             "default to video only."},
            {"love_video", "play", guarded<play>, "play(video: any)", "Start or resume playback."},
            {"love_video", "pause", guarded<pause>, "pause(video: any)", "Pause playback."},
            {"love_video", "seek", guarded<seek>, "seek(video: any, seconds: float)", "Seek the decoder."},
            {"love_video", "tell", guarded<tell>, "tell(video: any) -> float", "Get playback position."},
            {"love_video", "is_playing", guarded<is_playing>, "is_playing(video: any) -> bool",
             "Get playback state."},
            {"love_video", "update", guarded<update>, "update(video: any, dt: float)",
             "Advance decoding on the main thread; call from update()."},
            {"love_video", "dimensions", guarded<dimensions>, "dimensions(video: any) -> vec2",
             "Get video dimensions."},
            {"love_video", "draw", guarded<draw>,
             "draw(video: any, position: vec2, rotation: float = 0, scale: vec2 = nil, origin: vec2 = nil)",
             "Draw native YUV video textures."},
            {"love_video", "set_filter", guarded<set_filter>, "set_filter(video: any, filter: string)",
             "Set video sampling."},
            {"love_video", "source", guarded<get_source>, "source(video: any) -> any",
             "Get an independent retained audio-source alias, or nil."},
            {"love_video", "filter", guarded<get_filter>, "filter(video: any) -> any",
             "Get video texture filtering and anisotropy."},
            {"love_video", "set_source", guarded<set_source>, "set_source(video: any, source: any)",
             "Retain and synchronize with an audio source; nil restores delta timing."},
        });
}
} // namespace lovescua
