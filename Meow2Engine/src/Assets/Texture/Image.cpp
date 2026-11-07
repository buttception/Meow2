/// @file
/// @brief Image decoding through stb_image, and the memory hooks its implementation (StbImage.cpp) calls.

#include "Meow2/Assets/Texture/Image.h"

#include "Assets/LibraryMemory.h"
#include "Meow2/Core/Memory/MemTag.h"

#include <stb_image.h>

#include <climits>
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
} // namespace Meow2::Assets
