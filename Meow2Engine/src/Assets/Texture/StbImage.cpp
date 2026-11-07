/// @file
/// @brief stb_image's implementation. Built in its own library (`meow2_stb_image`), outside the engine's warnings.
///
/// Only PNG and JPEG, the two formats glTF 2.0 allows without extensions, and only from memory: files are read through
/// the engine's file system. Its memory goes through the meow2Stb* hooks, defined in Image.cpp.

#include <cstddef>

extern "C"
{
    void* meow2StbMalloc(std::size_t size);
    void* meow2StbRealloc(void* memory, std::size_t size);
    void meow2StbFree(void* memory);
}

#define STBI_MALLOC(size) meow2StbMalloc(size)
#define STBI_REALLOC(memory, size) meow2StbRealloc(memory, size)
#define STBI_FREE(memory) meow2StbFree(memory)
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_NO_STDIO
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
