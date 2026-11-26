/// @file
/// @brief Meow2Editor entry point (composition root). Sets up logging, crash dumps and settings, prints its name and
///        the engine version (checked by the smoke tests) and runs the Qt main window.

#include "Commands/EditorCommandRegistry.h"
#include "Commands/EditorCommands.h"
#include "Commands/EditorSettings.h"
#include "Commands/UndoStack.h"
#include "Editing/SceneEditor.h"
#include "EditorMainWindow.h"
#include "EditorTheme.h"
#include "Mcp/EditorArguments.h"
#include "Mcp/McpHttpServer.h"
#include "Mcp/McpProtocol.h"
#include "Project/SceneDocument.h"
#include "Viewports/EditorRenderer.h"
#include "Viewports/EditorWorld.h"
#include "Viewports/ViewportController.h"
#include "WindowPlacement.h"

#include <Meow2/Assets/AssetManager.h>
#include <Meow2/Core/Config/Config.h>
#include <Meow2/Core/CpuFeatures.h>
#include <Meow2/Core/Crash/CrashHandler.h>
#include <Meow2/Core/Events/EventBus.h>
#include <Meow2/Core/Id/UuidGenerator.h>
#include <Meow2/Core/Jobs/JobSystem.h>
#include <Meow2/Core/Log/AssertLogging.h>
#include <Meow2/Core/Log/ConsoleBuffer.h>
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
#include <Meow2/Platform/FileSystem/FileSystem.h>
#include <Meow2/Platform/Input/InputSystem.h>
#include <Meow2/Platform/Window/WindowConfig.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <optional>
#include <QApplication>
#include <QByteArray>
#include <QClipboard>
#include <QRandomGenerator>
#include <QTimer>
#include <string>
#include <utility>
#include <vector>

namespace
{
/// @brief Qt's name for a `platform.videoDriver` value. The key uses SDL's names (shared with Player and
///        Renderer); Qt calls X11 "xcb". Everything else ("windows", "wayland", "offscreen") is the same.
std::string qtPlatformName(const std::string& videoDriver)
{
    return videoDriver == "x11" ? "xcb" : videoDriver;
}

/// @brief `editor.mcp.port`'s default: an unassigned port, the same every run, so an agent's setup keeps working.
constexpr std::int64_t c_defaultMcpPort = 47717;

/// @brief What the MCP server tells the model about itself (the `instructions` of initialize and server/discover).
const QString c_mcpInstructions = QStringLiteral(
    "Meow2Editor, a game engine's scene editor. Every editor action is a tool, named like the menu's command with "
    "dots as underscores (entity_create, component_setField, scene_save). Changes are undoable (edit_undo) and appear "
    "in "
    "the editor at once. Entities are named by EntityId strings; scene_hierarchy and entity_get read the scene. "
    "viewport_screenshot returns a PNG of the editor or game view, to check your work. Save with scene_save.");

/// @brief A new bearer token for the MCP server: 32 random bytes from the system's generator, as hex.
/// @return 64 hex characters.
std::string randomToken()
{
    std::array<quint32, 8> words{};
    QRandomGenerator::system()->fillRange(words.data(), words.size());
    return QByteArray(reinterpret_cast<const char*>(words.data()), sizeof(words)).toHex().toStdString();
}

/// @brief The viewports' size when headless: `editor.headless.width` × `editor.headless.height` (1280 × 720).
/// @param arguments The command line's options.
/// @param config    The settings.
/// @param renderer  The renderer; without one there's nothing to size.
/// @return The size, or nothing when not headless.
std::optional<std::pair<std::uint32_t, std::uint32_t>> headlessSize(const Meow2::Editor::EditorArguments& arguments,
                                                                    const Meow2::Config::Config& config,
                                                                    const Meow2::Editor::EditorRenderer* renderer)
{
    if (!arguments.headless || renderer == nullptr)
    {
        return std::nullopt;
    }
    const auto side = [&config](const char* key, std::int64_t fallback)
    {
        return static_cast<std::uint32_t>(std::clamp<std::int64_t>(config.getInt(key, fallback), 16, 8192));
    };
    return std::pair{side("editor.headless.width", 1280), side("editor.headless.height", 720)};
}
} // namespace

int main(int argc, char** argv)
{
    using namespace Qt::StringLiterals;
    // First: a CPU without this build's instruction sets (AVX2) gets a clear message instead of a crash.
    if (!Meow2::checkCpuFeatures("Meow2Editor"))
    {
        return 1;
    }

    // Composition root (techstack.md §18): everything is created here, in dependency order.
    const std::filesystem::path logDirectory =
        Meow2::Log::logDirectoryFromArguments(argc, argv).value_or(Meow2::Log::defaultLogDirectory());
    // The last log lines, for the console dock. Declared first: every logger writes into it until it's destroyed.
    Meow2::Log::ConsoleBuffer consoleLines{2000};
    Meow2::Log::LogSystem logSystem{{.appName = "Editor", .directory = logDirectory, .consoleBuffer = &consoleLines}};
    Meow2::Log::Logger engineLog = logSystem.createLogger("Engine");
    Meow2::Log::Logger appLog = logSystem.createLogger("Editor");
    const Meow2::Log::ScopedAssertLogging assertLogging{engineLog};
    MEOW2_PROFILE_THREAD_NAME("Main");
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
    // Things that happen are delivered through it (techstack.md §22); the game loop dispatches it each frame.
    Meow2::Events::EventBus events{Meow2::Memory::globalTracker()};

    std::printf("Meow2Editor (Meow2 Engine %s, %s/%s/%s)\n", Meow2::getVersionString(), Meow2::c_platformName,
                Meow2::c_compilerName, Meow2::c_configName);
    MEOW2_LOG_INFO(appLog, "Meow2Editor started: engine {}, {} / {} / {}", Meow2::getVersionString(),
                   Meow2::c_platformName, Meow2::c_compilerName, Meow2::c_configName);
    MEOW2_LOG_INFO(appLog, "Log file: {}", logSystem.filePath().string());

    // The editor's own options: --headless, --mcp, --mcp-port, --mcp-token (docs/wiki/Mcp.md).
    const Meow2::Result<Meow2::Editor::EditorArguments> arguments = Meow2::Editor::parseEditorArguments(argc, argv);
    if (!arguments)
    {
        std::fprintf(stderr, "Meow2Editor: %s\n", arguments.error().message.c_str());
        MEOW2_LOG_ERROR(appLog, "Bad command line: {}", arguments.error());
        return 2;
    }

    // Qt owns the windows and the event loop (techstack.md §9). platform.videoDriver = "offscreen" runs without a
    // desktop (tests, CI); it's passed as Qt's own -platform argument. qtArguments must outlive the QApplication.
    // Headless always runs on it: no window is ever shown.
    std::string platformName =
        arguments->headless ? std::string{"offscreen"} : qtPlatformName(config.getString("platform.videoDriver", ""));
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
    // The Meow2 theme (docs/wiki/Theme.md): black, red accent, Barlow from fonts/ next to the exe.
    Meow2::Editor::applyEditorTheme(application, Meow2::executablePath().parent_path() / "fonts", engineLog);

    // Every editor action is a command (techstack.md §20): menus, toolbar, shortcuts and (later) MCP run them here.
    Meow2::Editor::EditorSettings settings{config};
    Meow2::Editor::UndoStack history{static_cast<std::size_t>(settings.intValue(Meow2::Editor::c_undoLimitKey))};
    Meow2::Editor::EditorCommandRegistry commands{history, appLog};
    if (const Meow2::Result<void> registered = Meow2::Editor::registerEditorCommands(
            {.commands = commands, .history = history, .settings = settings, .console = consoleLines});
        !registered)
    {
        MEOW2_LOG_ERROR(appLog, "Editor commands not registered: {}", registered.error());
    }

    // The viewports' world and renderer. The renderer needs native windows: on the offscreen platform (tests, CI) the
    // editor runs without one, and the viewports stay empty, unless it's headless: then the viewports draw into
    // offscreen textures (a GPU without a desktop), for agents to see through viewport.screenshot.
    Meow2::Input::InputSystem input{events};
    Meow2::UuidGenerator ids;
    Meow2::FileSystem::FileSystem files{engineLog};
    if (const Meow2::Result<void> cooked =
            files.mountDirectory("cooked", Meow2::executablePath().parent_path() / "cooked");
        !cooked)
    {
        MEOW2_LOG_WARN(appLog, "No cooked shaders: {}", cooked.error());
    }
    Meow2::Assets::AssetManager assets{files, ids, engineLog};
    // What scene files save and load in the editor: the engine's and the renderer's components.
    Meow2::Scene::ComponentRegistry components;
    Meow2::Scene::registerSceneComponents(components);
    Meow2::Renderer::registerRenderComponents(components);
    Meow2::Editor::EditorWorld world{
        {.ids = ids, .jobs = jobs, .components = components, .assets = assets, .log = engineLog}};
    std::unique_ptr<Meow2::Editor::EditorRenderer> renderer;
    if ((arguments->headless || QGuiApplication::platformName() != u"offscreen") &&
        config.getBool("editor.render", true))
    {
        const std::string backend = config.getString("rhi.backend", "vulkan");
        Meow2::Result<std::unique_ptr<Meow2::Editor::EditorRenderer>> made = Meow2::Editor::EditorRenderer::create(
            {.assets = assets, .events = events, .jobs = jobs, .log = engineLog},
            {.backend = backend == "d3d12" ? Meow2::Rhi::RhiBackend::D3D12 : Meow2::Rhi::RhiBackend::Vulkan,
             .validation = config.getBool("rhi.validation", false),
             .presentation = !arguments->headless});
        if (made)
        {
            renderer = std::move(*made);
        }
        else
        {
            MEOW2_LOG_ERROR(appLog, "The viewports can't draw: {}", made.error());
        }
    }
    // The project folder (editor.project) and its scene: the most recent one, or a new one.
    Meow2::Editor::SceneDocument document{{.commands = commands,
                                           .history = history,
                                           .config = config,
                                           .world = world,
                                           .renderer = renderer.get(),
                                           .files = files,
                                           .assets = assets,
                                           .log = appLog}};

    // The edit commands (entity.*, component.*, selection.*) the Hierarchy and Inspector docks run.
    Meow2::Editor::SceneEditor editor{{.commands = commands, .world = world, .components = components, .log = appLog}};

    Meow2::Editor::EditorMainWindow window{{.commands = commands,
                                            .settings = settings,
                                            .console = consoleLines,
                                            .log = appLog,
                                            .world = world,
                                            .input = input,
                                            .document = document,
                                            .editor = editor,
                                            .renderer = renderer.get(),
                                            .headlessSize = headlessSize(*arguments, config, renderer.get())}};
    if (!arguments->headless)
    {
        Meow2::Editor::showWithDesc(
            window, Meow2::Platform::windowDescFromConfig(config, {.title = "Meow2Editor"}, engineLog), engineLog);
    }

    // The MCP server (techstack.md §20, docs/wiki/Mcp.md): every command as a tool, for AI agents. Off unless --mcp
    // (or editor.mcp.enabled); 127.0.0.1 only, and every request must carry this run's token.
    const bool mcpOn = arguments->mcp || config.getBool("editor.mcp.enabled", false);
    const QByteArray mcpToken = QByteArray::fromStdString(arguments->mcpToken.value_or(randomToken()));
    Meow2::Editor::McpProtocol mcpProtocol{
        commands,
        {.serverName = u"Meow2Editor"_s,
         .serverVersion = QString::fromLatin1(Meow2::getVersionString()),
         .instructions = c_mcpInstructions,
         .token = mcpToken,
         // project.open could point the editor at any folder; agents work in the project it was started with.
         .hidden = {u"project.open"_s, u"project.new"_s, u"mcp.copySetup"_s}},
        appLog};
    Meow2::Editor::McpHttpServer mcpServer{mcpProtocol, appLog};
    QString mcpSetup;
    if (mcpOn)
    {
        const auto port = static_cast<std::uint16_t>(std::clamp<std::int64_t>(
            arguments->mcpPort.value_or(config.getInt("editor.mcp.port", c_defaultMcpPort)), 0, 65535));
        if (const Meow2::Result<std::uint16_t> listening = mcpServer.start(port); listening)
        {
            // The token isn't logged (logs get shared); Help → Copy MCP setup has it, or the agent chose it.
            mcpSetup = QString::fromLatin1("claude mcp add --transport http meow2 http://127.0.0.1:%1/mcp --header "
                                           "\"Authorization: Bearer %2\"")
                           .arg(*listening)
                           .arg(QString::fromLatin1(mcpToken));
            std::printf("MCP server on http://127.0.0.1:%u/mcp\n", static_cast<unsigned>(*listening));
            std::fflush(stdout); // read by whoever started it (a test, an agent), maybe through a pipe
        }
        else
        {
            MEOW2_LOG_ERROR(appLog, "MCP server not started: {}", listening.error());
        }
    }
    [[maybe_unused]] const Meow2::Result<void> copySetup =
        commands.add({.name = u"mcp.copySetup"_s,
                      .description = u"Copies the command that connects Claude Code to this editor's MCP server."_s,
                      .isEnabled =
                          [&mcpSetup]
                      {
                          return !mcpSetup.isEmpty();
                      }},
                     [&mcpSetup](const QJsonObject&) -> Meow2::Result<Meow2::Editor::CommandOutcome>
                     {
                         QGuiApplication::clipboard()->setText(mcpSetup);
                         return Meow2::Editor::CommandOutcome{};
                     });

    // The game loop (techstack.md §23). Qt runs the event loop and pumps the OS events itself, so a timer calls
    // tick() and the loop has no pumpEvents. Nothing to simulate or draw yet; app.quitAfterFrames ends a smoke run.
    const std::int64_t quitAfterFrames = config.getInt("app.quitAfterFrames", 0);
    Meow2::Time::FrameClock clock;
    Meow2::Time::GameLoop loop{clock,
                               events,
                               Meow2::Time::gameLoopDescFromConfig(config, engineLog),
                               {.update = [](double)
                                {
                                    MEOW2_PROFILE_MEMORY(Meow2::Memory::globalTracker());
                                }}}; // memory plots in Tracy
    // Frame pacing. Qt's default (coarse) timers run on the Windows timer tick of ~15.6 ms, so a 16 ms timer fired
    // every 15 or 31 ms: uneven frames, at most 60 a second whatever the display. Now a frame that drew a viewport is
    // followed at once (interval 0: as soon as Qt's queued events are handled), and drawing waits for the display's
    // vsync, which paces the editor at the display's refresh rate. With nothing drawn (hidden or minimised viewports)
    // nothing would wait, so a precise 16 ms timer paces the loop instead of spinning a core.
    QTimer frameTimer;
    frameTimer.setTimerType(Qt::PreciseTimer);
    QObject::connect(&frameTimer, &QTimer::timeout,
                     [&]
                     {
                         loop.tick();
                         window.tick(clock.deltaSecondsFloat()); // console, viewports, toolbar state
                         // Headless frames draw offscreen, which waits for no display, so 16 ms paces them too.
                         frameTimer.setInterval(window.viewports().drewLastFrame() && !arguments->headless ? 0 : 16);
                         if (quitAfterFrames > 0 && static_cast<std::int64_t>(clock.frameCount()) >= quitAfterFrames)
                         {
                             QApplication::quit();
                         }
                     });
    frameTimer.start(16);
    const int exitCode = QApplication::exec(); // returns when the window is closed (or quit() is called)

    // Printed too, not only logged: the smoke tests read stdout, and Release logs to the file only.
    std::printf("Ran %llu frames\n", static_cast<unsigned long long>(clock.frameCount()));
    MEOW2_LOG_INFO(appLog, "Ran {} frames ({} fixed steps) in {:.2f} s", clock.frameCount(), loop.fixedStepCount(),
                   clock.totalSeconds());
    // Reopen where the user left it, docks included. Not after a headless run: its window was never shown (the
    // offscreen platform gives it 0, 0 and a made-up size), and saving that would move the user's next window.
    if (!arguments->headless)
    {
        Meow2::Editor::storeWindowState(config, window);
        window.saveLayout();
    }
    if (const Meow2::Result<void> saved = config.save(); !saved)
    {
        MEOW2_LOG_WARN(appLog, "Settings not saved: {}", saved.error());
    }
    mcpServer.stop(); // no request may run a command while the rest goes away
    commands.remove(u"mcp.copySetup"_s);
    MEOW2_LOG_INFO(appLog, "Meow2Editor exiting");
    return exitCode;
}
