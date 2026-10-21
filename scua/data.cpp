// LÖVE binary codecs and hashes; decompression has an explicit output ceiling.
#include "binding_util.h"
#include "data/DataModule.h"
#include <zlib.h>
namespace lovescua
{
namespace
{
using namespace binding;
struct Bytes
{
    const char *data;
    size_t size;
};
Bytes bytes(scua_ctx *ctx, int index)
{
    const uint8_t *p = nullptr;
    size_t n = 0;
    if (scua_arg_bytes(ctx, index, &p, &n) != SCUA_OK || n > 32 * 1024 * 1024)
        throw std::invalid_argument("expected bytes, maximum 32 MiB");
    return {reinterpret_cast<const char *>(p), n};
}
void compress(scua_ctx *ctx)
{
    if (scua_arg_count(ctx) < 2 || scua_arg_count(ctx) > 3)
        throw std::invalid_argument("compress expects format, bytes and optional level");
    auto name = string(ctx, 0);
    love::data::Compressor::Format format;
    if (!love::data::Compressor::getConstant(name.c_str(), format))
        throw std::invalid_argument("unknown compression format");
    int level = supplied(ctx, 2) ? integer(ctx, 2, -1, 9) : -1;
    auto input = bytes(ctx, 1);
    auto result = love::data::compress(format, input.data, input.size, level);
    struct Release
    {
        void operator()(love::data::CompressedData *p)
        {
            p->release();
        }
    };
    std::unique_ptr<love::data::CompressedData, Release> owned(result);
    scua_return_bytes(ctx, static_cast<const uint8_t *>(result->getData()), result->getSize());
}
void decompress(scua_ctx *ctx)
{
    arity(ctx, 2);
    auto name = string(ctx, 0);
    auto input = bytes(ctx, 1);
    constexpr size_t limit = 32 * 1024 * 1024;
    if (name == "lz4")
    {
        if (input.size < 4)
            throw std::invalid_argument("invalid lz4 header");
        auto p = reinterpret_cast<const uint8_t *>(input.data);
        uint32_t size =
            uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
        if (size > limit)
            throw std::invalid_argument("decompression exceeds 32 MiB");
        size_t actual = 0;
        std::unique_ptr<char[]> output(
            love::data::decompress(love::data::Compressor::FORMAT_LZ4, input.data, input.size, actual));
        scua_return_bytes(ctx, reinterpret_cast<const uint8_t *>(output.get()), actual);
        return;
    }
    int bits = name == "gzip" ? 31 : name == "zlib" ? 15 : name == "deflate" ? -15 : 0;
    if (!bits)
        throw std::invalid_argument("unknown compression format");
    z_stream stream{};
    if (inflateInit2(&stream, bits) != Z_OK)
        throw std::runtime_error("could not initialize decompressor");
    struct End
    {
        z_stream *s;
        ~End()
        {
            inflateEnd(s);
        }
    } end{&stream};
    stream.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(input.data));
    stream.avail_in = input.size;
    std::vector<uint8_t> output;
    int status = Z_OK;
    while (status == Z_OK)
    {
        size_t offset = output.size();
        if (offset > limit)
            throw std::invalid_argument("decompression exceeds 32 MiB");
        output.resize(std::min(limit + 1, offset + 65536));
        stream.next_out = output.data() + offset;
        stream.avail_out = output.size() - offset;
        status = inflate(&stream, Z_NO_FLUSH);
        output.resize(output.size() - stream.avail_out);
        if (output.size() > limit)
            throw std::invalid_argument("decompression exceeds 32 MiB");
    }
    if (status != Z_STREAM_END)
        throw std::invalid_argument("invalid compressed data");
    scua_return_bytes(ctx, output.data(), output.size());
}
void encode(scua_ctx *ctx)
{
    arity(ctx, 2);
    auto name = string(ctx, 0);
    love::data::EncodeFormat format;
    if (!love::data::getConstant(name.c_str(), format))
        throw std::invalid_argument("unknown encoding");
    auto input = bytes(ctx, 1);
    size_t size = 0;
    std::unique_ptr<char[]> result(love::data::encode(format, input.data, input.size, size));
    scua_return_str(ctx, result.get(), size);
}
void decode(scua_ctx *ctx)
{
    arity(ctx, 2);
    auto name = string(ctx, 0), input = string(ctx, 1);
    if (input.size() > 32 * 1024 * 1024)
        throw std::invalid_argument("encoded input too large");
    love::data::EncodeFormat format;
    if (!love::data::getConstant(name.c_str(), format))
        throw std::invalid_argument("unknown encoding");
    size_t size = 0;
    std::unique_ptr<char[]> result(love::data::decode(format, input.data(), input.size(), size));
    scua_return_bytes(ctx, reinterpret_cast<const uint8_t *>(result.get()), size);
}
void hash(scua_ctx *ctx)
{
    arity(ctx, 2);
    auto name = string(ctx, 0);
    love::data::HashFunction::Function format;
    if (!love::data::HashFunction::getConstant(name.c_str(), format))
        throw std::invalid_argument("unknown hash function");
    auto input = bytes(ctx, 1);
    auto output = love::data::hash(format, input.data, input.size);
    scua_return_bytes(ctx, reinterpret_cast<const uint8_t *>(output.data()), output.size());
}
} // namespace
void registerData(scua_partition *p, Host &state)
{
    registerFunctions(
        p, state,
        {
            {"love_data", "compress", guarded<compress>,
             "compress(format: string, input: bytes, level: int = -1) -> bytes",
             "Compress using lz4, zlib, gzip or deflate."},
            {"love_data", "decompress", guarded<decompress>,
             "decompress(format: string, input: bytes) -> bytes", "Decompress with a 32 MiB output limit."},
            {"love_data", "encode", guarded<encode>, "encode(format: string, input: bytes) -> string",
             "Encode hex or base64."},
            {"love_data", "decode", guarded<decode>, "decode(format: string, input: string) -> bytes",
             "Decode hex or base64."},
            {"love_data", "hash", guarded<hash>, "hash(algorithm: string, input: bytes) -> bytes",
             "Hash with md5, sha1, sha224, sha256, sha384 or sha512."},
        });
}
} // namespace lovescua
