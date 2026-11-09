/// @file
/// @brief Block compression of one image (BC7, BC5), the part of texture cooking done by a library. Internal.

#pragma once

#include "Meow2/Assets/Texture/CookedTexture.h"
#include "Meow2/Core/Result.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace Meow2::Assets
{
/// @brief Compresses RGBA8 pixels into 4 × 4 blocks.
///
/// Implemented with DirectXTex (src/Assets/Texture/DirectXTex/), the only place that includes it: on Windows its
/// header brings in the D3D11 and Windows headers.
/// @param pixels RGBA8, rows tightly packed, `width` × `height` × 4 bytes.
/// @param width  In pixels; > 0.
/// @param height In pixels; > 0. A strip of a larger image works too, as long as it's a multiple of 4 rows.
/// @param format Bc7Unorm, Bc7Srgb or Bc5Unorm (which keeps R and G).
/// @return The blocks, rows of blocks tightly packed, or the library's error.
/// @note Thread-safe: separate calls may run at the same time on different data.
[[nodiscard]] Result<std::vector<std::byte>> compressBlocks(std::span<const std::byte> pixels, std::uint32_t width,
                                                            std::uint32_t height, CookedFormat format);

/// @brief Decompresses 4 × 4 blocks back to RGBA8, for tests and checks.
/// @param blocks The blocks, as compressBlocks() made them.
/// @param width  In pixels.
/// @param height In pixels.
/// @param format The blocks' format.
/// @return RGBA8 pixels, rows tightly packed, or the library's error.
[[nodiscard]] Result<std::vector<std::byte>> decompressBlocks(std::span<const std::byte> blocks, std::uint32_t width,
                                                              std::uint32_t height, CookedFormat format);
} // namespace Meow2::Assets
