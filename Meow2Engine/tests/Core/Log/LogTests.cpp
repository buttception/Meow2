/// @file
/// @brief Tests for the Log module: LogSystem, Logger, the MEOW2_LOG_* macros and ScopedAssertLogging.

#include <Meow2/Core/Assert.h>
#include <Meow2/Core/Log/AssertLogging.h>
#include <Meow2/Core/Log/LogSystem.h>

#include <gtest/gtest.h>

#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <regex>
#include <string>
#include <system_error>

namespace
{
namespace fs = std::filesystem;
using Meow2::Log::LogLevel;
using Meow2::Log::LogSystem;
using Meow2::Log::LogSystemDesc;

/// @brief A fresh, empty folder for one test. Deterministic (test name + config), because on Windows a death test
///        re-runs the test body in a child process, and parent and child must use the same folder.
fs::path testDirectory(std::string_view testName)
{
    const fs::path directory =
        fs::temp_directory_path() / std::format("meow2-logtests-{}-{}", Meow2::c_configName, testName);
    std::error_code ignored;
    fs::remove_all(directory, ignored);
    return directory;
}

/// @brief Settings for tests: file only (no console noise), every level.
LogSystemDesc testDesc(const fs::path& directory)
{
    return LogSystemDesc{.appName = "Test", .directory = directory, .console = false, .minimumLevel = LogLevel::Debug};
}

/// @brief Contents of the one log file in `directory`, or "" if there is none.
std::string readLogFile(const fs::path& directory)
{
    std::error_code error;
    for (const fs::directory_entry& entry : fs::directory_iterator(directory, error))
    {
        if (entry.path().extension() == ".log")
        {
            std::ifstream file(entry.path(), std::ios::binary);
            return std::string{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
        }
    }
    return {};
}

bool contains(const std::string& text, std::string_view part)
{
    return text.find(part) != std::string::npos;
}
} // namespace

TEST(Logger, WritesFormattedLine)
{
    const fs::path directory = testDirectory("WritesFormattedLine");
    {
        LogSystem logSystem{testDesc(directory)};
        Meow2::Log::Logger log = logSystem.createLogger("Test");
        log.info("value {}", 42);
    } // destroying the system and logger flushes and closes the file

    const std::string content = readLogFile(directory);
    const std::regex line{R"(\[\d{2}:\d{2}:\d{2}\.\d{3}\]<info> Test: value 42\r?\n)"};
    EXPECT_TRUE(std::regex_search(content, line)) << content;
}

TEST(Logger, LevelNames)
{
    const fs::path directory = testDirectory("LevelNames");
    {
        LogSystem logSystem{testDesc(directory)};
        Meow2::Log::Logger log = logSystem.createLogger("Test");
        log.debug("d");
        log.info("i");
        log.warn("w");
        log.error("e");
    }
    const std::string content = readLogFile(directory);
    EXPECT_TRUE(contains(content, "<debug> Test: d")) << content;
    EXPECT_TRUE(contains(content, "<info> Test: i")) << content;
    EXPECT_TRUE(contains(content, "<warn> Test: w")) << content;
    EXPECT_TRUE(contains(content, "<error> Test: e")) << content;
}

TEST(Logger, MinimumLevelFilters)
{
    const fs::path directory = testDirectory("MinimumLevelFilters");
    {
        LogSystemDesc desc = testDesc(directory);
        desc.minimumLevel = LogLevel::Warn;
        LogSystem logSystem{desc};
        Meow2::Log::Logger log = logSystem.createLogger("Test");
        log.info("dropped");
        log.warn("kept");
    }
    const std::string content = readLogFile(directory);
    EXPECT_FALSE(contains(content, "dropped")) << content;
    EXPECT_TRUE(contains(content, "<warn> Test: kept")) << content;
}

TEST(Logger, LoggersShareSinks)
{
    const fs::path directory = testDirectory("LoggersShareSinks");
    {
        LogSystem logSystem{testDesc(directory)};
        Meow2::Log::Logger engine = logSystem.createLogger("Engine");
        Meow2::Log::Logger app = logSystem.createLogger("Test");
        EXPECT_EQ(engine.name(), "Engine");
        MEOW2_LOG_INFO(engine, "from the engine");
        MEOW2_LOG_INFO(app, "from the app");
    }
    const std::string content = readLogFile(directory);
    EXPECT_TRUE(contains(content, "<info> Engine: from the engine")) << content;
    EXPECT_TRUE(contains(content, "<info> Test: from the app")) << content;
}

TEST(Logger, BracesInMessageArePrintedAsIs)
{
    const fs::path directory = testDirectory("BracesInMessageArePrintedAsIs");
    {
        LogSystem logSystem{testDesc(directory)};
        Meow2::Log::Logger log = logSystem.createLogger("Test");
        log.info("a set: {}", "{1, 2}");
        log.write(LogLevel::Info, "raw {} stays");
    }
    const std::string content = readLogFile(directory);
    EXPECT_TRUE(contains(content, "a set: {1, 2}")) << content;
    EXPECT_TRUE(contains(content, "raw {} stays")) << content;
}

TEST(Logger, DebugCompiledOutInRelease)
{
    const fs::path directory = testDirectory("DebugCompiledOutInRelease");
    int evaluations = 0;
    {
        LogSystem logSystem{testDesc(directory)};
        Meow2::Log::Logger log = logSystem.createLogger("Test");
        MEOW2_LOG_DEBUG(log, "count {}", ++evaluations);
    }
    EXPECT_EQ(evaluations, Meow2::Log::c_debugLoggingEnabled ? 1 : 0);
    EXPECT_EQ(contains(readLogFile(directory), "<debug> Test: count 1"), Meow2::Log::c_debugLoggingEnabled);
}

TEST(Logger, FatalFlushesAndAborts)
{
    const fs::path directory = testDirectory("FatalFlushesAndAborts");
    EXPECT_DEATH(
        {
            LogSystem logSystem{testDesc(directory)};
            Meow2::Log::Logger log = logSystem.createLogger("Test");
            MEOW2_LOG_FATAL(log, "boom {}", 7);
        },
        "");
    // The child process wrote and flushed the file before aborting.
    EXPECT_TRUE(contains(readLogFile(directory), "<fatal> Test: boom 7")) << readLogFile(directory);
}

TEST(LogSystem, CreatesDirectoryAndNamesFile)
{
    const fs::path directory = testDirectory("CreatesDirectoryAndNamesFile") / "nested" / "logs";
    LogSystem logSystem{testDesc(directory)};

    EXPECT_TRUE(fs::is_directory(directory));
    ASSERT_FALSE(logSystem.filePath().empty());
    EXPECT_TRUE(fs::equivalent(logSystem.filePath().parent_path(), directory));
    const std::regex name{R"(Meow2Test_\d{4}-\d{2}-\d{2}_\d{2}-\d{2}-\d{2}\.log)"};
    EXPECT_TRUE(std::regex_match(logSystem.filePath().filename().string(), name))
        << logSystem.filePath().filename().string();
}

TEST(LogSystem, UnusableDirectoryFallsBackToConsole)
{
    // A *file* where the folder should be: the folder can't be created.
    const fs::path directory = testDirectory("UnusableDirectoryFallsBackToConsole");
    fs::create_directories(directory.parent_path());
    std::ofstream(directory) << "not a folder";

    LogSystem logSystem{testDesc(directory)};
    Meow2::Log::Logger log = logSystem.createLogger("Test");
    log.info("still works"); // must not throw or crash

    EXPECT_TRUE(logSystem.filePath().empty());
    std::error_code ignored;
    fs::remove(directory, ignored);
}

TEST(LogSystem, LogDirFromArguments)
{
    using Meow2::Log::logDirectoryFromArguments;
    const char* const separate[] = {"app.exe", "--other", "--log-dir", "C:/logs"};
    const char* const joined[] = {"app.exe", "--log-dir=D:/somewhere/logs"};
    const char* const missingValue[] = {"app.exe", "--log-dir"};
    const char* const none[] = {"app.exe", "--log-dirx=nope"};

    EXPECT_EQ(logDirectoryFromArguments(4, separate), fs::path("C:/logs"));
    EXPECT_EQ(logDirectoryFromArguments(2, joined), fs::path("D:/somewhere/logs"));
    EXPECT_EQ(logDirectoryFromArguments(2, missingValue), std::nullopt);
    EXPECT_EQ(logDirectoryFromArguments(2, none), std::nullopt);
    EXPECT_EQ(logDirectoryFromArguments(0, nullptr), std::nullopt);
}

TEST(LogSystem, DefaultDirectoryIsNextToExecutable)
{
    const fs::path directory = Meow2::Log::defaultLogDirectory();
    EXPECT_EQ(directory.filename(), "logs");
    EXPECT_TRUE(fs::exists(directory.parent_path() / "Meow2EngineCoreTests.exe") ||
                fs::exists(directory.parent_path() / "Meow2EngineCoreTests"));
}

TEST(AssertLogging, FailureIsLogged)
{
    if constexpr (!Meow2::c_assertsEnabled)
    {
        GTEST_SKIP() << "asserts are compiled out in this configuration";
    }
    const fs::path directory = testDirectory("AssertFailureIsLogged");
    EXPECT_DEATH(
        {
            LogSystem logSystem{testDesc(directory)};
            Meow2::Log::Logger engine = logSystem.createLogger("Engine");
            const Meow2::Log::ScopedAssertLogging assertLogging{engine};
            int value = 1;
            MEOW2_ASSERT(value == 2, "logged message");
        },
        "Assertion failed: value == 2"); // the default handler still prints to stderr
    const std::string content = readLogFile(directory);
    EXPECT_TRUE(contains(content, "<error> Engine: Assertion failed: value == 2 (logged message) at")) << content;
}

TEST(AssertLogging, RestoresPreviousHandler)
{
    const fs::path directory = testDirectory("RestoresPreviousHandler");
    LogSystem logSystem{testDesc(directory)};
    Meow2::Log::Logger engine = logSystem.createLogger("Engine");
    {
        const Meow2::Log::ScopedAssertLogging assertLogging{engine};
        EXPECT_NE(Meow2::setAssertHandler(nullptr), &Meow2::defaultAssertHandler); // ours was active
        Meow2::setAssertHandler(nullptr);
    }
    // After the scope, the handler from before it (the default) is back.
    EXPECT_EQ(Meow2::setAssertHandler(nullptr), &Meow2::defaultAssertHandler);
}
