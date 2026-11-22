/// @file
/// @brief The last few seconds of frame times, for the stats windows of Meow2Renderer and the editor.

#pragma once

#include <array>
#include <cstddef>

namespace Meow2::Time
{
/// @brief A ring of the last 240 frame times (four seconds at 60 frames a second): average, worst, and the history
///        in order for a graph.
/// @par Thread safety
/// Not thread-safe.
class FrameTimes
{
public:
    /// @brief How many frames are kept.
    static constexpr std::size_t c_capacity = 240;

    /// @brief Adds a frame, dropping the oldest once full.
    /// @param milliseconds How long it took.
    void add(float milliseconds);

    /// @brief The average of the kept frames.
    /// @return Milliseconds; 0 before the first frame.
    [[nodiscard]] float average() const;

    /// @brief The slowest kept frame.
    /// @return Milliseconds; 0 before the first frame.
    [[nodiscard]] float worst() const;

    /// @brief How many frames are kept so far (at most c_capacity).
    /// @return The count.
    [[nodiscard]] std::size_t count() const
    {
        return m_count;
    }

    /// @brief The kept frames in order, oldest first, for a graph; zeros before the first ones while not yet full.
    /// @return A copy.
    [[nodiscard]] std::array<float, c_capacity> ordered() const;

private:
    std::array<float, c_capacity> m_times{};
    std::size_t m_next = 0;
    std::size_t m_count = 0;
};
} // namespace Meow2::Time
