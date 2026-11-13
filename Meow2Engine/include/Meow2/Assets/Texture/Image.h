/// @file
/// @brief Decoded images: PNG and JPEG bytes to RGBA8 pixels on the CPU.

#pragma once

#include <Meow2/Core/Result.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace Meow2::Assets
{
/// @brief An image on the CPU: 8-bit RGBA, top row first, rows tightly packed. Plain data.
struct Image
{
    std::uint32_t width = 0;         ///< Width in pixels.
    std::uint32_t height = 0;        ///< Height in pixels.
    std::vector<std::byte> pixels{}; ///< width × height × 4 bytes: R, G, B, A.
};

/// @brief Decodes a PNG or JPEG file in memory, the two image formats of core glTF 2.0.
///
/// Every image becomes 4 channels: grey and RGB get an opaque alpha. Colour space isn't changed; whoever uses the image
/// knows whether it's sRGB (base colour) or linear (normals, roughness).
/// @param encoded The file's bytes.
/// @return The image, or ErrorCode::ParseError naming what the decoder didn't like (an unsupported format included).
/// @note Thread-safe. Decoding memory is counted under MemTag::Assets.
[[nodiscard]] Result<Image> decodeImage(std::span<const std::byte> encoded);

/// @brief Encodes an image as a PNG file in memory (RGBA, 8 bits per channel), for screenshots and golden images.
///
/// Lossless, so decodeImage() gives back the same pixels. The colour values are written as they are: a PNG viewer
/// shows them as sRGB.
/// @param image The image; `pixels` holds width × height × 4 bytes.
/// @return The file's bytes, or ErrorCode::InvalidArgument for an empty or inconsistent image.
/// @note Thread-safe. Encoding memory is counted under MemTag::Assets.
[[nodiscard]] Result<std::vector<std::byte>> encodePng(const Image& image);

/// @brief The mip chain of an image: the image, then each level half the size of the one before (rounded down, at
///        least 1), down to 1 × 1.
///
/// Each level is a 2 × 2 box filter of the one before. For colour images (`srgb`), the averaging is done on linear
/// light and encoded back, so distant textures don't darken; alpha and data images (normals, roughness) are averaged
/// as they are.
/// @param image The full-size image, RGBA8.
/// @param srgb  Whether its RGB is sRGB-encoded colour.
/// @return Every level, the full-size image first.
/// @note Thread-safe.
[[nodiscard]] std::vector<Image> makeMipChain(const Image& image, bool srgb);

/// @brief How many mip levels an image of this size has, down to 1 × 1.
/// @param width  Width in pixels; > 0.
/// @param height Height in pixels; > 0.
/// @return floor(log2(max(width, height))) + 1.
[[nodiscard]] std::uint32_t mipLevelCount(std::uint32_t width, std::uint32_t height);
} // namespace Meow2::Assets
