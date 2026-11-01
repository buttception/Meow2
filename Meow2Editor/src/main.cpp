/// @file
/// @brief Meow2Editor entry point (composition root). Sets up logging, crash dumps and settings, prints its name and
///        the engine version (checked by the smoke tests) and runs the Qt main window.

#include "EditorMainWindow.h"
#include "WindowPlacement.h"

#include <Meow2/Core/Config/Config.h>
#include <Meow2/Core/Crash/CrashHandler.h>
#include <Meow2/Core/Jobs/JobSystem.h>
#include <Meow2/Core/Log/AssertLogging.h>
#include <Meow2/Core/Log/LogSystem.h>
#include <Meow2/Core/Memory/GlobalTracking.h>
#include <Meow2/Core/Memory/LeakCheck.h>
#include <Meow2/Core/Platform.h>
#include <Meow2/Core/Time/FrameClock.h>
#include <Meow2/Core/Version.h>
#include <Meow2/Platform/Window/WindowConfig.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <QApplication>
#include <QTimer>
#include <string>
#include <vector>

namespace
{
/// @brief Qt's name for a `platform.videoDriver` value. The key uses SDL's names (shared with Player and
///        Renderer); Qt calls X11 "xcb". Everything else ("windows", "wayland", "offscreen") is the same.
std::string qtPlatformName(const std::string& videoDriver)
{
    return videoDriver == "x11" ? "xcb" : videoDriver;
}
} // namespace

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
    // The editor's `new` isn't tracked (Qt's DLLs free what we allocate; see CMakeLists.txt), so this covers only
    // allocations made through library hooks, which the editor will get with Jolt and friends.
    const Meow2::Memory::ScopedLeakCheck leakCheck{Meow2::Memory::globalTracker(), engineLog};
    // Settings (techstack.md §3): Meow2Editor.toml next to the exe (or --config <path>), --set key=value overrides.
    Meow2::Config::Config config = Meow2::Config::openAppConfig("Editor", argc, argv, engineLog);
    // Work on all cores (techstack.md §19). jobs.threadCount = 0: one thread per hardware thread.
    const std::int64_t jobThreads = config.getInt("jobs.threadCount", 0);
    Meow2::Jobs::JobSystem jobs{
        {.threadCount = static_cast<std::uint32_t>(std::clamp<std::int64_t>(jobThreads, 0, 256))}, engineLog};

    std::printf("Meow2Editor (Meow2 Engine %s, %s/%s/%s)\n", Meow2::getVersionString(), Meow2::c_platformName,
                Meow2::c_compilerName, Meow2::c_configName);
    MEOW2_LOG_INFO(appLog, "Meow2Editor started: engine {}, {} / {} / {}", Meow2::getVersionString(),
                   Meow2::c_platformName, Meow2::c_compilerName, Meow2::c_configName);
    MEOW2_LOG_INFO(appLog, "Log file: {}", logSystem.filePath().string());

    // Qt owns the windows and the event loop (techstack.md §9). platform.videoDriver = "offscreen" runs without a
    // desktop (tests, CI); it's passed as Qt's own -platform argument. qtArguments must outlive the QApplication.
    std::string platformName = qtPlatformName(config.getString("platform.videoDriver", ""));
    std::string platformOption = "-platform";
    std::vector<char*> qtArguments(argv, argv + argc);
    if (!platformName.empty())
    {
        qtArguments.push_back(platformOption.data());
        qtArguments.push_back(platformName.data());
    }
    qtArguments.push_back(nullptr); // argv[argc] is null by convention
    int qtArgumentCount = static_cast<int>(qtArguments.size()) - 1;
    QApplication application{qtArgumentCount, qtArguments.data()};
    MEOW2_LOG_INFO(engineLog, "Qt {}, platform '{}'", qVersion(), QGuiApplication::platformName().toStdString());

    Meow2::Editor::EditorMainWindow window;
    Meow2::Editor::showWithDesc(
        window, Meow2::Platform::windowDescFromConfig(config, {.title = "Meow2Editor"}, engineLog), engineLog);

    // The frame tick: Qt runs the event loop, a timer calls the engine (techstack.md §23: tick() from a Qt timer).
    // For now it only measures frames; app.quitAfterFrames ends a smoke run.
    const std::int64_t quitAfterFrames = config.getInt("app.quitAfterFrames", 0);
    Meow2::Time::FrameClock clock;
    QTimer frameTimer;
    QObject::connect(&frameTimer, &QTimer::timeout,
                     [&]
                     {
                         clock.tick(); // this frame's time; the game loop (M2) will hand it to update()
                         if (quitAfterFrames > 0 && static_cast<std::int64_t>(clock.frameCount()) >= quitAfterFrames)
                         {
                             QApplication::quit();
                         }
                     });
    frameTimer.start(16);
    const int exitCode = QApplication::exec(); // returns when the window is closed (or quit() is called)

    // Printed too, not only logged: the smoke tests read stdout, and Release logs to the file only.
    std::printf("Ran %llu frames\n", static_cast<unsigned long long>(clock.frameCount()));
    MEOW2_LOG_INFO(appLog, "Ran {} frames in {:.2f} s", clock.frameCount(), clock.totalSeconds());
    Meow2::Editor::storeWindowState(config, window); // reopen where the user left it
    if (const Meow2::Result<void> saved = config.save(); !saved)
    {
        MEOW2_LOG_WARN(appLog, "Settings not saved: {}", saved.error());
    }
    MEOW2_LOG_INFO(appLog, "Meow2Editor exiting");
    return exitCode;
}
