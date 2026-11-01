/// @file
/// @brief Meow2Player entry point (composition root). Sets up logging, crash dumps and settings,
///        prints its name and the engine version (checked by the smoke tests) and opens its window.

#include <Meow2/Core/Config/Config.h>
#include <Meow2/Core/Crash/CrashHandler.h>
#include <Meow2/Core/Jobs/JobSystem.h>
#include <Meow2/Core/Log/AssertLogging.h>
#include <Meow2/Core/Log/LogSystem.h>
#include <Meow2/Core/Memory/GlobalTracking.h>
#include <Meow2/Core/Memory/LeakCheck.h>
#include <Meow2/Core/Platform.h>
#include <Meow2/Core/Profiling/Profile.h>
#include <Meow2/Core/Time/FrameClock.h>
#include <Meow2/Core/Version.h>
#include <Meow2/Platform/Platform.h>
#include <Meow2/Platform/Window/Window.h>
#include <Meow2/Platform/Window/WindowConfig.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
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
    MEOW2_PROFILE_THREAD_NAME("Main");
    std::filesystem::path dumpPath =
        logSystem.filePath().empty() ? logDirectory / "Meow2Player_crash.dmp" : logSystem.filePath();
    dumpPath.replace_extension(".dmp"); // a crash writes Meow2Player_<date>_<time>.dmp next to the log
    const Meow2::Crash::CrashHandler crashHandler{{.dumpPath = dumpPath}, engineLog};
    // Last of the infrastructure: everything created after it is gone when it checks for leaks at the end of main.
    const Meow2::Memory::ScopedLeakCheck leakCheck{Meow2::Memory::globalTracker(), engineLog};
    // Settings (techstack.md §3): Meow2Player.toml next to the exe (or --config <path>), --set key=value overrides.
    Meow2::Config::Config config = Meow2::Config::openAppConfig("Player", argc, argv, engineLog);
    // Work on all cores (techstack.md §19). jobs.threadCount = 0: one thread per hardware thread.
    const std::int64_t jobThreads = config.getInt("jobs.threadCount", 0);
    Meow2::Jobs::JobSystem jobs{
        {.threadCount = static_cast<std::uint32_t>(std::clamp<std::int64_t>(jobThreads, 0, 256))}, engineLog};

    std::printf("Meow2Player (Meow2 Engine %s, %s/%s/%s)\n", Meow2::getVersionString(), Meow2::c_platformName,
                Meow2::c_compilerName, Meow2::c_configName);
    MEOW2_LOG_INFO(appLog, "Meow2Player started: engine {}, {} / {} / {}", Meow2::getVersionString(),
                   Meow2::c_platformName, Meow2::c_compilerName, Meow2::c_configName);
    MEOW2_LOG_INFO(appLog, "Log file: {}", logSystem.filePath().string());

    // One window (techstack.md §9). platform.videoDriver = "offscreen" runs without a desktop (tests, CI).
    Meow2::Result<Meow2::Platform::Platform> platform =
        Meow2::Platform::Platform::create({.videoDriver = config.getString("platform.videoDriver", "")}, engineLog);
    if (!platform)
    {
        MEOW2_LOG_ERROR(appLog, "Can't start the windowing layer: {}", platform.error());
        return 1;
    }
    const Meow2::Platform::WindowDesc windowDesc =
        Meow2::Platform::windowDescFromConfig(config, {.title = "Meow2Player"}, engineLog);
    Meow2::Result<Meow2::Platform::Window> window = Meow2::Platform::Window::create(*platform, engineLog, windowDesc);
    if (!window)
    {
        MEOW2_LOG_ERROR(appLog, "Can't open the window: {}", window.error());
        return 1;
    }

    // Until the game loop exists (M2), wait for events and do nothing else. app.quitAfterFrames ends a smoke run.
    const std::int64_t quitAfterFrames = config.getInt("app.quitAfterFrames", 0);
    Meow2::Time::FrameClock clock;
    while (!platform->quitRequested() && !window->closeRequested() &&
           (quitAfterFrames <= 0 || static_cast<std::int64_t>(clock.frameCount()) < quitAfterFrames))
    {
        clock.tick(); // this frame's time; the game loop (M2) will hand it to update()
        platform->pumpEvents(std::chrono::milliseconds{16});  // sleeps while idle instead of spinning a core
        MEOW2_PROFILE_MEMORY(Meow2::Memory::globalTracker()); // memory per tag, as plots in Tracy
        MEOW2_PROFILE_FRAME_MARK();
    }
    // Printed too, not only logged: the smoke tests read stdout, and Release logs to the file only.
    std::printf("Ran %llu frames\n", static_cast<unsigned long long>(clock.frameCount()));
    MEOW2_LOG_INFO(appLog, "Ran {} frames in {:.2f} s", clock.frameCount(), clock.totalSeconds());

    Meow2::Platform::storeWindowState(config, *window); // reopen where the user left it
    if (const Meow2::Result<void> saved = config.save(); !saved)
    {
        MEOW2_LOG_WARN(appLog, "Settings not saved: {}", saved.error());
    }
    MEOW2_LOG_INFO(appLog, "Meow2Player exiting");
    return 0;
}
