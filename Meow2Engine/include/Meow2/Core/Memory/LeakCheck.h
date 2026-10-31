#pragma once

/// @file
/// @brief ScopedLeakCheck: logs which memory tags gained allocations that were never freed while it was alive.
///
/// @code
/// int main()
/// {
///     Meow2::Log::LogSystem logSystem{...};
///     Meow2::Log::Logger engineLog = logSystem.createLogger("Engine");
///     const Meow2::Memory::ScopedLeakCheck leakCheck{Meow2::Memory::globalTracker(), engineLog};
///     // ... everything the app creates after this point ...
/// }   // destroyed before leakCheck; then leakCheck logs "No memory leaks" or the leaks per tag
/// @endcode
/// @see techstack.md §6, docs/wiki/Memory.md

#include <Meow2/Core/Log/Logger.h>
#include <Meow2/Core/Memory/MemTag.h>
#include <Meow2/Core/Memory/TrackingAllocator.h>

#include <array>
#include <string>

namespace Meow2::Memory
{
/// @brief Compares a tracker's live allocations per tag at construction and destruction, and logs the difference.
///
/// The composition root creates one right after the logging (and the crash handler), so it covers the app's whole
/// run while the log is still alive. Everything declared after it is destroyed before it, so whatever is still
/// allocated at its destruction, and wasn't before its construction, was never freed: a leak.
///
/// - No leaks: `info` "No memory leaks (N allocations during the run)".
/// - Leaks: `warn` with the number of allocations and bytes per tag (the tags that gained live allocations).
/// - In builds without memory tracking (Release, see MEOW2_MEMORY_TRACKING) it logs that at `debug` and checks
///   nothing.
///
/// @note The check counts allocations, it doesn't identify them: a leak and an unrelated free of a block that was
///       allocated *before* the check started cancel out in the same tag. Memory that's freed after `main` returns
///       (e.g. by a static destructor) shows as a leak. Neither happens in engine code today (no mutable statics).
/// @par Thread safety
/// Not thread-safe; create and destroy it on one thread. Allocations on other threads are counted (the tracker is
/// thread-safe), so stop other threads before it's destroyed.
/// @par Lifetime
/// The tracker and the Logger must outlive the check.
class ScopedLeakCheck
{
public:
    /// @brief Records the tracker's live allocations per tag as the baseline.
    /// @param tracker Tracker to watch, usually globalTracker().
    /// @param log     Logger the result is written to when the check ends.
    ScopedLeakCheck(const TrackingAllocator& tracker, Log::Logger& log);

    /// @brief Logs the result (see the class description).
    ~ScopedLeakCheck();

    ScopedLeakCheck(const ScopedLeakCheck&) = delete;            ///< Not copyable: one check, one report.
    ScopedLeakCheck& operator=(const ScopedLeakCheck&) = delete; ///< Not copyable: one check, one report.

    /// @brief The leaks so far, as the destructor would log them.
    /// @return `"Memory leaks: …"` plus one line per tag that gained live allocations since construction, or an
    ///         empty string if there are none.
    [[nodiscard]] std::string report() const;

private:
    const TrackingAllocator& m_tracker;
    Log::Logger& m_log;
    std::array<MemTagStats, c_memTagCount> m_baseline;
};
} // namespace Meow2::Memory
