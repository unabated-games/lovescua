// Native LÖVE OpenAL audio and Lullaby decoders; opened lazily on first use.
#include "binding_util.h"
#ifdef LOVE_SCUA_AUDIO
#include "audio/RecordingDevice.h"
#include "audio/openal/Audio.h"
#include "data/ByteData.h"
#include "data/DataStream.h"
#include "sound/lullaby/Sound.h"
namespace lovescua
{
love::audio::Audio &audioModule(scua_ctx *ctx)
{
    auto &state = binding::host(ctx);
    if (!state.audio)
        state.audio = new love::audio::openal::Audio();
    return *state.audio;
}
love::audio::Source *newAudioStream(scua_ctx *ctx, const uint8_t *data, size_t size)
{
    love::StrongRef<love::data::ByteData> bytes(new love::data::ByteData(data, size),
                                                love::Acquire::NORETAIN);
    love::StrongRef<love::data::DataStream> stream(new love::data::DataStream(bytes.get()),
                                                   love::Acquire::NORETAIN);
    love::sound::lullaby::Sound module;
    love::StrongRef<love::sound::Decoder> decoder(module.newDecoder(stream.get(), 16384),
                                                  love::Acquire::NORETAIN);
    return audioModule(ctx).newSource(decoder.get());
}
namespace
{
using namespace binding;
template <class T> struct Release
{
    void operator()(T *p)
    {
        if (p)
            p->release();
    }
};
template <class T> using Owned = std::unique_ptr<T, Release<T>>;
auto sound(scua_ctx *ctx)
{
    return objectArgument<love::sound::SoundData>(ctx, 0, ObjectKind::SoundData);
}
auto source(scua_ctx *ctx)
{
    return objectArgument<love::audio::Source>(ctx, 0, ObjectKind::AudioSource);
}
love::audio::Audio &audio(scua_ctx *ctx)
{
    return audioModule(ctx);
}
Owned<love::sound::Decoder> decoder(scua_ctx *ctx, int index)
{
    const uint8_t *p = nullptr;
    size_t n = 0;
    if (scua_arg_bytes(ctx, index, &p, &n) != SCUA_OK || n == 0 || n > 32 * 1024 * 1024)
        throw std::invalid_argument("expected encoded audio bytes, maximum 32 MiB");
    Owned<love::data::ByteData> data(new love::data::ByteData(p, n));
    Owned<love::data::DataStream> stream(new love::data::DataStream(data.get()));
    love::sound::lullaby::Sound module;
    return Owned<love::sound::Decoder>(module.newDecoder(stream.get(), 16384));
}
void love_sound_new_data(scua_ctx *ctx)
{
    arity(ctx, 4);
    int samples = integer(ctx, 0, 1, 8000000), rate = integer(ctx, 1, 8000, 192000),
        depth = integer(ctx, 2, 8, 16), channels = integer(ctx, 3, 1, 2);
    if (depth != 8 && depth != 16)
        throw std::invalid_argument("bit depth must be 8 or 16");
    returnObject(ctx, ObjectKind::SoundData, new love::sound::SoundData(samples, rate, depth, channels));
}
void love_sound_decode(scua_ctx *ctx)
{
    arity(ctx, 1);
    auto d = decoder(ctx, 0);
    if (d->getDuration() > 120)
        throw std::invalid_argument(
            "decoded sound is limited to 120 seconds; use streaming for longer audio");
    std::vector<uint8_t> pcm;
    int count = 0;
    while ((count = d->decode()) > 0)
    {
        if (pcm.size() + count > 32 * 1024 * 1024)
            throw std::invalid_argument("decoded sound exceeds 32 MiB");
        auto start = static_cast<const uint8_t *>(d->getBuffer());
        pcm.insert(pcm.end(), start, start + count);
    }
    if (pcm.empty())
        throw std::invalid_argument("sound contains no samples");
    int frames = pcm.size() / (d->getChannelCount() * (d->getBitDepth() / 8));
    returnObject(ctx, ObjectKind::SoundData,
                 new love::sound::SoundData(pcm.data(), frames, d->getSampleRate(), d->getBitDepth(),
                                            d->getChannelCount()));
}
void love_sound_info(scua_ctx *ctx)
{
    arity(ctx, 1);
    auto d = sound(ctx);
    if (!d)
        return;
    Record r(ctx);
    r.integer("samples", d->getSampleCount());
    r.integer("sample_rate", d->getSampleRate());
    r.integer("bit_depth", d->getBitDepth());
    r.integer("channels", d->getChannelCount());
    r.integer("bytes", d->getSize());
    r.num("duration", d->getDuration());
    r.done();
}
void love_sound_get_sample(scua_ctx *ctx)
{
    arity(ctx, 3);
    auto d = sound(ctx);
    if (d)
        scua_return_float(ctx, d->getSample(integer(ctx, 1, 0, d->getSampleCount() - 1),
                                            integer(ctx, 2, 1, d->getChannelCount())));
}
void love_sound_set_sample(scua_ctx *ctx)
{
    arity(ctx, 4);
    auto value = number(ctx, 3);
    if (value < -1 || value > 1)
        throw std::invalid_argument("sample must be in [-1,1]");
    auto d = sound(ctx);
    if (d)
        d->setSample(integer(ctx, 1, 0, d->getSampleCount() - 1), integer(ctx, 2, 1, d->getChannelCount()),
                     value);
}
void love_sound_clone(scua_ctx *ctx)
{
    arity(ctx, 1);
    auto d = sound(ctx);
    if (d)
        returnObject(ctx, ObjectKind::SoundData, d->clone());
}
void love_sound_slice_data(scua_ctx *ctx)
{
    arity(ctx, 3);
    auto d = sound(ctx);
    if (d)
    {
        int start = integer(ctx, 1, 0, d->getSampleCount() - 1),
            count = integer(ctx, 2, 1, d->getSampleCount() - start);
        returnObject(ctx, ObjectKind::SoundData, d->slice(start, count));
    }
}
void love_sound_bytes(scua_ctx *ctx)
{
    arity(ctx, 1);
    auto d = sound(ctx);
    if (d)
        scua_return_bytes(ctx, static_cast<const uint8_t *>(d->getData()), d->getSize());
}
void love_audio_new_source(scua_ctx *ctx)
{
    arity(ctx, 1);
    auto d = sound(ctx);
    if (d)
        returnObject(ctx, ObjectKind::AudioSource, audio(ctx).newSource(d));
}
void love_audio_new_stream(scua_ctx *ctx)
{
    arity(ctx, 1);
    auto d = decoder(ctx, 0);
    returnObject(ctx, ObjectKind::AudioSource, audio(ctx).newSource(d.get()));
}
void love_audio_clone(scua_ctx *ctx)
{
    arity(ctx, 1);
    auto d = source(ctx);
    if (d)
        returnObject(ctx, ObjectKind::AudioSource, d->clone());
}
void love_audio_play(scua_ctx *ctx)
{
    arity(ctx, 1);
    auto d = source(ctx);
    if (d)
        scua_return_bool(ctx, d->play());
}
void love_audio_stop(scua_ctx *ctx)
{
    arity(ctx, 1);
    auto d = source(ctx);
    if (d)
        d->stop();
}
void love_audio_pause(scua_ctx *ctx)
{
    arity(ctx, 1);
    auto d = source(ctx);
    if (d)
        d->pause();
}
void love_audio_is_playing(scua_ctx *ctx)
{
    arity(ctx, 1);
    auto d = source(ctx);
    if (d)
        scua_return_bool(ctx, d->isPlaying());
}
void love_audio_is_finished(scua_ctx *ctx)
{
    arity(ctx, 1);
    auto d = source(ctx);
    if (d)
        scua_return_bool(ctx, d->isFinished());
}
void love_audio_pitch(scua_ctx *ctx)
{
    arity(ctx, 1);
    auto d = source(ctx);
    if (d)
        scua_return_float(ctx, d->getPitch());
}
void love_audio_volume(scua_ctx *ctx)
{
    arity(ctx, 1);
    auto d = source(ctx);
    if (d)
        scua_return_float(ctx, d->getVolume());
}
void love_audio_is_looping(scua_ctx *ctx)
{
    arity(ctx, 1);
    auto d = source(ctx);
    if (d)
        scua_return_bool(ctx, d->isLooping());
}
void love_audio_is_relative(scua_ctx *ctx)
{
    arity(ctx, 1);
    auto d = source(ctx);
    if (d)
        scua_return_bool(ctx, d->isRelative());
}
void love_audio_channel_count(scua_ctx *ctx)
{
    arity(ctx, 1);
    auto d = source(ctx);
    if (d)
        scua_return_int(ctx, d->getChannelCount());
}
void love_audio_set_pitch(scua_ctx *ctx)
{
    arity(ctx, 2);
    auto value = number(ctx, 1);
    if (value < 0.0001 || value > 100)
        throw std::invalid_argument("value outside supported range");
    auto d = source(ctx);
    if (d)
        d->setPitch(value);
}
void love_audio_set_volume(scua_ctx *ctx)
{
    arity(ctx, 2);
    auto value = number(ctx, 1);
    if (value < 0 || value > 1)
        throw std::invalid_argument("value outside supported range");
    auto d = source(ctx);
    if (d)
        d->setVolume(value);
}
void love_audio_set_reference_distance(scua_ctx *ctx)
{
    arity(ctx, 2);
    auto value = number(ctx, 1);
    if (value < 0.0001 || value > 1000000)
        throw std::invalid_argument("value outside supported range");
    auto d = source(ctx);
    if (d)
        d->setReferenceDistance(value);
}
void love_audio_set_rolloff(scua_ctx *ctx)
{
    arity(ctx, 2);
    auto value = number(ctx, 1);
    if (value < 0 || value > 1000000)
        throw std::invalid_argument("value outside supported range");
    auto d = source(ctx);
    if (d)
        d->setRolloffFactor(value);
}
void love_audio_set_max_distance(scua_ctx *ctx)
{
    arity(ctx, 2);
    auto value = number(ctx, 1);
    if (value < 0.0001 || value > 1e+20)
        throw std::invalid_argument("value outside supported range");
    auto d = source(ctx);
    if (d)
        d->setMaxDistance(value);
}
void love_audio_set_looping(scua_ctx *ctx)
{
    arity(ctx, 2);
    bool enabled = boolean(ctx, 1);
    auto d = source(ctx);
    if (d)
        d->setLooping(enabled);
}
void love_audio_set_relative(scua_ctx *ctx)
{
    arity(ctx, 2);
    bool enabled = boolean(ctx, 1);
    auto d = source(ctx);
    if (d)
        d->setRelative(enabled);
}
void love_audio_set_position(scua_ctx *ctx)
{
    arity(ctx, 2);
    float v[3];
    vector(ctx, 1, v, 3);
    auto d = source(ctx);
    if (d)
        d->setPosition(v);
}
void love_audio_position(scua_ctx *ctx)
{
    arity(ctx, 1);
    auto d = source(ctx);
    if (d)
    {
        float v[3];
        d->getPosition(v);
        scua_return_vec(ctx, v, 3);
    }
}
void love_audio_set_velocity(scua_ctx *ctx)
{
    arity(ctx, 2);
    float v[3];
    vector(ctx, 1, v, 3);
    auto d = source(ctx);
    if (d)
        d->setVelocity(v);
}
void love_audio_velocity(scua_ctx *ctx)
{
    arity(ctx, 1);
    auto d = source(ctx);
    if (d)
    {
        float v[3];
        d->getVelocity(v);
        scua_return_vec(ctx, v, 3);
    }
}
void love_audio_set_direction(scua_ctx *ctx)
{
    arity(ctx, 2);
    float v[3];
    vector(ctx, 1, v, 3);
    auto d = source(ctx);
    if (d)
        d->setDirection(v);
}
void love_audio_direction(scua_ctx *ctx)
{
    arity(ctx, 1);
    auto d = source(ctx);
    if (d)
    {
        float v[3];
        d->getDirection(v);
        scua_return_vec(ctx, v, 3);
    }
}
void love_audio_seek(scua_ctx *ctx)
{
    arity(ctx, 2);
    auto seconds = number(ctx, 1);
    if (seconds < 0)
        throw std::invalid_argument("seek must be nonnegative");
    auto d = source(ctx);
    if (d)
        d->seek(seconds, love::audio::Source::UNIT_SECONDS);
}
void love_audio_tell(scua_ctx *ctx)
{
    arity(ctx, 1);
    auto d = source(ctx);
    if (d)
        scua_return_float(ctx, d->tell(love::audio::Source::UNIT_SECONDS));
}
void love_audio_duration(scua_ctx *ctx)
{
    arity(ctx, 1);
    auto d = source(ctx);
    if (d)
        scua_return_float(ctx, d->getDuration(love::audio::Source::UNIT_SECONDS));
}
void love_audio_stop_all(scua_ctx *ctx)
{
    arity(ctx, 0);
    audio(ctx).stop();
}
void love_audio_set_master_volume(scua_ctx *ctx)
{
    arity(ctx, 1);
    auto v = number(ctx, 0);
    if (v < 0 || v > 1)
        throw std::invalid_argument("volume must be in [0,1]");
    audio(ctx).setVolume(v);
}
void love_audio_master_volume(scua_ctx *ctx)
{
    arity(ctx, 0);
    scua_return_float(ctx, audio(ctx).getVolume());
}
void love_audio_active_count(scua_ctx *ctx)
{
    arity(ctx, 0);
    scua_return_int(ctx, audio(ctx).getActiveSourceCount());
}
void love_audio_max_sources(scua_ctx *ctx)
{
    arity(ctx, 0);
    scua_return_int(ctx, audio(ctx).getMaxSources());
}
void love_audio_set_listener_position(scua_ctx *ctx)
{
    arity(ctx, 1);
    float v[3];
    vector(ctx, 0, v, 3);
    audio(ctx).setPosition(v);
}
void love_audio_listener_position(scua_ctx *ctx)
{
    arity(ctx, 0);
    float v[3];
    audio(ctx).getPosition(v);
    scua_return_vec(ctx, v, 3);
}

auto managedDecoder(scua_ctx *ctx)
{
    return objectArgument<love::sound::Decoder>(ctx, 0, ObjectKind::Decoder);
}
auto recording(scua_ctx *ctx)
{
    return objectArgument<love::audio::RecordingDevice>(ctx, 0, ObjectKind::Recording);
}
std::string handleString(scua_partition *p, scua_Handle h)
{
    size_t n = 0;
    auto text = scua_to_lstring(p, h, &n);
    if (!text || std::memchr(text, 0, n))
        throw std::invalid_argument("expected a string without NUL");
    return {text, n};
}
double handleNumber(scua_partition *p, scua_Handle h)
{
    int type = scua_typeof(p, h);
    double v = scua_to_num(p, h);
    if ((type != SCUA_TYPE_INT && type != SCUA_TYPE_FLOAT) || !std::isfinite(v) ||
        std::abs(v) > std::numeric_limits<float>::max())
        throw std::invalid_argument("expected a finite parameter number");
    return v;
}
// The 0.37 C API exposes named lookup but no table iterator. An explicit array
// keeps misspelled and duplicate parameter names visible at this boundary.
template <class Fn> void parameterList(scua_ctx *ctx, int index, Fn consume)
{
    if (!supplied(ctx, index))
        return;
    auto p = scua_ctx_partition(ctx);
    auto list = scua_arg(ctx, index);
    if (scua_typeof(p, list) != SCUA_TYPE_ARRAY)
        throw std::invalid_argument("parameters must be an array of {name, value} records");
    for (int i = 0; i <= 64; ++i)
    {
        auto item = scua_index(p, list, i);
        if (!scua_handle_valid(p, item))
            return;
        if (i == 64)
            throw std::invalid_argument("maximum 64 audio parameters");
        auto name = handleString(p, scua_field(p, item, "name"));
        consume(name, scua_field(p, item, "value"));
    }
}
using Effect = love::audio::Effect;
using Filter = love::audio::Filter;
std::map<Filter::Parameter, float> filterParameters(scua_ctx *ctx, int typeIndex, int valuesIndex)
{
    auto name = string(ctx, typeIndex);
    Filter::Type type;
    if (!Filter::getConstant(name.c_str(), type) || type == Filter::TYPE_BASIC)
        throw std::invalid_argument("unknown filter type");
    std::map<Filter::Parameter, float> result{{Filter::FILTER_TYPE, float(type)}};
    parameterList(ctx, valuesIndex,
                  [&](const std::string &key, scua_Handle v)
                  {
                      Filter::Parameter parameter;
                      if ((!Filter::getConstant(key.c_str(), parameter, type) &&
                           !Filter::getConstant(key.c_str(), parameter, Filter::TYPE_BASIC)) ||
                          parameter == Filter::FILTER_TYPE)
                          throw std::invalid_argument("unknown filter parameter: " + key);
                      float value = handleNumber(scua_ctx_partition(ctx), v);
                      if (value < 0 || value > 1)
                          throw std::invalid_argument("filter gains must be in [0,1]");
                      if (!result.emplace(parameter, value).second)
                          throw std::invalid_argument("duplicate filter parameter: " + key);
                  });
    return result;
}
void new_decoder(scua_ctx *ctx)
{
    arity(ctx, 1);
    returnObject(ctx, ObjectKind::Decoder, decoder(ctx, 0).release());
}
void decoder_info(scua_ctx *ctx)
{
    arity(ctx, 1);
    auto d = managedDecoder(ctx);
    if (!d)
        return;
    Record r(ctx);
    r.integer("sample_rate", d->getSampleRate());
    r.integer("channels", d->getChannelCount());
    r.integer("bit_depth", d->getBitDepth());
    r.num("duration", d->getDuration());
    r.boolean("finished", d->isFinished());
    r.boolean("seekable", d->isSeekable());
    r.done();
}
void decode_next(scua_ctx *ctx)
{
    arity(ctx, 1);
    auto d = managedDecoder(ctx);
    if (!d)
        return;
    int n = d->decode();
    if (n <= 0)
    {
        scua_return_value(ctx, scua_nil(scua_ctx_partition(ctx)));
        return;
    }
    int stride = d->getChannelCount() * (d->getBitDepth() / 8);
    if (n > d->getSize() || stride <= 0 || n % stride)
        throw std::runtime_error("invalid decoder PCM buffer");
    returnObject(ctx, ObjectKind::SoundData,
                 new love::sound::SoundData(d->getBuffer(), n / stride, d->getSampleRate(), d->getBitDepth(),
                                            d->getChannelCount()));
}
void decoder_seek(scua_ctx *ctx)
{
    arity(ctx, 2);
    double t = number(ctx, 1);
    if (t < 0 || t > 86400)
        throw std::invalid_argument("decoder seek must be in [0,86400]");
    auto d = managedDecoder(ctx);
    if (d)
        scua_return_bool(ctx, d->seek(t));
}
void decoder_clone(scua_ctx *ctx)
{
    arity(ctx, 1);
    auto d = managedDecoder(ctx);
    if (d)
        returnObject(ctx, ObjectKind::Decoder, d->clone());
}
void new_queue(scua_ctx *ctx)
{
    arity(ctx, 4);
    int rate = integer(ctx, 0, 8000, 192000), depth = integer(ctx, 1, 8, 16),
        channels = integer(ctx, 2, 1, 2), buffers = integer(ctx, 3, 1, 64);
    if (depth != 8 && depth != 16)
        throw std::invalid_argument("bit depth must be 8 or 16");
    returnObject(ctx, ObjectKind::AudioSource, audio(ctx).newSource(rate, depth, channels, buffers));
}
void queue(scua_ctx *ctx)
{
    arity(ctx, 2);
    auto s = source(ctx);
    auto d = objectArgument<love::sound::SoundData>(ctx, 1, ObjectKind::SoundData);
    if (s && d)
        scua_return_bool(ctx, s->queue(d->getData(), d->getSize(), d->getSampleRate(), d->getBitDepth(),
                                       d->getChannelCount()));
}
void free_buffers(scua_ctx *ctx)
{
    arity(ctx, 1);
    auto s = source(ctx);
    if (s)
        scua_return_int(ctx, s->getFreeBufferCount());
}
void source_type(scua_ctx *ctx)
{
    arity(ctx, 1);
    auto s = source(ctx);
    if (s)
    {
        const char *name = "unknown";
        love::audio::Source::getConstant(s->getType(), name);
        returnString(ctx, name);
    }
}
void set_cone(scua_ctx *ctx)
{
    arity(ctx, 2);
    float v[4];
    vector(ctx, 1, v, 4);
    if (v[0] < 0 || v[1] < v[0] || v[1] > 6.283185308 || v[2] < 0 || v[2] > 1 || v[3] < 0 || v[3] > 1)
        throw std::invalid_argument("cone requires 0 <= inner <= outer <= 2pi and gains in [0,1]");
    auto s = source(ctx);
    if (s)
        s->setCone(v[0], v[1], v[2], v[3]);
}
void cone(scua_ctx *ctx)
{
    arity(ctx, 1);
    auto s = source(ctx);
    if (s)
    {
        float v[4];
        s->getCone(v[0], v[1], v[2], v[3]);
        scua_return_vec(ctx, v, 4);
    }
}
void set_listener_velocity(scua_ctx *ctx)
{
    arity(ctx, 1);
    float v[3];
    vector(ctx, 0, v, 3);
    audio(ctx).setVelocity(v);
}
void listener_velocity(scua_ctx *ctx)
{
    arity(ctx, 0);
    float v[3];
    audio(ctx).getVelocity(v);
    scua_return_vec(ctx, v, 3);
}
void set_listener_orientation(scua_ctx *ctx)
{
    arity(ctx, 2);
    float v[6];
    vector(ctx, 0, v, 3);
    vector(ctx, 1, v + 3, 3);
    double dot = 0, a = 0, b = 0;
    for (int i = 0; i < 3; ++i)
    {
        dot += double(v[i]) * v[i + 3];
        a += double(v[i]) * v[i];
        b += double(v[i + 3]) * v[i + 3];
    }
    if (a < 1e-12 || b < 1e-12 || dot * dot >= a * b * 0.999999)
        throw std::invalid_argument("listener vectors must be nonzero and nonparallel");
    audio(ctx).setOrientation(v);
}
void listener_orientation(scua_ctx *ctx)
{
    arity(ctx, 0);
    float v[6];
    audio(ctx).getOrientation(v);
    Record r(ctx);
    r.set("forward", scua_vec(r.p, v, 3));
    r.set("up", scua_vec(r.p, v + 3, 3));
    r.done();
}
void set_doppler(scua_ctx *ctx)
{
    arity(ctx, 1);
    float v = number(ctx, 0);
    if (v < 0 || v > 100)
        throw std::invalid_argument("doppler scale must be in [0,100]");
    audio(ctx).setDopplerScale(v);
}
void doppler(scua_ctx *ctx)
{
    arity(ctx, 0);
    scua_return_float(ctx, audio(ctx).getDopplerScale());
}
void set_distance_model(scua_ctx *ctx)
{
    arity(ctx, 1);
    auto name = string(ctx, 0);
    love::audio::Audio::DistanceModel model;
    if (!love::audio::Audio::getConstant(name.c_str(), model))
        throw std::invalid_argument("unknown distance model");
    audio(ctx).setDistanceModel(model);
}
void distance_model(scua_ctx *ctx)
{
    arity(ctx, 0);
    const char *name = "unknown";
    love::audio::Audio::getConstant(audio(ctx).getDistanceModel(), name);
    returnString(ctx, name);
}
void effects_info(scua_ctx *ctx)
{
    arity(ctx, 0);
    auto &a = audio(ctx);
    Record r(ctx);
    r.boolean("supported", a.isEFXsupported());
    r.integer("scene_limit", a.getMaxSceneEffects());
    r.integer("source_limit", a.getMaxSourceEffects());
    r.done();
}
void set_effect(scua_ctx *ctx)
{
    if (scua_arg_count(ctx) < 2 || scua_arg_count(ctx) > 3)
        throw std::invalid_argument("set_effect expects 2 or 3 arguments");
    auto name = string(ctx, 0), kind = string(ctx, 1);
    Effect::Type type;
    if (!Effect::getConstant(kind.c_str(), type) || type == Effect::TYPE_BASIC)
        throw std::invalid_argument("unknown effect type");
    std::map<Effect::Parameter, float> params{{Effect::EFFECT_TYPE, float(type)}};
    auto p = scua_ctx_partition(ctx);
    parameterList(ctx, 2,
                  [&](const std::string &key, scua_Handle v)
                  {
                      Effect::Parameter parameter;
                      if ((!Effect::getConstant(key.c_str(), parameter, type) &&
                           !Effect::getConstant(key.c_str(), parameter, Effect::TYPE_BASIC)) ||
                          parameter == Effect::EFFECT_TYPE)
                          throw std::invalid_argument("unknown effect parameter: " + key);
                      float value = 0;
                      switch (Effect::getParameterType(parameter))
                      {
                      case Effect::PARAM_FLOAT:
                          value = handleNumber(p, v);
                          break;
                      case Effect::PARAM_BOOL:
                          if (scua_typeof(p, v) != SCUA_TYPE_BOOL)
                              throw std::invalid_argument("effect parameter must be boolean");
                          value = scua_to_bool(p, v);
                          break;
                      case Effect::PARAM_WAVEFORM:
                      {
                          auto wave = handleString(p, v);
                          Effect::Waveform waveform;
                          if (!Effect::getConstant(wave.c_str(), waveform))
                              throw std::invalid_argument("unknown effect waveform");
                          value = waveform;
                          break;
                      }
                      default:
                          throw std::invalid_argument("invalid effect parameter type");
                      }
                      if (!params.emplace(parameter, value).second)
                          throw std::invalid_argument("duplicate effect parameter: " + key);
                  });
    scua_return_bool(ctx, audio(ctx).setEffect(name.c_str(), params));
}
void remove_effect(scua_ctx *ctx)
{
    arity(ctx, 1);
    auto name = string(ctx, 0);
    scua_return_bool(ctx, audio(ctx).unsetEffect(name.c_str()));
}
void effects(scua_ctx *ctx)
{
    arity(ctx, 0);
    std::vector<std::string> values;
    audio(ctx).getActiveEffects(values);
    returnStrings(ctx, values);
}
void source_effects(scua_ctx *ctx)
{
    arity(ctx, 1);
    auto s = source(ctx);
    if (s)
    {
        std::vector<std::string> values;
        s->getActiveEffects(values);
        returnStrings(ctx, values);
    }
}
void attach_effect(scua_ctx *ctx)
{
    arity(ctx, 2);
    auto name = string(ctx, 1);
    auto s = source(ctx);
    if (s)
        scua_return_bool(ctx, s->setEffect(name.c_str()));
}
void detach_effect(scua_ctx *ctx)
{
    arity(ctx, 2);
    auto name = string(ctx, 1);
    auto s = source(ctx);
    if (s)
        scua_return_bool(ctx, s->unsetEffect(name.c_str()));
}
void set_filter(scua_ctx *ctx)
{
    if (scua_arg_count(ctx) < 2 || scua_arg_count(ctx) > 3)
        throw std::invalid_argument("set_filter expects 2 or 3 arguments");
    auto params = filterParameters(ctx, 1, 2);
    auto s = source(ctx);
    if (s)
        scua_return_bool(ctx, s->setFilter(params));
}
void attach_effect_filtered(scua_ctx *ctx)
{
    if (scua_arg_count(ctx) < 3 || scua_arg_count(ctx) > 4)
        throw std::invalid_argument("expected source, effect name, filter type and optional parameters");
    auto name = string(ctx, 1);
    auto params = filterParameters(ctx, 2, 3);
    auto s = source(ctx);
    if (s)
        scua_return_bool(ctx, s->setEffect(name.c_str(), params));
}
void clear_filter(scua_ctx *ctx)
{
    arity(ctx, 1);
    auto s = source(ctx);
    if (s)
        scua_return_bool(ctx, s->setFilter());
}
void recording_names(scua_ctx *ctx)
{
    arity(ctx, 0);
    std::vector<std::string> names;
    for (auto d : audio(ctx).getRecordingDevices())
        names.emplace_back(d->getName());
    returnStrings(ctx, names);
}
void recording_get(scua_ctx *ctx)
{
    arity(ctx, 1);
    int i = integer(ctx, 0, 0, 255);
    const auto &devices = audio(ctx).getRecordingDevices();
    if (size_t(i) >= devices.size())
    {
        scua_return_value(ctx, scua_nil(scua_ctx_partition(ctx)));
        return;
    }
    devices[i]->retain();
    returnObject(ctx, ObjectKind::Recording, devices[i]);
}
void recording_info(scua_ctx *ctx)
{
    arity(ctx, 1);
    auto d = recording(ctx);
    if (d)
    {
        Record r(ctx);
        r.str("name", d->getName());
        r.boolean("recording", d->isRecording());
        r.integer("samples", d->getSampleCount());
        r.integer("capacity", d->getMaxSamples());
        r.integer("sample_rate", d->getSampleRate());
        r.integer("bit_depth", d->getBitDepth());
        r.integer("channels", d->getChannelCount());
        r.done();
    }
}
void recording_start(scua_ctx *ctx)
{
    arity(ctx, 5);
    int count = integer(ctx, 1, 1, 8000000), rate = integer(ctx, 2, 8000, 192000),
        depth = integer(ctx, 3, 8, 16), channels = integer(ctx, 4, 1, 2);
    if (depth != 8 && depth != 16)
        throw std::invalid_argument("bit depth must be 8 or 16");
    auto d = recording(ctx);
    if (d)
        scua_return_bool(ctx, d->start(count, rate, depth, channels));
}
void recording_stop(scua_ctx *ctx)
{
    arity(ctx, 1);
    auto d = recording(ctx);
    if (d)
        d->stop();
}
void recording_data(scua_ctx *ctx)
{
    arity(ctx, 1);
    auto d = recording(ctx);
    if (d)
    {
        auto data = d->getData();
        if (data)
            returnObject(ctx, ObjectKind::SoundData, data);
        else
            scua_return_value(ctx, scua_nil(scua_ctx_partition(ctx)));
    }
}

} // namespace
void registerAudio(scua_partition *p, Host &state)
{
    for (auto kind :
         {ObjectKind::SoundData, ObjectKind::AudioSource, ObjectKind::Decoder, ObjectKind::Recording})
        registerObjectType(p, state, kind);
    registerFunctions(
        p, state,
        {
            {"love_sound", "new_data", guarded<love_sound_new_data>,
             "new_data(samples: int, rate: int, depth: int, channels: int) -> any",
             "Create editable PCM samples (maximum 32 MiB)."},
            {"love_sound", "decode", guarded<love_sound_decode>, "decode(encoded: bytes) -> any",
             "Decode WAV, Ogg Vorbis, FLAC or MP3 bytes into PCM."},
            {"love_sound", "info", guarded<love_sound_info>, "info(data: any) -> any", "Get PCM metadata."},
            {"love_sound", "get_sample", guarded<love_sound_get_sample>,
             "get_sample(data: any, index: int, channel: int) -> float",
             "Read a zero-based sample and one-based channel."},
            {"love_sound", "set_sample", guarded<love_sound_set_sample>,
             "set_sample(data: any, index: int, channel: int, value: float)", "Write a PCM sample."},
            {"love_sound", "clone", guarded<love_sound_clone>, "clone(data: any) -> any", "Copy PCM data."},
            {"love_sound", "slice_data", guarded<love_sound_slice_data>,
             "slice_data(data: any, start: int, count: int) -> any", "Copy a sample range."},
            {"love_sound", "bytes", guarded<love_sound_bytes>, "bytes(data: any) -> bytes",
             "Copy PCM bytes."},
            {"love_audio", "new_source", guarded<love_audio_new_source>, "new_source(data: any) -> any",
             "Create a static playback source from PCM data."},
            {"love_audio", "new_stream", guarded<love_audio_new_stream>, "new_stream(encoded: bytes) -> any",
             "Create a streaming playback source from encoded resource bytes."},
            {"love_audio", "clone", guarded<love_audio_clone>, "clone(source: any) -> any",
             "Clone a source with independent playback state."},
            {"love_audio", "play", guarded<love_audio_play>, "play(source: any) -> bool", "Source play."},
            {"love_audio", "stop", guarded<love_audio_stop>, "stop(source: any)", "Source stop."},
            {"love_audio", "pause", guarded<love_audio_pause>, "pause(source: any)", "Source pause."},
            {"love_audio", "is_playing", guarded<love_audio_is_playing>, "is_playing(source: any) -> bool",
             "Source is playing."},
            {"love_audio", "is_finished", guarded<love_audio_is_finished>, "is_finished(source: any) -> bool",
             "Source is finished."},
            {"love_audio", "pitch", guarded<love_audio_pitch>, "pitch(source: any) -> float",
             "Source pitch."},
            {"love_audio", "volume", guarded<love_audio_volume>, "volume(source: any) -> float",
             "Source volume."},
            {"love_audio", "is_looping", guarded<love_audio_is_looping>, "is_looping(source: any) -> bool",
             "Source is looping."},
            {"love_audio", "is_relative", guarded<love_audio_is_relative>, "is_relative(source: any) -> bool",
             "Source is relative."},
            {"love_audio", "channel_count", guarded<love_audio_channel_count>,
             "channel_count(source: any) -> int", "Source channel count."},
            {"love_audio", "set_pitch", guarded<love_audio_set_pitch>, "set_pitch(source: any, value: float)",
             "Set source pitch."},
            {"love_audio", "set_volume", guarded<love_audio_set_volume>,
             "set_volume(source: any, value: float)", "Set source volume."},
            {"love_audio", "set_reference_distance", guarded<love_audio_set_reference_distance>,
             "set_reference_distance(source: any, value: float)", "Set source reference distance."},
            {"love_audio", "set_rolloff", guarded<love_audio_set_rolloff>,
             "set_rolloff(source: any, value: float)", "Set source rolloff."},
            {"love_audio", "set_max_distance", guarded<love_audio_set_max_distance>,
             "set_max_distance(source: any, value: float)", "Set source max distance."},
            {"love_audio", "set_looping", guarded<love_audio_set_looping>,
             "set_looping(source: any, enabled: bool)", "Configure source looping."},
            {"love_audio", "set_relative", guarded<love_audio_set_relative>,
             "set_relative(source: any, enabled: bool)", "Configure source relative."},
            {"love_audio", "set_position", guarded<love_audio_set_position>,
             "set_position(source: any, value: vec3)", "Set the source position."},
            {"love_audio", "position", guarded<love_audio_position>, "position(source: any) -> vec3",
             "Get the source position."},
            {"love_audio", "set_velocity", guarded<love_audio_set_velocity>,
             "set_velocity(source: any, value: vec3)", "Set the source velocity."},
            {"love_audio", "velocity", guarded<love_audio_velocity>, "velocity(source: any) -> vec3",
             "Get the source velocity."},
            {"love_audio", "set_direction", guarded<love_audio_set_direction>,
             "set_direction(source: any, value: vec3)", "Set the source direction."},
            {"love_audio", "direction", guarded<love_audio_direction>, "direction(source: any) -> vec3",
             "Get the source direction."},
            {"love_audio", "seek", guarded<love_audio_seek>, "seek(source: any, seconds: float)",
             "Seek in seconds."},
            {"love_audio", "tell", guarded<love_audio_tell>, "tell(source: any) -> float",
             "Playback time in seconds."},
            {"love_audio", "duration", guarded<love_audio_duration>, "duration(source: any) -> float",
             "Playback time in seconds."},
            {"love_audio", "stop_all", guarded<love_audio_stop_all>, "stop_all()", "Stop every source."},
            {"love_audio", "set_master_volume", guarded<love_audio_set_master_volume>,
             "set_master_volume(value: float)", "Set listener volume."},
            {"love_audio", "master_volume", guarded<love_audio_master_volume>, "master_volume() -> float",
             "Get listener volume."},
            {"love_audio", "active_count", guarded<love_audio_active_count>, "active_count() -> int",
             "Get playing source count."},
            {"love_audio", "max_sources", guarded<love_audio_max_sources>, "max_sources() -> int",
             "Get playback source capacity."},
            {"love_audio", "set_listener_position", guarded<love_audio_set_listener_position>,
             "set_listener_position(position: vec3)", "Set listener position."},
            {"love_audio", "listener_position", guarded<love_audio_listener_position>,
             "listener_position() -> vec3", "Get listener position."},
            {"love_sound", "new_decoder", guarded<new_decoder>, "new_decoder(encoded: bytes) -> any",
             "Create a managed incremental decoder."},
            {"love_sound", "decoder_info", guarded<decoder_info>, "decoder_info(decoder: any) -> any",
             "Get decoder format, duration and EOF state."},
            {"love_sound", "decode_next", guarded<decode_next>, "decode_next(decoder: any) -> any",
             "Decode one PCM chunk, or nil at EOF."},
            {"love_sound", "decoder_seek", guarded<decoder_seek>,
             "decoder_seek(decoder: any, seconds: float) -> bool", "Seek an incremental decoder."},
            {"love_sound", "decoder_clone", guarded<decoder_clone>, "decoder_clone(decoder: any) -> any",
             "Clone a decoder starting at the beginning."},
            {"love_audio", "new_queue", guarded<new_queue>,
             "new_queue(rate: int, depth: int, channels: int, buffers: int) -> any",
             "Create a queueable PCM source with 1..64 buffers."},
            {"love_audio", "queue", guarded<queue>, "queue(source: any, data: any) -> bool",
             "Copy PCM into the next queue buffer."},
            {"love_audio", "free_buffers", guarded<free_buffers>, "free_buffers(source: any) -> int",
             "Get available queue buffers."},
            {"love_audio", "source_type", guarded<source_type>, "source_type(source: any) -> string",
             "Get static, stream or queue source type."},
            {"love_audio", "set_cone", guarded<set_cone>, "set_cone(source: any, value: vec4)",
             "Set inner/outer radians and volume/high-frequency gains."},
            {"love_audio", "cone", guarded<cone>, "cone(source: any) -> vec4", "Get source cone settings."},
            {"love_audio", "set_listener_velocity", guarded<set_listener_velocity>,
             "set_listener_velocity(value: vec3)", "Set listener velocity."},
            {"love_audio", "listener_velocity", guarded<listener_velocity>, "listener_velocity() -> vec3",
             "Get listener velocity."},
            {"love_audio", "set_listener_orientation", guarded<set_listener_orientation>,
             "set_listener_orientation(forward: vec3, up: vec3)",
             "Set nonparallel listener direction vectors."},
            {"love_audio", "listener_orientation", guarded<listener_orientation>,
             "listener_orientation() -> any", "Get forward and up vectors."},
            {"love_audio", "set_doppler", guarded<set_doppler>, "set_doppler(scale: float)",
             "Set Doppler scale in [0,100]."},
            {"love_audio", "doppler", guarded<doppler>, "doppler() -> float", "Get Doppler scale."},
            {"love_audio", "set_distance_model", guarded<set_distance_model>,
             "set_distance_model(model: string)", "Choose distance attenuation."},
            {"love_audio", "distance_model", guarded<distance_model>, "distance_model() -> string",
             "Get distance attenuation model."},
            {"love_audio", "effects_info", guarded<effects_info>, "effects_info() -> any",
             "Get EFX support and scene/source limits."},
            {"love_audio", "set_effect", guarded<set_effect>,
             "set_effect(name: string, kind: string, parameters: any = nil) -> bool",
             "Create an effect; parameters is an array of {name, value} records."},
            {"love_audio", "remove_effect", guarded<remove_effect>, "remove_effect(name: string) -> bool",
             "Remove a scene effect."},
            {"love_audio", "effects", guarded<effects>, "effects() -> any", "List scene effects."},
            {"love_audio", "source_effects", guarded<source_effects>, "source_effects(source: any) -> any",
             "List effects attached to a source."},
            {"love_audio", "attach_effect", guarded<attach_effect>,
             "attach_effect(source: any, name: string) -> bool", "Attach a named scene effect."},
            {"love_audio", "detach_effect", guarded<detach_effect>,
             "detach_effect(source: any, name: string) -> bool", "Detach an effect."},
            {"love_audio", "set_filter", guarded<set_filter>,
             "set_filter(source: any, kind: string, parameters: any = nil) -> bool",
             "Set lowpass/highpass/bandpass; array of {name, value} gains."},
            {"love_audio", "attach_effect_filtered", guarded<attach_effect_filtered>,
             "attach_effect_filtered(source: any, name: string, kind: string, parameters: any = nil) -> bool",
             "Route a source into an effect through a named low/high/bandpass send filter."},
            {"love_audio", "clear_filter", guarded<clear_filter>, "clear_filter(source: any) -> bool",
             "Clear a source filter."},
            {"love_audio", "recording_names", guarded<recording_names>, "recording_names() -> any",
             "Enumerate capture devices; may request OS permission."},
            {"love_audio", "recording_get", guarded<recording_get>, "recording_get(index: int) -> any",
             "Retain a zero-based recording device, or nil."},
            {"love_audio", "recording_info", guarded<recording_info>, "recording_info(device: any) -> any",
             "Get capture format and buffered sample count."},
            {"love_audio", "recording_start", guarded<recording_start>,
             "recording_start(device: any, samples: int, rate: int, depth: int, channels: int) -> bool",
             "Start capture with explicit format; may request OS permission."},
            {"love_audio", "recording_stop", guarded<recording_stop>, "recording_stop(device: any)",
             "Stop microphone capture."},
            {"love_audio", "recording_data", guarded<recording_data>, "recording_data(device: any) -> any",
             "Drain captured samples into PCM, or nil."},
        });
}
void shutdownAudio(Host &state)
{
    if (state.audio)
    {
        state.audio->release();
        state.audio = nullptr;
    }
}
} // namespace lovescua
#else
namespace lovescua
{
void registerAudio(scua_partition *, Host &) {}
void shutdownAudio(Host &) {}
} // namespace lovescua
#endif
