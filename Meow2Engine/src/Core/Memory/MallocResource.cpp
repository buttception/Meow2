/// @file
/// @brief MallocResource implementation.

#include "Meow2/Core/Memory/MallocResource.h"

#include "Meow2/Core/Assert.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <new>

namespace Meow2::Memory
{
namespace
{
constexpr std::size_t c_mallocAlignment = alignof(std::max_align_t);

/// Over-aligned requests: malloc `bytes + alignment` (which always leaves room for the stored pointer), round up to
/// the alignment, and store malloc's pointer in the bytes just before the aligned address. Portable, unlike
/// `_aligned_malloc` (Windows only) or `std::aligned_alloc` (not in MSVC's CRT).
void* allocateOverAligned(std::size_t bytes, std::size_t alignment)
{
    if (bytes > std::numeric_limits<std::size_t>::max() - alignment)
    {
        throw std::bad_alloc{};
    }
    void* raw = std::malloc(bytes + alignment);
    if (raw == nullptr)
    {
        throw std::bad_alloc{};
    }
    // alignment > alignof(max_align_t) >= sizeof(void*), and raw is max_align_t-aligned, so rounding up always moves
    // by at least alignof(max_align_t) bytes: enough room for the pointer below `aligned`.
    const auto address = reinterpret_cast<std::uintptr_t>(raw);
    const std::uintptr_t aligned = (address + alignment) & ~(static_cast<std::uintptr_t>(alignment) - 1);
    void* result = reinterpret_cast<void*>(aligned);
    std::memcpy(static_cast<std::byte*>(result) - sizeof(void*), &raw, sizeof(void*));
    return result;
}

void deallocateOverAligned(void* pointer)
{
    void* raw = nullptr;
    std::memcpy(&raw, static_cast<std::byte*>(pointer) - sizeof(void*), sizeof(void*));
    std::free(raw);
}
} // namespace

void* MallocResource::do_allocate(std::size_t bytes, std::size_t alignment)
{
    MEOW2_ASSERT(alignment != 0 && (alignment & (alignment - 1)) == 0, "alignment must be a power of two");
    if (alignment > c_mallocAlignment)
    {
        return allocateOverAligned(bytes, alignment);
    }
    // malloc(0) may return null; one byte gives every request a distinct, freeable pointer.
    void* pointer = std::malloc(std::max<std::size_t>(bytes, 1));
    if (pointer == nullptr)
    {
        throw std::bad_alloc{};
    }
    return pointer;
}

void MallocResource::do_deallocate(void* pointer, std::size_t /*bytes*/, std::size_t alignment)
{
    // The alignment picks the path, so it must match the allocation (the memory_resource contract says it does).
    if (alignment > c_mallocAlignment)
    {
        deallocateOverAligned(pointer);
        return;
    }
    std::free(pointer);
}

bool MallocResource::do_is_equal(const std::pmr::memory_resource& other) const noexcept
{
    // Stateless, so any MallocResource could free another's memory; identity is still the safe, simple answer.
    return this == &other;
}
} // namespace Meow2::Memory
