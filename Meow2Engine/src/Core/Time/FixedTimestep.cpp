/// @file
/// @brief FixedTimestep (FixedTimestep.h).

#include "Meow2/Core/Time/FixedTimestep.h"

#include "Meow2/Core/Assert.h"

#include <cmath>

namespace Meow2::Time
{
FixedTimestep::FixedTimestep(double stepSeconds, std::uint32_t maxStepsPerFrame)
    : m_stepSeconds(stepSeconds)
    , m_maxStepsPerFrame(maxStepsPerFrame)
{
    MEOW2_ASSERT(stepSeconds > 0.0, "the fixed step must be positive");
    MEOW2_ASSERT(maxStepsPerFrame > 0, "at least one fixed step per frame");
}

FixedStepResult FixedTimestep::advance(double frameDeltaSeconds)
{
    if (frameDeltaSeconds > 0.0)
    {
        m_accumulatedSeconds += frameDeltaSeconds;
    }

    FixedStepResult result;
    while (m_accumulatedSeconds >= m_stepSeconds && result.steps < m_maxStepsPerFrame)
    {
        m_accumulatedSeconds -= m_stepSeconds;
        ++result.steps;
    }
    if (m_accumulatedSeconds >= m_stepSeconds)
    {
        // Over the cap: keep only the part of a step, so the next frames start fresh instead of catching up.
        m_accumulatedSeconds = std::fmod(m_accumulatedSeconds, m_stepSeconds);
        result.droppedTime = true;
    }
    return result;
}

double FixedTimestep::alpha() const
{
    return m_accumulatedSeconds / m_stepSeconds;
}

double FixedTimestep::accumulatedSeconds() const
{
    return m_accumulatedSeconds;
}

double FixedTimestep::stepSeconds() const
{
    return m_stepSeconds;
}

std::uint32_t FixedTimestep::maxStepsPerFrame() const
{
    return m_maxStepsPerFrame;
}
} // namespace Meow2::Time
