#pragma once

/// @file
/// @brief FixedTimestep: turns variable frame times into a whole number of constant simulation steps.
///
/// @code
/// Meow2::Time::FixedTimestep timestep{1.0 / 60.0, 8};
/// const Meow2::Time::FixedStepResult result = timestep.advance(frameDt);
/// for (std::uint32_t i = 0; i < result.steps; ++i)
/// {
///     fixedUpdate(timestep.stepSeconds()); // physics, gameplay: always the same dt
/// }
/// render(timestep.alpha()); // interpolate previous -> current state by alpha
/// @endcode
/// @see techstack.md §23, docs/wiki/GameLoop.md, Glenn Fiedler, "Fix Your Timestep!"

#include <cstdint>

namespace Meow2::Time
{
/// @brief What one FixedTimestep::advance() decided. Plain data.
struct FixedStepResult
{
    std::uint32_t steps = 0;  ///< How many fixed steps to run this frame (0..maxStepsPerFrame).
    bool droppedTime = false; ///< More steps were due than the cap allows; the excess time was thrown away.
};

/// @brief Accumulates frame time and hands it out in steps of a constant length (the accumulator pattern).
///
/// Each frame, advance() adds the frame's delta to an accumulator and takes out as many whole steps as fit. The
/// remainder (less than one step) carries over to the next frame, so over time every second of real time becomes
/// exactly one second of simulated time. alpha() is how far the accumulator is into the next step, 0..1, used to
/// interpolate between the last two simulation states when rendering.
///
/// **Step cap:** if a frame would need more than `maxStepsPerFrame` steps (the machine can't keep up, or a long
/// stall), only that many run and the excess is dropped, keeping just the fraction below one step. The game then
/// runs slower than real time for that frame instead of falling ever further behind (the "spiral of death").
/// @par Thread safety
/// Not thread-safe; owned by the game loop on the main thread.
class FixedTimestep
{
public:
    /// @brief Creates an empty accumulator.
    /// @param stepSeconds      Length of one fixed step in seconds, e.g. 1/60.
    /// @param maxStepsPerFrame Most steps advance() returns for one frame.
    /// @pre `stepSeconds > 0` and `maxStepsPerFrame > 0` (asserted).
    FixedTimestep(double stepSeconds, std::uint32_t maxStepsPerFrame);

    /// @brief Adds one frame's time and says how many fixed steps to run for it.
    /// @param frameDeltaSeconds The frame's (clamped) delta in seconds; negative counts as 0.
    /// @return The number of steps, and whether time was dropped because of the cap.
    FixedStepResult advance(double frameDeltaSeconds);

    /// @brief How far the accumulated time is into the next step.
    /// @return A value in [0, 1): 0 right on a step boundary, 0.5 halfway to the next step.
    [[nodiscard]] double alpha() const;

    /// @brief Time waiting in the accumulator for the next step.
    /// @return Seconds, in [0, stepSeconds()).
    [[nodiscard]] double accumulatedSeconds() const;

    /// @brief The length of one fixed step.
    /// @return Seconds.
    [[nodiscard]] double stepSeconds() const;

    /// @brief The step cap.
    /// @return Most steps per frame.
    [[nodiscard]] std::uint32_t maxStepsPerFrame() const;

private:
    double m_stepSeconds;
    std::uint32_t m_maxStepsPerFrame;
    double m_accumulatedSeconds = 0.0;
};
} // namespace Meow2::Time
