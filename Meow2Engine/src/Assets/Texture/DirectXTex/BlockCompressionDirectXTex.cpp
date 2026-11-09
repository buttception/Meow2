/// @file
/// @brief Block compression with DirectXTex (BlockCompression.h).

#include "Assets/Texture/BlockCompression.h"
#include "Meow2/Core/Memory/MemTag.h"

#include <DirectXTex.h>

#include <cstring>
#include <format>

namespace Meow2::Assets
{
namespace
{
/// @brief The DXGI format of a cooked format.
DXGI_FORMAT dxgiFormat(CookedFormat format)
{
    switch (format)
    {
        case CookedFormat::Rgba8Unorm:
            return DXGI_FORMAT_R8G8B8A8_UNORM;
        case CookedFormat::Rgba8Srgb:
            return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
        case CookedFormat::Bc7Unorm:
            return DXGI_FORMAT_BC7_UNORM;
        case CookedFormat::Bc7Srgb:
            return DXGI_FORMAT_BC7_UNORM_SRGB;
        case CookedFormat::Bc5Unorm:
            return DXGI_FORMAT_BC5_UNORM;
    }
    return DXGI_FORMAT_UNKNOWN;
}

/// @brief The pixel format the blocks come from, or go back to: sRGB when the blocks are.
DXGI_FORMAT sourceFormat(CookedFormat format)
{
    return format == CookedFormat::Bc7Srgb ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM;
}

/// @brief The bytes of a DirectXTex image, tightly packed.
std::vector<std::byte> copyOut(const DirectX::Image& image)
{
    std::vector<std::byte> bytes(image.slicePitch);
    std::memcpy(bytes.data(), image.pixels, image.slicePitch);
    return bytes;
}
} // namespace

Result<std::vector<std::byte>> compressBlocks(std::span<const std::byte> pixels, std::uint32_t width,
                                              std::uint32_t height, CookedFormat format)
{
    const Memory::MemTagScope tag{Memory::MemTag::Assets};
    DirectX::Image source{};
    source.width = width;
    source.height = height;
    source.format = sourceFormat(format);
    source.rowPitch = std::size_t{width} * 4;
    source.slicePitch = source.rowPitch * height;
    // DirectXTex only reads the source; its Image type has no const pixel pointer.
    source.pixels = const_cast<std::uint8_t*>(reinterpret_cast<const std::uint8_t*>(pixels.data()));
    // BC7's quick mode tries fewer of its partition modes: close in quality, several times faster than the default.
    // Both sides sRGB (or both linear): the encoder compares colours as stored, with no conversion.
    DirectX::TEX_COMPRESS_FLAGS flags = DirectX::TEX_COMPRESS_DEFAULT;
    if (format == CookedFormat::Bc7Unorm || format == CookedFormat::Bc7Srgb)
    {
        flags |= DirectX::TEX_COMPRESS_BC7_QUICK;
    }
    DirectX::ScratchImage compressed;
    if (const HRESULT result =
            DirectX::Compress(source, dxgiFormat(format), flags, DirectX::TEX_THRESHOLD_DEFAULT, compressed);
        FAILED(result))
    {
        return fail(ErrorCode::Unknown,
                    std::format("Texture compression failed (0x{:08X})", static_cast<std::uint32_t>(result)));
    }
    return copyOut(*compressed.GetImage(0, 0, 0));
}

Result<std::vector<std::byte>> decompressBlocks(std::span<const std::byte> blocks, std::uint32_t width,
                                                std::uint32_t height, CookedFormat format)
{
    const Memory::MemTagScope tag{Memory::MemTag::Assets};
    DirectX::Image source{};
    source.width = width;
    source.height = height;
    source.format = dxgiFormat(format);
    if (const HRESULT result = DirectX::ComputePitch(source.format, width, height, source.rowPitch, source.slicePitch);
        FAILED(result) || source.slicePitch != blocks.size())
    {
        return fail(ErrorCode::InvalidArgument, "Block data doesn't match its size");
    }
    source.pixels = const_cast<std::uint8_t*>(reinterpret_cast<const std::uint8_t*>(blocks.data()));
    DirectX::ScratchImage pixels;
    if (const HRESULT result = DirectX::Decompress(source, sourceFormat(format), pixels); FAILED(result))
    {
        return fail(ErrorCode::Unknown,
                    std::format("Texture decompression failed (0x{:08X})", static_cast<std::uint32_t>(result)));
    }
    return copyOut(*pixels.GetImage(0, 0, 0));
}
} // namespace Meow2::Assets
