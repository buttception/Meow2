#pragma once

/// @file
/// @brief ConsoleDock: the editor's log console, a dock over the engine's ConsoleBuffer.

#include <Meow2/Core/Log/ConsoleBuffer.h>

#include <array>
#include <cstdint>
#include <deque>
#include <QColor>
#include <QDockWidget>
#include <QString>

class QCheckBox;
class QLineEdit;
class QPlainTextEdit;

namespace Meow2::Editor
{
/// @brief The colour a level's tag is drawn in; the rest of the line keeps the normal text colour (as PowerShell
///        colours only `WARNING:` and `ERROR:`).
/// @param level The level.
/// @return Grey for debug, the text colour for info, yellow for warnings, the bright accent red for errors.
[[nodiscard]] QColor consoleLevelColor(Log::LogLevel level);

/// @brief A line as the log file has it: `[time]<level> logger: message`.
/// @param line The line.
/// @return The text.
[[nodiscard]] QString formatConsoleLine(const Log::LogLine& line);

/// @brief The log console: the last lines every logger wrote, filtered by level and text.
///
/// It doesn't subscribe to anything: tick() (called every frame on the GUI thread) copies the lines added since the
/// last call out of the ConsoleBuffer, which any thread may write to. So no Qt signal ever crosses threads, and a
/// burst of logging costs one copy per frame.
///
/// Filters, Clear and Follow are how the dock *shows* the log, not editor state, so they don't go through commands;
/// an agent reads the log with the `log.lines` query instead.
/// @par Thread safety
/// Not thread-safe; GUI thread only, like every QWidget.
/// @par Lifetime
/// The ConsoleBuffer must outlive it.
class ConsoleDock final : public QDockWidget
{
public:
    /// @brief Creates the dock, empty until the first tick().
    /// @param buffer       The lines to show.
    /// @param followNewest Whether it stays scrolled to the newest line (`editor.console.followNewest`).
    /// @param parent       Owning widget, usually the main window.
    ConsoleDock(const Log::ConsoleBuffer& buffer, bool followNewest, QWidget* parent = nullptr);

    /// @brief Takes the lines added since the last call and shows those that pass the filters.
    void tick();

    /// @brief Shows or hides a level's lines.
    /// @param level The level.
    /// @param shown Whether its lines are shown.
    void setLevelShown(Log::LogLevel level, bool shown);

    /// @brief Shows only lines whose message or logger contain `text`, ignoring case; empty shows all.
    /// @param text The filter.
    void setFilterText(const QString& text);

    /// @brief Forgets every line taken so far; new ones still arrive.
    void clear();

    /// @brief Copies the shown lines (or the selection, if there is one) to the clipboard.
    void copy();

    /// @brief The text area, for tests and for the window to focus.
    /// @return The read-only text area, owned by the dock.
    [[nodiscard]] QPlainTextEdit& view()
    {
        return *m_view;
    }

private:
    /// @brief Whether a line passes the level and text filters.
    [[nodiscard]] bool passes(const Log::LogLine& line) const;

    /// @brief Appends one line: the level tag in its colour, the rest in the normal text colour.
    void appendLine(const Log::LogLine& line);

    /// @brief Shows every kept line again after a filter changed.
    void rebuild();

    const Log::ConsoleBuffer& m_buffer;
    std::deque<Log::LogLine> m_lines; ///< Lines taken so far, at most the buffer's capacity.
    std::uint64_t m_next = 0;         ///< The next sequence to ask the buffer for.
    std::array<bool, 5> m_levelShown{true, true, true, true, true}; ///< By Log::LogLevel.
    QString m_filterText;
    QPlainTextEdit* m_view = nullptr;         ///< Owned by Qt (parent–child).
    QLineEdit* m_filterEdit = nullptr;        ///< Owned by Qt.
    QCheckBox* m_follow = nullptr;            ///< Owned by Qt.
    std::array<QCheckBox*, 4> m_levelBoxes{}; ///< Debug to Error; fatal ends the program, so it has none.
};
} // namespace Meow2::Editor
