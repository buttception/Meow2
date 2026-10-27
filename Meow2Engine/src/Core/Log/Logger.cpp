/// @file
/// @brief Logger implementation over spdlog.

#include "Core/Log/LoggerImpl.h"
#include "Meow2/Core/Debugger.h"

#include <cstdlib>
#include <utility>

namespace Meow2::Log
{
spdlog::level::level_enum toSpdlogLevel(LogLevel level)
{
    switch (level)
    {
        case LogLevel::Debug:
            return spdlog::level::debug;
        case LogLevel::Info:
            return spdlog::level::info;
        case LogLevel::Warn:
            return spdlog::level::warn;
        case LogLevel::Error:
            return spdlog::level::err;
        case LogLevel::Fatal:
            return spdlog::level::critical;
    }
    return spdlog::level::critical;
}

Logger::Logger(std::unique_ptr<Impl> impl)
    : m_impl(std::move(impl))
{
}

Logger::~Logger() = default;
Logger::Logger(Logger&& other) noexcept = default;
Logger& Logger::operator=(Logger&& other) noexcept = default;

const std::string& Logger::name() const
{
    return m_impl->name;
}

void Logger::write(LogLevel level, std::string_view message)
{
    if (level == LogLevel::Fatal)
    {
        writeFatal(message);
    }
    // string_view_t selects spdlog's non-template overload: the message is printed as-is, never
    // re-interpreted as a format string (so "{}" inside an already formatted message is safe).
    m_impl->logger->log(toSpdlogLevel(level), spdlog::string_view_t{message.data(), message.size()});
}

void Logger::writeFatal(std::string_view message)
{
    m_impl->logger->log(spdlog::level::critical, spdlog::string_view_t{message.data(), message.size()});
    m_impl->logger->flush(); // all sinks are shared, so this flushes every logger's output

    if (isDebuggerAttached())
    {
        MEOW2_DEBUG_BREAK();
    }
    // The crash handler (later in M1) turns this into a .dmp next to the log file.
    std::abort();
}

void Logger::flush()
{
    m_impl->logger->flush();
}
} // namespace Meow2::Log
