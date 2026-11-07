/// @file
/// @brief Allocator hooks for the C libraries the asset importers use (cgltf, stb_image), counted under MemTag::Assets.

#pragma once

#include "Meow2/Core/Memory/GlobalTracking.h"
#include "Meow2/Core/Memory/MemTag.h"
#include "Meow2/Core/Memory/TrackingAllocator.h"

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <cstring>

namespace Meow2::Assets
{
// C libraries hand back memory through plain function pointers with no size on free, so these reach the process-wide
// tracker like SDL's hooks (techstack.md §6, §18). Without tracking (Release, sanitizer builds) they're malloc and
// free.

/// malloc's guarantee: suitable for any fundamental type.
constexpr std::size_t c_libraryAlignment = alignof(std::max_align_t);

/// @brief Allocates for a C library, like malloc.
/// @param size Bytes.
/// @return The memory, or null on failure (C callers expect null, never an exception).
inline void* libraryAllocate(std::size_t size) noexcept
{
    if constexpr (Memory::c_memoryTrackingAvailable)
    {
        try
        {
            return Memory::globalTracker().allocateTagged(size, c_libraryAlignment, Memory::MemTag::Assets);
        }
        catch (...)
        {
            return nullptr;
        }
    }
    else
    {
        return std::malloc(size);
    }
}

/// @brief Frees memory from libraryAllocate(), like free.
/// @param memory The memory; null is ignored.
inline void libraryFree(void* memory) noexcept
{
    if (memory == nullptr)
    {
        return;
    }
    if constexpr (Memory::c_memoryTrackingAvailable)
    {
        Memory::globalTracker().deallocateUnsized(memory);
    }
    else
    {
        std::free(memory); // NOLINT(cppcoreguidelines-no-malloc): the C library's own contract
    }
}

/// @brief Resizes memory from libraryAllocate(), like realloc.
/// @param memory The memory; null allocates.
/// @param size   The new size in bytes.
/// @return The memory, or null on failure, in which case `memory` is still valid.
inline void* libraryReallocate(void* memory, std::size_t size) noexcept
{
    if constexpr (Memory::c_memoryTrackingAvailable)
    {
        if (memory == nullptr)
        {
            return libraryAllocate(size);
        }
        // The tracker has no in-place resize: allocate, copy, free.
        void* resized = libraryAllocate(size);
        if (resized != nullptr)
        {
            std::memcpy(resized, memory, std::min(size, Memory::globalTracker().allocationSize(memory)));
            libraryFree(memory);
        }
        return resized;
    }
    else
    {
        return std::realloc(memory, size); // NOLINT(cppcoreguidelines-no-malloc): the C library's own contract
    }
}
} // namespace Meow2::Assets
