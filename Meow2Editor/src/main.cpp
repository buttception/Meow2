/// @file
/// @brief Meow2Editor entry point (composition root). Qt arrives in M9; for now it sets up logging and
///        prints its name and the engine version, which the smoke test checks.

#include <Meow2/Core/Crash/CrashHandler.h>
#include <Meow2/Core/Log/AssertLogging.h>
#include <Meow2/Core/Log/LogSystem.h>
#include <Meow2/Core/Memory/GlobalTracking.h>
#include <Meow2/Core/Memory/LeakCheck.h>
#include <Meow2/Core/Platform.h>
#include <Meow2/Core/Version.h>

#include <cstdio>

int main(int argc, char** argv)
{
    // Composition root (techstack.md §18): everything is created here, in dependency order.
    const std::filesystem::path logDirectory =
        Meow2::Log::logDirectoryFromArguments(argc, argv).value_or(Meow2::Log::defaultLogDirectory());
    Meow2::Log::LogSystem logSystem{{.appName = "Editor", .directory = logDirectory}};
    Meow2::Log::Logger engineLog = logSystem.createLogger("Engine");
    Meow2::Log::Logger appLog = logSystem.createLogger("Editor");
    const Meow2::Log::ScopedAssertLogging assertLogging{engineLog};
    std::filesystem::path dumpPath =
        logSystem.filePath().empty() ? logDirectory / "Meow2Editor_crash.dmp" : logSystem.filePath();
    dumpPath.replace_extension(".dmp"); // a crash writes Meow2Editor_<date>_<time>.dmp next to the log
    const Meow2::Crash::CrashHandler crashHandler{{.dumpPath = dumpPath}, engineLog};
    // Last of the infrastructure: everything created after it is gone when it checks for leaks at the end of main.
    const Meow2::Memory::ScopedLeakCheck leakCheck{Meow2::Memory::globalTracker(), engineLog};

    std::printf("Meow2Editor (Meow2 Engine %s, %s/%s/%s)\n", Meow2::getVersionString(), Meow2::c_platformName,
                Meow2::c_compilerName, Meow2::c_configName);
    MEOW2_LOG_INFO(appLog, "Meow2Editor started: engine {}, {} / {} / {}", Meow2::getVersionString(),
                   Meow2::c_platformName, Meow2::c_compilerName, Meow2::c_configName);
    MEOW2_LOG_INFO(appLog, "Log file: {}", logSystem.filePath().string());
    MEOW2_LOG_INFO(appLog, "Meow2Editor exiting");
    return 0;
}
