// Remaining audio controls: native snapshots, filtered sends, devices and source groups.
#include "options.h"
#ifdef LOVE_SCUA_AUDIO
#include "audio/Audio.h"
#include "audio/Source.h"
#include "sound/Decoder.h"
#include "sound/SoundData.h"
namespace lovescua
{
namespace
{
using namespace binding;
using Source = love::audio::Source;
using Filter = love::audio::Filter;
using Effect = love::audio::Effect;
auto source(scua_ctx *c, int i = 0)
{
    return objectArgument<Source>(c, i, ObjectKind::AudioSource);
}
void vec(Record &r, const char *name, float x, float y)
{
    float v[] = {x, y};
    r.set(name, scua_vec(r.p, v, 2));
}
void source_info(scua_ctx *ctx)
{
    arity(ctx, 1);
    auto s = source(ctx);
    if (!s)
        return;
    Record r(ctx);
    const char *kind = "unknown";
    Source::getConstant(s->getType(), kind);
    r.str("type", kind);
    r.boolean("playing", s->isPlaying());
    r.boolean("finished", s->isFinished());
    r.boolean("looping", s->isLooping());
    r.boolean("relative", s->isRelative());
    r.integer("channels", s->getChannelCount());
    r.num("pitch", s->getPitch());
    r.num("volume", s->getVolume());
    vec(r, "volume_limits", s->getMinVolume(), s->getMaxVolume());
    r.num("seconds", s->tell(Source::UNIT_SECONDS));
    r.num("samples", s->tell(Source::UNIT_SAMPLES));
    r.num("duration", s->getDuration(Source::UNIT_SECONDS));
    r.num("duration_samples", s->getDuration(Source::UNIT_SAMPLES));
    if (s->getChannelCount() == 1)
    {
        vec(r, "attenuation", s->getReferenceDistance(), s->getMaxDistance());
        r.num("rolloff", s->getRolloffFactor());
        r.num("air_absorption", s->getAirAbsorptionFactor());
        float v[3];
        s->getPosition(v);
        r.set("position", scua_vec(r.p, v, 3));
        s->getVelocity(v);
        r.set("velocity", scua_vec(r.p, v, 3));
        s->getDirection(v);
        r.set("direction", scua_vec(r.p, v, 3));
    }
    r.done();
}
void set_volume_limits(scua_ctx *ctx)
{
    arity(ctx, 2);
    float v[2];
    vector(ctx, 1, v, 2);
    if (v[0] < 0 || v[0] > v[1] || v[1] > 1)
        throw std::invalid_argument("volume limits require 0 <= min <= max <= 1");
    auto s = source(ctx);
    if (s)
    {
        s->setMinVolume(v[0]);
        s->setMaxVolume(v[1]);
    }
}
void set_air_absorption(scua_ctx *ctx)
{
    arity(ctx, 2);
    float n = number(ctx, 1);
    if (n < 0 || n > 10)
        throw std::invalid_argument("air absorption must be in [0,10]");
    auto s = source(ctx);
    if (s)
        s->setAirAbsorptionFactor(n);
}
void set_attenuation(scua_ctx *ctx)
{
    arity(ctx, 2);
    float v[2];
    vector(ctx, 1, v, 2);
    if (v[0] < 0 || v[1] < v[0])
        throw std::invalid_argument("attenuation requires 0 <= reference <= maximum");
    auto s = source(ctx);
    if (s)
    {
        s->setReferenceDistance(v[0]);
        s->setMaxDistance(v[1]);
    }
}
void seek_samples(scua_ctx *ctx)
{
    arity(ctx, 2);
    int n = integer(ctx, 1, 0, 2147483647);
    auto s = source(ctx);
    if (s)
        s->seek(n, Source::UNIT_SAMPLES);
}
void sound_copy(scua_ctx *ctx)
{
    arity(ctx, 5);
    auto dest = objectArgument<love::sound::SoundData>(ctx, 0, ObjectKind::SoundData),
         src = objectArgument<love::sound::SoundData>(ctx, 1, ObjectKind::SoundData);
    if (!src || !dest)
        return;
    int from = integer(ctx, 2, 0, src->getSampleCount()),
        count = integer(ctx, 3, 0, src->getSampleCount() - from),
        to = integer(ctx, 4, 0, dest->getSampleCount());
    if (count > dest->getSampleCount() - to)
        throw std::invalid_argument("PCM copy exceeds destination bounds");
    dest->copyFrom(src, from, count, to);
}
void decoder_rewind(scua_ctx *ctx)
{
    arity(ctx, 1);
    auto d = objectArgument<love::sound::Decoder>(ctx, 0, ObjectKind::Decoder);
    if (d)
        scua_return_bool(ctx, d->rewind());
}
void new_source_from_decoder(scua_ctx *ctx)
{
    arity(ctx, 1);
    auto d = objectArgument<love::sound::Decoder>(ctx, 0, ObjectKind::Decoder);
    if (d)
    {
        love::StrongRef<love::sound::Decoder> copy(d->clone(), love::Acquire::NORETAIN);
        returnObject(ctx, ObjectKind::AudioSource, audioModule(ctx).newSource(copy.get()));
    }
}
template <class Parameter, class Type, class GetName, class GetType>
scua_Handle parameters(scua_ctx *ctx, const std::map<Parameter, float> &values, Parameter typeParameter,
                       Type type, GetName getName, GetType getType)
{
    auto p = scua_ctx_partition(ctx);
    auto array = scua_new_array(p, 0);
    for (auto &entry : values)
    {
        if (entry.first == typeParameter)
            continue;
        const char *name = nullptr;
        if (!getName(entry.first, name, type))
            continue;
        Record r(ctx);
        r.str("name", name);
        switch (getType(entry.first))
        {
        case Effect::PARAM_BOOL:
            r.boolean("value", entry.second != 0);
            break;
        case Effect::PARAM_WAVEFORM:
        {
            const char *wave = "unknown";
            Effect::getConstant(Effect::Waveform(int(entry.second)), wave);
            r.str("value", wave);
            break;
        }
        default:
            r.num("value", entry.second);
            break;
        }
        if (scua_array_push(p, array, r.value) != SCUA_OK)
            throw std::runtime_error("could not build audio parameters");
    }
    return array;
}
void returnFilter(scua_ctx *ctx, const std::map<Filter::Parameter, float> &values)
{
    auto it = values.find(Filter::FILTER_TYPE);
    auto type = it == values.end() ? Filter::TYPE_BASIC : Filter::Type(int(it->second));
    const char *name = "none";
    if (type != Filter::TYPE_BASIC)
        Filter::getConstant(type, name);
    Record r(ctx);
    r.str("type", name);
    r.set("parameters",
          parameters(
              ctx, values, Filter::FILTER_TYPE, type, [](auto p, const char *&n, auto t)
              { return Filter::getConstant(p, n, t) || Filter::getConstant(p, n, Filter::TYPE_BASIC); },
              [](auto) { return Effect::PARAM_FLOAT; }));
    r.done();
}
void get_filter(scua_ctx *ctx)
{
    arity(ctx, 1);
    auto s = source(ctx);
    if (!s)
        return;
    std::map<Filter::Parameter, float> values;
    if (!s->getFilter(values))
    {
        scua_return_value(ctx, scua_nil(scua_ctx_partition(ctx)));
        return;
    }
    returnFilter(ctx, values);
}
void get_source_effect(scua_ctx *ctx)
{
    arity(ctx, 2);
    auto name = string(ctx, 1);
    auto s = source(ctx);
    if (!s)
        return;
    std::map<Filter::Parameter, float> values;
    if (!s->getEffect(name.c_str(), values))
    {
        scua_return_value(ctx, scua_nil(scua_ctx_partition(ctx)));
        return;
    }
    returnFilter(ctx, values);
}
void get_effect(scua_ctx *ctx)
{
    arity(ctx, 1);
    auto name = string(ctx, 0);
    std::map<Effect::Parameter, float> values;
    if (!audioModule(ctx).getEffect(name.c_str(), values))
    {
        scua_return_value(ctx, scua_nil(scua_ctx_partition(ctx)));
        return;
    }
    auto type = Effect::Type(int(values.at(Effect::EFFECT_TYPE)));
    const char *kind = "unknown";
    Effect::getConstant(type, kind);
    Record r(ctx);
    r.str("type", kind);
    r.set("parameters",
          parameters(
              ctx, values, Effect::EFFECT_TYPE, type, [](auto p, const char *&n, auto t)
              { return Effect::getConstant(p, n, t) || Effect::getConstant(p, n, Effect::TYPE_BASIC); },
              [](auto p) { return Effect::getParameterType(p); }));
    r.done();
}
std::vector<love::StrongRef<Source>> sources(scua_ctx *ctx)
{
    auto p = scua_ctx_partition(ctx);
    auto array = scua_arg(ctx, 0);
    if (scua_typeof(p, array) != SCUA_TYPE_ARRAY)
        throw std::invalid_argument("sources must be an array");
    std::vector<love::StrongRef<Source>> result;
    for (int i = 0; i <= 256; i++)
    {
        auto value = scua_index(p, array, i);
        if (!scua_handle_valid(p, value))
            break;
        if (i == 256)
            throw std::invalid_argument("maximum 256 sources in a group");
        void *storage = nullptr;
        scua_UserdataLease lease{};
        if (scua_userdata_borrow(p, value, host(ctx).objectTypes[size_t(ObjectKind::AudioSource)], &storage,
                                 &lease) != SCUA_OK)
            throw std::invalid_argument("source groups require live audio sources");
        result.emplace_back(static_cast<Source *>(static_cast<ObjectResource *>(storage)->object));
        if (scua_userdata_release(p, lease) != SCUA_OK)
            throw std::runtime_error("could not release source lease");
    }
    return result;
}
template <int Operation> void source_group(scua_ctx *ctx)
{
    arity(ctx, 1);
    auto refs = sources(ctx);
    std::vector<Source *> list;
    for (auto &s : refs)
        list.push_back(s.get());
    auto &a = audioModule(ctx);
    if constexpr (Operation == 0)
        scua_return_bool(ctx, a.play(list));
    else if constexpr (Operation == 1)
        a.pause(list);
    else
        a.stop(list);
}
void pause_all(scua_ctx *ctx)
{
    arity(ctx, 0);
    auto list = audioModule(ctx).pause();
    std::vector<love::StrongRef<Source>> refs;
    for (auto s : list)
        refs.emplace_back(s);
    auto p = scua_ctx_partition(ctx);
    auto array = scua_new_array(p, 0);
    for (auto &s : refs)
    {
        scua_Handle value{};
        if (scua_userdata_new(p, host(ctx).objectTypes[size_t(ObjectKind::AudioSource)],
                              sizeof(ObjectResource), s.get(), &value) != SCUA_OK)
            throw std::runtime_error("could not retain paused source");
        ++host(ctx).liveObjects;
        int status = scua_array_push(p, array, value);
        scua_unpin_global(p, value);
        if (status != SCUA_OK)
            throw std::runtime_error("could not build paused sources");
    }
    scua_return_value(ctx, array);
}
void playback_devices(scua_ctx *ctx)
{
    arity(ctx, 0);
    std::vector<std::string> names;
    audioModule(ctx).getPlaybackDevices(names);
    returnStrings(ctx, names);
}
void playback_device(scua_ctx *ctx)
{
    arity(ctx, 0);
    returnString(ctx, audioModule(ctx).getPlaybackDevice());
}
void set_playback_device(scua_ctx *ctx)
{
    arity(ctx, 1);
    auto name = supplied(ctx, 0) ? string(ctx, 0) : "";
    audioModule(ctx).setPlaybackDevice(name.empty() ? nullptr : name.c_str());
}
void spatialization(scua_ctx *ctx)
{
    arity(ctx, 0);
    const char *filter = nullptr;
    bool enabled = audioModule(ctx).getOutputSpatialization(filter);
    Record r(ctx);
    r.boolean("enabled", enabled);
    r.str("filter", filter ? filter : "");
    r.done();
}
void set_spatialization(scua_ctx *ctx)
{
    if (scua_arg_count(ctx) < 1 || scua_arg_count(ctx) > 2)
        throw std::invalid_argument("expected enabled and optional HRTF filter name");
    bool enabled = boolean(ctx, 0);
    auto filter = supplied(ctx, 1) ? string(ctx, 1) : "";
    scua_return_bool(
        ctx, audioModule(ctx).setOutputSpatialization(enabled, filter.empty() ? nullptr : filter.c_str()));
}
void spatialization_filters(scua_ctx *ctx)
{
    arity(ctx, 0);
    std::vector<std::string> names;
    audioModule(ctx).getOutputSpatializationFilters(names);
    returnStrings(ctx, names);
}
void set_mix_with_system(scua_ctx *ctx)
{
    arity(ctx, 1);
    scua_return_bool(ctx, love::audio::Audio::setMixWithSystem(boolean(ctx, 0)));
}
void pause_context(scua_ctx *ctx)
{
    arity(ctx, 0);
    audioModule(ctx).pauseContext();
}
void resume_context(scua_ctx *ctx)
{
    arity(ctx, 0);
    audioModule(ctx).resumeContext();
}
} // namespace
void registerAudioControls(scua_partition *p, Host &state)
{
    registerFunctions(
        p, state,
        {
            {"love_audio", "source_info", guarded<source_info>, "source_info(source: any) -> any",
             "Snapshot source properties, volume limits, sample/second positions and mono spatial settings."},
            {"love_audio", "set_volume_limits", guarded<set_volume_limits>,
             "set_volume_limits(source: any, limits: vec2)", "Set minimum/maximum source gain."},
            {"love_audio", "set_air_absorption", guarded<set_air_absorption>,
             "set_air_absorption(source: any, factor: float)", "Set mono air absorption in [0,10]."},
            {"love_audio", "set_attenuation", guarded<set_attenuation>,
             "set_attenuation(source: any, distances: vec2)", "Set reference/maximum attenuation distances."},
            {"love_audio", "seek_samples", guarded<seek_samples>, "seek_samples(source: any, sample: int)",
             "Seek using zero-based sample frames."},
            {"love_audio", "new_source_from_decoder", guarded<new_source_from_decoder>,
             "new_source_from_decoder(decoder: any) -> any",
             "Create an independent stream from a managed decoder clone."},
            {"love_audio", "get_filter", guarded<get_filter>, "get_filter(source: any) -> any",
             "Get filter type and named parameters, or nil."},
            {"love_audio", "get_effect", guarded<get_effect>, "get_effect(name: string) -> any",
             "Get effect type and typed parameters, or nil."},
            {"love_audio", "get_source_effect", guarded<get_source_effect>,
             "get_source_effect(source: any, name: string) -> any",
             "Get an attached effect's send filter, or nil if unattached."},
            {"love_audio", "play_many", guarded<source_group<0>>, "play_many(sources: any) -> bool",
             "Start a checked group of sources together."},
            {"love_audio", "pause_many", guarded<source_group<1>>, "pause_many(sources: any)",
             "Pause a checked source group."},
            {"love_audio", "stop_many", guarded<source_group<2>>, "stop_many(sources: any)",
             "Stop a checked source group."},
            {"love_audio", "pause_all", guarded<pause_all>, "pause_all() -> any",
             "Pause active sources and return independent retained aliases."},
            {"love_audio", "playback_devices", guarded<playback_devices>, "playback_devices() -> any",
             "Enumerate available playback device names."},
            {"love_audio", "playback_device", guarded<playback_device>, "playback_device() -> string",
             "Get the active playback device name."},
            {"love_audio", "set_playback_device", guarded<set_playback_device>,
             "set_playback_device(name: string = nil)",
             "Reopen this app's audio device; nil selects the default. Requires backend support."},
            {"love_audio", "spatialization", guarded<spatialization>, "spatialization() -> any",
             "Get output HRTF state and filter name."},
            {"love_audio", "set_spatialization", guarded<set_spatialization>,
             "set_spatialization(enabled: bool, filter: string = nil) -> bool",
             "Configure output HRTF; false when unsupported."},
            {"love_audio", "spatialization_filters", guarded<spatialization_filters>,
             "spatialization_filters() -> any", "Enumerate available HRTF filters."},
            {"love_audio", "set_mix_with_system", guarded<set_mix_with_system>,
             "set_mix_with_system(enabled: bool) -> bool",
             "Request mixing with other apps on supported platforms."},
            {"love_audio", "pause_context", guarded<pause_context>, "pause_context()",
             "Pause the app's audio context where supported."},
            {"love_audio", "resume_context", guarded<resume_context>, "resume_context()",
             "Resume the app's audio context where supported."},
            {"love_sound", "copy_data", guarded<sound_copy>,
             "copy_data(destination: any, source: any, source_start: int, count: int, destination_start: "
             "int)",
             "Copy PCM sample frames with bounds/channel checks and native bit-depth conversion."},
            {"love_sound", "decoder_rewind", guarded<decoder_rewind>, "decoder_rewind(decoder: any) -> bool",
             "Rewind an incremental decoder."},
        });
}
} // namespace lovescua
#else
namespace lovescua
{
void registerAudioControls(scua_partition *, Host &) {}
} // namespace lovescua
#endif
