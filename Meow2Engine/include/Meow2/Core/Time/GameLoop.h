#pragma once

/// @file
/// @brief GameLoop: one frame of the engine, in fixed phases: events, fixed-rate simulation, update, render.
///
/// @code
/// Meow2::Time::FrameClock clock;
/// Meow2::Time::GameLoop loop{clock, events, Meow2::Time::gameLoopDescFromConfig(config, engineLog),
///                            {.pumpEvents = [&] { platform.pumpEvents(); },
///                             .fixedUpdate = [&](double dt) { physics.step(dt); },
///                             .update = [&](double dt) { camera.update(dt); },
///                             .render = [&](double alpha) { renderer.render(alpha); }}};
/// while (running)
/// {
///     loop.tick();
/// }
/// @endcode
/// @see techstack.md §23, docs/wiki/GameLoop.md

#include <Meow2/Core/Events/EventBus.h>
#include <Meow2/Core/Time/FixedTimestep.h>
#include <Meow2/Core/Time/FrameClock.h>

#include <cstdint>
#include <functional>

namespace Meow2::Time
{
/// @brief How the loop steps the simulation. Plain data; usually from gameLoopDescFromConfig().
struct GameLoopDesc
{
    double fixedStepSeconds = 1.0 / 60.0;    ///< Length of one fixedUpdate step, in seconds.
    std::uint32_t maxFixedStepsPerFrame = 8; ///< Most fixedUpdate calls per frame (see FixedTimestep).
};

/// @brief What the loop calls in each phase. Plain data; any callback may be empty, and is then skipped.
struct GameLoopCallbacks
{
    /// @brief Collects OS events (e.g. Platform::pumpEvents()); the events they enqueue are delivered right after.
    std::function<void()> pumpEvents;

    /// @brief Simulation at a constant rate (physics, gameplay); called 0..n times per frame.
    /// The parameter is the fixed step in seconds, the same every call.
    std::function<void(double)> fixedUpdate;

    /// @brief Once per frame with the frame's clamped delta in seconds (camera, animation, UI).
    std::function<void(double)> update;

    /// @brief Draws the frame. The parameter is alpha, 0..1: how far real time is between the last two fixed
    /// steps, for interpolating between their states.
    std::function<void(double)> render;
};

/// @brief Runs one frame per tick(): measure, pump, dispatch, fixedUpdate × n, update, dispatch, render.
///
/// The order of one tick() (techstack.md §23):
/// 1. FrameClock::tick(): the frame's delta, clamped.
/// 2. `pumpEvents`, then EventBus::dispatchQueued(): window (and later input) events arrive.
/// 3. `fixedUpdate(step)` as many times as FixedTimestep::advance() says.
/// 4. `update(frameDelta)`, then EventBus::dispatchQueued(): events raised by the simulation and update arrive.
/// 5. `render(alpha)`.
/// 6. The profiler's frame mark.
///
/// It doesn't own the outer loop: Player and Renderer call `while (running) loop.tick();`, and the editor calls
/// tick() from a Qt timer, because Qt owns its event loop. It doesn't pace frames either: that's the swapchain's
/// vsync (M5); until then the apps' `pumpEvents` sleeps while idle.
/// @par Thread safety
/// Not thread-safe; main thread only. The callbacks run on the calling thread; they may start jobs.
/// @par Lifetime
/// The FrameClock and the EventBus must outlive the GameLoop.
class GameLoop
{
public:
    /// @brief Sets up the loop. Nothing runs until the first tick().
    /// @param clock     Measures the frames. Must outlive the loop; tick() advances it.
    /// @param events    Its queue is dispatched after the pump and after update. Must outlive the loop.
    /// @param desc      Fixed step and step cap.
    /// @param callbacks What each phase calls.
    GameLoop(FrameClock& clock, Events::EventBus& events, const GameLoopDesc& desc, GameLoopCallbacks callbacks);

    /// @brief Runs one frame, reading the time from the clock.
    void tick();

    /// @brief Runs one frame at time `now` (tests, replays).
    /// @param now The frame's time; see FrameClock::tick(TimePoint).
    void tick(FrameClock::TimePoint now);

    /// @brief How many fixedUpdate steps ran in total.
    /// @return The step count since the loop was created.
    [[nodiscard]] std::uint64_t fixedStepCount() const;

    /// @brief Whether the last tick() dropped simulation time because of the step cap.
    /// @return `true` if the frame needed more than GameLoopDesc::maxFixedStepsPerFrame steps.
    [[nodiscard]] bool droppedTime() const;

    /// @brief The accumulator, e.g. for its step length or alpha.
    /// @return The loop's FixedTimestep.
    [[nodiscard]] const FixedTimestep& fixedTimestep() const;

private:
    void runFrame(double frameDeltaSeconds);

    FrameClock& m_clock;
    Events::EventBus& m_events;
    FixedTimestep m_fixedTimestep;
    GameLoopCallbacks m_callbacks;
    std::uint64_t m_fixedStepCount = 0;
    bool m_droppedTime = false;
};
} // namespace Meow2::Time
