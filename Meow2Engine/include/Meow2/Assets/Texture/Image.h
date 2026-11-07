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
} // namespace Meow2::Assets
