// Select the first Vorbis logical stream without altering Ogg page contents/CRC.
#pragma once
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <vector>
namespace lovescua
{
inline std::vector<uint8_t> oggVorbisStream(const uint8_t *data, size_t size)
{
    if (size > 32 * 1024 * 1024)
        throw std::invalid_argument("Ogg resource exceeds 32 MiB");
    auto read32 = [](const uint8_t *p)
    { return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24); };
    auto scan = [&](auto page)
    {
        size_t offset = 0;
        while (offset < size)
        {
            if (size - offset < 27 || std::memcmp(data + offset, "OggS", 4) || data[offset + 4] != 0)
                throw std::invalid_argument("invalid Ogg page header");
            auto header = data + offset;
            size_t count = header[26], head = 27 + count;
            if (size - offset < head)
                throw std::invalid_argument("truncated Ogg segment table");
            size_t body = 0;
            for (size_t i = 0; i < count; ++i)
                body += header[27 + i];
            if (body > size - offset - head)
                throw std::invalid_argument("truncated Ogg page body");
            page(offset, head, body, read32(header + 14), header[5]);
            offset += head + body;
        }
    };
    uint32_t selected = 0;
    bool found = false;
    scan(
        [&](size_t offset, size_t head, size_t body, uint32_t serial, uint8_t flags)
        {
            if (!found && (flags & 2) && body >= 7 && std::memcmp(data + offset + head, "\x01vorbis", 7) == 0)
            {
                selected = serial;
                found = true;
            }
        });
    std::vector<uint8_t> result;
    if (found)
        scan(
            [&](size_t offset, size_t head, size_t body, uint32_t serial, uint8_t)
            {
                if (serial == selected)
                    result.insert(result.end(), data + offset, data + offset + head + body);
            });
    return result;
}
} // namespace lovescua
