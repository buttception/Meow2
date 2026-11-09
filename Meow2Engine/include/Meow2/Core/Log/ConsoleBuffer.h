/// @file
/// @brief ConsoleBuffer: the most recent log lines in memory, for in-app consoles.

#pragma once

#include <Meow2/Core/Log/Logger.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace Meow2::Log
{
/// @brief One log line as a console shows it. Plain data.
struct LogLine
{
    std::uint64_t sequence = 0;      ///< Position among every line ever added, from 0; tells a reader what's new.
    std::string time{};              ///< Local time, `hh:mm:ss.mmm`.
    LogLevel level = LogLevel::Info; ///< Its level.
    std::string logger{};            ///< The channel's name, e.g. `Engine`.
    std::string message{};           ///< The text, without the time, level and name.
};

/// @brief The last lines a LogSystem wrote, kept in a ring of fixed capacity for in-app consoles.
///
/// The Renderer's Console window and the editor's console dock read it; a LogSystem writes into it through a sink
/// (LogSystemDesc::consoleBuffer). Once full, each new line replaces the oldest.
///
/// A reader remembers the sequence after the last line it got and asks for what came since; if it fell more than
/// the capacity behind, it's told how many lines it missed.
///
/// **All its memory is taken at construction:** a fixed slot per line, so add() never allocates. Logging stays cheap
/// on every thread, and the buffer's size never changes while the app runs, so an app's leak check (made after the
/// buffer, because it needs a logger) doesn't count the lines it still holds at exit. A message longer than
/// c_maxMessageBytes is cut, ending in `…`; the log file keeps it whole.
/// @par Thread safety
/// Thread-safe: add() and linesSince() take one internal mutex for the time of a copy; any thread may log while the
/// main thread reads.
/// @par Lifetime
/// Owned by the composition root. It must outlive the LogSystem and every Logger writing into it.
class ConsoleBuffer
{
public:
    /// @brief The longest message a line keeps, in bytes of UTF-8.
    static constexpr std::size_t c_maxMessageBytes = 1000;
    /// @brief The longest logger name a line keeps, in bytes.
    static constexpr std::size_t c_maxLoggerBytes = 31;

    /// @brief An empty buffer, with the memory for all its lines (about 1 KB each).
    /// @param capacity How many lines it keeps; at least 1.
    explicit ConsoleBuffer(std::size_t capacity = 2000);

    /// @brief Adds a line, replacing the oldest when full. Its sequence is assigned here. Doesn't allocate.
    /// @param level   Its level.
    /// @param time    Local time, `hh:mm:ss.mmm` (longer is cut).
    /// @param logger  The channel's name (cut at c_maxLoggerBytes).
    /// @param message The text (cut at c_maxMessageBytes, on a UTF-8 character boundary, ending in `…`).
    void add(LogLevel level, std::string_view time, std::string_view logger, std::string_view message);

    /// @brief Adds a line given as a LogLine (its `sequence` is ignored); see the other overload.
    /// @param line The line.
    void add(const LogLine& line)
    {
        add(line.level, line.time, line.logger, line.message);
    }

    /// @brief Copies the lines added from `sequence` on, oldest first.
    /// @param sequence    The next sequence the reader hasn't seen: 0 at first, then nextSequence() of the last call.
    /// @param[out] lines  Receives the lines (appended).
    /// @return How many lines after `sequence` were already overwritten, so the reader missed them; usually 0.
    std::uint64_t linesSince(std::uint64_t sequence, std::vector<LogLine>& lines) const;

    /// @brief The sequence the next added line will get, i.e. how many lines were ever added.
    /// @return The count.
    [[nodiscard]] std::uint64_t nextSequence() const;

    /// @brief How many lines it keeps.
    /// @return The capacity given to the constructor (at least 1).
    [[nodiscard]] std::size_t capacity() const
    {
        return m_slots.size();
    }

private:
    /// @brief One line's storage, fixed size, so the ring never allocates after construction.
    struct Slot
    {
        LogLevel level = LogLevel::Info;                   ///< Its level.
        std::uint8_t timeLength = 0;                       ///< Bytes used in `time`.
        std::uint8_t loggerLength = 0;                     ///< Bytes used in `logger`.
        std::uint16_t messageLength = 0;                   ///< Bytes used in `message`.
        std::array<char, 12> time{};                       ///< `hh:mm:ss.mmm`.
        std::array<char, c_maxLoggerBytes> logger{};       ///< The channel's name.
        std::array<char, c_maxMessageBytes + 3> message{}; ///< The text, plus room for `…` when it's cut.
    };

    mutable std::mutex m_mutex; ///< Guards everything below.
    std::vector<Slot> m_slots;  ///< The ring; slot `sequence % capacity` holds that line.
    std::uint64_t m_next = 0;   ///< Sequence of the next line.
};
} // namespace Meow2::Log
