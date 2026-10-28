// Managed GPU buffers and instanced meshes for the SCUA host.
#include "binding_util.h"
#include "data/ByteData.h"
#include "graphics/Buffer.h"
#include "graphics/GraphicsReadback.h"
#include "graphics/Mesh.h"
#include "graphics/Shader.h"
#include <set>
namespace lovescua
{
namespace
{
using namespace binding;
using namespace love::graphics;
constexpr size_t maxBytes = 32 * 1024 * 1024;
constexpr int maxElements = 1048576;
auto buffer(scua_ctx *ctx, int index = 0)
{
    return objectArgument<Buffer>(ctx, index, ObjectKind::Buffer);
}
auto mesh(scua_ctx *ctx)
{
    return objectArgument<Mesh>(ctx, 0, ObjectKind::Mesh);
}
std::string textValue(scua_partition *p, scua_Handle value)
{
    size_t n = 0;
    auto text = scua_to_lstring(p, value, &n);
    if (!text || std::memchr(text, 0, n))
        throw std::invalid_argument("expected a string without NUL");
    return {text, n};
}
double numericValue(scua_partition *p, scua_Handle value)
{
    int type = scua_typeof(p, value);
    double n = scua_to_num(p, value);
    if ((type != SCUA_TYPE_INT && type != SCUA_TYPE_FLOAT) || !std::isfinite(n) ||
        std::abs(n) > std::numeric_limits<float>::max())
        throw std::invalid_argument("buffer components must be finite numbers");
    return n;
}
std::vector<Buffer::DataDeclaration> format(scua_ctx *ctx, int index)
{
    auto p = scua_ctx_partition(ctx);
    auto input = scua_arg(ctx, index);
    if (scua_typeof(p, input) != SCUA_TYPE_ARRAY)
        throw std::invalid_argument("buffer format must be an array of {name, format} records");
    std::vector<Buffer::DataDeclaration> result;
    std::set<std::string> names;
    for (int i = 0; i <= 16; ++i)
    {
        auto value = scua_index(p, input, i);
        if (!scua_handle_valid(p, value))
            break;
        if (i == 16)
            throw std::invalid_argument("maximum 16 buffer members");
        auto name = textValue(p, scua_field(p, value, "name"));
        auto type = textValue(p, scua_field(p, value, "format"));
        DataFormat f;
        if (name.empty() || name.size() > 128 || !names.insert(name).second)
            throw std::invalid_argument("buffer member names must be unique and contain 1..128 bytes");
        if (!getConstant(type.c_str(), f))
            throw std::invalid_argument("unknown buffer data format: " + type);
        auto arrayLength = scua_field(p, value, "array_length");
        if (scua_handle_valid(p, arrayLength) && scua_typeof(p, arrayLength) != SCUA_TYPE_NIL &&
            numericValue(p, arrayLength) != 0)
            throw std::invalid_argument("buffer member arrays are not supported");
        result.emplace_back(name, f);
    }
    if (result.empty())
        throw std::invalid_argument("buffer format must not be empty");
    return result;
}
uint32_t usages(scua_ctx *ctx)
{
    if (!supplied(ctx, 2))
        return BUFFERUSAGEFLAG_VERTEX;
    auto p = scua_ctx_partition(ctx);
    auto input = scua_arg(ctx, 2);
    if (scua_typeof(p, input) != SCUA_TYPE_ARRAY)
        throw std::invalid_argument("buffer usages must be an array of strings");
    uint32_t flags = 0;
    for (int i = 0; i <= 5; ++i)
    {
        auto item = scua_index(p, input, i);
        if (!scua_handle_valid(p, item))
            break;
        if (i == 5)
            throw std::invalid_argument("maximum five buffer usages");
        auto name = textValue(p, item);
        BufferUsage usage;
        if (!getConstant(name.c_str(), usage) || usage == BUFFERUSAGE_UNIFORM)
            throw std::invalid_argument("unknown buffer usage: " + name);
        uint32_t bit = 1u << usage;
        if (flags & bit)
            throw std::invalid_argument("duplicate buffer usage");
        flags |= bit;
    }
    return flags;
}
void new_buffer(scua_ctx *ctx)
{
    if (scua_arg_count(ctx) < 2 || scua_arg_count(ctx) > 4)
        throw std::invalid_argument("new_buffer expects 2 to 4 arguments");
    auto declarations = format(ctx, 0);
    int length = integer(ctx, 1, 1, maxElements);
    uint32_t flags = usages(ctx);
    BufferDataUsage dataUsage = BUFFERDATAUSAGE_DYNAMIC;
    if (supplied(ctx, 3))
    {
        auto name = string(ctx, 3);
        if (!getConstant(name.c_str(), dataUsage))
            throw std::invalid_argument("invalid buffer data usage");
    }
    // Bound GPU allocation before invoking the engine's std430 layout builder.
    size_t offset = 0, structureAlignment = 1;
    for (auto &decl : declarations)
    {
        auto info = getDataFormatInfo(decl.format);
        size_t alignment = 1;
        if (flags & BUFFERUSAGEFLAG_SHADER_STORAGE)
        {
            int c = info.isMatrix ? info.matrixRows : info.components;
            alignment = (c == 3 ? 4 : c) * info.componentSize;
        }
        structureAlignment = std::max(structureAlignment, alignment);
        offset = (offset + alignment - 1) / alignment * alignment + info.size;
    }
    size_t stride = (offset + structureAlignment - 1) / structureAlignment * structureAlignment;
    if (stride == 0 || stride > 2048 || size_t(length) > maxBytes / stride)
        throw std::invalid_argument("buffer allocation exceeds 32 MiB or 2048-byte stride");
    Buffer::Settings settings(flags, dataUsage);
    settings.zeroInitialize = true;
    returnObject(ctx, ObjectKind::Buffer,
                 graphics(ctx, false).newBuffer(settings, declarations, nullptr, 0, length));
}
template <class T> void store(uint8_t *dest, double value)
{
    T v = static_cast<T>(value);
    std::memcpy(dest, &v, sizeof(v));
}
void packValue(scua_partition *p, scua_Handle value, const Buffer::DataMember &member, uint8_t *dest)
{
    const auto &info = member.info;
    std::vector<double> components;
    float vec[4];
    int n = scua_to_vec(p, value, vec, 4);
    auto append = [&](scua_Handle v)
    {
        if (info.baseType == DATA_BASETYPE_BOOL)
        {
            if (scua_typeof(p, v) != SCUA_TYPE_BOOL)
                throw std::invalid_argument("buffer bool members require booleans");
            components.push_back(scua_to_bool(p, v));
        }
        else
            components.push_back(numericValue(p, v));
    };
    if (n)
    {
        if (info.baseType == DATA_BASETYPE_BOOL)
            throw std::invalid_argument("buffer bool members require booleans");
        for (int i = 0; i < n; ++i)
        {
            if (!std::isfinite(vec[i]))
                throw std::invalid_argument("buffer vector must be finite");
            components.push_back(vec[i]);
        }
    }
    else if (scua_typeof(p, value) == SCUA_TYPE_ARRAY)
    {
        for (int i = 0; i <= info.components; ++i)
        {
            auto item = scua_index(p, value, i);
            if (!scua_handle_valid(p, item))
                break;
            if (i == info.components)
                throw std::invalid_argument("buffer member component count mismatch");
            append(item);
        }
    }
    else
        append(value);
    if (int(components.size()) != info.components)
        throw std::invalid_argument("buffer member component count mismatch: " + member.decl.name);
    for (int i = 0; i < info.components; ++i)
    {
        double v =
            components[info.isMatrix ? (i % info.matrixRows) * info.matrixColumns + i / info.matrixRows : i];
        auto out = dest + i * info.componentSize;
        int bits = info.componentSize * 8;
        switch (info.baseType)
        {
        case DATA_BASETYPE_FLOAT:
            store<float>(out, v);
            break;
        case DATA_BASETYPE_BOOL:
            store<int32_t>(out, v);
            break;
        case DATA_BASETYPE_SNORM:
            if (v < -1 || v > 1)
                throw std::invalid_argument("signed normalized buffer component must be in [-1,1]");
            v *= double((uint64_t(1) << (bits - 1)) - 1);
            if (bits == 8)
                store<int8_t>(out, v);
            else
                store<int16_t>(out, v);
            break;
        case DATA_BASETYPE_UNORM:
            if (v < 0 || v > 1)
                throw std::invalid_argument("normalized buffer component must be in [0,1]");
            v *= double((uint64_t(1) << bits) - 1);
            if (bits == 8)
                store<uint8_t>(out, v);
            else
                store<uint16_t>(out, v);
            break;
        case DATA_BASETYPE_INT:
            if (std::floor(v) != v || v < -double(uint64_t(1) << (bits - 1)) ||
                v > double((uint64_t(1) << (bits - 1)) - 1))
                throw std::invalid_argument("signed buffer component outside integer range");
            if (bits == 8)
                store<int8_t>(out, v);
            else if (bits == 16)
                store<int16_t>(out, v);
            else
                store<int32_t>(out, v);
            break;
        case DATA_BASETYPE_UINT:
            if (std::floor(v) != v || v < 0 || v > double((uint64_t(1) << bits) - 1))
                throw std::invalid_argument("unsigned buffer component outside integer range");
            if (bits == 8)
                store<uint8_t>(out, v);
            else if (bits == 16)
                store<uint16_t>(out, v);
            else
                store<uint32_t>(out, v);
            break;
        default:
            throw std::invalid_argument("unsupported buffer component type");
        }
    }
}
void buffer_set_data(scua_ctx *ctx)
{
    if (scua_arg_count(ctx) < 2 || scua_arg_count(ctx) > 3)
        throw std::invalid_argument("buffer_set_data expects 2 or 3 arguments");
    auto b = buffer(ctx);
    if (!b)
        return;
    size_t start = supplied(ctx, 2) ? integer(ctx, 2, 0, b->getArrayLength() - 1) : 0;
    auto p = scua_ctx_partition(ctx);
    auto input = scua_arg(ctx, 1);
    if (scua_typeof(p, input) != SCUA_TYPE_ARRAY)
        throw std::invalid_argument("buffer data must be an array of records");
    size_t stride = b->getArrayStride(), capacity = b->getArrayLength() - start;
    std::vector<uint8_t> data;
    for (size_t i = 0; i <= capacity; ++i)
    {
        auto record = scua_index(p, input, i);
        if (!scua_handle_valid(p, record))
            break;
        if (i == capacity)
            throw std::invalid_argument("buffer data exceeds available elements");
        data.resize((i + 1) * stride, 0);
        for (auto &member : b->getDataMembers())
        {
            auto value = scua_field(p, record, member.decl.name.c_str());
            if (scua_typeof(p, record) != SCUA_TYPE_TABLE && b->getDataMembers().size() == 1)
                value = record;
            packValue(p, value, member, data.data() + i * stride + member.offset);
        }
    }
    if (data.empty())
        throw std::invalid_argument("buffer data must not be empty");
    if (!b->fill(start * stride, data.size(), data.data()))
        throw std::runtime_error("could not upload buffer data");
}
void buffer_set_bytes(scua_ctx *ctx)
{
    arity(ctx, 3);
    auto b = buffer(ctx);
    if (!b)
        return;
    int offset = integer(ctx, 2, 0, b->getSize() - 1);
    const uint8_t *data = nullptr;
    size_t size = 0;
    if (scua_arg_bytes(ctx, 1, &data, &size) != SCUA_OK || size == 0 || size > b->getSize() - offset)
        throw std::invalid_argument("buffer bytes must be nonempty and fit the destination");
    if (!b->fill(offset, size, data))
        throw std::runtime_error("could not upload buffer bytes");
}
void buffer_info(scua_ctx *ctx)
{
    arity(ctx, 1);
    auto b = buffer(ctx);
    if (!b)
        return;
    Record r(ctx);
    r.integer("bytes", b->getSize());
    r.integer("length", b->getArrayLength());
    r.integer("stride", b->getArrayStride());
    r.str("debug_name", b->getDebugName());
    auto uses = scua_new_array(r.p, 0);
    for (int i = 0; i < BUFFERUSAGE_MAX_ENUM; ++i)
        if (b->getUsageFlags() & (1u << i))
        {
            const char *name = "unknown";
            getConstant(BufferUsage(i), name);
            if (scua_array_push(r.p, uses, scua_string(r.p, name, std::strlen(name))) != SCUA_OK)
                throw std::runtime_error("buffer usage allocation failed");
        }
    r.set("usages", uses);
    const char *usage = "unknown";
    getConstant(b->getDataUsage(), usage);
    r.str("data_usage", usage);
    auto fields = scua_new_array(r.p, 0);
    for (auto &m : b->getDataMembers())
    {
        Record f(ctx);
        f.str("name", m.decl.name);
        const char *type = "unknown";
        getConstant(m.decl.format, type);
        f.str("format", type);
        f.integer("offset", m.offset);
        f.integer("bytes", m.size);
        if (scua_array_push(r.p, fields, f.value) != SCUA_OK)
            throw std::runtime_error("could not build buffer format");
    }
    r.set("format", fields);
    r.done();
}
void buffer_clear(scua_ctx *ctx)
{
    arity(ctx, 1);
    auto b = buffer(ctx);
    if (b)
        b->clear(0, b->getSize());
}
void buffer_read(scua_ctx *ctx)
{
    arity(ctx, 1);
    auto b = buffer(ctx);
    if (!b)
        return;
    auto data = graphics(ctx, false).readbackBuffer(b, 0, b->getSize(), nullptr, 0);
    struct Release
    {
        love::data::ByteData *p;
        ~Release()
        {
            p->release();
        }
    } owned{data};
    scua_return_bytes(ctx, static_cast<uint8_t *>(data->getData()), data->getSize());
}
void buffer_read_async(scua_ctx *ctx)
{
    arity(ctx, 1);
    auto b = buffer(ctx);
    if (b)
        returnObject(ctx, ObjectKind::Readback,
                     graphics(ctx, false).readbackBufferAsync(b, 0, b->getSize(), nullptr, 0));
}
void readback_bytes(scua_ctx *ctx)
{
    arity(ctx, 1);
    auto r = objectArgument<GraphicsReadback>(ctx, 0, ObjectKind::Readback);
    if (!r)
        return;
    r->update();
    if (r->hasError())
        throw std::runtime_error("GPU readback failed");
    auto data = r->getBufferData();
    if (data)
        scua_return_bytes(ctx, static_cast<uint8_t *>(data->getData()), data->getSize());
    else
        scua_return_value(ctx, scua_nil(scua_ctx_partition(ctx)));
}
void copy_buffer(scua_ctx *ctx)
{
    arity(ctx, 5);
    auto a = buffer(ctx), b = buffer(ctx, 1);
    if (!a || !b)
        return;
    int source = integer(ctx, 2, 0, a->getSize() - 1), dest = integer(ctx, 3, 0, b->getSize() - 1);
    int size = integer(ctx, 4, 1, std::min(a->getSize() - source, b->getSize() - dest));
    graphics(ctx, false).copyBuffer(a, b, source, dest, size);
}
void mesh_from_buffer(scua_ctx *ctx)
{
    arity(ctx, 2);
    auto b = buffer(ctx);
    auto name = string(ctx, 1);
    PrimitiveType mode;
    if (!getConstant(name.c_str(), mode))
        throw std::invalid_argument("invalid mesh draw mode");
    if (!b)
        return;
    std::vector<Mesh::BufferAttribute> attributes;
    for (auto &m : b->getDataMembers())
    {
        Mesh::BufferAttribute a;
        a.name = a.nameInBuffer = m.decl.name;
        a.buffer.set(b);
        a.enabled = true;
        attributes.push_back(a);
    }
    returnObject(ctx, ObjectKind::Mesh, graphics(ctx, false).newMesh(attributes, mode));
}
void mesh_attach_attribute(scua_ctx *ctx)
{
    if (scua_arg_count(ctx) < 5 || scua_arg_count(ctx) > 6)
        throw std::invalid_argument("mesh_attach_attribute expects 5 or 6 arguments");
    auto m = mesh(ctx);
    auto name = string(ctx, 1);
    auto b = buffer(ctx, 2);
    auto member = string(ctx, 3);
    auto step = string(ctx, 4);
    AttributeStep mode;
    if (!getConstant(step.c_str(), mode))
        throw std::invalid_argument("attribute step must be pervertex or perinstance");
    int start = 0;
    if (b && supplied(ctx, 5))
        start = integer(ctx, 5, 0, b->getArrayLength() - 1);
    if (m && b)
    {
        if (mode == STEP_PER_VERTEX && b->getArrayLength() - start < m->getVertexCount())
            throw std::invalid_argument("vertex attribute buffer is shorter than the mesh vertex count");
        m->attachAttribute(name, b, nullptr, member, start, mode);
    }
}
void mesh_detach_attribute(scua_ctx *ctx)
{
    arity(ctx, 2);
    auto name = string(ctx, 1);
    auto m = mesh(ctx);
    if (m)
        scua_return_bool(ctx, m->detachAttribute(name));
}
void mesh_attribute_enabled(scua_ctx *ctx)
{
    arity(ctx, 3);
    auto name = string(ctx, 1);
    bool enabled = boolean(ctx, 2);
    auto m = mesh(ctx);
    if (m)
        m->setAttributeEnabled(name, enabled);
}
void draw_instanced(scua_ctx *ctx)
{
    auto matrix = transform(ctx, 2);
    int count = integer(ctx, 1, 1, maxElements);
    auto m = mesh(ctx);
    if (!m)
        return;
    for (auto &a : m->getAttachedAttributes())
        if (a.enabled && a.step == STEP_PER_INSTANCE &&
            size_t(count) > a.buffer->getArrayLength() - a.startArrayIndex)
            throw std::invalid_argument("instance count exceeds attached buffer capacity");
    m->drawInstanced(&graphics(ctx), matrix, count);
}
void shader_send_buffer(scua_ctx *ctx)
{
    arity(ctx, 3);
    auto name = string(ctx, 1);
    auto s = objectArgument<Shader>(ctx, 0, ObjectKind::Shader);
    auto b = buffer(ctx, 2);
    if (!s || !b)
        return;
    auto info = s->getUniformInfo(name);
    if (!info ||
        (info->baseType != Shader::UNIFORM_TEXELBUFFER && info->baseType != Shader::UNIFORM_STORAGEBUFFER))
        throw std::invalid_argument("expected an active buffer uniform");
    s->sendBuffers(info, &b, 1);
}

void returnBuffer(scua_ctx *ctx, Buffer *b)
{
    if (b)
    {
        b->retain();
        returnObject(ctx, ObjectKind::Buffer, b);
    }
    else
        scua_return_value(ctx, scua_nil(scua_ctx_partition(ctx)));
}
void mesh_set_index_buffer(scua_ctx *ctx)
{
    arity(ctx, 2);
    auto m = mesh(ctx);
    if (!m)
        return;
    if (!supplied(ctx, 1))
    {
        m->setIndexBuffer(nullptr);
        return;
    }
    auto b = buffer(ctx, 1);
    if (b)
        m->setIndexBuffer(b);
}
void mesh_index_buffer(scua_ctx *ctx)
{
    arity(ctx, 1);
    auto m = mesh(ctx);
    if (m)
    {
        m->flush();
        returnBuffer(ctx, m->getIndexBuffer());
    }
}
void mesh_vertex_buffer(scua_ctx *ctx)
{
    arity(ctx, 1);
    auto m = mesh(ctx);
    if (m)
    {
        m->flush();
        returnBuffer(ctx, m->getVertexBuffer());
    }
}
void mesh_index_count(scua_ctx *ctx)
{
    arity(ctx, 1);
    auto m = mesh(ctx);
    if (m)
        scua_return_int(ctx, m->getIndexCount());
}
void draw_indirect(scua_ctx *ctx)
{
    auto matrix = transform(ctx, 3);
    auto m = mesh(ctx);
    auto args = buffer(ctx, 1);
    if (!m || !args)
        return;
    int index = integer(ctx, 2, 0, args->getArrayLength() - 1);
    m->drawIndirect(&graphics(ctx), matrix, args, index);
}
void new_compute_shader(scua_ctx *ctx)
{
    arity(ctx, 1);
    auto code = string(ctx, 0);
    if (code.size() > 1024 * 1024)
        throw std::invalid_argument("compute shader source exceeds 1 MiB");
    if (!graphics(ctx, false).getCapabilities().features[Graphics::FEATURE_GLSL4])
        throw std::invalid_argument("compute shaders are not supported on this renderer");
    returnObject(ctx, ObjectKind::Shader, graphics(ctx, false).newComputeShader(code, {}));
}
auto shader(scua_ctx *ctx)
{
    return objectArgument<Shader>(ctx, 0, ObjectKind::Shader);
}
void shader_has_stage(scua_ctx *ctx)
{
    arity(ctx, 2);
    auto name = string(ctx, 1);
    ShaderStageType stage;
    if (!ShaderStage::getConstant(name.c_str(), stage))
        throw std::invalid_argument("shader stage must be vertex, pixel or compute");
    auto s = shader(ctx);
    if (s)
        scua_return_bool(ctx, s->hasStage(stage));
}
void shader_threadgroup_size(scua_ctx *ctx)
{
    arity(ctx, 1);
    auto s = shader(ctx);
    if (!s)
        return;
    if (!s->hasStage(SHADERSTAGE_COMPUTE))
        throw std::invalid_argument("expected a compute shader");
    int x, y, z;
    s->getLocalThreadgroupSize(&x, &y, &z);
    float v[] = {float(x), float(y), float(z)};
    scua_return_vec(ctx, v, 3);
}
void shader_buffer_format(scua_ctx *ctx)
{
    arity(ctx, 2);
    auto name = string(ctx, 1);
    auto s = shader(ctx);
    if (!s)
        return;
    auto format = s->getBufferFormat(name);
    auto p = scua_ctx_partition(ctx);
    if (!format)
    {
        scua_return_value(ctx, scua_nil(p));
        return;
    }
    auto result = scua_new_array(p, 0);
    for (auto &field : *format)
    {
        Record r(ctx);
        r.str("name", field.name.empty() ? "value" : field.name);
        const char *name = "unknown";
        getConstant(field.format, name);
        r.str("format", name);
        r.integer("array_length", field.arrayLength);
        if (scua_array_push(p, result, r.value) != SCUA_OK)
            throw std::runtime_error("could not construct reflected buffer format");
    }
    scua_return_value(ctx, result);
}
void dispatch_threadgroups(scua_ctx *ctx)
{
    int count = scua_arg_count(ctx);
    if (count < 2 || count > 4)
        throw std::invalid_argument("dispatch_threadgroups expects 2 to 4 arguments");
    auto s = shader(ctx);
    if (!s)
        return;
    if (!s->hasStage(SHADERSTAGE_COMPUTE))
        throw std::invalid_argument("expected a compute shader");
    int x = integer(ctx, 1, 1, 65535), y = supplied(ctx, 2) ? integer(ctx, 2, 1, 65535) : 1,
        z = supplied(ctx, 3) ? integer(ctx, 3, 1, 65535) : 1;
    int lx, ly, lz;
    s->getLocalThreadgroupSize(&lx, &ly, &lz);
    uint64_t work = uint64_t(x) * y * z;
    for (int n : {lx, ly, lz})
    {
        if (n <= 0 || work > 16777216u / uint64_t(n))
            throw std::invalid_argument("direct compute dispatch exceeds 16 million shader invocations");
        work *= n;
    }
    graphics(ctx, false).dispatchThreadgroups(s, x, y, z);
}
void dispatch_indirect(scua_ctx *ctx)
{
    if (scua_arg_count(ctx) < 2 || scua_arg_count(ctx) > 3)
        throw std::invalid_argument("dispatch_indirect expects 2 or 3 arguments");
    auto s = shader(ctx);
    auto args = buffer(ctx, 1);
    if (!s || !args)
        return;
    int index = supplied(ctx, 2) ? integer(ctx, 2, 0, args->getArrayLength() - 1) : 0;
    graphics(ctx, false).dispatchIndirect(s, args, index);
}
template <class T, class Read>
std::vector<love::StrongRef<T>> resourceArray(scua_ctx *ctx, int maximum, Read read)
{
    auto p = scua_ctx_partition(ctx);
    auto input = scua_arg(ctx, 2);
    if (scua_typeof(p, input) != SCUA_TYPE_ARRAY)
        throw std::invalid_argument("shader resources must be an array");
    std::vector<love::StrongRef<T>> result;
    for (int i = 0; i <= std::min(maximum, 64); ++i)
    {
        auto value = scua_index(p, input, i);
        if (!scua_handle_valid(p, value))
            break;
        if (i == std::min(maximum, 64))
            throw std::invalid_argument("shader resource array exceeds declared size or 64 resources");
        result.push_back(read(p, value));
    }
    if (result.empty())
        throw std::invalid_argument("shader resource array must not be empty");
    return result;
}
void shader_send_textures(scua_ctx *ctx)
{
    arity(ctx, 3);
    auto name = string(ctx, 1);
    auto s = shader(ctx);
    if (!s)
        return;
    auto info = s->getUniformInfo(name);
    if (!info ||
        (info->baseType != Shader::UNIFORM_SAMPLER && info->baseType != Shader::UNIFORM_STORAGETEXTURE))
        throw std::invalid_argument("expected an active texture uniform");
    auto owned = resourceArray<Texture>(
        ctx, info->count,
        [&](scua_partition *p, scua_Handle value)
        {
            void *storage = nullptr;
            scua_UserdataLease lease{};
            if (scua_userdata_borrow(p, value, host(ctx).imageType, &storage, &lease) != SCUA_OK)
                throw std::invalid_argument("shader textures must be live managed textures");
            love::StrongRef<Texture> texture(static_cast<ImageResource *>(storage)->texture);
            if (scua_userdata_release(p, lease) != SCUA_OK)
                throw std::runtime_error("could not release texture lease");
            return texture;
        });
    std::vector<Texture *> textures;
    for (auto &t : owned)
        textures.push_back(t.get());
    s->sendTextures(info, textures.data(), textures.size());
}
void shader_send_buffers(scua_ctx *ctx)
{
    arity(ctx, 3);
    auto name = string(ctx, 1);
    auto s = shader(ctx);
    if (!s)
        return;
    auto info = s->getUniformInfo(name);
    if (!info ||
        (info->baseType != Shader::UNIFORM_TEXELBUFFER && info->baseType != Shader::UNIFORM_STORAGEBUFFER))
        throw std::invalid_argument("expected an active buffer uniform");
    auto owned = resourceArray<Buffer>(
        ctx, info->count,
        [&](scua_partition *p, scua_Handle value)
        {
            void *storage = nullptr;
            scua_UserdataLease lease{};
            if (scua_userdata_borrow(p, value, host(ctx).objectTypes[size_t(ObjectKind::Buffer)], &storage,
                                     &lease) != SCUA_OK)
                throw std::invalid_argument("shader buffers must be live managed buffers");
            love::StrongRef<Buffer> b(static_cast<Buffer *>(static_cast<ObjectResource *>(storage)->object));
            if (scua_userdata_release(p, lease) != SCUA_OK)
                throw std::runtime_error("could not release buffer lease");
            return b;
        });
    std::vector<Buffer *> buffers;
    for (auto &b : owned)
        buffers.push_back(b.get());
    s->sendBuffers(info, buffers.data(), buffers.size());
}

} // namespace
void registerBuffers(scua_partition *p, Host &state)
{
    registerObjectType(p, state, ObjectKind::Buffer);
    registerFunctions(
        p, state,
        {
            {"love_graphics", "new_buffer", guarded<new_buffer>,
             "new_buffer(format: any, length: int, usages: any = nil, data_usage: string = \"dynamic\") -> "
             "any",
             "Create a zeroed GPU buffer, maximum 32 MiB. Format is [{name, format}]; usages defaults to "
             "vertex."},
            {"love_graphics", "buffer_set_data", guarded<buffer_set_data>,
             "buffer_set_data(buffer: any, records: any, start: int = 0)",
             "Upload records matching the format, or scalars/vectors for single-member buffers; validate the "
             "full batch before writing."},
            {"love_graphics", "buffer_set_bytes", guarded<buffer_set_bytes>,
             "buffer_set_bytes(buffer: any, data: bytes, offset: int)",
             "Upload raw bytes with checked byte bounds."},
            {"love_graphics", "buffer_info", guarded<buffer_info>, "buffer_info(buffer: any) -> any",
             "Get element count, stride, byte size and member offsets."},
            {"love_graphics", "buffer_clear", guarded<buffer_clear>, "buffer_clear(buffer: any)",
             "Reset a buffer to zero."},
            {"love_graphics", "buffer_read", guarded<buffer_read>, "buffer_read(buffer: any) -> bytes",
             "Copy GPU buffer contents to SCUA bytes synchronously."},
            {"love_graphics", "buffer_read_async", guarded<buffer_read_async>,
             "buffer_read_async(buffer: any) -> any", "Start a managed asynchronous GPU buffer readback."},
            {"love_graphics", "readback_bytes", guarded<readback_bytes>,
             "readback_bytes(readback: any) -> any",
             "Copy completed buffer readback bytes, or nil while waiting."},
            {"love_graphics", "copy_buffer", guarded<copy_buffer>,
             "copy_buffer(source: any, destination: any, source_offset: int, destination_offset: int, size: "
             "int)",
             "Copy GPU buffer bytes; offsets and size must be multiples of four."},
            {"love_graphics", "mesh_from_buffer", guarded<mesh_from_buffer>,
             "mesh_from_buffer(buffer: any, mode: string) -> any",
             "Retain vertex buffer attributes in a custom-format mesh."},
            {"love_graphics", "mesh_attach_attribute", guarded<mesh_attach_attribute>,
             "mesh_attach_attribute(mesh: any, name: string, buffer: any, member: string, step: string, "
             "start: int = 0)",
             "Retain an attribute from a GPU buffer; step is pervertex or perinstance."},
            {"love_graphics", "mesh_detach_attribute", guarded<mesh_detach_attribute>,
             "mesh_detach_attribute(mesh: any, name: string) -> bool", "Detach a vertex attribute."},
            {"love_graphics", "mesh_attribute_enabled", guarded<mesh_attribute_enabled>,
             "mesh_attribute_enabled(mesh: any, name: string, enabled: bool)",
             "Enable or disable a mesh attribute."},
            {"love_graphics", "draw_instanced", guarded<draw_instanced>,
             "draw_instanced(mesh: any, instances: int, position: vec2, rotation: float = 0, scale: vec2 = "
             "nil, origin: vec2 = nil)",
             "Draw mesh instances using checked per-instance buffer capacity."},
            {"love_graphics", "shader_send_buffer", guarded<shader_send_buffer>,
             "shader_send_buffer(shader: any, name: string, buffer: any)",
             "Retain a texel or shader-storage buffer in a shader uniform."},
            {"love_graphics", "mesh_set_index_buffer", guarded<mesh_set_index_buffer>,
             "mesh_set_index_buffer(mesh: any, buffer: any)",
             "Retain an index buffer, or nil to disable indexed drawing."},
            {"love_graphics", "mesh_index_buffer", guarded<mesh_index_buffer>,
             "mesh_index_buffer(mesh: any) -> any", "Retain the mesh index buffer, or nil."},
            {"love_graphics", "mesh_vertex_buffer", guarded<mesh_vertex_buffer>,
             "mesh_vertex_buffer(mesh: any) -> any",
             "Flush and retain a mesh-owned vertex buffer, or nil for externally assembled meshes."},
            {"love_graphics", "mesh_index_count", guarded<mesh_index_count>,
             "mesh_index_count(mesh: any) -> int", "Get the mesh index count."},
            {"love_graphics", "draw_indirect", guarded<draw_indirect>,
             "draw_indirect(mesh: any, arguments: any, index: int, position: vec2, rotation: float = 0, "
             "scale: vec2 = nil, origin: vec2 = nil)",
             "Draw using a GPU argument buffer; indices are zero-based argument elements."},
            {"love_graphics", "new_compute_shader", guarded<new_compute_shader>,
             "new_compute_shader(source: string) -> any",
             "Compile a managed compute shader; requires glsl4 support."},
            {"love_graphics", "shader_has_stage", guarded<shader_has_stage>,
             "shader_has_stage(shader: any, stage: string) -> bool",
             "Check vertex, pixel or compute stage presence."},
            {"love_graphics", "shader_threadgroup_size", guarded<shader_threadgroup_size>,
             "shader_threadgroup_size(shader: any) -> vec3",
             "Get compiled compute local threadgroup dimensions."},
            {"love_graphics", "shader_buffer_format", guarded<shader_buffer_format>,
             "shader_buffer_format(shader: any, name: string) -> any",
             "Reflect a storage-buffer format usable by new_buffer, or nil if absent."},
            {"love_graphics", "dispatch_threadgroups", guarded<dispatch_threadgroups>,
             "dispatch_threadgroups(shader: any, x: int, y: int = 1, z: int = 1)",
             "Dispatch compute work; direct dispatches are limited to 16777216 shader invocations."},
            {"love_graphics", "dispatch_indirect", guarded<dispatch_indirect>,
             "dispatch_indirect(shader: any, arguments: any, index: int = 0)",
             "Dispatch compute from a GPU argument buffer; contents must respect device limits."},
            {"love_graphics", "shader_send_textures", guarded<shader_send_textures>,
             "shader_send_textures(shader: any, name: string, textures: any)",
             "Retain an array of textures in a sampler/image uniform, maximum 64."},
            {"love_graphics", "shader_send_buffers", guarded<shader_send_buffers>,
             "shader_send_buffers(shader: any, name: string, buffers: any)",
             "Retain an array of texel/storage buffers in a uniform, maximum 64."},
        });
}
} // namespace lovescua
