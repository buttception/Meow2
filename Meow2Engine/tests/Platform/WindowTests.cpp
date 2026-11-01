/// @file
/// @brief Tests for Platform and Window over SDL3. They use SDL's `offscreen` video driver, so they run without a
///        desktop (Linux CI); one Windows-only test uses the real driver with a hidden window.

#include <Meow2/Core/Log/LogSystem.h>
#include <Meow2/Core/Memory/GlobalTracking.h>
#include <Meow2/Platform/Platform.h>
#include <Meow2/Platform/Window/Window.h>

#include <gtest/gtest.h>
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_video.h>

#include <filesystem>
#include <format>
#include <optional>
#include <system_error>

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
        Meow2::Result<Platform> platform = Platform::create({.videoDriver = "offscreen"}, m_log);
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

TEST_F(WindowTest, RejectsBadSize)
{
    const Meow2::Result<Window> window = Window::create(*m_platform, m_log, {.width = 0, .height = 600});
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
    const Meow2::Result<Platform> second = Platform::create({.videoDriver = "offscreen"}, m_log);
    ASSERT_FALSE(second.has_value());
    EXPECT_EQ(second.error().code, Meow2::ErrorCode::AlreadyExists);
}

TEST_F(WindowTest, PlatformCanBeRecreated)
{
    m_platform.reset();
    Meow2::Result<Platform> again = Platform::create({.videoDriver = "offscreen"}, m_log);
    ASSERT_TRUE(again.has_value()) << std::format("{}", again.error());
    m_platform.emplace(std::move(*again));
    const Window window = createWindow({.hidden = true});
    EXPECT_EQ(window.bounds().width, 1280);
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
        Meow2::Result<Platform> platform = Platform::create({.videoDriver = "offscreen"}, m_log);
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
    const Meow2::Result<Platform> platform = Platform::create({.videoDriver = "no-such-driver"}, m_log);
    ASSERT_FALSE(platform.has_value());
    EXPECT_EQ(platform.error().code, Meow2::ErrorCode::Unsupported);
}

#if defined(_WIN32)
TEST(WindowWin32, NativeHandle)
{
    const fs::path directory = fs::temp_directory_path() / std::format("meow2-window-{}-native", Meow2::c_configName);
    Meow2::Log::LogSystem logSystem{{.appName = "Test", .directory = directory, .console = false}};
    Meow2::Log::Logger log = logSystem.createLogger("Engine");
    Meow2::Result<Platform> platform = Platform::create({}, log); // the real Windows driver
    ASSERT_TRUE(platform.has_value()) << std::format("{}", platform.error());
    EXPECT_EQ(platform->videoDriver(), "windows");

    Meow2::Result<Window> window = Window::create(*platform, log, {.width = 320, .height = 240, .hidden = true});
    ASSERT_TRUE(window.has_value()) << std::format("{}", window.error());
    const Meow2::Platform::NativeWindowHandle handle = window->nativeHandle();
    EXPECT_EQ(handle.type, Meow2::Platform::NativeWindowType::Win32);
    EXPECT_NE(handle.window, nullptr);
}
#endif
