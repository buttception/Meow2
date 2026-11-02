#pragma once

/// @file
/// @brief Profiling macros: zones, frame marks and thread names for the Tracy profiler, compiled out of Release.
///
/// @code
/// void Physics::step()
/// {
///     MEOW2_PROFILE_ZONE();                     // a zone named after the function, until the end of the scope
///     {
///         MEOW2_PROFILE_ZONE_NAMED("Broadphase");
///         broadphase();
///     }
/// }
///
/// while (running)                                 // the app's loop
/// {
///     tick();
///     MEOW2_PROFILE_FRAME_MARK();               // one frame on Tracy's timeline
/// }
/// @endcode
///
/// Connect the Tracy viewer (download it from Tracy's GitHub releases, the same version as `external/tracy`) to a
/// running Debug or RelWithDebInfo app: it listens on localhost only and records nothing until a viewer connects.
/// @see techstack.md §3, docs/wiki/Profiling.md

#include <cstddef>
#include <cstdint>

/// @def MEOW2_PROFILING
/// @brief 1 when profiling is compiled in (Debug and RelWithDebInfo with the `MEOW2_PROFILING` CMake option on),
///        0 otherwise. Set by CMake; defaults to 0 if a file is compiled without it.
#ifndef MEOW2_PROFILING
#define MEOW2_PROFILING 0
#endif

namespace Meow2::Memory
{
class TrackingAllocator;
} // namespace Meow2::Memory

namespace Meow2::Profiling
{
/// @brief Whether profiling is compiled into this build.
inline constexpr bool c_profilingEnabled = MEOW2_PROFILING == 1;

/// @brief Where a zone is in the source code. Plain data, laid out exactly like Tracy's C source location.
///
/// The zone macros create one `static const` instance per zone, so Tracy can identify the zone by its address.
struct SourceLocation
{
    const char* name;     ///< Zone name, or null to use the function name.
    const char* function; ///< Function the zone is in (`__func__`).
    const char* file;     ///< Source file (`__FILE__`).
    std::uint32_t line;   ///< Source line (`__LINE__`).
    std::uint32_t color;  ///< 0xRRGGBB colour in the viewer, or 0 for the default.
};

/// @brief Measures the time from its construction to its destruction as one zone. Use the macros instead.
/// @par Thread safety
/// Thread-safe in the sense that each thread records its own zones; a zone must end on the thread it began on.
class ScopedZone
{
public:
    /// @brief Begins a zone (does nothing without profiling, or while no viewer is connected).
    /// @param location The zone's static source location.
    explicit ScopedZone(const SourceLocation& location) noexcept;

    /// @brief Ends the zone.
    ~ScopedZone();

    ScopedZone(const ScopedZone&) = delete;            ///< Not copyable: one zone, one end.
    ScopedZone& operator=(const ScopedZone&) = delete; ///< Not copyable: one zone, one end.

private:
    // Tracy's zone context (id, active flag, connection id), kept as bytes so no Tracy header is needed here. The
    // size and alignment are checked against Tracy's type in the implementation. Unused when profiling is compiled
    // out (e.g. sanitizer builds), hence [[maybe_unused]] for Clang's -Wunused-private-field.
    [[maybe_unused]] alignas(8) std::byte m_context[16];
};

/// @brief Marks the end of a frame (the timeline's frame boundaries). Call once per frame.
void frameMark() noexcept;

/// @brief Names the calling thread in the viewer, e.g. `"Main"`, `"Job thread 3"`. The name is copied.
/// @param name Thread name (UTF-8).
void setThreadName(const char* name) noexcept;

/// @brief Plots the current bytes of every memory tag ("Memory/<Tag>" plots). Call once per frame.
/// @param tracker Usually Memory::globalTracker().
void plotMemory(const Memory::TrackingAllocator& tracker) noexcept;
} // namespace Meow2::Profiling

/// @cond
#define MEOW2_PROFILE_CONCAT_INNER(a, b) a##b
#define MEOW2_PROFILE_CONCAT(a, b) MEOW2_PROFILE_CONCAT_INNER(a, b)
/// @endcond

#if MEOW2_PROFILING
#define MEOW2_PROFILE_ZONE_NAMED(name)                                                                                 \
    static const ::Meow2::Profiling::SourceLocation MEOW2_PROFILE_CONCAT(c_meow2ProfileLocation, __LINE__){            \
        name, __func__, __FILE__, static_cast<std::uint32_t>(__LINE__), 0};                                            \
    const ::Meow2::Profiling::ScopedZone MEOW2_PROFILE_CONCAT(c_meow2ProfileZone, __LINE__)                            \
    {                                                                                                                  \
        MEOW2_PROFILE_CONCAT(c_meow2ProfileLocation, __LINE__)                                                         \
    }
#define MEOW2_PROFILE_ZONE() MEOW2_PROFILE_ZONE_NAMED(nullptr)
#define MEOW2_PROFILE_FRAME_MARK() ::Meow2::Profiling::frameMark()
#define MEOW2_PROFILE_THREAD_NAME(name) ::Meow2::Profiling::setThreadName(name)
#define MEOW2_PROFILE_MEMORY(tracker) ::Meow2::Profiling::plotMemory(tracker)
#else
/// @def MEOW2_PROFILE_ZONE_NAMED(name)
/// @brief A profiling zone called `name` (a string literal) from here to the end of the scope. Nothing in Release.
/// @param name Zone name shown in the viewer.
#define MEOW2_PROFILE_ZONE_NAMED(name) static_cast<void>(0)
/// @def MEOW2_PROFILE_ZONE()
/// @brief A profiling zone named after the enclosing function, to the end of the scope. Nothing in Release.
#define MEOW2_PROFILE_ZONE() static_cast<void>(0)
/// @def MEOW2_PROFILE_FRAME_MARK()
/// @brief Marks the end of a frame. Nothing in Release.
#define MEOW2_PROFILE_FRAME_MARK() static_cast<void>(0)
/// @def MEOW2_PROFILE_THREAD_NAME(name)
/// @brief Names the calling thread in the viewer. Nothing in Release.
/// @param name Thread name (UTF-8); copied.
#define MEOW2_PROFILE_THREAD_NAME(name) static_cast<void>(0)
/// @def MEOW2_PROFILE_MEMORY(tracker)
/// @brief Plots the current bytes per memory tag of `tracker`. Nothing in Release.
/// @param tracker A Memory::TrackingAllocator, usually Memory::globalTracker().
#define MEOW2_PROFILE_MEMORY(tracker) static_cast<void>(0)
#endif
