/// @file
/// @brief Cooked textures: images turned into a GPU-ready format (mips, BC7 or BC5 compression) and the `.m2texture`
///        file that stores them.

#pragma once

#include <Meow2/Assets/Texture/Image.h>
#include <Meow2/Core/Jobs/JobSystem.h>
#include <Meow2/Core/Result.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace Meow2::Assets
{
/// @brief What a texture holds, which decides how it's cooked.
enum class TextureKind : std::uint8_t
{
    Color,    ///< sRGB colour (base colour, emissive): averaged in linear light, BC7 sRGB.
    Data,     ///< Linear data (metallic-roughness, occlusion): BC7 linear.
    NormalMap ///< A tangent-space normal map: BC5, only X and Y kept (the shader rebuilds Z).
};

/// @brief The pixel format of a cooked texture. Mirrors the RHI's formats, without depending on them.
enum class CookedFormat : std::uint8_t
{
    Rgba8Unorm, ///< 8-bit RGBA, linear.
    Rgba8Srgb,  ///< 8-bit RGBA, sRGB.
    Bc7Unorm,   ///< BC7, linear: 16 bytes per 4 × 4 block.
    Bc7Srgb,    ///< BC7, sRGB.
    Bc5Unorm    ///< BC5: two channels, 16 bytes per 4 × 4 block.
};

/// @brief One mip level of a cooked texture. Plain data.
struct CookedMip
{
    std::uint32_t width = 0;        ///< In pixels.
    std::uint32_t height = 0;       ///< In pixels.
    std::vector<std::byte> bytes{}; ///< Rows tightly packed: pixels, or rows of 4 × 4 blocks for BC formats.
};

/// @brief A texture ready to upload: its format and every mip level, the full size first. Plain data.
struct CookedTexture
{
    CookedFormat format = CookedFormat::Rgba8Unorm; ///< How the bytes are laid out.
    std::vector<CookedMip> mips{};                  ///< At least one.
};

/// @brief Whether a format is block-compressed (4 × 4 pixel blocks).
/// @param format The format.
/// @return True for BC7 and BC5.
[[nodiscard]] constexpr bool isBlockCompressed(CookedFormat format) noexcept
{
    return format == CookedFormat::Bc7Unorm || format == CookedFormat::Bc7Srgb || format == CookedFormat::Bc5Unorm;
}

/// @brief Cooks an image: its mip chain (TextureKind::Color averaged in linear light), then, with `compress`, every
///        level compressed to BC7 (colour and data) or BC5 (normal maps) with DirectXTex.
///
/// Compression is spread over the job system in strips of rows. An image whose sides aren't multiples of 4 can't be
/// block-compressed (the GPU's rule for the top level) and is cooked as RGBA8 instead.
/// @param image    The image, RGBA8.
/// @param kind     What it holds.
/// @param compress Whether to compress; false keeps RGBA8 (fast, for previews and tests).
/// @param jobs     Runs the compression in parallel; null compresses on the calling thread.
/// @return The cooked texture, or ErrorCode::InvalidArgument for an empty image, or an error from DirectXTex.
/// @note Thread-safe. BC7 is slow: seconds per 2048-pixel texture even spread over the job system; cache the result
///       (TextureCache).
[[nodiscard]] Result<CookedTexture> cookTexture(const Image& image, TextureKind kind, bool compress,
                                                Jobs::JobSystem* jobs = nullptr);

/// @brief The bytes of a `.m2texture` file: a versioned header (magic, version, format, mip count) and each level's
///        size and bytes.
/// @param texture The texture.
/// @return The file's contents.
[[nodiscard]] std::vector<std::byte> encodeM2Texture(const CookedTexture& texture);

/// @brief Reads a `.m2texture` file.
/// @param bytes The file's contents.
/// @return The texture, or ErrorCode::ParseError for a wrong magic, version or a truncated file.
[[nodiscard]] Result<CookedTexture> decodeM2Texture(std::span<const std::byte> bytes);

/// @brief A name for the cooked form of an image: a hash of its pixels and size, the kind, compression and the
///        cooker's version, as 16 hex digits. The same image cooked the same way always gets the same key.
/// @param image    The image.
/// @param kind     What it holds.
/// @param compress Whether it's compressed.
/// @return The key.
[[nodiscard]] std::string textureCacheKey(const Image& image, TextureKind kind, bool compress);
} // namespace Meow2::Assets
