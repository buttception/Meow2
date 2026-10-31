#pragma once

/// @file
/// @brief Debug fill patterns: byte values engine allocators write into memory so misuse is easy to spot.
///
/// In Debug and RelWithDebInfo builds, engine allocators fill memory they hand out with c_allocatedFill and memory
/// that's freed (or reset, for arenas) with c_freedFill. Reading memory that was never written, or that was already
/// freed, then shows a recognisable value in the debugger instead of plausible-looking old data. Read as a pointer,
/// both patterns are invalid addresses on x64 (`0xCDCDCDCDCDCDCDCD`), so dereferencing one crashes at once.
///
/// The values are the ones the Microsoft debug C runtime uses for its heap, so they look the same in engine and CRT
/// memory. Fills only show *reads* of stale memory; *writes* to freed memory are AddressSanitizer's job.
///
/// Each allocator can turn its fills off at runtime (e.g. BlockArena::setDebugFill), for profiling. Release builds
/// never fill.
/// @see techstack.md §6, docs/wiki/Memory.md

#include <Meow2/Core/Platform.h>

#include <cstddef>

/// @def MEOW2_MEMORY_DEBUG_FILL
/// @brief 1 when engine allocators support debug fill patterns: Debug and RelWithDebInfo by default.
///
/// Can be forced by defining it (0 or 1) on the compiler command line, e.g. to profile a RelWithDebInfo build with
/// no fill code at all.

#ifndef MEOW2_MEMORY_DEBUG_FILL
#if MEOW2_CONFIG_DEBUG || MEOW2_CONFIG_RELWITHDEBINFO
#define MEOW2_MEMORY_DEBUG_FILL 1
#else
#define MEOW2_MEMORY_DEBUG_FILL 0
#endif
#endif

namespace Meow2::Memory
{
/// @brief `true` when this build supports debug fills (see MEOW2_MEMORY_DEBUG_FILL).
inline constexpr bool c_debugFillAvailable = MEOW2_MEMORY_DEBUG_FILL == 1;

/// @brief Written into memory when it's handed out ("clean"): allocated, but not written by its owner yet.
inline constexpr std::byte c_allocatedFill{0xCD};

/// @brief Written into memory when it's freed or its arena is reset ("dead"): nobody may read it any more.
inline constexpr std::byte c_freedFill{0xDD};

/// @brief Written into guard bytes around an allocation ("fence"), to detect overruns when it's freed.
///        Written and checked by TrackingAllocator (16 bytes behind every allocation).
inline constexpr std::byte c_guardFill{0xFD};
} // namespace Meow2::Memory
