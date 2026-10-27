/// @file
/// @brief Meow2Player entry point (composition root). For now it sets up logging and
///        prints its name and the engine version, which the smoke test checks.

#include <Meow2/Core/Log/AssertLogging.h>
#include <Meow2/Core/Log/LogSystem.h>
#include <Meow2/Core/Platform.h>
#include <Meow2/Core/Version.h>

#include <cstdio>

int main(int argc, char** argv)
{
    // Composition root (techstack.md §18): everything is created here, in dependency order.
    const std::filesystem::path logDirectory =
        Meow2::Log::logDirectoryFromArguments(argc, argv).value_or(Meow2::Log::defaultLogDirectory());
    Meow2::Log::LogSystem logSystem{{.appName = "Player", .directory = logDirectory}};
    Meow2::Log::Logger engineLog = logSystem.createLogger("Engine");
    Meow2::Log::Logger appLog = logSystem.createLogger("Player");
    const Meow2::Log::ScopedAssertLogging assertLogging{engineLog};

    std::printf("Meow2Player (Meow2 Engine %s, %s/%s/%s)\n", Meow2::getVersionString(), Meow2::c_platformName,
                Meow2::c_compilerName, Meow2::c_configName);
    MEOW2_LOG_INFO(appLog, "Meow2Player started: engine {}, {} / {} / {}", Meow2::getVersionString(),
                   Meow2::c_platformName, Meow2::c_compilerName, Meow2::c_configName);
    MEOW2_LOG_INFO(appLog, "Log file: {}", logSystem.filePath().string());
    MEOW2_LOG_INFO(appLog, "Meow2Player exiting");
    return 0;
}
