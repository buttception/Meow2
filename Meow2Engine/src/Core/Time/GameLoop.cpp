/// @file
/// @brief GameLoop (GameLoop.h).

#include "Meow2/Core/Time/GameLoop.h"

#include "Meow2/Core/Profiling/Profile.h"

#include <utility>

namespace Meow2::Time
{
GameLoop::GameLoop(FrameClock& clock, Events::EventBus& events, const GameLoopDesc& desc, GameLoopCallbacks callbacks)
    : m_clock(clock)
    , m_events(events)
    , m_fixedTimestep(desc.fixedStepSeconds, desc.maxFixedStepsPerFrame)
    , m_callbacks(std::move(callbacks))
{
}

void GameLoop::tick()
{
    runFrame(m_clock.tick());
}

void GameLoop::tick(FrameClock::TimePoint now)
{
    runFrame(m_clock.tick(now));
}

void GameLoop::runFrame(double frameDeltaSeconds)
{
    {
        MEOW2_PROFILE_ZONE_NAMED("Events");
        if (m_callbacks.pumpEvents)
        {
            m_callbacks.pumpEvents();
        }
        m_events.dispatchQueued();
    }

    const FixedStepResult steps = m_fixedTimestep.advance(frameDeltaSeconds);
    m_droppedTime = steps.droppedTime;
    if (m_callbacks.fixedUpdate)
    {
        MEOW2_PROFILE_ZONE_NAMED("Fixed update");
        for (std::uint32_t i = 0; i < steps.steps; ++i)
        {
            m_callbacks.fixedUpdate(m_fixedTimestep.stepSeconds());
        }
    }
    m_fixedStepCount += steps.steps;

    {
        MEOW2_PROFILE_ZONE_NAMED("Update");
        if (m_callbacks.update)
        {
            m_callbacks.update(frameDeltaSeconds);
        }
        // Events raised by the simulation and update reach their handlers before the frame is drawn.
        m_events.dispatchQueued();
    }

    if (m_callbacks.extract)
    {
        MEOW2_PROFILE_ZONE_NAMED("Extract");
        m_callbacks.extract();
    }
    if (m_callbacks.render)
    {
        MEOW2_PROFILE_ZONE_NAMED("Render");
        m_callbacks.render(m_fixedTimestep.alpha());
    }
    MEOW2_PROFILE_FRAME_MARK();
}

std::uint64_t GameLoop::fixedStepCount() const
{
    return m_fixedStepCount;
}

bool GameLoop::droppedTime() const
{
    return m_droppedTime;
}

const FixedTimestep& GameLoop::fixedTimestep() const
{
    return m_fixedTimestep;
}
} // namespace Meow2::Time
