/// @file
/// @brief Meow2Renderer's Console window: the log, live, from the engine's ConsoleBuffer.

#pragma once

#include "UiTheme.h"

#include <Meow2/Core/Log/ConsoleBuffer.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <string_view>

namespace Meow2::RendererApp
{
/// @brief Which lines the console shows. Plain data.
struct ConsoleFilter
{
    std::array<bool, 5> levels{true, true, true, true, true}; ///< Per Log::LogLevel, in its order: shown or not.
    std::string text{}; ///< Lines must contain this (in the logger name or message, any case); empty for all.
};

/// @brief Whether a line passes a filter.
/// @param line   The line.
/// @param filter The filter.
/// @return True if its level is shown and it contains the filter's text.
[[nodiscard]] bool consoleLineMatches(const Log::LogLine& line, const ConsoleFilter& filter);

/// @brief The Console window: every line the app logs, coloured by level, with filters, copy and clear.
///
/// Each frame it copies the lines added since the last frame out of the ConsoleBuffer (one short lock), then draws
/// from its own copy, so logging threads never wait for the UI.
/// @par Thread safety
/// Not thread-safe; use on the main thread. The buffer itself may be written from any thread.
/// @par Lifetime
/// The ConsoleBuffer must outlive the window.
class ConsoleWindow
{
public:
    /// @brief A window over a buffer, showing what's already in it.
    /// @param buffer The app's log lines.
    explicit ConsoleWindow(const Log::ConsoleBuffer& buffer);

    /// @brief Copies the new lines out of the buffer. draw() calls it; tests call it directly.
    void update();

    /// @brief Draws the window.
    /// @param fonts The theme's fonts: lines are drawn in the monospace one.
    void draw(const UiFonts& fonts = {});

    /// @brief The lines it holds, oldest first (at most the buffer's capacity).
    /// @return The lines.
    [[nodiscard]] const std::deque<Log::LogLine>& lines() const
    {
        return m_lines;
    }

    /// @brief How many lines were lost because the buffer filled up between two updates.
    /// @return The count since the window was made or cleared.
    [[nodiscard]] std::uint64_t missedLines() const
    {
        return m_missed;
    }

    /// @brief The filter, for the window's controls and tests.
    /// @return It, to change.
    [[nodiscard]] ConsoleFilter& filter()
    {
        return m_filter;
    }

    /// @brief Forgets the lines shown so far; new ones keep coming.
    void clear();

private:
    const Log::ConsoleBuffer& m_buffer;   ///< Where lines come from.
    std::deque<Log::LogLine> m_lines;     ///< Copies, oldest first.
    std::uint64_t m_next = 0;             ///< Sequence of the next line to copy.
    std::uint64_t m_missed = 0;           ///< Lines overwritten before they were copied.
    ConsoleFilter m_filter;               ///< What's shown.
    std::array<char, 256> m_filterText{}; ///< ImGui's edit buffer for the filter text.
    bool m_followNewest = true;           ///< Keep the newest line in view.
};
} // namespace Meow2::RendererApp
