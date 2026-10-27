/// @file
/// @brief ScopedAssertLogging: an assert handler that logs, flushes, then defers to the default handler.

#include "Meow2/Core/Log/AssertLogging.h"

#include <atomic>

namespace Meow2::Log
{
namespace
{
// GLOBAL: an AssertHandler is a plain function pointer without user data, so the logger it
// writes to has to be reachable globally. Set and restored only by ScopedAssertLogging
// (created by the composition root); atomic, because asserts can fail on any thread.
std::atomic<Logger*> s_assertLogger{nullptr};

AssertAction logThenDefault(const AssertInfo& info)
{
    if (Logger* const logger = s_assertLogger.load())
    {
        if (info.message[0] != '\0')
        {
            logger->error("Assertion failed: {} ({}) at {}({}) in {}", info.expression, info.message, info.file,
                          info.line, info.function);
        }
        else
        {
            logger->error("Assertion failed: {} at {}({}) in {}", info.expression, info.file, info.line, info.function);
        }
        logger->flush(); // the next step is usually abort()
    }
    return defaultAssertHandler(info);
}
} // namespace

ScopedAssertLogging::ScopedAssertLogging(Logger& logger)
{
    m_previousLogger = s_assertLogger.exchange(&logger);
    m_previousHandler = setAssertHandler(&logThenDefault);
}

ScopedAssertLogging::~ScopedAssertLogging()
{
    setAssertHandler(m_previousHandler);
    s_assertLogger.store(m_previousLogger);
}
} // namespace Meow2::Log
