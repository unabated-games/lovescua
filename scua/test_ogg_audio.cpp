// CPU-only checks for Ogg logical stream selection and truncated-page bounds.
#include "ogg_audio.h"
#include <cstdlib>
#include <iostream>
#include <string>
using namespace lovescua;
void require(bool condition)
{
    if (!condition)
        std::abort();
}
std::vector<uint8_t> page(uint32_t serial, uint8_t flags, std::string data)
{
    std::vector<uint8_t> result(28, 0);
    std::memcpy(result.data(), "OggS", 4);
    result[5] = flags;
    result[26] = 1;
    result[27] = data.size();
    for (int i = 0; i < 4; i++)
        result[14 + i] = (serial >> (i * 8)) & 255;
    result.insert(result.end(), data.begin(), data.end());
    return result;
}
void concat(std::vector<uint8_t> &a, const std::vector<uint8_t> &b)
{
    a.insert(a.end(), b.begin(), b.end());
}
int main()
{
    auto video = page(23, 2, "theora");
    auto audio = page(0, 2, std::string("\x01vorbis", 7));
    auto end = page(0, 4, "audio");
    auto second = page(55, 2, std::string("\x01vorbis", 7));
    auto input = video;
    concat(input, audio);
    concat(input, second);
    concat(input, end);
    auto expected = audio;
    concat(expected, end);
    require(oggVorbisStream(input.data(), input.size()) == expected);
    require(oggVorbisStream(video.data(), video.size()).empty());
    for (size_t size : {size_t(1), size_t(26), size_t(27), input.size() - 1})
    {
        bool failed = false;
        try
        {
            oggVorbisStream(input.data(), size);
        }
        catch (const std::invalid_argument &)
        {
            failed = true;
        }
        require(failed);
    }
    input[4] = 1;
    bool failed = false;
    try
    {
        oggVorbisStream(input.data(), input.size());
    }
    catch (const std::invalid_argument &)
    {
        failed = true;
    }
    require(failed);
    std::cout << "PASS: multiplexed Vorbis selection, serial zero, ignored tracks and truncated Ogg pages\n";
}
