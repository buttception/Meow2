/// @file
/// @brief LogSystem implementation: sinks, line format, file naming, log-folder helpers.

#include "Meow2/Core/Log/LogSystem.h"

#include "Core/CommandLine.h"
#include "Core/Log/LoggerImpl.h"
#include "Meow2/Core/Process.h"

#include <spdlog/details/os.h>
#include <spdlog/pattern_formatter.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <ctime>
#include <exception>
#include <format>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace Meow2::Log
{
namespace
{
/// @brief Pattern flag `%*`: our level names (`debug info warn error fatal`) instead of spdlog's
///        (`debug info warning error critical`).
class LevelNameFlag final : public spdlog::custom_flag_formatter
{
public:
    void format(const spdlog::details::log_msg& msg, const std::tm& /*time*/, spdlog::memory_buf_t& dest) override
    {
        const std::string_view name = levelName(msg.level);
        dest.append(name.data(), name.data() + name.size());
    }

    [[nodiscard]] std::unique_ptr<custom_flag_formatter> clone() const override
    {
        return std::make_unique<LevelNameFlag>();
    }

private:
    static std::string_view levelName(spdlog::level::level_enum level)
    {
        switch (level)
        {
            case spdlog::level::trace:
            case spdlog::level::debug:
                return "debug";
            case spdlog::level::info:
                return "info";
            case spdlog::level::warn:
                return "warn";
            case spdlog::level::err:
                return "error";
            case spdlog::level::critical:
                return "fatal";
            case spdlog::level::off:
            case spdlog::level::n_levels:
                break;
        }
        return "?";
    }
};

/// @brief `[14:03:27.512]<warn> Engine: message` (techstack.md §7). `%^…%$` marks the part the
///        console colours by level; file sinks ignore the markers.
std::unique_ptr<spdlog::formatter> makeLineFormatter()
{
    auto formatter = std::make_unique<spdlog::pattern_formatter>();
    formatter->add_flag<LevelNameFlag>('*').set_pattern("[%H:%M:%S.%e]<%^%*%$> %n: %v");
    return formatter;
}

/// @brief `yyyy-mm-dd_hh-mm-ss` in local time, for file names (no ':' allowed on Windows).
std::string fileTimestamp()
{
    const std::tm now = spdlog::details::os::localtime(std::time(nullptr));
    return std::format("{:04}-{:02}-{:02}_{:02}-{:02}-{:02}", now.tm_year + 1900, now.tm_mon + 1, now.tm_mday,
                       now.tm_hour, now.tm_min, now.tm_sec);
}
} // namespace

struct LogSystem::Impl
{
    std::vector<spdlog::sink_ptr> sinks;
    std::filesystem::path filePath; // empty: no file sink
    LogLevel minimumLevel = LogLevel::Info;
};

LogSystem::LogSystem(const LogSystemDesc& desc)
    : m_impl(std::make_unique<Impl>())
{
    m_impl->minimumLevel = desc.minimumLevel;

    std::string fileError;
    std::error_code directoryError;
    std::filesystem::create_directories(desc.directory, directoryError);
    if (directoryError)
    {
        fileError = std::format("can't create folder {}: {}", desc.directory.string(), directoryError.message());
    }
    else
    {
        const std::filesystem::path path =
            std::filesystem::absolute(desc.directory) / std::format("Meow2{}_{}.log", desc.appName, fileTimestamp());
        try
        {
            // filename_t is the platform's native path string (wchar_t on Windows, see
            // SPDLOG_WCHAR_FILENAMES in cmake/Meow2External.cmake), so no lossy conversion.
            m_impl->sinks.push_back(std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
                spdlog::filename_t{path.native()}, desc.maxFileBytes, desc.maxRotatedFiles));
            m_impl->filePath = path;
        }
        catch (const std::exception& error)
        {
            fileError = std::format("can't open {}: {}", path.string(), error.what());
        }
    }

    if (desc.console)
    {
        m_impl->sinks.push_back(std::make_shared<spdlog::sinks::stdout_color_sink_mt>());
    }

    for (const spdlog::sink_ptr& sink : m_impl->sinks)
    {
        sink->set_formatter(makeLineFormatter());
    }

    if (!fileError.empty())
    {
        // Logging must never stop the program from starting: carry on with the console only.
        createLogger("Log").warn("No log file ({}); logging to the console only", fileError);
    }
}

LogSystem::~LogSystem()
{
    flush();
}

Logger LogSystem::createLogger(std::string_view name)
{
    auto impl = std::make_unique<Logger::Impl>();
    impl->name = std::string{name};
    // Not registered in spdlog's global registry: loggers are owned and injected (techstack.md §18).
    impl->logger = std::make_shared<spdlog::logger>(impl->name, m_impl->sinks.begin(), m_impl->sinks.end());
    impl->logger->set_level(toSpdlogLevel(m_impl->minimumLevel));
    impl->logger->flush_on(spdlog::level::warn); // warnings and errors reach the disk even if we crash next
    return Logger{std::move(impl)};
}

const std::filesystem::path& LogSystem::filePath() const
{
    return m_impl->filePath;
}

void LogSystem::flush()
{
    for (const spdlog::sink_ptr& sink : m_impl->sinks)
    {
        sink->flush();
    }
}

std::filesystem::path defaultLogDirectory()
{
    const std::filesystem::path executable = executablePath();
    if (executable.empty())
    {
        return "logs";
    }
    return executable.parent_path() / "logs";
}

std::optional<std::filesystem::path> logDirectoryFromArguments(int argc, const char* const* argv)
{
    const std::vector<std::string_view> values = Detail::optionValues(argc, argv, "--log-dir");
    if (values.empty())
    {
        return std::nullopt;
    }
    return std::filesystem::path{values.back()}; // the last one wins, like any repeated option
}
} // namespace Meow2::Log
