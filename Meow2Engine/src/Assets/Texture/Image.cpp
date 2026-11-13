/// @file
/// @brief Image decoding through stb_image, PNG encoding through stb_image_write, and the memory hooks their
///        implementations (StbImage.cpp, StbImageWrite.cpp) call.

#include "Meow2/Assets/Texture/Image.h"

#include "Assets/LibraryMemory.h"
#include "Meow2/Core/Memory/MemTag.h"

#include <stb_image.h>
#include <stb_image_write.h>

#include <algorithm>
#include <array>
#include <climits>
#include <cmath>
#include <cstring>
#include <format>

// stb_image's allocator, chosen at compile time by STBI_MALLOC and friends. C functions with no user data, so they use
// the process-wide hooks like SDL's (techstack.md §6, §18).
extern "C"
{
    void* meow2StbMalloc(std::size_t size)
    {
        return Meow2::Assets::libraryAllocate(size);
    }

    void* meow2StbRealloc(void* memory, std::size_t size)
    {
        return Meow2::Assets::libraryReallocate(memory, size);
    }

    void meow2StbFree(void* memory)
    {
        Meow2::Assets::libraryFree(memory);
    }
}

namespace Meow2::Assets
{
Result<Image> decodeImage(std::span<const std::byte> encoded)
{
    if (encoded.empty() || encoded.size() > static_cast<std::size_t>(INT_MAX))
    {
        return fail(ErrorCode::ParseError, "Image: empty or too large");
    }
    int width = 0;
    int height = 0;
    int channels = 0;
    stbi_uc* pixels = stbi_load_from_memory(reinterpret_cast<const stbi_uc*>(encoded.data()),
                                            static_cast<int>(encoded.size()), &width, &height, &channels, 4);
    if (pixels == nullptr)
    {
        return fail(ErrorCode::ParseError, std::format("Image: {}", stbi_failure_reason()));
    }
    const Memory::MemTagScope tag{Memory::MemTag::Assets};
    Image image;
    image.width = static_cast<std::uint32_t>(width);
    image.height = static_cast<std::uint32_t>(height);
    image.pixels.resize(std::size_t{image.width} * image.height * 4);
    std::memcpy(image.pixels.data(), pixels, image.pixels.size());
    stbi_image_free(pixels);
    return image;
}

Result<std::vector<std::byte>> encodePng(const Image& image)
{
    if (image.width == 0 || image.height == 0 || image.width > static_cast<std::uint32_t>(INT_MAX / 4) ||
        image.height > static_cast<std::uint32_t>(INT_MAX) ||
        image.pixels.size() != std::size_t{image.width} * image.height * 4)
    {
        return fail(ErrorCode::InvalidArgument,
                    std::format("PNG: {}x{} with {} bytes of pixels", image.width, image.height, image.pixels.size()));
    }
    const Memory::MemTagScope tag{Memory::MemTag::Assets};
    std::vector<std::byte> file;
    // stb_image_write hands the file over in pieces; they're appended in order.
    const auto append = [](void* context, void* data, int size)
    {
        auto* out = static_cast<std::vector<std::byte>*>(context);
        const auto* bytes = static_cast<const std::byte*>(data);
        out->insert(out->end(), bytes, bytes + size);
    };
    const int written =
        stbi_write_png_to_func(append, &file, static_cast<int>(image.width), static_cast<int>(image.height), 4,
                               image.pixels.data(), static_cast<int>(image.width * 4));
    if (written == 0 || file.empty())
    {
        return fail(ErrorCode::Unknown, "PNG: encoding failed");
    }
    return file;
}
} // namespace Meow2::Assets

namespace Meow2::Assets
{
namespace
{
/// @brief sRGB-encoded 8-bit value → linear 0..1, exactly (the piecewise sRGB curve).
float srgbToLinear(std::uint8_t value)
{
    const float c = static_cast<float>(value) / 255.0F;
    return c <= 0.04045F ? c / 12.92F : std::pow((c + 0.055F) / 1.055F, 2.4F);
}

/// @brief Linear 0..1 → sRGB-encoded 8-bit value, rounded.
std::byte linearToSrgb(float value)
{
    const float c = std::clamp(value, 0.0F, 1.0F);
    const float encoded = c <= 0.0031308F ? c * 12.92F : 1.055F * std::pow(c, 1.0F / 2.4F) - 0.055F;
    return static_cast<std::byte>(static_cast<int>(encoded * 255.0F + 0.5F));
}

/// @brief The next level: each pixel the average of up to 2 × 2 pixels of `source`.
Image halve(const Image& source, bool srgb, const std::array<float, 256>& toLinear)
{
    Image level;
    level.width = std::max(source.width / 2, 1U);
    level.height = std::max(source.height / 2, 1U);
    level.pixels.resize(std::size_t{level.width} * level.height * 4);
    const auto at = [&source](std::uint32_t x, std::uint32_t y, std::uint32_t channel)
    {
        // Odd sizes: the last row or column pairs with itself.
        x = std::min(x, source.width - 1);
        y = std::min(y, source.height - 1);
        return std::to_integer<std::uint8_t>(source.pixels[(std::size_t{y} * source.width + x) * 4 + channel]);
    };
    for (std::uint32_t y = 0; y < level.height; ++y)
    {
        for (std::uint32_t x = 0; x < level.width; ++x)
        {
            for (std::uint32_t channel = 0; channel < 4; ++channel)
            {
                const std::array<std::uint8_t, 4> texels{at(x * 2, y * 2, channel), at(x * 2 + 1, y * 2, channel),
                                                         at(x * 2, y * 2 + 1, channel),
                                                         at(x * 2 + 1, y * 2 + 1, channel)};
                std::byte& out = level.pixels[(std::size_t{y} * level.width + x) * 4 + channel];
                if (srgb && channel < 3)
                {
                    const float sum =
                        toLinear[texels[0]] + toLinear[texels[1]] + toLinear[texels[2]] + toLinear[texels[3]];
                    out = linearToSrgb(sum * 0.25F);
                }
                else
                {
                    const unsigned sum = 2U + texels[0] + texels[1] + texels[2] + texels[3]; // +2: round to nearest
                    out = static_cast<std::byte>(sum / 4U);
                }
            }
        }
    }
    return level;
}
} // namespace

std::uint32_t mipLevelCount(std::uint32_t width, std::uint32_t height)
{
    std::uint32_t levels = 1;
    for (std::uint32_t size = std::max(width, height); size > 1; size /= 2)
    {
        ++levels;
    }
    return levels;
}

std::vector<Image> makeMipChain(const Image& image, bool srgb)
{
    const Memory::MemTagScope tag{Memory::MemTag::Assets};
    std::array<float, 256> toLinear{};
    for (std::size_t i = 0; i < toLinear.size(); ++i)
    {
        toLinear[i] = srgbToLinear(static_cast<std::uint8_t>(i));
    }
    std::vector<Image> chain;
    chain.reserve(mipLevelCount(image.width, image.height));
    chain.push_back(image);
    while (chain.back().width > 1 || chain.back().height > 1)
    {
        chain.push_back(halve(chain.back(), srgb, toLinear));
    }
    return chain;
}
} // namespace Meow2::Assets
