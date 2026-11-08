/// @file
/// @brief Texture cooking and the `.m2texture` file (CookedTexture.h).

#include "Meow2/Assets/Texture/CookedTexture.h"

#include "Assets/Texture/BlockCompression.h"
#include "Meow2/Core/Memory/MemTag.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <format>
#include <optional>

namespace Meow2::Assets
{
namespace
{
/// @brief Bumped whenever the cooker's output changes for the same input, so old cache entries stop matching.
constexpr std::uint32_t c_cookerVersion = 1;
/// @brief The first four bytes of every `.m2texture` file.
constexpr std::array<char, 4> c_magic{'M', '2', 'T', 'X'};
/// @brief Version of the `.m2texture` layout.
constexpr std::uint32_t c_fileVersion = 1;
/// @brief Rows of pixels compressed by one job: a multiple of 4, small enough to spread a 1024-pixel image over cores.
constexpr std::uint32_t c_stripRows = 64;

/// @brief The bytes a level of this size takes in this format, rows tightly packed.
std::size_t levelBytes(CookedFormat format, std::uint32_t width, std::uint32_t height)
{
    if (isBlockCompressed(format))
    {
        return std::size_t{(width + 3) / 4} * ((height + 3) / 4) * 16;
    }
    return std::size_t{width} * height * 4;
}

/// @brief The cooked format for a kind.
CookedFormat formatFor(TextureKind kind, bool compress)
{
    switch (kind)
    {
        case TextureKind::Color:
            return compress ? CookedFormat::Bc7Srgb : CookedFormat::Rgba8Srgb;
        case TextureKind::Data:
            return compress ? CookedFormat::Bc7Unorm : CookedFormat::Rgba8Unorm;
        case TextureKind::NormalMap:
            return compress ? CookedFormat::Bc5Unorm : CookedFormat::Rgba8Unorm;
    }
    return CookedFormat::Rgba8Unorm;
}

/// @brief A copy of an image grown to multiples of 4 by repeating its last column and row.
///
/// Only the small mips need it (the top level is already a multiple of 4): their blocks cover pixels past the edge,
/// which the GPU never samples, so repeating the edge just keeps those pixels from skewing the block's colours.
Image padToBlocks(const Image& image)
{
    const std::uint32_t width = (image.width + 3) / 4 * 4;
    const std::uint32_t height = (image.height + 3) / 4 * 4;
    if (width == image.width && height == image.height)
    {
        return image;
    }
    Image padded{.width = width, .height = height, .pixels = std::vector<std::byte>(std::size_t{width} * height * 4)};
    for (std::uint32_t y = 0; y < height; ++y)
    {
        const std::uint32_t sourceY = std::min(y, image.height - 1);
        for (std::uint32_t x = 0; x < width; ++x)
        {
            const std::uint32_t sourceX = std::min(x, image.width - 1);
            std::memcpy(&padded.pixels[(std::size_t{y} * width + x) * 4],
                        &image.pixels[(std::size_t{sourceY} * image.width + sourceX) * 4], 4);
        }
    }
    return padded;
}

/// @brief Compresses one level, split into strips of rows that run as jobs when `jobs` is set.
Result<std::vector<std::byte>> compressLevel(const Image& level, CookedFormat format, Jobs::JobSystem* jobs)
{
    const Image padded = padToBlocks(level);
    const std::uint32_t strips = (padded.height + c_stripRows - 1) / c_stripRows;
    const std::size_t rowBytes = std::size_t{padded.width} * 4;
    // Each strip writes only its own slot, so the jobs share nothing; the strips are joined in order afterwards.
    std::vector<std::optional<Result<std::vector<std::byte>>>> results(strips);
    const auto compressStrips = [&](std::uint32_t begin, std::uint32_t end, std::uint32_t /*threadIndex*/)
    {
        for (std::uint32_t strip = begin; strip < end; ++strip)
        {
            const std::uint32_t firstRow = strip * c_stripRows;
            const std::uint32_t rows = std::min(c_stripRows, padded.height - firstRow);
            const std::span<const std::byte> pixels{padded.pixels.data() + firstRow * rowBytes, rows * rowBytes};
            results[strip] = compressBlocks(pixels, padded.width, rows, format);
        }
    };
    if (jobs != nullptr && strips > 1)
    {
        jobs->wait(jobs->parallelFor(strips, 1, compressStrips));
    }
    else
    {
        compressStrips(0, strips, 0);
    }
    std::vector<std::byte> blocks;
    blocks.reserve(levelBytes(format, level.width, level.height));
    for (auto& result : results)
    {
        if (!result->has_value())
        {
            return fail(result->error().code, result->error().message);
        }
        blocks.insert(blocks.end(), (*result)->begin(), (*result)->end());
    }
    return blocks;
}

/// @brief Appends a value's bytes. The file is little-endian, like every platform Meow2 targets (x64, arm64).
template <class T>
void write(std::vector<std::byte>& out, const T& value)
{
    const auto* bytes = reinterpret_cast<const std::byte*>(&value);
    out.insert(out.end(), bytes, bytes + sizeof(T));
}

/// @brief Reads values from the front of a byte span, failing instead of reading past its end.
class Reader
{
public:
    /// @brief Reads from `bytes`, which must outlive the reader.
    explicit Reader(std::span<const std::byte> bytes)
        : m_bytes(bytes)
    {
    }

    /// @brief Reads one value; false when too few bytes are left.
    template <class T>
    bool read(T& value)
    {
        if (m_bytes.size() < sizeof(T))
        {
            return false;
        }
        std::memcpy(&value, m_bytes.data(), sizeof(T));
        m_bytes = m_bytes.subspan(sizeof(T));
        return true;
    }

    /// @brief Takes the next `count` bytes; nullopt when too few are left.
    std::optional<std::span<const std::byte>> take(std::uint64_t count)
    {
        if (m_bytes.size() < count)
        {
            return std::nullopt;
        }
        const auto taken = m_bytes.first(static_cast<std::size_t>(count));
        m_bytes = m_bytes.subspan(static_cast<std::size_t>(count));
        return taken;
    }

    /// @brief Whether every byte has been read.
    [[nodiscard]] bool empty() const
    {
        return m_bytes.empty();
    }

private:
    std::span<const std::byte> m_bytes; ///< What's left to read.
};
} // namespace

Result<CookedTexture> cookTexture(const Image& image, TextureKind kind, bool compress, Jobs::JobSystem* jobs)
{
    if (image.width == 0 || image.height == 0 || image.pixels.size() != std::size_t{image.width} * image.height * 4)
    {
        return fail(ErrorCode::InvalidArgument, "Cannot cook an empty or malformed image");
    }
    const Memory::MemTagScope tag{Memory::MemTag::Assets};
    // The GPU needs block-compressed top levels to be whole blocks; other sizes stay RGBA8 (rare in glTF assets).
    const bool blocksFit = image.width % 4 == 0 && image.height % 4 == 0;
    const CookedFormat format = formatFor(kind, compress && blocksFit);
    std::vector<Image> levels = makeMipChain(image, kind == TextureKind::Color);
    CookedTexture texture{.format = format, .mips = {}};
    texture.mips.reserve(levels.size());
    for (Image& level : levels)
    {
        CookedMip mip{.width = level.width, .height = level.height, .bytes = {}};
        if (isBlockCompressed(format))
        {
            Result<std::vector<std::byte>> blocks = compressLevel(level, format, jobs);
            if (!blocks)
            {
                return fail(blocks.error().code, blocks.error().message);
            }
            mip.bytes = std::move(*blocks);
        }
        else
        {
            mip.bytes = std::move(level.pixels);
        }
        texture.mips.push_back(std::move(mip));
    }
    return texture;
}

std::vector<std::byte> encodeM2Texture(const CookedTexture& texture)
{
    std::vector<std::byte> out;
    std::size_t total = 16;
    for (const CookedMip& mip : texture.mips)
    {
        total += 16 + mip.bytes.size();
    }
    out.reserve(total);
    write(out, c_magic);
    write(out, c_fileVersion);
    write(out, static_cast<std::uint32_t>(texture.format));
    write(out, static_cast<std::uint32_t>(texture.mips.size()));
    for (const CookedMip& mip : texture.mips)
    {
        write(out, mip.width);
        write(out, mip.height);
        write(out, static_cast<std::uint64_t>(mip.bytes.size()));
        out.insert(out.end(), mip.bytes.begin(), mip.bytes.end());
    }
    return out;
}

Result<CookedTexture> decodeM2Texture(std::span<const std::byte> bytes)
{
    Reader reader{bytes};
    std::array<char, 4> magic{};
    std::uint32_t version = 0;
    std::uint32_t format = 0;
    std::uint32_t mipCount = 0;
    if (!reader.read(magic) || magic != c_magic)
    {
        return fail(ErrorCode::ParseError, "Not a .m2texture file");
    }
    if (!reader.read(version) || version != c_fileVersion)
    {
        return fail(ErrorCode::ParseError, std::format(".m2texture version {} isn't supported", version));
    }
    if (!reader.read(format) || format > static_cast<std::uint32_t>(CookedFormat::Bc5Unorm) || !reader.read(mipCount) ||
        mipCount == 0 || mipCount > 32)
    {
        return fail(ErrorCode::ParseError, "Bad .m2texture header");
    }
    CookedTexture texture{.format = static_cast<CookedFormat>(format), .mips = {}};
    texture.mips.reserve(mipCount);
    for (std::uint32_t i = 0; i < mipCount; ++i)
    {
        CookedMip mip{};
        std::uint64_t size = 0;
        if (!reader.read(mip.width) || !reader.read(mip.height) || !reader.read(size) || mip.width == 0 ||
            mip.height == 0 || size != levelBytes(texture.format, mip.width, mip.height))
        {
            return fail(ErrorCode::ParseError, std::format("Bad .m2texture level {}", i));
        }
        const auto data = reader.take(size);
        if (!data)
        {
            return fail(ErrorCode::ParseError, "Truncated .m2texture file");
        }
        mip.bytes.assign(data->begin(), data->end());
        texture.mips.push_back(std::move(mip));
    }
    if (!reader.empty())
    {
        return fail(ErrorCode::ParseError, "Unexpected bytes after the last .m2texture level");
    }
    return texture;
}

std::string textureCacheKey(const Image& image, TextureKind kind, bool compress)
{
    // 64-bit FNV-1a: simple and fast enough (tens of milliseconds for a 2048-pixel image, far less than cooking it).
    // A collision would need two images of the same size to hash alike, which is negligible for one project's assets.
    std::uint64_t hash = 14695981039346656037ull;
    const auto mix = [&hash](std::span<const std::byte> bytes)
    {
        for (const std::byte byte : bytes)
        {
            hash = (hash ^ static_cast<std::uint64_t>(byte)) * 1099511628211ull;
        }
    };
    const std::array<std::uint32_t, 5> header{c_cookerVersion, image.width, image.height,
                                              static_cast<std::uint32_t>(kind), compress ? 1u : 0u};
    mix(std::as_bytes(std::span{header}));
    mix(image.pixels);
    return std::format("{:016x}", hash);
}
} // namespace Meow2::Assets
