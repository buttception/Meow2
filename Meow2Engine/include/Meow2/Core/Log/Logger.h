#pragma once

/// @file
/// @brief Logger: a named logging channel (`Engine`, `Renderer`, …) and the `MEOW2_LOG_*` macros.
///
/// Lines look like `[14:03:27.512]<warn> Engine: Swapchain recreated (1600x900)`: local time with milliseconds,
/// the level, the logger name, then the message. Messages use `std::format` syntax (`{}` placeholders), checked at
/// compile time.
///
/// @code
/// MEOW2_LOG_INFO(m_log, "Window created ({}x{})", width, height);
/// MEOW2_LOG_DEBUG(m_log, "Visible instances: {}", countVisible());   // removed from Release, args not evaluated
/// MEOW2_LOG_FATAL(m_log, "Device lost: {}", reason);                  // logs, flushes, then ends the program
/// @endcode
///
/// Loggers are created by a LogSystem (see LogSystem.h) and injected into the classes that use them.
/// @see techstack.md §7

#include <Meow2/Core/Platform.h>

#include <format>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

/// @def MEOW2_LOG_DEBUG_ENABLED
/// @brief 1 when `MEOW2_LOG_DEBUG` produces output: Debug and RelWithDebInfo by default, 0 in Release.
///
/// Can be forced (0 or 1) with a compile definition.

#ifndef MEOW2_LOG_DEBUG_ENABLED
#if MEOW2_CONFIG_DEBUG || MEOW2_CONFIG_RELWITHDEBINFO
#define MEOW2_LOG_DEBUG_ENABLED 1
#else
#define MEOW2_LOG_DEBUG_ENABLED 0
#endif
#endif

/// @brief Logging: LogSystem, Logger and the assert-to-log route.
namespace Meow2::Log
{
/// @brief `true` when `MEOW2_LOG_DEBUG` produces output (see MEOW2_LOG_DEBUG_ENABLED).
inline constexpr bool c_debugLoggingEnabled = MEOW2_LOG_DEBUG_ENABLED == 1;

/// @brief Severity of a log message, lowest to highest. Printed as `debug`, `info`, `warn`, `error`, `fatal`.
enum class LogLevel
{
    Debug, ///< Details for developers. Compiled out of Release when logged through `MEOW2_LOG_DEBUG`.
    Info,  ///< Normal events worth knowing about (startup, a level loaded).
    Warn,  ///< Something unexpected that the program handled.
    Error, ///< An operation failed; the program keeps running.
    Fatal  ///< The program can't continue. Logging at this level ends the program (Logger::fatal).
};

class LogSystem;

/// @brief A named logging channel. All loggers of one LogSystem write to the same sinks (file, console).
///
/// Created by LogSystem::createLogger(), owned by the composition root, and passed by reference to the classes that
/// log (dependency injection, techstack.md §18). Move-only.
/// @par Thread safety
/// Thread-safe: any thread may log through the same Logger at the same time (the sinks lock internally).
/// @par Lifetime
/// A Logger keeps its sinks alive, so it may outlive the LogSystem that created it, but log files are flushed and
/// closed only when the last Logger using them is gone.
class Logger
{
public:
    /// @brief Releases the channel; flushes buffered output if this was the last user of the sinks.
    ~Logger();

    /// @brief Moves the channel; the moved-from Logger must not be used afterwards.
    /// @param other Logger to take the channel from.
    Logger(Logger&& other) noexcept;

    /// @brief Moves the channel; the moved-from Logger must not be used afterwards.
    /// @param other Logger to take the channel from.
    /// @return `*this`.
    Logger& operator=(Logger&& other) noexcept;

    Logger(const Logger&) = delete;            ///< Not copyable: pass by reference.
    Logger& operator=(const Logger&) = delete; ///< Not copyable: pass by reference.

    /// @brief The name shown in every line, e.g. `"Engine"`.
    /// @return The logger's name.
    [[nodiscard]] const std::string& name() const;

    /// @brief Logs at LogLevel::Debug. Prefer `MEOW2_LOG_DEBUG`, which is removed from Release builds.
    /// @tparam Args  Types of the format arguments.
    /// @param  format `std::format` string, checked at compile time.
    /// @param  args   Values for the `{}` placeholders.
    template <class... Args>
    void debug(std::format_string<Args...> format, Args&&... args)
    {
        write(LogLevel::Debug, std::format(format, std::forward<Args>(args)...));
    }

    /// @brief Logs at LogLevel::Info.
    /// @tparam Args  Types of the format arguments.
    /// @param  format `std::format` string, checked at compile time.
    /// @param  args   Values for the `{}` placeholders.
    template <class... Args>
    void info(std::format_string<Args...> format, Args&&... args)
    {
        write(LogLevel::Info, std::format(format, std::forward<Args>(args)...));
    }

    /// @brief Logs at LogLevel::Warn. Warnings and higher are flushed to disk immediately.
    /// @tparam Args  Types of the format arguments.
    /// @param  format `std::format` string, checked at compile time.
    /// @param  args   Values for the `{}` placeholders.
    template <class... Args>
    void warn(std::format_string<Args...> format, Args&&... args)
    {
        write(LogLevel::Warn, std::format(format, std::forward<Args>(args)...));
    }

    /// @brief Logs at LogLevel::Error. Flushed to disk immediately.
    /// @tparam Args  Types of the format arguments.
    /// @param  format `std::format` string, checked at compile time.
    /// @param  args   Values for the `{}` placeholders.
    template <class... Args>
    void error(std::format_string<Args...> format, Args&&... args)
    {
        write(LogLevel::Error, std::format(format, std::forward<Args>(args)...));
    }

    /// @brief Logs at LogLevel::Fatal, flushes every sink, then ends the program.
    ///
    /// With a debugger attached it first breaks (`MEOW2_DEBUG_BREAK`), then calls `std::abort()`, so the crash
    /// handler (once installed) writes a dump.
    /// @tparam Args  Types of the format arguments.
    /// @param  format `std::format` string, checked at compile time.
    /// @param  args   Values for the `{}` placeholders.
    template <class... Args>
    [[noreturn]] void fatal(std::format_string<Args...> format, Args&&... args)
    {
        writeFatal(std::format(format, std::forward<Args>(args)...));
    }

    /// @brief Logs an already formatted message. LogLevel::Fatal is handled like writeFatal().
    /// @param level   Severity.
    /// @param message The text, without line prefix or newline.
    void write(LogLevel level, std::string_view message);

    /// @brief Logs an already formatted message at LogLevel::Fatal, flushes, then ends the program.
    /// @param message The text, without line prefix or newline.
    [[noreturn]] void writeFatal(std::string_view message);

    /// @brief Writes everything buffered so far to the sinks (file and console).
    void flush();

private:
    friend class LogSystem;

    struct Impl;

    /// @brief Used by LogSystem only.
    /// @param impl The channel implementation.
    explicit Logger(std::unique_ptr<Impl> impl);

    std::unique_ptr<Impl> m_impl;
};
} // namespace Meow2::Log

/// @def MEOW2_LOG_DEBUG(logger, ...)
/// @brief Logs at `debug` level. **Removed from Release builds:** the arguments aren't evaluated there.
/// @param logger A Meow2::Log::Logger.
/// @param ...    `std::format` string and its arguments.

/// @def MEOW2_LOG_INFO(logger, ...)
/// @brief Logs at `info` level.
/// @param logger A Meow2::Log::Logger.
/// @param ...    `std::format` string and its arguments.

/// @def MEOW2_LOG_WARN(logger, ...)
/// @brief Logs at `warn` level (flushed immediately).
/// @param logger A Meow2::Log::Logger.
/// @param ...    `std::format` string and its arguments.

/// @def MEOW2_LOG_ERROR(logger, ...)
/// @brief Logs at `error` level (flushed immediately).
/// @param logger A Meow2::Log::Logger.
/// @param ...    `std::format` string and its arguments.

/// @def MEOW2_LOG_FATAL(logger, ...)
/// @brief Logs at `fatal` level, flushes, then ends the program (Meow2::Log::Logger::fatal). Never returns.
/// @param logger A Meow2::Log::Logger.
/// @param ...    `std::format` string and its arguments.

#if MEOW2_LOG_DEBUG_ENABLED
#define MEOW2_LOG_DEBUG(logger, ...) (logger).debug(__VA_ARGS__)
#else
// sizeof: `logger` is still compiled (and counts as used), the arguments are never evaluated.
#define MEOW2_LOG_DEBUG(logger, ...) static_cast<void>(sizeof(logger))
#endif
#define MEOW2_LOG_INFO(logger, ...) (logger).info(__VA_ARGS__)
#define MEOW2_LOG_WARN(logger, ...) (logger).warn(__VA_ARGS__)
#define MEOW2_LOG_ERROR(logger, ...) (logger).error(__VA_ARGS__)
#define MEOW2_LOG_FATAL(logger, ...) (logger).fatal(__VA_ARGS__)
