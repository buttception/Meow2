/// @file
/// @brief ScopedLeakCheck implementation.

#include "Meow2/Core/Memory/LeakCheck.h"

#include "Meow2/Core/Memory/GlobalTracking.h"

#include <format>
#include <iterator>

namespace Meow2::Memory
{
ScopedLeakCheck::ScopedLeakCheck(const TrackingAllocator& tracker, Log::Logger& log)
    : m_tracker(tracker)
    , m_log(log)
    , m_baseline{}
{
    for (std::size_t i = 0; i < c_memTagCount; ++i)
    {
        m_baseline[i] = m_tracker.stats(static_cast<MemTag>(i));
    }
}

ScopedLeakCheck::~ScopedLeakCheck()
{
    if (!c_memoryTrackingAvailable)
    {
        MEOW2_LOG_DEBUG(m_log, "Memory tracking is off in this build: no leak check");
        return;
    }
    const std::string leaks = report();
    if (!leaks.empty())
    {
        MEOW2_LOG_WARN(m_log, "{}", leaks);
        return;
    }
    std::uint64_t allocations = 0;
    for (std::size_t i = 0; i < c_memTagCount; ++i)
    {
        allocations += m_tracker.stats(static_cast<MemTag>(i)).totalAllocations - m_baseline[i].totalAllocations;
    }
    MEOW2_LOG_INFO(m_log, "No memory leaks ({} allocations during the run)", allocations);
}

std::string ScopedLeakCheck::report() const
{
    // Per tag: how many more allocations (and bytes) are live now than at the start. Only increases are leaks; a
    // decrease means blocks from before the check were freed, which is fine.
    // All counters are read before anything is formatted: the report's own strings are allocations too (even an
    // empty std::string allocates in MSVC Debug builds), and must not count themselves as leaks.
    std::array<MemTagStats, c_memTagCount> leaked{};
    std::size_t leakedAllocations = 0;
    std::size_t leakedBytes = 0;
    for (std::size_t i = 0; i < c_memTagCount; ++i)
    {
        const MemTagStats now = m_tracker.stats(static_cast<MemTag>(i));
        if (now.liveAllocations > m_baseline[i].liveAllocations)
        {
            leaked[i].liveAllocations = now.liveAllocations - m_baseline[i].liveAllocations;
            leaked[i].currentBytes =
                now.currentBytes > m_baseline[i].currentBytes ? now.currentBytes - m_baseline[i].currentBytes : 0;
            leakedAllocations += leaked[i].liveAllocations;
            leakedBytes += leaked[i].currentBytes;
        }
    }
    if (leakedAllocations == 0)
    {
        return {};
    }

    std::string report =
        std::format("Memory leaks: {} allocations, {} bytes not freed", leakedAllocations, leakedBytes);
    for (std::size_t i = 0; i < c_memTagCount; ++i)
    {
        if (leaked[i].liveAllocations != 0)
        {
            std::format_to(std::back_inserter(report), "\n  {}: {} allocations, {} bytes",
                           memTagName(static_cast<MemTag>(i)), leaked[i].liveAllocations, leaked[i].currentBytes);
        }
    }
    return report;
}
} // namespace Meow2::Memory
