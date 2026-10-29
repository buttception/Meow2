#pragma once

/// @file
/// @brief CrashHandler: writes a minidump (`.dmp`) and flushes the log when the program crashes.
///
/// A **minidump** is a snapshot of a crashed process: the call stacks and registers of every thread, the list of
/// loaded modules (with their versions), and selected memory. Opened in Visual Studio together with the matching
/// `.pdb` files, it shows the crash as if a debugger had been attached at that moment: source line, call stack,
/// local variables. It is written in every configuration, Release included, because Release is what players run.
///
/// @code
/// Meow2::Log::Logger engineLog = logSystem.createLogger("Engine");
/// std::filesystem::path dumpPath = logSystem.filePath();         // pair the dump with this run's log
/// dumpPath.replace_extension(".dmp");
/// const Meow2::Crash::CrashHandler crashHandler{{.dumpPath = dumpPath}, engineLog};
/// @endcode
///
/// Crashes it catches (Windows):
/// - unhandled structured exceptions: access violations, stack overflows, divide by zero, …;
/// - `std::abort()` / `SIGABRT`, which includes `MEOW2_LOG_FATAL` and failed asserts;
/// - `std::terminate` (e.g. an uncaught C++ exception), pure virtual function calls, and CRT invalid-parameter
///   errors.
///
/// With a debugger attached nothing is written: the debugger stops at the crash instead. On Linux and Android the
/// handler does nothing yet (isActive() is `false`); dumps there come later (crashpad/breakpad).
/// @see techstack.md §8, docs/wiki/CrashDumps.md

#include <Meow2/Core/Log/Logger.h>

#include <filesystem>
#include <memory>

/// @brief Crash handling: minidumps and log flushing on crashes.
namespace Meow2::Crash
{
/// @brief How a CrashHandler is set up. Plain data.
struct CrashHandlerDesc
{
    /// @brief Full path of the `.dmp` written on a crash, usually this run's log file with the extension `.dmp`.
    ///        Its folder is created if missing.
    std::filesystem::path dumpPath;
};

/// @brief While alive, a crash writes a minidump to CrashHandlerDesc::dumpPath, logs it and flushes the log.
///
/// Create one, in the composition root, right after the logging (it logs through the given Logger), and keep it
/// alive until the end of `main`. Only one may exist at a time (asserted). The destructor restores the handlers that
/// were installed before.
///
/// How a crash is handled (Windows): the crashing thread does as little as possible, because its stack may be
/// exhausted (stack overflow) and the heap may be corrupt. It records what happened and wakes a watchdog thread that
/// was created up front. The watchdog writes the dump with `MiniDumpWriteDump`, logs the path, flushes the log, and
/// then lets the crashing thread end the process. Crashes without an exception context (abort, terminate, …)
/// raise a private structured exception first, so the dump opens at the place of the crash.
/// @par Thread safety
/// Crashes on any thread are handled; if several threads crash at once, the first one writes the dump and the others
/// wait. Create and destroy the handler on the main thread.
/// @par Lifetime
/// The Logger must outlive the CrashHandler.
class CrashHandler
{
public:
    /// @brief Installs the crash hooks and starts the watchdog thread.
    /// @param desc Where to write the dump.
    /// @param log  Logger the crash is reported to (and flushed). Must outlive the handler.
    CrashHandler(const CrashHandlerDesc& desc, Log::Logger& log);

    /// @brief Stops the watchdog and restores the previously installed handlers.
    ~CrashHandler();

    CrashHandler(const CrashHandler&) = delete;            ///< Not copyable.
    CrashHandler& operator=(const CrashHandler&) = delete; ///< Not copyable.
    CrashHandler(CrashHandler&&) = delete;                 ///< Not movable: the OS hooks point at it.
    CrashHandler& operator=(CrashHandler&&) = delete;      ///< Not movable: the OS hooks point at it.

    /// @brief Whether crashes on this platform produce a dump.
    /// @return `true` on Windows; `false` on platforms without a backend yet (Linux, Android).
    [[nodiscard]] bool isActive() const;

    /// @brief The file a crash would be written to.
    /// @return CrashHandlerDesc::dumpPath as given.
    [[nodiscard]] const std::filesystem::path& dumpPath() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
} // namespace Meow2::Crash
