/// @file
/// @brief Meow2Renderer entry point (composition root). Sets up logging, crash dumps and settings,
///        prints its name and the engine version (checked by the smoke tests), opens its window and clears it.

#include <Meow2/Core/Config/Config.h>
#include <Meow2/Core/CpuFeatures.h>
#include <Meow2/Core/Crash/CrashHandler.h>
#include <Meow2/Core/Events/EventBus.h>
#include <Meow2/Core/Jobs/JobSystem.h>
#include <Meow2/Core/Log/AssertLogging.h>
#include <Meow2/Core/Log/LogSystem.h>
#include <Meow2/Core/Memory/GlobalTracking.h>
#include <Meow2/Core/Memory/LeakCheck.h>
#include <Meow2/Core/Platform.h>
#include <Meow2/Core/Profiling/Profile.h>
#include <Meow2/Core/Time/FrameClock.h>
#include <Meow2/Core/Time/GameLoop.h>
#include <Meow2/Core/Time/GameLoopConfig.h>
#include <Meow2/Core/Version.h>
#include <Meow2/Graphics/Rhi/IRhiDevice.h>
#include <Meow2/Math/Color.h>
#include <Meow2/Platform/Input/InputSystem.h>
#include <Meow2/Platform/Input/SdlGamepadSource.h>
#include <Meow2/Platform/Input/SdlInputSource.h>
#include <Meow2/Platform/Platform.h>
#include <Meow2/Platform/Window/Window.h>
#include <Meow2/Platform/Window/WindowConfig.h>
#include <Meow2/Platform/Window/WindowEvents.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <utility>

int main(int argc, char** argv)
{
    // First: a CPU without this build's instruction sets (AVX2) gets a clear message instead of a crash.
    if (!Meow2::checkCpuFeatures("Meow2Renderer"))
    {
        return 1;
    }

    // Composition root (techstack.md §18): everything is created here, in dependency order.
    const std::filesystem::path logDirectory =
        Meow2::Log::logDirectoryFromArguments(argc, argv).value_or(Meow2::Log::defaultLogDirectory());
    Meow2::Log::LogSystem logSystem{{.appName = "Renderer", .directory = logDirectory}};
    Meow2::Log::Logger engineLog = logSystem.createLogger("Engine");
    Meow2::Log::Logger appLog = logSystem.createLogger("Renderer");
    const Meow2::Log::ScopedAssertLogging assertLogging{engineLog};
    MEOW2_PROFILE_THREAD_NAME("Main");
    std::filesystem::path dumpPath =
        logSystem.filePath().empty() ? logDirectory / "Meow2Renderer_crash.dmp" : logSystem.filePath();
    dumpPath.replace_extension(".dmp"); // a crash writes Meow2Renderer_<date>_<time>.dmp next to the log
    const Meow2::Crash::CrashHandler crashHandler{{.dumpPath = dumpPath}, engineLog};
    // Last of the infrastructure: everything created after it is gone when it checks for leaks at the end of main.
    const Meow2::Memory::ScopedLeakCheck leakCheck{Meow2::Memory::globalTracker(), engineLog};
    // Settings (techstack.md §3): Meow2Renderer.toml next to the exe (or --config <path>), --set key=value overrides.
    Meow2::Config::Config config = Meow2::Config::openAppConfig("Renderer", argc, argv, engineLog);
    // Work on all cores (techstack.md §19). jobs.threadCount = 0: one thread per hardware thread.
    const std::int64_t jobThreads = config.getInt("jobs.threadCount", 0);
    Meow2::Jobs::JobSystem jobs{
        {.threadCount = static_cast<std::uint32_t>(std::clamp<std::int64_t>(jobThreads, 0, 256))}, engineLog};
    // Things that happen (window and input events) are delivered through it (techstack.md §22).
    Meow2::Events::EventBus events{Meow2::Memory::globalTracker()};
    // Keyboard, mouse and gamepads (techstack.md §9), fed by the SDL input sources below.
    Meow2::Input::InputSystem input{events};

    std::printf("Meow2Renderer (Meow2 Engine %s, %s/%s/%s)\n", Meow2::getVersionString(), Meow2::c_platformName,
                Meow2::c_compilerName, Meow2::c_configName);
    MEOW2_LOG_INFO(appLog, "Meow2Renderer started: engine {}, {} / {} / {}", Meow2::getVersionString(),
                   Meow2::c_platformName, Meow2::c_compilerName, Meow2::c_configName);
    MEOW2_LOG_INFO(appLog, "Log file: {}", logSystem.filePath().string());

    // One window (techstack.md §9). platform.videoDriver = "offscreen" runs without a desktop (tests, CI).
    Meow2::Result<Meow2::Platform::Platform> platform = Meow2::Platform::Platform::create(
        {.videoDriver = config.getString("platform.videoDriver", "")}, engineLog, events);
    if (!platform)
    {
        MEOW2_LOG_ERROR(appLog, "Can't start the windowing layer: {}", platform.error());
        return 1;
    }
    const Meow2::Platform::WindowDesc windowDesc =
        Meow2::Platform::windowDescFromConfig(config, {.title = "Meow2Renderer"}, engineLog);
    Meow2::Result<Meow2::Platform::Window> window = Meow2::Platform::Window::create(*platform, engineLog, windowDesc);
    if (!window)
    {
        MEOW2_LOG_ERROR(appLog, "Can't open the window: {}", window.error());
        return 1;
    }
    // SDL keyboard, mouse and text → input (techstack.md §9). Destroyed before the Platform: declared after it.
    const Meow2::Input::SdlInputSource keyboardAndMouse{*platform, input};
    // Gamepads are optional: without them (no gamepad support on this machine) the app still runs.
    Meow2::Result<Meow2::Input::SdlGamepadSource> gamepads =
        Meow2::Input::SdlGamepadSource::create(*platform, input, engineLog);
    if (!gamepads)
    {
        MEOW2_LOG_WARN(appLog, "Gamepads unavailable: {}", gamepads.error());
    }

    // Closing the window (or Ctrl+C, logoff) ends the loop. Declared after the window and the bus: destroyed first.
    bool running = true;
    const Meow2::Events::Subscription onClose = events.subscribe<Meow2::Platform::WindowCloseRequested>(
        [&running](const Meow2::Platform::WindowCloseRequested&) { running = false; });
    const Meow2::Events::Subscription onQuit = events.subscribe<Meow2::Platform::QuitRequested>(
        [&running](const Meow2::Platform::QuitRequested&) { running = false; });
    const Meow2::Events::Subscription onResize = events.subscribe<Meow2::Platform::WindowResized>(
        [&appLog]([[maybe_unused]] const Meow2::Platform::WindowResized& e) // unused in Release: no debug log
        {
            MEOW2_LOG_DEBUG(appLog, "Window resized: {}x{} ({}x{} pixels)", e.width, e.height, e.pixelWidth,
                            e.pixelHeight);
        });
    // Keys let go while another window has focus never send a release: release everything on focus loss.
    const Meow2::Events::Subscription onFocus = events.subscribe<Meow2::Platform::WindowFocusChanged>(
        [&input](const Meow2::Platform::WindowFocusChanged& e)
        {
            if (!e.focused)
            {
                input.releaseAll();
            }
        });

    // The GPU (techstack.md §11). SDL's offscreen driver has no window to draw to, so smoke runs and CI need no GPU.
    Meow2::Log::Logger rhiLog = logSystem.createLogger("RHI");
    std::unique_ptr<Meow2::Rhi::IRhiDevice> device;
    std::unique_ptr<Meow2::Rhi::IRhiSwapchain> swapchain; // destroyed first: declared after the device
    if (const Meow2::Platform::NativeWindowHandle nativeWindow = window->nativeHandle();
        nativeWindow.type != Meow2::Platform::NativeWindowType::None)
    {
        const bool validationByDefault = MEOW2_CONFIG_RELEASE == 0;
        Meow2::Result<std::unique_ptr<Meow2::Rhi::IRhiDevice>> createdDevice = Meow2::Rhi::createRhiDevice(
            {.appName = "Meow2Renderer", .validation = config.getBool("rhi.validation", validationByDefault)}, rhiLog);
        if (!createdDevice)
        {
            MEOW2_LOG_ERROR(appLog, "Can't start the GPU: {}", createdDevice.error());
            Meow2::Platform::showErrorDialog("Meow2Renderer", createdDevice.error().message);
            return 1;
        }
        device = std::move(*createdDevice);
        Meow2::Result<std::unique_ptr<Meow2::Rhi::IRhiSwapchain>> createdSwapchain =
            device->createSwapchain({.window = nativeWindow,
                                     .width = static_cast<std::uint32_t>(window->pixelWidth()),
                                     .height = static_cast<std::uint32_t>(window->pixelHeight()),
                                     .vsync = config.getBool("rhi.vsync", true),
                                     .name = "Main window"});
        if (!createdSwapchain)
        {
            MEOW2_LOG_ERROR(appLog, "Can't draw to the window: {}", createdSwapchain.error());
            Meow2::Platform::showErrorDialog("Meow2Renderer", createdSwapchain.error().message);
            return 1;
        }
        swapchain = std::move(*createdSwapchain);
    }

    // One frame of drawing: for now, a cleared window. Returns false if the GPU stopped working.
    const auto renderFrame = [&device, &swapchain, &window, &appLog]
    {
        const auto width = static_cast<std::uint32_t>(window->pixelWidth());
        const auto height = static_cast<std::uint32_t>(window->pixelHeight());
        if (swapchain->needsResize() || width != swapchain->width() || height != swapchain->height())
        {
            if (const Meow2::Result<void> resized = swapchain->resize(width, height); !resized)
            {
                MEOW2_LOG_ERROR(appLog, "Can't resize the swapchain: {}", resized.error());
                return false;
            }
        }
        Meow2::Result<Meow2::Rhi::IRhiCommandList*> list = device->beginFrame();
        if (!list)
        {
            MEOW2_LOG_ERROR(appLog, "Can't start a frame: {}", list.error());
            return false;
        }
        if (device->acquire(*swapchain))
        {
            (*list)->clearSwapchain(*swapchain, Meow2::Math::Color{.r = 0.02F, .g = 0.03F, .b = 0.06F});
        }
        if (const Meow2::Result<void> ended = device->endFrame(); !ended)
        {
            MEOW2_LOG_ERROR(appLog, "Can't finish a frame: {}", ended.error());
            return false;
        }
        return true;
    };

    // The game loop (techstack.md §23): events, fixedUpdate × n, update, render. Nothing to simulate yet.
    // app.quitAfterFrames ends a smoke run.
    const std::int64_t quitAfterFrames = config.getInt("app.quitAfterFrames", 0);
    Meow2::Time::FrameClock clock;
    Meow2::Time::GameLoop loop{
        clock,
        events,
        Meow2::Time::gameLoopDescFromConfig(config, engineLog),
        {// Sleeps up to 16 ms while nothing is being drawn, instead of spinning a core. With a window, vsync paces
         // frames. Input is applied right after the pump, so its events are in the loop's first dispatch.
         .pumpEvents =
             [&platform, &input, &window, &device]
         {
             const bool drawing = device != nullptr && !window->minimized();
             platform->pumpEvents(std::chrono::milliseconds{drawing ? 0 : 16});
             input.beginFrame();
         },
         // Gameplay's fixed step goes before endFixedStep(), which ends this step's latched presses.
         .fixedUpdate = [&input](double) { input.endFixedStep(); },
         .update = [](double) { MEOW2_PROFILE_MEMORY(Meow2::Memory::globalTracker()); }, // memory plots in Tracy
         .render =
             [&device, &window, &renderFrame, &running](double)
         {
             if (device != nullptr && !window->minimized() && !renderFrame())
             {
                 running = false;
             }
         }}};
    while (running && (quitAfterFrames <= 0 || static_cast<std::int64_t>(clock.frameCount()) < quitAfterFrames))
    {
        loop.tick();
    }
    // Printed too, not only logged: the smoke tests read stdout, and Release logs to the file only.
    std::printf("Ran %llu frames\n", static_cast<unsigned long long>(clock.frameCount()));
    MEOW2_LOG_INFO(appLog, "Ran {} frames ({} fixed steps) in {:.2f} s", clock.frameCount(), loop.fixedStepCount(),
                   clock.totalSeconds());

    Meow2::Platform::storeWindowState(config, *window); // reopen where the user left it
    if (const Meow2::Result<void> saved = config.save(); !saved)
    {
        MEOW2_LOG_WARN(appLog, "Settings not saved: {}", saved.error());
    }
    MEOW2_LOG_INFO(appLog, "Meow2Renderer exiting");
    return 0;
}
