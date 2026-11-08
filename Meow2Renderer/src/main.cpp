/// @file
/// @brief Meow2Renderer entry point (composition root). Sets up logging, crash dumps and settings,
///        prints its name and the engine version (checked by the smoke tests), opens its window, draws a glTF scene or
///        a grid of cubes through the GPU-driven path, and shows an ImGui shell to pick scenes and read stats.

#include "ImGuiLayer.h"
#include "RendererUi.h"
#include "SceneView.h"

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
#include <Meow2/Graphics/Renderer/RenderDevice.h>
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

#include <imgui.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <format>
#include <memory>
#include <optional>
#include <string>
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
    // Owns the device, and switches it between Vulkan and D3D12 (F10) while the app runs (techstack.md §11).
    std::unique_ptr<Meow2::Renderer::RenderDevice> renderDevice;
    std::unique_ptr<Meow2::Rhi::IRhiSwapchain> swapchain; // destroyed first: declared after the device
    Meow2::Rhi::TextureHandle depthTexture;
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
    // rhi.backend picks the graphics API to start with: "vulkan" (everywhere) or "d3d12" (Windows). Each keeps its own
    // pipeline cache.
    const std::string backendName = config.getString("rhi.backend", "vulkan");
    const Meow2::Rhi::RhiBackend backend =
        backendName == "d3d12" ? Meow2::Rhi::RhiBackend::D3D12 : Meow2::Rhi::RhiBackend::Vulkan;
    if (backendName != "d3d12" && backendName != "vulkan")
    {
        MEOW2_LOG_WARN(appLog, "Unknown rhi.backend '{}': using vulkan", backendName);
    }
    constexpr std::array<Meow2::Rhi::RhiBackend, 2> c_backends{Meow2::Rhi::RhiBackend::Vulkan,
                                                               Meow2::Rhi::RhiBackend::D3D12};
    const auto pipelineCachePath = [](Meow2::Rhi::RhiBackend cacheBackend)
    {
        return std::string{cacheBackend == Meow2::Rhi::RhiBackend::D3D12 ? "cache/d3d12-pipelines.bin"
                                                                         : "cache/vulkan-pipelines.bin"};
    };
    const std::vector<std::byte> savedPipelines =
        files.readFile(pipelineCachePath(backend)).value_or(std::vector<std::byte>{});
    // renderer.scene opens a glTF 2.0 file (.gltf or .glb) or a Khronos sample by name (DamagedHelmet), fetched with
    // cmake -P cmake/FetchGltfSample.cmake <Name> or from the Scenes window. Empty: a grid of cubes.
    const std::filesystem::path sampleRoot{config.getString("renderer.sampleAssets", MEOW2_SAMPLE_ASSETS_DIR)};
    std::filesystem::path startScene;
    std::string sceneError;
    if (const std::string sceneName = config.getString("renderer.scene", ""); !sceneName.empty())
    {
        const std::filesystem::path named{sceneName};
        startScene = named.extension() == ".gltf" || named.extension() == ".glb"
                         ? named
                         : Meow2::RendererApp::findSample(sampleRoot, sceneName);
        if (startScene.empty() || !std::filesystem::exists(startScene))
        {
            sceneError = std::format("No scene '{}'. Pick a sample in the Scenes window, or fetch it: cmake -P "
                                     "cmake/FetchGltfSample.cmake {}",
                                     sceneName, sceneName);
            MEOW2_LOG_ERROR(appLog, "{}", sceneError);
            startScene.clear();
        }
    }
    std::optional<Meow2::Renderer::PipelineLibrary> pipelines;
    // The open scene, and the ImGui shell (renderer.ui; F1 hides it). Both use the pipelines: declared after them.
    std::optional<Meow2::RendererApp::SceneViewDeps> viewDeps;
    std::unique_ptr<Meow2::RendererApp::SceneView> view;
    std::unique_ptr<Meow2::RendererApp::ImGuiLayer> ui;
    const Meow2::Platform::NativeWindowHandle nativeWindow = window->nativeHandle();
    const bool vsync = config.getBool("rhi.vsync", true);
    const auto makeSwapchain = [&swapchain, &window, nativeWindow, vsync](Meow2::Rhi::IRhiDevice& device)
    {
        Meow2::Result<std::unique_ptr<Meow2::Rhi::IRhiSwapchain>> created =
            device.createSwapchain({.window = nativeWindow,
                                    .width = static_cast<std::uint32_t>(window->pixelWidth()),
                                    .height = static_cast<std::uint32_t>(window->pixelHeight()),
                                    .vsync = vsync,
                                    .name = "Main window"});
        if (!created)
        {
            return Meow2::Result<void>{Meow2::Unexpected<Meow2::Error>{created.error()}};
        }
        swapchain = std::move(*created);
        return Meow2::Result<void>{};
    };
    // The swapchain and depth buffer let go of the device and come back on the new one, like the pipelines and scene.
    Meow2::Renderer::DeviceUser windowUser;
    if (nativeWindow.type != Meow2::Platform::NativeWindowType::None)
    {
        const bool validationByDefault = MEOW2_CONFIG_RELEASE == 0;
        Meow2::Result<std::unique_ptr<Meow2::Renderer::RenderDevice>> createdDevice =
            Meow2::Renderer::RenderDevice::create({.backend = backend,
                                                   .appName = "Meow2Renderer",
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
        renderDevice = std::move(*createdDevice);
        for (const Meow2::Rhi::RhiBackend other : c_backends)
        {
            if (other != backend)
            {
                renderDevice->setPipelineCache(
                    other, files.readFile(pipelineCachePath(other)).value_or(std::vector<std::byte>{}));
            }
        }
        if (const Meow2::Result<void> made = makeSwapchain(renderDevice->device()); !made)
        {
            MEOW2_LOG_ERROR(appLog, "Can't draw to the window: {}", made.error());
            Meow2::Platform::showErrorDialog("Meow2Renderer", made.error().message);
            return 1;
        }
        // Pipelines are built for this format, so a backend whose swapchain has another can't take over.
        const Meow2::Rhi::RhiFormat colorFormat = swapchain->format();
        windowUser = renderDevice->addUser(
            "Window",
            [&swapchain, &depthTexture](Meow2::Rhi::IRhiDevice& device)
            {
                device.destroy(depthTexture);
                depthTexture = {}; // made again by the first frame on the new device
                swapchain.reset();
            },
            [&makeSwapchain, &swapchain, colorFormat](Meow2::Rhi::IRhiDevice& device) -> Meow2::Result<void>
            {
                if (Meow2::Result<void> made = makeSwapchain(device); !made)
                {
                    return made;
                }
                if (swapchain->format() != colorFormat)
                {
                    swapchain.reset();
                    return Meow2::fail(Meow2::ErrorCode::Unsupported,
                                       "The window's swapchain has another format on this backend");
                }
                return {};
            });

        // The scene through the GPU-driven path (shaders/GpuDriven.hlsl). Without cooked shaders the window is simply
        // cleared: a machine with no DXC can still run.
        pipelines.emplace(Meow2::Renderer::PipelineLibraryDeps{*renderDevice, assets, events, rhiLog});
        const auto assetId = [](std::string_view text)
        {
            return Meow2::Uuid::parse(text)
                .and_then([](const Meow2::Uuid& uuid) { return Meow2::AssetId::fromUuid(uuid); })
                .value_or(Meow2::AssetId{});
        };
        viewDeps.emplace(Meow2::RendererApp::SceneViewDeps{.device = *renderDevice,
                                                           .pipelines = *pipelines,
                                                           .files = files,
                                                           .ids = ids,
                                                           .jobs = jobs,
                                                           .log = appLog,
                                                           .shader = assetId("01a1b5e2-3c12-8000-80a4-7b19d2e6c053"),
                                                           .colorFormat = colorFormat});
        const auto cubeCount = static_cast<std::uint32_t>(
            std::clamp<std::int64_t>(config.getInt("renderer.instances", 100'000), 1, 1'000'000));
        Meow2::Result<std::unique_ptr<Meow2::RendererApp::SceneView>> opened =
            startScene.empty() ? Meow2::RendererApp::SceneView::cubeGrid(*viewDeps, cubeCount)
                               : Meow2::RendererApp::SceneView::openModel(*viewDeps, startScene);
        if (!opened && !startScene.empty())
        {
            sceneError = std::format("Can't open {}: {}", startScene.string(), opened.error().message);
            MEOW2_LOG_ERROR(appLog, "{}", sceneError);
            opened = Meow2::RendererApp::SceneView::cubeGrid(*viewDeps, cubeCount);
        }
        if (opened)
        {
            view = std::move(*opened);
        }
        else
        {
            MEOW2_LOG_WARN(appLog, "No scene: {}", opened.error());
        }
        if (config.getBool("renderer.ui", true))
        {
            Meow2::Result<std::unique_ptr<Meow2::RendererApp::ImGuiLayer>> createdUi =
                Meow2::RendererApp::ImGuiLayer::create({*renderDevice, *pipelines, events, appLog},
                                                       assetId("01a1b5e2-3c14-8000-80a4-7b19d2e6c055"), colorFormat);
            if (createdUi)
            {
                ui = std::move(*createdUi);
            }
            else
            {
                MEOW2_LOG_WARN(appLog, "No UI: {}", createdUi.error());
            }
        }
    }

    Meow2::Time::FrameClock clock;

    // F10 switches between Vulkan and D3D12 at the next frame boundary. rhi.swapAtFrame switches once with no keyboard.
    std::optional<Meow2::Rhi::RhiBackend> pendingSwap;
    const Meow2::Events::Subscription onSwapKey = events.subscribe<Meow2::Input::KeyPressed>(
        [&pendingSwap, &renderDevice](const Meow2::Input::KeyPressed& e)
        {
            if (e.key == Meow2::Input::Key::F10 && !e.repeat && renderDevice)
            {
                pendingSwap = renderDevice->backend() == Meow2::Rhi::RhiBackend::Vulkan
                                  ? Meow2::Rhi::RhiBackend::D3D12
                                  : Meow2::Rhi::RhiBackend::Vulkan;
            }
        });
    const std::int64_t swapAtFrame = config.getInt("rhi.swapAtFrame", 0);
    // renderer.switchSceneAtFrame opens renderer.switchScene (a sample name) once at that frame, like picking it in the
    // Scenes window: for checks with no mouse.
    const std::int64_t switchSceneAtFrame = config.getInt("renderer.switchSceneAtFrame", 0);
    const std::string switchScene = config.getString("renderer.switchScene", "");

    // The UI's windows and what they ask for, applied between frames. F1 shows and hides the UI.
    Meow2::RendererApp::ScenesWindow scenesWindow{sampleRoot, jobs, appLog};
    Meow2::RendererApp::FrameTimes frameTimes;
    std::optional<Meow2::RendererApp::SceneRequest> pendingScene;
    bool showUi = true;
    const Meow2::Events::Subscription onUiKey = events.subscribe<Meow2::Input::KeyPressed>(
        [&showUi](const Meow2::Input::KeyPressed& e)
        {
            if (e.key == Meow2::Input::Key::F1 && !e.repeat)
            {
                showUi = !showUi;
            }
        });

    // One frame of drawing. Returns false if the GPU stopped working.
    const auto makeDepth = [&renderDevice, &depthTexture, &appLog](std::uint32_t width, std::uint32_t height)
    {
        Meow2::Rhi::IRhiDevice& device = renderDevice->device();
        if (depthTexture.isSet())
        {
            device.destroy(depthTexture);
            depthTexture = {};
        }
        Meow2::Result<Meow2::Rhi::TextureHandle> depth =
            device.createTexture({.width = width,
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
    const auto renderFrame = [&renderDevice, &swapchain, &window, &appLog, &view, &viewDeps, &ui, &jobs, &depthTexture,
                              &makeDepth, &renderDoc, &clock, &pendingSwap, &pendingScene, &sceneError, &scenesWindow,
                              &frameTimes, &showUi, &sampleRoot, &switchScene, captureFrame, swapAtFrame,
                              switchSceneAtFrame,
                              cubeCount = static_cast<std::uint32_t>(
                                  std::clamp<std::int64_t>(config.getInt("renderer.instances", 100'000), 1, 1'000'000))]
    {
        if (swapAtFrame > 0 && static_cast<std::int64_t>(clock.frameCount()) == swapAtFrame)
        {
            pendingSwap = renderDevice->backend() == Meow2::Rhi::RhiBackend::Vulkan ? Meow2::Rhi::RhiBackend::D3D12
                                                                                    : Meow2::Rhi::RhiBackend::Vulkan;
        }
        // Between frames: everything is released, the device replaced, and everything made again on the new one.
        if (const std::optional<Meow2::Rhi::RhiBackend> target = std::exchange(pendingSwap, std::nullopt))
        {
            if (const Meow2::Result<void> swapped = renderDevice->swapTo(*target); !swapped)
            {
                MEOW2_LOG_WARN(appLog, "{}", swapped.error());
                if (!renderDevice->hasDevice())
                {
                    return false;
                }
            }
        }
        if (switchSceneAtFrame > 0 && static_cast<std::int64_t>(clock.frameCount()) == switchSceneAtFrame)
        {
            pendingScene = Meow2::RendererApp::SceneRequest{
                .file = switchScene.empty() ? std::filesystem::path{}
                                            : Meow2::RendererApp::findSample(sampleRoot, switchScene)};
        }
        // Between frames: a scene picked in the UI. The new one is made before the old one goes, so a scene that
        // fails to open leaves the current one on screen.
        if (const std::optional<Meow2::RendererApp::SceneRequest> request = std::exchange(pendingScene, std::nullopt);
            request && viewDeps)
        {
            Meow2::Result<std::unique_ptr<Meow2::RendererApp::SceneView>> opened =
                request->file.empty() ? Meow2::RendererApp::SceneView::cubeGrid(*viewDeps, cubeCount)
                                      : Meow2::RendererApp::SceneView::openModel(*viewDeps, request->file);
            if (opened)
            {
                view = std::move(*opened);
                sceneError.clear();
            }
            else
            {
                sceneError = std::format("Can't open {}: {}", request->file.string(), opened.error().message);
                MEOW2_LOG_ERROR(appLog, "{}", sceneError);
            }
        }
        Meow2::Rhi::IRhiDevice& device = renderDevice->device(); // fetched each frame: a switch replaces it
        const auto width = static_cast<std::uint32_t>(window->pixelWidth());
        const auto height = static_cast<std::uint32_t>(window->pixelHeight());
        if (swapchain->needsResize() || width != swapchain->width() || height != swapchain->height())
        {
            if (const Meow2::Result<void> resized = swapchain->resize(width, height); !resized)
            {
                MEOW2_LOG_ERROR(appLog, "Can't resize the swapchain: {}", resized.error());
                return false;
            }
            if (view && swapchain->width() > 0 && !makeDepth(swapchain->width(), swapchain->height()))
            {
                return false;
            }
        }
        if (view && !depthTexture.isSet() && swapchain->width() > 0 &&
            !makeDepth(swapchain->width(), swapchain->height()))
        {
            return false;
        }
        if (captureFrame > 0 && static_cast<std::int64_t>(clock.frameCount()) == captureFrame)
        {
            renderDoc.capture();
        }
        renderDoc.update();
        Meow2::Result<Meow2::Rhi::IRhiCommandList*> list = device.beginFrame();
        if (!list)
        {
            MEOW2_LOG_ERROR(appLog, "Can't start a frame: {}", list.error());
            return false;
        }
        constexpr Meow2::Math::Color c_clear{.r = 0.02F, .g = 0.03F, .b = 0.06F};
        frameTimes.add(static_cast<float>(clock.deltaSeconds() * 1000.0));

        // The UI's windows, built before drawing; what they ask for happens next frame.
        if (ui)
        {
            // ImGui works in window coordinates, the units of the mouse positions it gets. On Windows those are
            // pixels whatever the display scale; on a Retina Mac a pixel is half of one. The display scale (1.25 at
            // 125 %) only makes the UI bigger, through its style.
            const Meow2::Platform::WindowRect bounds = window->bounds();
            const float pixelsPerUnit =
                bounds.width > 0 ? static_cast<float>(window->pixelWidth()) / static_cast<float>(bounds.width) : 1.0F;
            ui->newFrame(clock.deltaSecondsFloat(), static_cast<float>(bounds.width), static_cast<float>(bounds.height),
                         pixelsPerUnit, window->displayScale() / pixelsPerUnit);
            if (showUi)
            {
                Meow2::RendererApp::StatsInfo stats{
                    .backend = renderDevice->backend() == Meow2::Rhi::RhiBackend::D3D12 ? "D3D12" : "Vulkan",
                    .gpu = device.adapter().name,
                    .frames = &frameTimes,
                    .width = swapchain->width(),
                    .height = swapchain->height(),
                    .scene = view ? std::string_view{view->name()} : std::string_view{"(none)"},
                    .gpuMemory = device.memoryStats(),
                    .validationErrors = device.validationErrorCount()};
                if (view)
                {
                    stats.entities = view->entityCount();
                    stats.instances = view->gpu().instanceCount();
                    stats.meshes = view->gpu().meshCount();
                    stats.materials = view->gpu().materialCount();
                    stats.textures = view->gpu().textureCount();
                    stats.importMilliseconds = view->importMilliseconds();
                }
                const Meow2::RendererApp::StatsActions actions = Meow2::RendererApp::drawStatsWindow(stats);
                if (actions.switchBackend)
                {
                    pendingSwap = renderDevice->backend() == Meow2::Rhi::RhiBackend::Vulkan
                                      ? Meow2::Rhi::RhiBackend::D3D12
                                      : Meow2::Rhi::RhiBackend::Vulkan;
                }
                if (actions.capture)
                {
                    renderDoc.capture();
                }
                if (std::optional<Meow2::RendererApp::SceneRequest> request =
                        scenesWindow.draw(view ? std::string_view{view->name()} : std::string_view{}, sceneError))
                {
                    pendingScene = std::move(request);
                }
            }
        }

        std::vector<Meow2::Rhi::IRhiCommandList*> lists;
        if (device.acquire(*swapchain))
        {
            Meow2::Renderer::RenderGraph graph;
            if (view)
            {
                // The camera circles the scene, looking at its centre, far enough to see all of it.
                const Meow2::Math::Vec3& center = view->center();
                const auto angle = static_cast<float>(clock.totalSeconds()) * 0.2F;
                const float distance = view->radius() * 1.7F;
                Meow2::Math::Transform camera;
                camera.position =
                    Meow2::Math::WorldPosition{center.x + std::sin(angle) * distance, center.y + distance * 0.25F,
                                               center.z - std::cos(angle) * distance};
                camera.rotation = Meow2::Math::lookRotation(Meow2::Math::Vec3{
                    center.x - camera.position.x, center.y - camera.position.y, center.z - camera.position.z});
                const float aspect = static_cast<float>(swapchain->width()) / static_cast<float>(swapchain->height());
                // The near plane follows the scene's size, so small models aren't clipped and big ones keep precision.
                Meow2::Math::CameraLens lens{};
                lens.nearZ = std::clamp(view->radius() * 0.01F, 0.001F, 0.1F);
                const Meow2::Math::CameraMatrices matrices = Meow2::Math::cameraMatrices(camera, lens, aspect);
                Meow2::Shader::FrameConstants frame{};
                frame.view = matrices.view;
                frame.projection = matrices.projection;
                frame.viewProjection = matrices.viewProjection;
                view->gpu().setView(device.frameSlot(), frame, Meow2::Math::frustumFromCamera(matrices));
                view->gpu().addPasses(graph, device.frameSlot(), swapchain.get(), {}, depthTexture, c_clear);
            }
            else
            {
                Meow2::Rhi::IRhiSwapchain* target = swapchain.get();
                graph.addPass({.name = "Clear",
                               .accesses = {},
                               .record =
                                   [target, clear = c_clear](Meow2::Rhi::IRhiCommandList& clearList)
                               {
                                   clearList.beginRendering(*target, clear);
                                   clearList.endRendering();
                               },
                               .small = true});
            }
            if (ui)
            {
                ui->addPasses(graph, device.frameSlot(), *swapchain);
            }
            graph.compile();
            Meow2::Result<std::vector<Meow2::Rhi::IRhiCommandList*>> recorded = graph.execute(device, jobs);
            if (view)
            {
                view->gpu().endFrame(graph);
            }
            if (ui)
            {
                ui->endFrame();
            }
            if (!recorded)
            {
                MEOW2_LOG_ERROR(appLog, "Can't record the frame: {}", recorded.error());
                return false;
            }
            lists = std::move(*recorded);
        }
        else if (ui)
        {
            ui->skipFrame();
        }
        if (const Meow2::Result<void> ended = device.endFrame(lists); !ended)
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
             [&platform, &input, &window, &renderDevice]
         {
             const bool drawing = renderDevice != nullptr && !window->minimized();
             platform->pumpEvents(std::chrono::milliseconds{drawing ? 0 : 16});
             input.beginFrame();
         },
         // Gameplay's fixed step goes before endFixedStep(), which ends this step's latched presses.
         .fixedUpdate = [&input](double) { input.endFixedStep(); },
         .update = [](double) { MEOW2_PROFILE_MEMORY(Meow2::Memory::globalTracker()); }, // memory plots in Tracy
         .render =
             [&renderDevice, &window, &renderFrame, &running](double)
         {
             if (renderDevice != nullptr && !window->minimized() && !renderFrame())
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

    if (renderDevice && renderDevice->hasDevice())
    {
        renderDevice->device().waitIdle();
        ui.reset();
        view.reset();
        pipelines.reset();
        windowUser.reset();
        renderDevice->device().destroy(depthTexture);
        swapchain.reset();
        // Every backend this run used, and the files of the others as they were. Empty when there's nothing (e.g. under
        // RenderDoc on D3D12): the file from the last normal run is kept.
        for (const Meow2::Rhi::RhiBackend cacheBackend : c_backends)
        {
            if (const std::vector<std::byte> pipelineCache = renderDevice->pipelineCache(cacheBackend);
                !pipelineCache.empty())
            {
                if (const Meow2::Result<void> saved = files.writeFile(pipelineCachePath(cacheBackend), pipelineCache);
                    !saved)
                {
                    MEOW2_LOG_WARN(appLog, "Pipeline cache not saved: {}", saved.error());
                }
            }
        }
    }
    else
    {
        ui.reset();
        view.reset();
        pipelines.reset();
        windowUser.reset();
        swapchain.reset();
    }
    Meow2::Platform::storeWindowState(config, *window); // reopen where the user left it
    if (const Meow2::Result<void> saved = config.save(); !saved)
    {
        MEOW2_LOG_WARN(appLog, "Settings not saved: {}", saved.error());
    }
    MEOW2_LOG_INFO(appLog, "Meow2Renderer exiting");
    return 0;
}
