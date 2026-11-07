/// @file
/// @brief RHI tests that need a real GPU and a desktop (`MEOW2_GPU_TESTS=ON`). Every test requires validation to stay
///        clean when the layer is installed.

#include <Meow2/Core/Events/EventBus.h>
#include <Meow2/Core/Log/LogSystem.h>
#include <Meow2/Core/Memory/GlobalTracking.h>
#include <Meow2/Graphics/Rhi/IRhiDevice.h>
#include <Meow2/Math/Color.h>
#include <Meow2/Platform/Platform.h>
#include <Meow2/Platform/Window/Window.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <utility>

namespace Meow2::Rhi
{
namespace
{
/// @brief A device with validation on, plus what it takes to open windows.
class RhiGpuTest : public ::testing::Test
{
protected:
    RhiGpuTest()
        : m_logSystem({.appName = "Test", .directory = {}, .console = true})
        , m_log(m_logSystem.createLogger("RHI"))
    {
    }

    void SetUp() override
    {
        Result<std::unique_ptr<IRhiDevice>> device =
            createRhiDevice({.appName = "Meow2 tests", .validation = true}, m_log);
        ASSERT_TRUE(device) << device.error().message;
        m_device = std::move(*device);
        Result<Platform::Platform> platform = Platform::Platform::create({}, m_log, m_events);
        ASSERT_TRUE(platform) << platform.error().message;
        m_platform.emplace(std::move(*platform));
    }

    void TearDown() override
    {
        m_platform.reset();
    }

    /// @brief Opens a window and a swapchain on it. vsync is off so the tests don't wait for the display.
    std::unique_ptr<IRhiSwapchain> makeSwapchain(Platform::Window& window)
    {
        Result<std::unique_ptr<IRhiSwapchain>> swapchain =
            m_device->createSwapchain({.window = window.nativeHandle(),
                                       .width = static_cast<std::uint32_t>(window.pixelWidth()),
                                       .height = static_cast<std::uint32_t>(window.pixelHeight()),
                                       .vsync = false});
        EXPECT_TRUE(swapchain) << swapchain.error().message;
        return swapchain ? std::move(*swapchain) : nullptr;
    }

    Platform::Window makeWindow(std::int32_t width = 320, std::int32_t height = 200)
    {
        Result<Platform::Window> window =
            Platform::Window::create(*m_platform, m_log, {.title = "RhiGpuTests", .width = width, .height = height});
        EXPECT_TRUE(window) << window.error().message;
        return std::move(*window);
    }

    /// @brief Runs one frame that clears each given swapchain.
    template <class... Swapchains>
    void clearFrame(Swapchains&... swapchains)
    {
        Result<IRhiCommandList*> list = m_device->beginFrame();
        ASSERT_TRUE(list) << list.error().message;
        const auto clear = [&](IRhiSwapchain& swapchain)
        {
            if (m_device->acquire(swapchain))
            {
                (*list)->clearSwapchain(swapchain, Math::Color{.r = 0.1F, .g = 0.2F, .b = 0.4F});
            }
        };
        (clear(swapchains), ...);
        const Result<void> ended = m_device->endFrame();
        ASSERT_TRUE(ended) << ended.error().message;
    }

    Log::LogSystem m_logSystem;
    Log::Logger m_log;
    Events::EventBus m_events{Memory::globalTracker()};
    std::unique_ptr<IRhiDevice> m_device;
    std::optional<Platform::Platform> m_platform;
};
} // namespace

TEST_F(RhiGpuTest, DeviceCreatesCleanly)
{
    EXPECT_EQ(m_device->backend(), RhiBackend::Vulkan);
    EXPECT_FALSE(m_device->adapter().name.empty());
    EXPECT_FALSE(m_device->adapter().apiVersion.empty());
    EXPECT_EQ(m_device->validationErrorCount(), 0U);
}

TEST_F(RhiGpuTest, ValidationIsActive)
{
    // Otherwise "validation stays clean" below proves nothing. The layer comes with the Vulkan SDK.
    EXPECT_TRUE(m_device->validationActive());
}

TEST_F(RhiGpuTest, EmptyFramesRun)
{
    for (int i = 0; i < 5; ++i)
    {
        ASSERT_TRUE(m_device->beginFrame());
        ASSERT_TRUE(m_device->endFrame());
    }
    EXPECT_EQ(m_device->validationErrorCount(), 0U);
}

TEST_F(RhiGpuTest, SwapchainClearsFrames)
{
    Platform::Window window = makeWindow();
    {
        const std::unique_ptr<IRhiSwapchain> swapchain = makeSwapchain(window);
        ASSERT_NE(swapchain, nullptr);
        EXPECT_GT(swapchain->width(), 0U);
        EXPECT_NE(swapchain->format(), RhiFormat::Unknown);
        for (int i = 0; i < 20; ++i)
        {
            clearFrame(*swapchain);
        }
    }
    EXPECT_EQ(m_device->validationErrorCount(), 0U);
}

TEST_F(RhiGpuTest, TwoWindowsShareOneDevice)
{
    Platform::Window first = makeWindow();
    Platform::Window second = makeWindow(200, 120);
    {
        const std::unique_ptr<IRhiSwapchain> a = makeSwapchain(first);
        const std::unique_ptr<IRhiSwapchain> b = makeSwapchain(second);
        ASSERT_NE(a, nullptr);
        ASSERT_NE(b, nullptr);
        for (int i = 0; i < 20; ++i)
        {
            clearFrame(*a, *b);
        }
    }
    EXPECT_EQ(m_device->validationErrorCount(), 0U);
}

TEST_F(RhiGpuTest, SwapchainResizes)
{
    Platform::Window window = makeWindow();
    {
        const std::unique_ptr<IRhiSwapchain> swapchain = makeSwapchain(window);
        ASSERT_NE(swapchain, nullptr);
        clearFrame(*swapchain);

        window.setSize(400, 300);
        m_platform->pumpEvents();
        const Result<void> resized = swapchain->resize(static_cast<std::uint32_t>(window.pixelWidth()),
                                                       static_cast<std::uint32_t>(window.pixelHeight()));
        ASSERT_TRUE(resized) << resized.error().message;
        EXPECT_EQ(swapchain->width(), static_cast<std::uint32_t>(window.pixelWidth()));
        clearFrame(*swapchain);
    }
    EXPECT_EQ(m_device->validationErrorCount(), 0U);
}

TEST_F(RhiGpuTest, ZeroSizeSkipsDrawing)
{
    Platform::Window window = makeWindow();
    {
        const std::unique_ptr<IRhiSwapchain> swapchain = makeSwapchain(window);
        ASSERT_NE(swapchain, nullptr);
        ASSERT_TRUE(swapchain->resize(0, 0));
        EXPECT_EQ(swapchain->width(), 0U);

        ASSERT_TRUE(m_device->beginFrame());
        EXPECT_FALSE(m_device->acquire(*swapchain));
        ASSERT_TRUE(m_device->endFrame());

        const Result<void> restored = swapchain->resize(static_cast<std::uint32_t>(window.pixelWidth()),
                                                        static_cast<std::uint32_t>(window.pixelHeight()));
        ASSERT_TRUE(restored) << restored.error().message;
        clearFrame(*swapchain);
    }
    EXPECT_EQ(m_device->validationErrorCount(), 0U);
}
} // namespace Meow2::Rhi
