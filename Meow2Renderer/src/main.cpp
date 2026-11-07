/// @file
/// @brief Meow2Renderer entry point (composition root). Sets up logging, crash dumps and settings,
///        prints its name and the engine version (checked by the smoke tests), opens its window and draws a GPU-driven
///        scene of cubes.

#include <Meow2/Assets/AssetManager.h>
#include <Meow2/Core/Config/Config.h>
#include <Meow2/Core/CpuFeatures.h>
#include <Meow2/Core/Crash/CrashHandler.h>
#include <Meow2/Core/Events/EventBus.h>
#include <Meow2/Core/Id/UuidGenerator.h>
#include <Meow2/Core/Jobs/JobSystem.h>
#include <Meow2/Core/Log/AssertLogging.h>
#include <Meow2/Core/Log/LogSystem.h>
#include <Meow2/Core/Memory/GlobalTracking.h>
#include <Meow2/Core/Memory/LeakCheck.h>
#include <Meow2/Core/Platform.h>
#include <Meow2/Core/Process.h>
#include <Meow2/Core/Profiling/Profile.h>
#include <Meow2/Core/Time/FrameClock.h>
#include <Meow2/Core/Time/GameLoop.h>
#include <Meow2/Core/Time/GameLoopConfig.h>
#include <Meow2/Core/Version.h>
#include <Meow2/Graphics/Renderer/GpuScene.h>
#include <Meow2/Graphics/Renderer/PipelineLibrary.h>
#include <Meow2/Graphics/Renderer/RenderGraph.h>
#include <Meow2/Graphics/Rhi/IRhiDevice.h>
#include <Meow2/Graphics/Rhi/RenderDocCapture.h>
#include <Meow2/Math/Camera.h>
#include <Meow2/Math/Color.h>
#include <Meow2/Math/Quaternion.h>
#include <Meow2/Math/Transform.h>
#include <Meow2/Platform/FileSystem/FileSystem.h>
#include <Meow2/Platform/Input/InputEvents.h>
#include <Meow2/Platform/Input/InputSystem.h>
#include <Meow2/Platform/Input/SdlGamepadSource.h>
#include <Meow2/Platform/Input/SdlInputSource.h>
#include <Meow2/Platform/Platform.h>
#include <Meow2/Platform/Window/Window.h>
#include <Meow2/Platform/Window/WindowConfig.h>
#include <Meow2/Platform/Window/WindowEvents.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>
#include <optional>
#include <system_error>
#include <utility>
#include <vector>

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
    // F11 captures a frame, and the RenderDoc window opens on it. rhi.renderDoc loads RenderDoc with no need to start
    // it first. rhi.captureFrame captures with no keyboard. Before the device: it has to hook the graphics API.
    Meow2::Rhi::RenderDocCapture renderDoc{rhiLog,
                                           {.load = config.getBool("rhi.renderDoc", false),
                                            .library = config.getString("rhi.renderDocPath", ""),
                                            .openWindow = config.getBool("rhi.renderDocOpenWindow", true)}};
    const std::int64_t captureFrame = config.getInt("rhi.captureFrame", 0);
    const Meow2::Events::Subscription onCaptureKey = events.subscribe<Meow2::Input::KeyPressed>(
        [&renderDoc](const Meow2::Input::KeyPressed& e)
        {
            if (e.key == Meow2::Input::Key::F11 && !e.repeat)
            {
                renderDoc.capture();
            }
        });
    std::unique_ptr<Meow2::Rhi::IRhiDevice> device;
    std::unique_ptr<Meow2::Rhi::IRhiSwapchain> swapchain; // destroyed first: declared after the device
    // Cooked shaders are next to the exe. Pipelines compiled by the driver are saved there too, in cache/, so a second
    // run skips that work. Absent files only mean a slower start.
    Meow2::FileSystem::FileSystem files{engineLog};
    Meow2::UuidGenerator ids;
    Meow2::Assets::AssetManager assets{files, ids, engineLog};
    const std::filesystem::path appFolder = Meow2::executablePath().parent_path();
    std::error_code folderError;
    std::filesystem::create_directories(appFolder / "cache", folderError);
    if (const Meow2::Result<void> cookedMount = files.mountDirectory("cooked", appFolder / "cooked"); !cookedMount)
    {
        MEOW2_LOG_WARN(appLog, "No cooked shaders: {}", cookedMount.error());
    }
    if (const Meow2::Result<void> cacheMount =
            files.mountDirectory("cache", appFolder / "cache", Meow2::FileSystem::MountAccess::ReadWrite);
        !cacheMount)
    {
        MEOW2_LOG_WARN(appLog, "The pipeline cache won't be saved: {}", cacheMount.error());
    }
    const std::vector<std::byte> savedPipelines =
        files.readFile("cache/vulkan-pipelines.bin").value_or(std::vector<std::byte>{});
    std::optional<Meow2::Renderer::PipelineLibrary> pipelines;
    std::unique_ptr<Meow2::Renderer::GpuScene> scene;
    Meow2::Rhi::TextureHandle depthTexture;
    if (const Meow2::Platform::NativeWindowHandle nativeWindow = window->nativeHandle();
        nativeWindow.type != Meow2::Platform::NativeWindowType::None)
    {
        const bool validationByDefault = MEOW2_CONFIG_RELEASE == 0;
        Meow2::Result<std::unique_ptr<Meow2::Rhi::IRhiDevice>> createdDevice =
            Meow2::Rhi::createRhiDevice({.appName = "Meow2Renderer",
                                         .validation = config.getBool("rhi.validation", validationByDefault),
                                         .recordingThreads = jobs.threadCount(),
                                         .pipelineCache = savedPipelines},
                                        rhiLog);
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

        // A grid of cubes through the GPU-driven path (shaders/GpuDriven.hlsl). Without cooked shaders the window is
        // simply cleared: a machine with no DXC can still run.
        pipelines.emplace(Meow2::Renderer::PipelineLibraryDeps{*device, assets, events, rhiLog});
        const auto instanceCount = static_cast<std::uint32_t>(
            std::clamp<std::int64_t>(config.getInt("renderer.instances", 100'000), 1, 1'000'000));
        const Meow2::Result<Meow2::Uuid> shaderUuid = Meow2::Uuid::parse("01a1b5e2-3c12-8000-80a4-7b19d2e6c053");
        const Meow2::Result<Meow2::AssetId> shaderId =
            shaderUuid.and_then([](const Meow2::Uuid& uuid) { return Meow2::AssetId::fromUuid(uuid); });
        Meow2::Result<std::unique_ptr<Meow2::Renderer::GpuScene>> createdScene =
            Meow2::Renderer::GpuScene::create(*device, *pipelines,
                                              {.shader = shaderId.value_or(Meow2::AssetId{}),
                                               .instanceCapacity = instanceCount,
                                               .colorFormat = swapchain->format()});
        if (createdScene)
        {
            scene = std::move(*createdScene);
            const Meow2::Result<std::uint32_t> cube = scene->addMesh(Meow2::Renderer::makeCube());
            if (!cube)
            {
                MEOW2_LOG_WARN(appLog, "No cube: {}", cube.error());
            }
            constexpr std::uint32_t c_side = 47;
            std::vector<Meow2::Shader::InstanceData> instances(instanceCount);
            for (std::uint32_t i = 0; i < instanceCount; ++i)
            {
                Meow2::Math::Transform transform;
                transform.position =
                    Meow2::Math::WorldPosition{(static_cast<float>(i % c_side) - 23.0F) * 3.0F,
                                               (static_cast<float>((i / c_side) % c_side) - 23.0F) * 3.0F,
                                               (static_cast<float>(i / (c_side * c_side)) - 23.0F) * 3.0F};
                instances[i].world = Meow2::Math::toMat4(transform);
                instances[i].material = i + 1;
            }
            scene->setInstances(instances);
        }
        else
        {
            MEOW2_LOG_WARN(appLog, "No scene: {}", createdScene.error());
        }
    }

    Meow2::Time::FrameClock clock;

    // One frame of drawing. Returns false if the GPU stopped working.
    const auto makeDepth = [&device, &depthTexture, &appLog](std::uint32_t width, std::uint32_t height)
    {
        if (depthTexture.isSet())
        {
            device->destroy(depthTexture);
            depthTexture = {};
        }
        Meow2::Result<Meow2::Rhi::TextureHandle> depth =
            device->createTexture({.width = width,
                                   .height = height,
                                   .format = Meow2::Rhi::RhiFormat::D32Float,
                                   .renderTarget = true,
                                   .name = "Depth"});
        if (!depth)
        {
            MEOW2_LOG_ERROR(appLog, "Can't create the depth buffer: {}", depth.error());
            return false;
        }
        depthTexture = *depth;
        return true;
    };
    const auto renderFrame = [&device, &swapchain, &window, &appLog, &scene, &jobs, &depthTexture, &makeDepth,
                              &renderDoc, &clock, captureFrame]
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
            if (scene && swapchain->width() > 0 && !makeDepth(swapchain->width(), swapchain->height()))
            {
                return false;
            }
        }
        if (scene && !depthTexture.isSet() && swapchain->width() > 0 &&
            !makeDepth(swapchain->width(), swapchain->height()))
        {
            return false;
        }
        if (captureFrame > 0 && static_cast<std::int64_t>(clock.frameCount()) == captureFrame)
        {
            renderDoc.capture();
        }
        renderDoc.update();
        Meow2::Result<Meow2::Rhi::IRhiCommandList*> list = device->beginFrame();
        if (!list)
        {
            MEOW2_LOG_ERROR(appLog, "Can't start a frame: {}", list.error());
            return false;
        }
        constexpr Meow2::Math::Color c_clear{.r = 0.02F, .g = 0.03F, .b = 0.06F};
        std::vector<Meow2::Rhi::IRhiCommandList*> lists;
        if (device->acquire(*swapchain))
        {
            if (scene)
            {
                // The camera circles the grid, looking at its centre.
                const auto angle = static_cast<float>(clock.totalSeconds()) * 0.2F;
                Meow2::Math::Transform camera;
                camera.position =
                    Meow2::Math::WorldPosition{std::sin(angle) * 120.0F, 30.0F, std::cos(angle) * -120.0F};
                camera.rotation = Meow2::Math::lookRotation(
                    Meow2::Math::Vec3{-camera.position.x, -camera.position.y, -camera.position.z});
                const float aspect = static_cast<float>(swapchain->width()) / static_cast<float>(swapchain->height());
                const Meow2::Math::CameraMatrices matrices =
                    Meow2::Math::cameraMatrices(camera, Meow2::Math::CameraLens{}, aspect);
                Meow2::Shader::FrameConstants frame{};
                frame.view = matrices.view;
                frame.projection = matrices.projection;
                frame.viewProjection = matrices.viewProjection;
                scene->setView(device->frameSlot(), frame, Meow2::Math::frustumFromCamera(matrices));

                Meow2::Renderer::RenderGraph graph;
                scene->addPasses(graph, device->frameSlot(), swapchain.get(), {}, depthTexture, c_clear);
                graph.compile();
                Meow2::Result<std::vector<Meow2::Rhi::IRhiCommandList*>> recorded = graph.execute(*device, jobs);
                scene->endFrame(graph);
                if (!recorded)
                {
                    MEOW2_LOG_ERROR(appLog, "Can't record the frame: {}", recorded.error());
                    return false;
                }
                lists = std::move(*recorded);
            }
            else
            {
                (*list)->beginRendering(*swapchain, c_clear);
                (*list)->endRendering();
            }
        }
        if (const Meow2::Result<void> ended = device->endFrame(lists); !ended)
        {
            MEOW2_LOG_ERROR(appLog, "Can't finish a frame: {}", ended.error());
            return false;
        }
        return true;
    };

    // The game loop (techstack.md §23): events, fixedUpdate × n, update, render. Nothing to simulate yet.
    // app.quitAfterFrames ends a smoke run.
    const std::int64_t quitAfterFrames = config.getInt("app.quitAfterFrames", 0);
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

    if (device)
    {
        device->waitIdle();
        scene.reset();
        if (depthTexture.isSet())
        {
            device->destroy(depthTexture);
        }
        if (const Meow2::Result<void> saved =
                files.writeFile("cache/vulkan-pipelines.bin", device->pipelineCacheData());
            !saved)
        {
            MEOW2_LOG_WARN(appLog, "Pipeline cache not saved: {}", saved.error());
        }
    }
    Meow2::Platform::storeWindowState(config, *window); // reopen where the user left it
    if (const Meow2::Result<void> saved = config.save(); !saved)
    {
        MEOW2_LOG_WARN(appLog, "Settings not saved: {}", saved.error());
    }
    MEOW2_LOG_INFO(appLog, "Meow2Renderer exiting");
    return 0;
}
