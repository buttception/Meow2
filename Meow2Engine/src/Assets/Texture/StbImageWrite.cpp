/// @file
/// @brief stb_image_write's implementation. Built in `meow2_stb_image` with stb_image's, outside the engine's warnings.
///
/// Only PNG, and only to memory (`stbi_write_png_to_func`): files are written through the engine's file system. Its
/// memory goes through the same meow2Stb* hooks as stb_image's, defined in Image.cpp.

#include <cstddef>

extern "C"
{
    void* meow2StbMalloc(std::size_t size);
    void* meow2StbRealloc(void* memory, std::size_t size);
    void meow2StbFree(void* memory);
}

#define STBIW_MALLOC(size) meow2StbMalloc(size)
#define STBIW_REALLOC(memory, size) meow2StbRealloc(memory, size)
#define STBIW_FREE(memory) meow2StbFree(memory)
#define STBI_WRITE_NO_STDIO
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>
