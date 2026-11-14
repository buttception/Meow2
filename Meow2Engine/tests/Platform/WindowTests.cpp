/// @file
/// @brief Tests for Platform and Window over SDL3. They use SDL's `offscreen` video driver, so they run without a
///        desktop (Linux CI); one Windows-only test uses the real driver with a hidden window.

#include <Meow2/Core/Events/EventBus.h>
#include <Meow2/Core/Log/LogSystem.h>
#include <Meow2/Core/Memory/GlobalTracking.h>
#include <Meow2/Platform/Platform.h>
#include <Meow2/Platform/Window/Window.h>
#include <Meow2/Platform/Window/WindowEvents.h>

#include <gtest/gtest.h>
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_video.h>

#include <cstddef>
#include <filesystem>
#include <format>
#include <memory_resource>
#include <optional>
#include <system_error>
#include <vector>

namespace
{
namespace fs = std::filesystem;
using Meow2::Platform::Platform;
using Meow2::Platform::Window;
using Meow2::Platform::WindowDesc;
using Meow2::Platform::WindowMode;

/// @brief A log in a fresh folder and an offscreen Platform for each test.
class WindowTest : public ::testing::Test
{
protected:
    WindowTest()
        : m_logSystem({.appName = "Test", .directory = logDirectory(), .console = false})
        , m_log(m_logSystem.createLogger("Engine"))
    {
    }

    void SetUp() override
    {
        Meow2::Result<Platform> platform = Platform::create({.videoDriver = "offscreen"}, m_log, m_events);
        ASSERT_TRUE(platform.has_value()) << std::format("{}", platform.error());
        m_platform.emplace(std::move(*platform));
    }

    /// @brief Creates a window, failing the test if that fails.
    Window createWindow(const WindowDesc& desc)
    {
        Meow2::Result<Window> window = Window::create(*m_platform, m_log, desc);
        EXPECT_TRUE(window.has_value()) << (window ? "" : std::format("{}", window.error()));
        return std::move(*window);
    }

    Meow2::Log::LogSystem m_logSystem;
    Meow2::Log::Logger m_log;
    Meow2::Events::EventBus m_events{*std::pmr::new_delete_resource()}; // outlives the Platform (declared first)
    std::optional<Platform> m_platform; // optional: created in SetUp, where a failure can be reported

private:
    static fs::path logDirectory()
    {
        const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
        const fs::path directory =
            fs::temp_directory_path() / std::format("meow2-window-{}-{}", Meow2::c_configName, info->name());
        std::error_code ignored;
        fs::remove_all(directory, ignored);
        return directory;
    }
};
} // namespace

TEST_F(WindowTest, CreatesWithDesc)
{
    const Window window = createWindow({.title = "Test", .width = 800, .height = 600, .hidden = true});
    EXPECT_EQ(m_platform->videoDriver(), "offscreen");
    EXPECT_EQ(window.title(), "Test");
    EXPECT_EQ(window.bounds().width, 800);
    EXPECT_EQ(window.bounds().height, 600);
    EXPECT_GE(window.pixelWidth(), 800);
    EXPECT_EQ(window.restoredBounds(), window.bounds());
    EXPECT_EQ(window.mode(), WindowMode::Windowed);
    EXPECT_FALSE(window.closeRequested());
    EXPECT_FALSE(window.wasResized());
    EXPECT_EQ(window.nativeHandle().type, Meow2::Platform::NativeWindowType::None) << "offscreen has no OS window";
}

TEST_F(WindowTest, ListsDisplays)
{
    const std::vector<Meow2::Platform::DisplayInfo> displays = m_platform->displays();
    ASSERT_FALSE(displays.empty()) << "the offscreen driver has one display";
    std::size_t primaries = 0;
    for (const Meow2::Platform::DisplayInfo& display : displays)
    {
        primaries += display.primary ? 1U : 0U;
        EXPECT_GT(display.usable.width, 0);
        EXPECT_GT(display.usable.height, 0);
    }
    EXPECT_EQ(primaries, 1U) << "exactly one primary display";
}

TEST_F(WindowTest, UnfocusableWindowIsNotFocusable)
{
    const Window normal = createWindow({.width = 320, .height = 200, .hidden = true});
    EXPECT_TRUE(normal.focusable());
    const Window quiet = createWindow({.width = 320, .height = 200, .hidden = true, .focusable = false});
    EXPECT_FALSE(quiet.focusable()) << "shown without activating it, so it never takes focus";
}

TEST_F(WindowTest, RejectsBadSize)
{
    // 0 means the default share of the display; a negative size is invalid.
    const Meow2::Result<Window> window = Window::create(*m_platform, m_log, {.width = -5, .height = 600});
    ASSERT_FALSE(window.has_value());
    EXPECT_EQ(window.error().code, Meow2::ErrorCode::InvalidArgument);
}

TEST_F(WindowTest, CloseRequest)
{
    const Window window = createWindow({.hidden = true});
    int count = 0;
    SDL_Window** windows = SDL_GetWindows(&count);
    ASSERT_EQ(count, 1);
    SDL_Event event{};
    event.type = SDL_EVENT_WINDOW_CLOSE_REQUESTED;
    event.window.windowID = SDL_GetWindowID(windows[0]);
    SDL_free(windows);
    ASSERT_TRUE(SDL_PushEvent(&event));

    m_platform->pumpEvents();
    EXPECT_TRUE(window.closeRequested());
}

TEST_F(WindowTest, ResizeUpdatesState)
{
    Window window = createWindow({.width = 640, .height = 480, .hidden = true});
    window.setSize(1024, 768);
    m_platform->pumpEvents();
    EXPECT_EQ(window.bounds().width, 1024);
    EXPECT_EQ(window.bounds().height, 768);
    EXPECT_EQ(window.restoredBounds().width, 1024);
    EXPECT_TRUE(window.wasResized());

    m_platform->pumpEvents();
    EXPECT_FALSE(window.wasResized()) << "reported for one pump only";
}

TEST_F(WindowTest, MoveUpdatesState)
{
    Window window = createWindow({.width = 640, .height = 480, .x = 10, .y = 20, .hidden = true});
    window.setPosition(50, 60);
    m_platform->pumpEvents();
    EXPECT_EQ(window.bounds().x, 50);
    EXPECT_EQ(window.bounds().y, 60);
    EXPECT_FALSE(window.wasResized());
}

TEST_F(WindowTest, RestoredBoundsSurviveFullscreen)
{
    Window window = createWindow({.width = 640, .height = 480, .hidden = true});
    window.setMode(WindowMode::Fullscreen);
    m_platform->pumpEvents();
    EXPECT_EQ(window.mode(), WindowMode::Fullscreen);
    EXPECT_EQ(window.restoredBounds().width, 640) << "fullscreen bounds aren't the ones to restore to";
    EXPECT_EQ(window.restoredBounds().height, 480);

    window.setMode(WindowMode::Windowed);
    m_platform->pumpEvents();
    EXPECT_EQ(window.mode(), WindowMode::Windowed);
    EXPECT_EQ(window.bounds().width, 640);
}

TEST_F(WindowTest, CreatesFullscreen)
{
    const Window window = createWindow({.width = 640, .height = 480, .mode = WindowMode::Fullscreen, .hidden = true});
    EXPECT_EQ(window.mode(), WindowMode::Fullscreen);
    EXPECT_EQ(window.restoredBounds().width, 640);
    EXPECT_EQ(window.restoredBounds().height, 480);
}

TEST_F(WindowTest, OffscreenPositionIsCentred)
{
    const Window window = createWindow({.width = 640, .height = 480, .x = -100000, .y = -100000, .hidden = true});
    EXPECT_GT(window.bounds().x, -100000) << "a position on no display falls back to centred";
}

TEST_F(WindowTest, SecondPlatformFails)
{
    const Meow2::Result<Platform> second = Platform::create({.videoDriver = "offscreen"}, m_log, m_events);
    ASSERT_FALSE(second.has_value());
    EXPECT_EQ(second.error().code, Meow2::ErrorCode::AlreadyExists);
}

TEST_F(WindowTest, PlatformCanBeRecreated)
{
    m_platform.reset();
    Meow2::Result<Platform> again = Platform::create({.videoDriver = "offscreen"}, m_log, m_events);
    ASSERT_TRUE(again.has_value()) << std::format("{}", again.error());
    m_platform.emplace(std::move(*again));
    const Window window = createWindow({.hidden = true});
    // No size: 80 % of the display's usable area (SDL's offscreen display is 1024 x 768, scale 1).
    EXPECT_EQ(window.bounds().width, static_cast<std::int32_t>(1024 * Meow2::Platform::c_defaultScreenShare));
    EXPECT_EQ(window.bounds().height, static_cast<std::int32_t>(768 * Meow2::Platform::c_defaultScreenShare));
    EXPECT_FLOAT_EQ(window.unitsPerLogical(), 1.0F);
}

TEST_F(WindowTest, TooBigShrinksToTheDisplay)
{
    // 4000 x 2250 logical units on a 1024 x 768 display: shrunk to its usable area less room for the frame, 16:9 kept.
    const Window window = createWindow({.width = 4000, .height = 2250, .hidden = true});
    EXPECT_LE(window.bounds().width, 1024 - 16);
    EXPECT_LE(window.bounds().height, 768 - 48);
    EXPECT_NEAR(static_cast<double>(window.bounds().width) / window.bounds().height, 16.0 / 9.0, 0.02);
}

TEST_F(WindowTest, SdlMemoryIsTracked)
{
    if (!Meow2::Memory::c_memoryTrackingAvailable)
    {
        GTEST_SKIP() << "SDL's memory hook is only installed with memory tracking (Debug/RelWithDebInfo)";
    }
    using Meow2::Memory::globalTracker;
    using Meow2::Memory::MemTag;
    m_platform.reset();
    const Meow2::Memory::MemTagStats before = globalTracker().stats(MemTag::Platform);
    {
        Meow2::Result<Platform> platform = Platform::create({.videoDriver = "offscreen"}, m_log, m_events);
        ASSERT_TRUE(platform.has_value()) << std::format("{}", platform.error());
        Meow2::Result<Window> window = Window::create(*platform, m_log, {.hidden = true});
        ASSERT_TRUE(window.has_value());
        EXPECT_GT(globalTracker().stats(MemTag::Platform).liveAllocations, before.liveAllocations)
            << "SDL allocates through the hook, under MemTag::Platform";
    }
    EXPECT_EQ(globalTracker().stats(MemTag::Platform).liveAllocations, before.liveAllocations)
        << "SDL_Quit frees everything SDL allocated";
}

TEST_F(WindowTest, UnknownDriverFails)
{
    m_platform.reset();
    const Meow2::Result<Platform> platform = Platform::create({.videoDriver = "no-such-driver"}, m_log, m_events);
    ASSERT_FALSE(platform.has_value());
    EXPECT_EQ(platform.error().code, Meow2::ErrorCode::Unsupported);
}

namespace
{
using Meow2::Platform::QuitRequested;
using Meow2::Platform::WindowCloseRequested;
using Meow2::Platform::WindowMoved;
using Meow2::Platform::WindowResized;
using Meow2::Platform::WindowStateChanged;

/// @brief WindowTest plus a record of every window and quit event the bus delivers.
class WindowEventsTest : public WindowTest
{
protected:
    /// @brief Pushes an SDL event of `type` for `window`, as the OS would.
    static void pushWindowEvent(SDL_EventType type, const Window& window)
    {
        SDL_Event event{};
        event.type = type;
        event.window.windowID = window.id();
        ASSERT_TRUE(SDL_PushEvent(&event));
    }

    /// @brief Pumps and delivers the queue, the way an app does once per frame.
    void pumpAndDispatch()
    {
        m_platform->pumpEvents();
        m_events.dispatchQueued();
    }

    /// @brief How many events of any kind arrived.
    [[nodiscard]] std::size_t totalEvents() const
    {
        return m_resized.size() + m_moved.size() + m_stateChanged.size() + m_closeRequested.size() + m_quitCount;
    }

    std::vector<WindowResized> m_resized;
    std::vector<WindowMoved> m_moved;
    std::vector<WindowStateChanged> m_stateChanged;
    std::vector<WindowCloseRequested> m_closeRequested;
    std::size_t m_quitCount = 0;

    // Last members: destroyed first, before the vectors their handlers write to (techstack.md §22).
    Meow2::Events::Subscription m_onResized =
        m_events.subscribe<WindowResized>([this](const WindowResized& e) { m_resized.push_back(e); });
    Meow2::Events::Subscription m_onMoved =
        m_events.subscribe<WindowMoved>([this](const WindowMoved& e) { m_moved.push_back(e); });
    Meow2::Events::Subscription m_onStateChanged =
        m_events.subscribe<WindowStateChanged>([this](const WindowStateChanged& e) { m_stateChanged.push_back(e); });
    Meow2::Events::Subscription m_onCloseRequested = m_events.subscribe<WindowCloseRequested>(
        [this](const WindowCloseRequested& e) { m_closeRequested.push_back(e); });
    Meow2::Events::Subscription m_onQuit =
        m_events.subscribe<QuitRequested>([this](const QuitRequested&) { ++m_quitCount; });
};
} // namespace

TEST_F(WindowEventsTest, ResizeEnqueuesResized)
{
    Window window = createWindow({.width = 640, .height = 480, .hidden = true});
    window.setSize(1024, 768);
    pumpAndDispatch();
    ASSERT_EQ(m_resized.size(), 1U);
    EXPECT_EQ(m_resized[0].window, window.id());
    EXPECT_NE(window.id(), 0U);
    EXPECT_EQ(m_resized[0].width, 1024);
    EXPECT_EQ(m_resized[0].height, 768);
    EXPECT_EQ(m_resized[0].pixelWidth, window.pixelWidth());
    EXPECT_EQ(m_resized[0].pixelHeight, window.pixelHeight());
}

TEST_F(WindowEventsTest, QueuedUntilDispatch)
{
    Window window = createWindow({.width = 640, .height = 480, .hidden = true});
    window.setSize(800, 600);
    m_platform->pumpEvents();
    EXPECT_TRUE(m_resized.empty()) << "pumpEvents only enqueues; no handler runs inside it";
    m_events.dispatchQueued();
    EXPECT_EQ(m_resized.size(), 1U);
}

TEST_F(WindowEventsTest, MoveEnqueuesMoved)
{
    Window window = createWindow({.width = 640, .height = 480, .x = 10, .y = 20, .hidden = true});
    window.setPosition(50, 60);
    pumpAndDispatch();
    ASSERT_EQ(m_moved.size(), 1U);
    EXPECT_EQ(m_moved[0].window, window.id());
    EXPECT_EQ(m_moved[0].x, 50);
    EXPECT_EQ(m_moved[0].y, 60);
    EXPECT_TRUE(m_resized.empty()) << "a move doesn't change the size";
}

TEST_F(WindowEventsTest, FullscreenEnqueuesStateChanged)
{
    Window window = createWindow({.width = 640, .height = 480, .hidden = true});
    window.setMode(WindowMode::Fullscreen);
    pumpAndDispatch();
    ASSERT_EQ(m_stateChanged.size(), 1U);
    EXPECT_EQ(m_stateChanged[0].window, window.id());
    EXPECT_EQ(m_stateChanged[0].mode, WindowMode::Fullscreen);
    EXPECT_FALSE(m_stateChanged[0].minimized);

    window.setMode(WindowMode::Windowed);
    pumpAndDispatch();
    ASSERT_EQ(m_stateChanged.size(), 2U);
    EXPECT_EQ(m_stateChanged[1].mode, WindowMode::Windowed);
}

TEST_F(WindowEventsTest, CloseRequestEnqueuedOnce)
{
    const Window window = createWindow({.hidden = true});
    pushWindowEvent(SDL_EVENT_WINDOW_CLOSE_REQUESTED, window);
    pushWindowEvent(SDL_EVENT_WINDOW_CLOSE_REQUESTED, window);
    pumpAndDispatch();
    pushWindowEvent(SDL_EVENT_WINDOW_CLOSE_REQUESTED, window);
    pumpAndDispatch();
    ASSERT_EQ(m_closeRequested.size(), 1U) << "sent once, however often the user clicks";
    EXPECT_EQ(m_closeRequested[0].window, window.id());
    EXPECT_TRUE(window.closeRequested());
}

TEST_F(WindowEventsTest, QuitEnqueuesQuitRequested)
{
    SDL_Event event{};
    event.type = SDL_EVENT_QUIT;
    ASSERT_TRUE(SDL_PushEvent(&event));
    ASSERT_TRUE(SDL_PushEvent(&event));
    pumpAndDispatch();
    pumpAndDispatch();
    EXPECT_EQ(m_quitCount, 1U);
    EXPECT_TRUE(m_platform->quitRequested());
}

TEST_F(WindowEventsTest, NoChangeNoEvents)
{
    Window window = createWindow({.width = 640, .height = 480, .hidden = true});
    window.setSize(800, 600);
    pumpAndDispatch();
    const std::size_t afterChange = totalEvents();
    pumpAndDispatch();
    pumpAndDispatch();
    EXPECT_EQ(totalEvents(), afterChange) << "nothing changed, so nothing is sent";
}

TEST_F(WindowEventsTest, CreationEnqueuesNothing)
{
    const Window window = createWindow({.width = 640, .height = 480, .hidden = true});
    pumpAndDispatch();
    EXPECT_EQ(totalEvents(), 0U) << "the initial state comes from create(), not from events";
}

#if defined(_WIN32)
TEST(WindowWin32, NativeHandle)
{
    const fs::path directory = fs::temp_directory_path() / std::format("meow2-window-{}-native", Meow2::c_configName);
    Meow2::Log::LogSystem logSystem{{.appName = "Test", .directory = directory, .console = false}};
    Meow2::Log::Logger log = logSystem.createLogger("Engine");
    Meow2::Events::EventBus events{*std::pmr::new_delete_resource()};
    Meow2::Result<Platform> platform = Platform::create({}, log, events); // the real Windows driver
    ASSERT_TRUE(platform.has_value()) << std::format("{}", platform.error());
    EXPECT_EQ(platform->videoDriver(), "windows");

    Meow2::Result<Window> window = Window::create(*platform, log, {.width = 320, .height = 240, .hidden = true});
    ASSERT_TRUE(window.has_value()) << std::format("{}", window.error());
    const Meow2::Platform::NativeWindowHandle handle = window->nativeHandle();
    EXPECT_EQ(handle.type, Meow2::Platform::NativeWindowType::Win32);
    EXPECT_NE(handle.window, nullptr);
}
#endif
