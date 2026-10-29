/// @file
/// @brief Meow2CrashProbe: a tiny program that sets up logging and a CrashHandler like an app does, then crashes in
///        the way given on the command line. Used by the Core.CrashHandler.* tests (cmake/RunCrashProbe.cmake),
///        which check that a valid `.dmp` and a log line appear.
///
/// Usage: `Meow2CrashProbe <folder> <mode>`, with mode one of: none, access-violation, stack-overflow, abort,
/// terminate, cpp-exception, invalid-parameter, fatal, assert.

#include <Meow2/Core/Assert.h>
#include <Meow2/Core/Crash/CrashHandler.h>
#include <Meow2/Core/Log/AssertLogging.h>
#include <Meow2/Core/Log/LogSystem.h>

#include <cstdio>
#include <cstdlib>
#include <exception>
#include <stdexcept>
#include <string_view>

namespace
{
/// @brief Recurses until the stack is gone. Work after the call keeps optimised builds from turning it into a loop;
///        the (never true) base case keeps MSVC from rejecting it as infinite recursion (C4717).
int recurseUntilStackOverflow(int depth)
{
    if (depth < 0)
    {
        return 0;
    }
    volatile char frame[1024];
    frame[0] = static_cast<char>(depth);
    const int deeper = recurseUntilStackOverflow(depth + 1);
    return deeper + frame[0];
}

/// @brief Crashes in the requested way. Returns only for "none" (or modes that don't crash in this build).
/// @return The exit code to use when nothing crashed.
int crash(std::string_view mode, Meow2::Log::Logger& log)
{
    if (mode == "none")
    {
        return 0;
    }
    if (mode == "access-violation")
    {
        int* volatile pointer = nullptr;
        *pointer = 42;
    }
    else if (mode == "stack-overflow")
    {
        return recurseUntilStackOverflow(0);
    }
    else if (mode == "abort")
    {
        std::abort();
    }
    else if (mode == "terminate")
    {
        std::terminate();
    }
    else if (mode == "cpp-exception")
    {
        throw std::runtime_error("nobody catches this");
    }
    else if (mode == "invalid-parameter")
    {
        // The CRT reports a null format string as an invalid parameter (the handler is ours).
        const char* volatile format = nullptr;
        std::printf(format);
    }
    else if (mode == "fatal")
    {
        MEOW2_LOG_FATAL(log, "probe fatal error {}", 7);
    }
    else if (mode == "assert")
    {
        int value = 1;
        MEOW2_ASSERT(value == 2, "probe assert");
        return 0; // asserts are compiled out in Release
    }
    std::fprintf(stderr, "Meow2CrashProbe: unknown or non-crashing mode '%.*s'\n", static_cast<int>(mode.size()),
                 mode.data());
    return 2;
}
} // namespace

int main(int argc, char** argv)
{
    if (argc < 3)
    {
        std::fprintf(stderr, "usage: Meow2CrashProbe <folder> <mode>\n");
        return 2;
    }

    // Same order as the apps' composition roots: logging, assert routing, then the crash handler.
    Meow2::Log::LogSystem logSystem{{.appName = "CrashProbe", .directory = argv[1], .console = false}};
    Meow2::Log::Logger log = logSystem.createLogger("Probe");
    const Meow2::Log::ScopedAssertLogging assertLogging{log};
    std::filesystem::path dumpPath = logSystem.filePath();
    dumpPath.replace_extension(".dmp");
    const Meow2::Crash::CrashHandler crashHandler{{.dumpPath = dumpPath}, log};

    return crash(argv[2], log);
}
