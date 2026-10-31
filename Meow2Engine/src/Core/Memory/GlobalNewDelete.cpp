/// @file
/// @brief Replacement global `operator new`/`delete`: every C++ heap allocation goes through globalTracker().
///
/// Not part of the Meow2Engine library: `meow2_track_global_new(<target>)` compiles this file into each executable.
/// In a static library the linker only pulls in object files something refers to, and nothing refers to a
/// replacement `operator new` by name, so it could silently be left out.
///
/// All replaceable forms are defined (plain/array × default/extended alignment × throwing/nothrow, and every delete
/// including sized ones), so no allocation can slip past the tracker and no tracked block can reach the CRT's free.
/// Compiled to nothing when MEOW2_MEMORY_TRACKING is 0 (Release).

#include <Meow2/Core/Memory/GlobalTracking.h>

#if MEOW2_MEMORY_TRACKING

#include <cstddef>
#include <new>

namespace
{
using Meow2::Memory::Detail::globalDelete;
using Meow2::Memory::Detail::globalNew;

/// Alignment of plain `new`: what the compiler assumes for any `new T` without `alignas`.
constexpr std::size_t c_defaultNewAlignment = __STDCPP_DEFAULT_NEW_ALIGNMENT__;

void* newNothrow(std::size_t bytes, std::size_t alignment) noexcept
{
    try
    {
        return globalNew(bytes, alignment);
    }
    catch (...)
    {
        return nullptr;
    }
}
} // namespace

// Throwing forms.
void* operator new(std::size_t bytes)
{
    return globalNew(bytes, c_defaultNewAlignment);
}

void* operator new[](std::size_t bytes)
{
    return globalNew(bytes, c_defaultNewAlignment);
}

void* operator new(std::size_t bytes, std::align_val_t alignment)
{
    return globalNew(bytes, static_cast<std::size_t>(alignment));
}

void* operator new[](std::size_t bytes, std::align_val_t alignment)
{
    return globalNew(bytes, static_cast<std::size_t>(alignment));
}

// nothrow forms: null instead of std::bad_alloc.
void* operator new(std::size_t bytes, const std::nothrow_t& /*tag*/) noexcept
{
    return newNothrow(bytes, c_defaultNewAlignment);
}

void* operator new[](std::size_t bytes, const std::nothrow_t& /*tag*/) noexcept
{
    return newNothrow(bytes, c_defaultNewAlignment);
}

void* operator new(std::size_t bytes, std::align_val_t alignment, const std::nothrow_t& /*tag*/) noexcept
{
    return newNothrow(bytes, static_cast<std::size_t>(alignment));
}

void* operator new[](std::size_t bytes, std::align_val_t alignment, const std::nothrow_t& /*tag*/) noexcept
{
    return newNothrow(bytes, static_cast<std::size_t>(alignment));
}

// Every delete form ends in the same place: the tracker reads size and alignment from the block's header.
void operator delete(void* pointer) noexcept
{
    globalDelete(pointer);
}

void operator delete[](void* pointer) noexcept
{
    globalDelete(pointer);
}

void operator delete(void* pointer, std::size_t /*bytes*/) noexcept
{
    globalDelete(pointer);
}

void operator delete[](void* pointer, std::size_t /*bytes*/) noexcept
{
    globalDelete(pointer);
}

void operator delete(void* pointer, std::align_val_t /*alignment*/) noexcept
{
    globalDelete(pointer);
}

void operator delete[](void* pointer, std::align_val_t /*alignment*/) noexcept
{
    globalDelete(pointer);
}

void operator delete(void* pointer, std::size_t /*bytes*/, std::align_val_t /*alignment*/) noexcept
{
    globalDelete(pointer);
}

void operator delete[](void* pointer, std::size_t /*bytes*/, std::align_val_t /*alignment*/) noexcept
{
    globalDelete(pointer);
}

void operator delete(void* pointer, const std::nothrow_t& /*tag*/) noexcept
{
    globalDelete(pointer);
}

void operator delete[](void* pointer, const std::nothrow_t& /*tag*/) noexcept
{
    globalDelete(pointer);
}

void operator delete(void* pointer, std::align_val_t /*alignment*/, const std::nothrow_t& /*tag*/) noexcept
{
    globalDelete(pointer);
}

void operator delete[](void* pointer, std::align_val_t /*alignment*/, const std::nothrow_t& /*tag*/) noexcept
{
    globalDelete(pointer);
}

#endif // MEOW2_MEMORY_TRACKING
