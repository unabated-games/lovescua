// Decoder EOF/seek tests require no graphics or audio device.
#include "data/ByteData.h"
#include "data/DataStream.h"
#include "ogg_audio.h"
#include "sound/lullaby/ModPlugDecoder.h"
#include "sound/lullaby/VorbisDecoder.h"
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
using love::Acquire;
using love::StrongRef;
void require(bool condition)
{
    if (!condition)
        std::abort();
}
std::vector<uint8_t> file(const char *path)
{
    std::ifstream input(path, std::ios::binary);
    require(bool(input));
    return {std::istreambuf_iterator<char>(input), {}};
}
void cycle(love::sound::Decoder *decoder)
{
    size_t size = 0;
    int n = 0;
    while ((n = decoder->decode()) > 0)
    {
        size += n;
        require(size <= 32 * 1024 * 1024);
    }
    require(size > 0 && decoder->isFinished());
    require(decoder->seek(0));
    require(!decoder->isFinished());
    require(decoder->decode() > 0);
    require(decoder->rewind());
    require(!decoder->isFinished());
    require(decoder->decode() > 0);
}
int main(int argc, char **argv)
{
    require(argc == 3);
    auto mod = file(argv[1]);
    StrongRef<love::data::ByteData> data(new love::data::ByteData(mod.data(), mod.size()), Acquire::NORETAIN);
    StrongRef<love::data::DataStream> stream(new love::data::DataStream(data.get()), Acquire::NORETAIN);
    StrongRef<love::sound::lullaby::ModPlugDecoder> tracker(
        new love::sound::lullaby::ModPlugDecoder(stream.get(), 16384), Acquire::NORETAIN);
    cycle(tracker.get());
    auto ogg = file(argv[2]);
    auto vorbis = lovescua::oggVorbisStream(ogg.data(), ogg.size());
    require(!vorbis.empty());
    StrongRef<love::data::ByteData> sound(new love::data::ByteData(vorbis.data(), vorbis.size()),
                                          Acquire::NORETAIN);
    StrongRef<love::data::DataStream> input(new love::data::DataStream(sound.get()), Acquire::NORETAIN);
    StrongRef<love::sound::lullaby::VorbisDecoder> decoder(
        new love::sound::lullaby::VorbisDecoder(input.get(), 16384), Acquire::NORETAIN);
    cycle(decoder.get());
    std::cout << "PASS: finite tracker and embedded Vorbis decoding through EOF, seek after EOF and rewind "
                 "(no window/device)\n";
}
