#pragma once

/// @file
/// @brief FrameClock: measures how long each frame took, clamped so a pause never becomes one huge step.
///
/// @code
/// Meow2::Time::FrameClock clock;                  // starts now
/// while (running)
/// {
///     const double frameDt = clock.tick();         // seconds since the previous tick, at most 0.25
///     update(clock.deltaSecondsFloat());           // gameplay takes a float
/// }
/// MEOW2_LOG_INFO(log, "{} frames in {:.2f} s", clock.frameCount(), clock.totalSeconds());
/// @endcode
/// @see techstack.md §23, docs/wiki/FrameClock.md

#include <chrono>
#include <cstdint>

namespace Meow2::Time
{
/// @brief Measures the time between frames with a monotonic clock, clamps it, and counts frames.
///
/// Call tick() once per frame, at the start of the frame. It returns how long the previous frame took, in seconds,
/// clamped to maxFrameDeltaSeconds(): a breakpoint, a window drag or a loading stall would otherwise produce a
/// multi-second step, and a fixed-timestep loop would then run hundreds of catch-up steps (the "spiral of death").
/// The unclamped value stays available for statistics.
///
/// Time comes from `std::chrono::steady_clock`, which never jumps back when the system clock is changed.
/// tick(TimePoint) takes the time explicitly, so tests (and replays) are deterministic.
/// @par Thread safety
/// Not thread-safe. The main thread owns it and passes the values on.
class FrameClock
{
public:
    /// @brief The clock time is read from.
    using Clock = std::chrono::steady_clock;

    /// @brief A point in time on Clock.
    using TimePoint = Clock::time_point;

    /// @brief Default limit for one frame's delta, in seconds (techstack.md §23).
    static constexpr double c_maxFrameDeltaSeconds = 0.25;

    /// @brief Starts the clock now.
    /// @param maxFrameDeltaSeconds Largest delta tick() returns, in seconds.
    /// @pre `maxFrameDeltaSeconds > 0` (asserted).
    explicit FrameClock(double maxFrameDeltaSeconds = c_maxFrameDeltaSeconds);

    /// @brief Starts the clock at a given time, e.g. a fixed point in a test.
    /// @param start                Time the first frame starts at.
    /// @param maxFrameDeltaSeconds Largest delta tick() returns, in seconds.
    /// @pre `maxFrameDeltaSeconds > 0` (asserted).
    FrameClock(TimePoint start, double maxFrameDeltaSeconds = c_maxFrameDeltaSeconds);

    /// @brief Ends the current frame and starts the next one, reading the time from Clock.
    /// @return The frame's delta in seconds, clamped to 0..maxFrameDeltaSeconds().
    double tick();

    /// @brief Ends the current frame and starts the next one at `now`.
    /// @param now Current time. A time before the previous tick counts as no time passing (delta 0).
    /// @return The frame's delta in seconds, clamped to 0..maxFrameDeltaSeconds().
    double tick(TimePoint now);

    /// @brief The delta of the last tick().
    /// @return Seconds, clamped; 0 before the first tick().
    [[nodiscard]] double deltaSeconds() const;

    /// @brief The delta of the last tick() as a float, for gameplay code (techstack.md §23).
    /// @return Seconds, clamped; 0 before the first tick().
    [[nodiscard]] float deltaSecondsFloat() const;

    /// @brief The real time between the last two ticks, before clamping, e.g. for frame-time statistics.
    /// @return Seconds; never negative.
    [[nodiscard]] double unclampedDeltaSeconds() const;

    /// @brief Whether the last tick() was cut down to maxFrameDeltaSeconds().
    /// @return `true` if the real delta was longer than the limit.
    [[nodiscard]] bool wasClamped() const;

    /// @brief How many times tick() has been called.
    /// @return Number of frames measured so far.
    [[nodiscard]] std::uint64_t frameCount() const;

    /// @brief The sum of all clamped deltas: game time, which doesn't jump after a pause.
    /// @return Seconds.
    [[nodiscard]] double totalSeconds() const;

    /// @brief The clamp limit.
    /// @return Seconds.
    [[nodiscard]] double maxFrameDeltaSeconds() const;

private:
    TimePoint m_previous;
    double m_maxFrameDeltaSeconds;
    double m_deltaSeconds = 0.0;
    double m_unclampedDeltaSeconds = 0.0;
    double m_totalSeconds = 0.0;
    std::uint64_t m_frameCount = 0;
};
} // namespace Meow2::Time
