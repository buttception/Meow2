#pragma once

/// @file
/// @brief The process-wide TrackingAllocator behind the global `operator new`/`delete` replacement.
///
/// In Debug and RelWithDebInfo, every executable that calls `meow2_track_global_new(<target>)` in CMake (all Meow2
/// apps and some tests) replaces the global `operator new` and `delete`. Every C++ heap allocation then goes through
/// globalTracker(), counted under the calling thread's currentMemTag() (see MemTagScope): `std::string`,
/// `std::vector`, `std::make_unique`, spdlog, everything.
///
/// The tracker lives for the whole process. It's ready before any global constructor runs and never destroyed,
/// because memory allocated during `main` may be freed after `main` returns (by static destructors), and that
/// `delete` must still find a working tracker.
///
/// @code
/// const Meow2::Memory::MemTagStats stats = Meow2::Memory::globalTracker().stats(Meow2::Memory::MemTag::Logging);
/// @endcode
/// @see techstack.md §6, docs/wiki/Memory.md

#include <Meow2/Core/Memory/TrackingAllocator.h>
#include <Meow2/Core/Platform.h>

#include <cstddef>

/// @def MEOW2_MEMORY_TRACKING
/// @brief 1 when `meow2_track_global_new()` replaces `operator new`/`delete` with the tracked versions: Debug and
///        RelWithDebInfo by default, never in Release.
///
/// Can be forced by defining it (0 or 1) on the compiler command line, e.g. 0 for sanitizer builds, which replace
/// `operator new` themselves.

#ifndef MEOW2_MEMORY_TRACKING
#if MEOW2_CONFIG_DEBUG || MEOW2_CONFIG_RELWITHDEBINFO
#define MEOW2_MEMORY_TRACKING 1
#else
#define MEOW2_MEMORY_TRACKING 0
#endif
#endif

namespace Meow2::Memory
{
/// @brief `true` when tracked `operator new`/`delete` are available in this build (see MEOW2_MEMORY_TRACKING).
inline constexpr bool c_memoryTrackingAvailable = MEOW2_MEMORY_TRACKING == 1;

/// @brief The tracker every global `new` and `delete` goes through (in executables that use the replacement).
///
/// Exists in every build. In Release, or in an executable without `meow2_track_global_new()`, nothing allocates
/// through it, so its counters stay at zero.
/// @return The process-wide tracker, over a MallocResource. Valid for the whole process, including global
///         constructors and destructors.
/// @par Thread safety
/// Thread-safe, like every TrackingAllocator.
[[nodiscard]] TrackingAllocator& globalTracker();

/// @brief Internal: the functions the replaced `operator new`/`delete` call. Not for other code.
namespace Detail
{
/// @brief Allocates for `operator new` from globalTracker(), under the current MemTag.
/// @param bytes     Size in bytes.
/// @param alignment Alignment in bytes; a power of two.
/// @return The memory. Throws `std::bad_alloc` when out of memory, as `operator new` must.
[[nodiscard]] void* globalNew(std::size_t bytes, std::size_t alignment);

/// @brief Frees memory from globalNew() for `operator delete`. Does nothing for null.
/// @param pointer Memory from globalNew(), or null.
void globalDelete(void* pointer) noexcept;
} // namespace Detail
} // namespace Meow2::Memory
