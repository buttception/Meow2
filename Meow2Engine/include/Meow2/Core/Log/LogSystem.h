#pragma once

/// @file
/// @brief LogSystem: owns the log sinks (file per run, console) and creates the Logger channels.
///
/// Typical composition root:
/// @code
/// using namespace Meow2::Log;
///
/// int main(int argc, char** argv)
/// {
///     const auto logDir = logDirectoryFromArguments(argc, argv).value_or(defaultLogDirectory());
///     LogSystem logSystem{{.appName = "Renderer", .directory = logDir}};
///     Logger engineLog = logSystem.createLogger("Engine");
///     Logger appLog = logSystem.createLogger("Renderer");
///     MEOW2_LOG_INFO(appLog, "Logging to {}", logSystem.filePath().string());
/// }
/// @endcode
/// @see techstack.md §7

#include <Meow2/Core/Log/Logger.h>

#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace Meow2::Log
{
/// @brief `true` when a LogSystem writes to the console by default: Debug and RelWithDebInfo.
inline constexpr bool c_consoleLoggingByDefault = MEOW2_CONFIG_DEBUG || MEOW2_CONFIG_RELWITHDEBINFO;

/// @brief How a LogSystem is set up. Plain data; every field has a default except where noted.
struct LogSystemDesc
{
    /// @brief Short app name used in the file name `Meow2<appName>_<yyyy-mm-dd_hh-mm-ss>.log`, e.g. `"Renderer"`.
    std::string appName = "App";

    /// @brief Folder for the log files. Created (with its parents) if missing. Usually defaultLogDirectory() or the
    ///        `--log-dir` argument (logDirectoryFromArguments()).
    std::filesystem::path directory = "logs";

    /// @brief Also print to standard output, in colour where the console supports it.
    bool console = c_consoleLoggingByDefault;

    /// @brief Size in bytes at which the file rotates (`….log` → `….1.log`).
    std::size_t maxFileBytes = std::size_t{10} * 1024 * 1024;

    /// @brief How many rotated files are kept in addition to the current one.
    std::size_t maxRotatedFiles = 5;

    /// @brief Lowest level written. `debug` messages need both this and MEOW2_LOG_DEBUG_ENABLED.
    LogLevel minimumLevel = c_debugLoggingEnabled ? LogLevel::Debug : LogLevel::Info;
};

/// @brief Owns the log sinks and creates the Logger channels that share them.
///
/// Create one per program, in the composition root, before anything that logs. If the log file can't be created
/// (no permission, bad path), the LogSystem still works with the console only, and reports it as a warning plus
/// an empty filePath(): logging must never stop the program from starting.
/// @par Thread safety
/// createLogger() and flush() may be called from any thread. Loggers are thread-safe.
class LogSystem
{
public:
    /// @brief Creates the sinks: the log file (and its folder) and, if requested, the console.
    /// @param desc How to set up the sinks.
    explicit LogSystem(const LogSystemDesc& desc);

    /// @brief Flushes all sinks. Loggers created by this LogSystem keep working (they share ownership of the sinks).
    ~LogSystem();

    LogSystem(const LogSystem&) = delete;            ///< Not copyable.
    LogSystem& operator=(const LogSystem&) = delete; ///< Not copyable.
    LogSystem(LogSystem&&) = delete;                 ///< Not movable: create it where it lives.
    LogSystem& operator=(LogSystem&&) = delete;      ///< Not movable: create it where it lives.

    /// @brief Creates a channel that writes to this system's sinks, e.g. `"Engine"` or `"Renderer"`.
    /// @param name Name shown in every line. Usually the binary or subsystem that logs.
    /// @return The new logger. Owned by the caller.
    [[nodiscard]] Logger createLogger(std::string_view name);

    /// @brief The log file of this run.
    /// @return Full path of the file, or an empty path if it couldn't be created (console only).
    [[nodiscard]] const std::filesystem::path& filePath() const;

    /// @brief Writes everything buffered so far to every sink.
    void flush();

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

/// @brief The default log folder: `logs` next to the running executable.
/// @return `<folder of the executable>/logs`, or `logs` relative to the working directory if the executable's
///         location can't be determined.
[[nodiscard]] std::filesystem::path defaultLogDirectory();

/// @brief Finds a `--log-dir <path>` or `--log-dir=<path>` command-line argument.
///
/// When the apps are started from the Visual Studio solution, CMake passes `--log-dir <repo>/logs`, so runs from the
/// IDE log into the repository's (git-ignored) `logs` folder.
/// @param argc Argument count, as given to `main`.
/// @param argv Arguments, as given to `main`. `argv[0]` (the program) is skipped.
/// @return The folder given on the command line, or `std::nullopt` if the option isn't there (or has no value).
[[nodiscard]] std::optional<std::filesystem::path> logDirectoryFromArguments(int argc, const char* const* argv);
} // namespace Meow2::Log
