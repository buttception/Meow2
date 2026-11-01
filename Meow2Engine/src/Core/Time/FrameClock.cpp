/// @file
/// @brief FrameClock implementation.

#include "Meow2/Core/Time/FrameClock.h"

#include "Meow2/Core/Assert.h"

#include <algorithm>

namespace Meow2::Time
{
FrameClock::FrameClock(double maxFrameDeltaSeconds)
    : FrameClock(Clock::now(), maxFrameDeltaSeconds)
{
}

FrameClock::FrameClock(TimePoint start, double maxFrameDeltaSeconds)
    : m_previous(start)
    , m_maxFrameDeltaSeconds(maxFrameDeltaSeconds)
{
    MEOW2_ASSERT(maxFrameDeltaSeconds > 0.0, "the frame delta limit must be positive");
}

double FrameClock::tick()
{
    return tick(Clock::now());
}

double FrameClock::tick(TimePoint now)
{
    // steady_clock doesn't go backwards, but a caller passing time points might; that frame then took no time,
    // and m_previous stays where it was so the next frame isn't counted twice.
    const double elapsed = std::chrono::duration<double>(now - m_previous).count();
    m_unclampedDeltaSeconds = std::max(elapsed, 0.0);
    m_deltaSeconds = std::min(m_unclampedDeltaSeconds, m_maxFrameDeltaSeconds);
    m_previous = std::max(now, m_previous);
    m_totalSeconds += m_deltaSeconds;
    ++m_frameCount;
    return m_deltaSeconds;
}

double FrameClock::deltaSeconds() const
{
    return m_deltaSeconds;
}

float FrameClock::deltaSecondsFloat() const
{
    return static_cast<float>(m_deltaSeconds);
}

double FrameClock::unclampedDeltaSeconds() const
{
    return m_unclampedDeltaSeconds;
}

bool FrameClock::wasClamped() const
{
    return m_unclampedDeltaSeconds > m_maxFrameDeltaSeconds;
}

std::uint64_t FrameClock::frameCount() const
{
    return m_frameCount;
}

double FrameClock::totalSeconds() const
{
    return m_totalSeconds;
}

double FrameClock::maxFrameDeltaSeconds() const
{
    return m_maxFrameDeltaSeconds;
}
} // namespace Meow2::Time
