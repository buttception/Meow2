#pragma once

/// @file
/// @brief ScopedAssertLogging: writes failed asserts to a Logger (and flushes) before the usual break/abort.

#include <Meow2/Core/Assert.h>
#include <Meow2/Core/Log/Logger.h>

namespace Meow2::Log
{
/// @brief While alive, failed asserts are also written to a Logger at `error` level and flushed.
///
/// Installs an AssertHandler (setAssertHandler()) that logs `Assertion failed: <expression> (<message>) at
/// <file>(<line>) in <function>`, flushes, and then behaves like defaultAssertHandler() (stderr, then break with a
/// debugger or abort without one). The destructor restores the previous handler. Create it in the composition root,
/// right after the engine logger:
/// @code
/// Meow2::Log::Logger engineLog = logSystem.createLogger("Engine");
/// Meow2::Log::ScopedAssertLogging assertLogging{engineLog};
/// @endcode
/// @par Thread safety
/// Asserts may fail on any thread; the logger is thread-safe. Create and destroy it on one thread, and not while
/// another thread is replacing the assert handler.
/// @par Lifetime
/// The Logger must outlive this object.
class ScopedAssertLogging
{
public:
    /// @brief Starts routing failed asserts to `logger`.
    /// @param logger Logger that receives the failures. Must outlive this object.
    explicit ScopedAssertLogging(Logger& logger);

    /// @brief Restores the assert handler (and logger) that were active before.
    ~ScopedAssertLogging();

    ScopedAssertLogging(const ScopedAssertLogging&) = delete;            ///< Not copyable.
    ScopedAssertLogging& operator=(const ScopedAssertLogging&) = delete; ///< Not copyable.
    ScopedAssertLogging(ScopedAssertLogging&&) = delete;                 ///< Not movable.
    ScopedAssertLogging& operator=(ScopedAssertLogging&&) = delete;      ///< Not movable.

private:
    AssertHandler m_previousHandler = nullptr;
    Logger* m_previousLogger = nullptr;
};
} // namespace Meow2::Log
