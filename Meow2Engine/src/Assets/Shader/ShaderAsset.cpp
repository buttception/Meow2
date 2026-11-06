/// @file
/// @brief The `.m2shader` file format and ShaderAsset.
///
/// Layout, little-endian, written and read in this order:
/// `"M2SH"`, version (u32), cache key (u64), `.meta` text (string), dependency count (u32) and strings,
/// blob count (u32), then per blob: variant (u32), entry (u32), target (u32), size (u64), bytes.
/// A string is a u32 length and its bytes.

#include <Meow2/Assets/Shader/ShaderAsset.h>
#include <Meow2/Platform/FileSystem/FileSystem.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <format>
#include <optional>
#include <utility>

namespace Meow2::Assets
{
namespace
{
constexpr std::array<char, 4> c_magic{'M', '2', 'S', 'H'};

/// @brief Appends plain values to a byte buffer.
class Writer
{
public:
    explicit Writer(std::vector<std::byte>& bytes)
        : m_bytes(bytes)
    {
    }

    template <class T>
    void value(const T& data)
    {
        raw(&data, sizeof(data));
    }

    void text(std::string_view data)
    {
        value(static_cast<std::uint32_t>(data.size()));
        raw(data.data(), data.size());
    }

    void raw(const void* data, std::size_t size)
    {
        const auto* first = static_cast<const std::byte*>(data);
        m_bytes.insert(m_bytes.end(), first, first + size);
    }

private:
    std::vector<std::byte>& m_bytes;
};

/// @brief Reads plain values from bytes. Every read is checked: a cooked file may be truncated or corrupt.
class Reader
{
public:
    explicit Reader(std::span<const std::byte> bytes)
        : m_bytes(bytes)
    {
    }

    template <class T>
    [[nodiscard]] bool value(T& data)
    {
        return raw(&data, sizeof(data));
    }

    [[nodiscard]] bool text(std::string& data)
    {
        std::uint32_t size = 0;
        if (!value(size) || size > m_bytes.size() - m_position)
        {
            return false;
        }
        data.resize(size);
        return raw(data.data(), size);
    }

    [[nodiscard]] bool bytes(std::vector<std::byte>& data, std::uint64_t size)
    {
        if (size > m_bytes.size() - m_position)
        {
            return false;
        }
        data.resize(static_cast<std::size_t>(size));
        return raw(data.data(), data.size());
    }

    /// @brief How many more bytes there are: a count read from a file can't promise more items than that.
    [[nodiscard]] std::size_t remaining() const
    {
        return m_bytes.size() - m_position;
    }

private:
    [[nodiscard]] bool raw(void* data, std::size_t size)
    {
        if (size > m_bytes.size() - m_position)
        {
            return false;
        }
        if (size > 0)
        {
            std::memcpy(data, m_bytes.data() + m_position, size);
        }
        m_position += size;
        return true;
    }

    std::span<const std::byte> m_bytes;
    std::size_t m_position = 0;
};

Unexpected<Error> corrupt()
{
    return fail(ErrorCode::ParseError, "corrupt .m2shader file");
}
} // namespace

std::vector<std::byte> serializeShader(const CookedShader& shader)
{
    std::vector<std::byte> bytes;
    Writer writer{bytes};
    writer.value(c_magic);
    writer.value(c_shaderFileVersion);
    writer.value(shader.cacheKey);
    writer.text(shader.metaText);
    writer.value(static_cast<std::uint32_t>(shader.dependencies.size()));
    for (const std::string& dependency : shader.dependencies)
    {
        writer.text(dependency);
    }
    writer.value(static_cast<std::uint32_t>(shader.blobs.size()));
    for (const CookedShaderBlob& blob : shader.blobs)
    {
        writer.value(blob.variant);
        writer.value(blob.entry);
        writer.value(static_cast<std::uint32_t>(blob.target));
        writer.value(static_cast<std::uint64_t>(blob.bytecode.size()));
        writer.raw(blob.bytecode.data(), blob.bytecode.size());
    }
    return bytes;
}

Result<CookedShader> deserializeShader(std::span<const std::byte> bytes)
{
    Reader reader{bytes};
    std::array<char, 4> magic{};
    if (!reader.value(magic) || magic != c_magic)
    {
        return fail(ErrorCode::ParseError, "not a .m2shader file");
    }
    std::uint32_t version = 0;
    if (!reader.value(version))
    {
        return corrupt();
    }
    if (version != c_shaderFileVersion)
    {
        return fail(ErrorCode::Unsupported,
                    std::format(".m2shader version {} (expected {}): cook it again", version, c_shaderFileVersion));
    }

    CookedShader shader;
    std::uint32_t dependencyCount = 0;
    if (!reader.value(shader.cacheKey) || !reader.text(shader.metaText) || !reader.value(dependencyCount) ||
        dependencyCount > reader.remaining())
    {
        return corrupt();
    }
    shader.dependencies.resize(dependencyCount);
    for (std::string& dependency : shader.dependencies)
    {
        if (!reader.text(dependency))
        {
            return corrupt();
        }
    }
    std::uint32_t blobCount = 0;
    if (!reader.value(blobCount) || blobCount > reader.remaining())
    {
        return corrupt();
    }
    shader.blobs.resize(blobCount);
    for (CookedShaderBlob& blob : shader.blobs)
    {
        std::uint32_t target = 0;
        std::uint64_t size = 0;
        if (!reader.value(blob.variant) || !reader.value(blob.entry) || !reader.value(target) || !reader.value(size) ||
            target > static_cast<std::uint32_t>(Shader::ShaderTarget::Spirv) || !reader.bytes(blob.bytecode, size))
        {
            return corrupt();
        }
        blob.target = static_cast<Shader::ShaderTarget>(target);
    }
    return shader;
}

std::string cookedShaderPath(AssetId id)
{
    return std::format("cooked/shaders/{}.m2shader", id.uuid());
}

ShaderAsset::ShaderAsset(CookedShader cooked, ShaderMeta meta)
    : m_cooked(std::move(cooked))
    , m_meta(std::move(meta))
{
}

Result<ShaderAsset> ShaderAsset::fromCooked(CookedShader cooked)
{
    Result<ShaderMeta> meta = parseShaderMeta(cooked.metaText, "cooked shader");
    if (!meta)
    {
        return Unexpected<Error>{meta.error()};
    }
    return ShaderAsset{std::move(cooked), std::move(*meta)};
}

Result<ShaderAsset> ShaderAsset::loadAsset(AssetManager& assets, AssetId id)
{
    const std::string path = cookedShaderPath(id);
    Result<std::vector<std::byte>> bytes = assets.files().readFile(path);
    if (!bytes)
    {
        return Unexpected<Error>{bytes.error()};
    }
    Result<CookedShader> cooked = deserializeShader(*bytes);
    if (!cooked)
    {
        return fail(cooked.error().code, std::format("{}: {}", path, cooked.error().message));
    }
    return fromCooked(std::move(*cooked));
}

Result<std::span<const std::byte>> ShaderAsset::bytecode(ShaderVariantKey key, std::string_view entry,
                                                         Shader::ShaderTarget target) const
{
    const auto named = [entry](const ShaderEntry& e)
    {
        return e.name == entry;
    };
    const auto found = std::find_if(m_meta.entries.begin(), m_meta.entries.end(), named);
    if (found == m_meta.entries.end())
    {
        return fail(ErrorCode::NotFound, std::format("no entry point {}", entry));
    }
    const auto entryIndex = static_cast<std::uint32_t>(found - m_meta.entries.begin());
    for (const CookedShaderBlob& blob : m_cooked.blobs)
    {
        if (blob.variant == key.index && blob.entry == entryIndex && blob.target == target)
        {
            return std::span<const std::byte>{blob.bytecode};
        }
    }
    return fail(ErrorCode::NotFound, std::format("variant {} of {} wasn't cooked for this target", key.index, entry));
}
} // namespace Meow2::Assets
