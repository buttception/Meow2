/// @file
/// @brief Tests for UploadRing, ReadbackQueue and OffscreenTarget on a real GPU (`MEOW2_GPU_TESTS=ON`).

#include "Graphics/Rhi/RhiTestBackend.h"

#include <Meow2/Core/Log/LogSystem.h>
#include <Meow2/Graphics/Renderer/FrameResources.h>
#include <Meow2/Graphics/Rhi/IRhiDevice.h>
#include <Meow2/Math/Color.h>

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <span>
#include <utility>
#include <vector>

namespace Meow2::Renderer
{
namespace
{
/// @brief A headless device with validation.
class FrameResourcesTest : public ::testing::Test
{
protected:
    FrameResourcesTest()
        : m_logSystem({.appName = "Test", .directory = {}, .console = true})
        , m_log(m_logSystem.createLogger("RHI"))
    {
    }

    void SetUp() override
    {
        Result<std::unique_ptr<Rhi::IRhiDevice>> device = Rhi::createRhiDevice(
            {.backend = Rhi::c_testBackend, .appName = "Meow2 tests", .validation = true, .presentation = false},
            m_log);
        ASSERT_TRUE(device) << device.error().message;
        m_device = std::move(*device);
    }

    Log::LogSystem m_logSystem;
    Log::Logger m_log;
    std::unique_ptr<Rhi::IRhiDevice> m_device;
};
} // namespace

TEST_F(FrameResourcesTest, UploadRingAlignsAndAdvances)
{
    Result<std::unique_ptr<UploadRing>> ring = UploadRing::create(*m_device, 1024);
    ASSERT_TRUE(ring) << ring.error().message;
    (*ring)->beginFrame(0);

    const Result<UploadAllocation> first = (*ring)->allocate(10);
    const Result<UploadAllocation> second = (*ring)->allocate(10, 64);
    ASSERT_TRUE(first);
    ASSERT_TRUE(second);
    EXPECT_EQ(first->offset, 0U);
    EXPECT_EQ(second->offset, 64U);
    EXPECT_EQ(second->memory.size(), 10U);
    EXPECT_NE(first->heapIndex, Rhi::c_invalidBindlessIndex);
}

TEST_F(FrameResourcesTest, UploadRingFullIsAnError)
{
    Result<std::unique_ptr<UploadRing>> ring = UploadRing::create(*m_device, 64);
    ASSERT_TRUE(ring) << ring.error().message;
    (*ring)->beginFrame(0);

    ASSERT_TRUE((*ring)->allocate(64));
    const Result<UploadAllocation> overflow = (*ring)->allocate(1);
    ASSERT_FALSE(overflow);
    EXPECT_EQ(overflow.error().code, ErrorCode::OutOfMemory);

    (*ring)->beginFrame(1);
    EXPECT_TRUE((*ring)->allocate(64));
}

TEST_F(FrameResourcesTest, UploadRingSlotsUseDifferentBuffers)
{
    Result<std::unique_ptr<UploadRing>> ring = UploadRing::create(*m_device, 64);
    ASSERT_TRUE(ring) << ring.error().message;
    (*ring)->beginFrame(0);
    const Result<UploadAllocation> first = (*ring)->allocate(16);
    (*ring)->beginFrame(1);
    const Result<UploadAllocation> second = (*ring)->allocate(16);
    ASSERT_TRUE(first);
    ASSERT_TRUE(second);
    EXPECT_NE(first->buffer, second->buffer);
}

TEST_F(FrameResourcesTest, ReadbackArrivesLater)
{
    constexpr std::array<std::uint32_t, 3> c_values{7, 8, 9};
    Result<std::unique_ptr<UploadRing>> ring = UploadRing::create(*m_device, 1024);
    ASSERT_TRUE(ring) << ring.error().message;
    ReadbackQueue readback{*m_device};
    std::vector<std::uint32_t> received;

    Result<Rhi::IRhiCommandList*> list = m_device->beginFrame();
    ASSERT_TRUE(list);
    (*ring)->beginFrame(m_device->frameSlot());
    readback.beginFrame(m_device->frameSlot());
    const Result<UploadAllocation> upload = (*ring)->allocate(sizeof(c_values));
    ASSERT_TRUE(upload);
    std::memcpy(upload->memory.data(), c_values.data(), sizeof(c_values));
    ASSERT_TRUE(readback.request(**list, upload->buffer, sizeof(c_values),
                                 [&received](std::span<const std::byte> bytes)
                                 {
                                     received.resize(bytes.size() / sizeof(std::uint32_t));
                                     std::memcpy(received.data(), bytes.data(), bytes.size());
                                 }));
    ASSERT_TRUE(m_device->endFrame());
    EXPECT_TRUE(received.empty()) << "nothing arrives in the frame that asked";

    // Round to the slot that asked: one callback, once.
    for (std::uint32_t i = 0; i < Rhi::c_maxFramesInFlight; ++i)
    {
        ASSERT_TRUE(m_device->beginFrame());
        readback.beginFrame(m_device->frameSlot());
        ASSERT_TRUE(m_device->endFrame());
    }
    ASSERT_EQ(received.size(), c_values.size());
    EXPECT_EQ(received[2], 9U);
    m_device->waitIdle();
    EXPECT_EQ(m_device->validationErrorCount(), 0U);
}

TEST_F(FrameResourcesTest, OffscreenTargetReadsBack)
{
    Result<std::unique_ptr<OffscreenTarget>> target = OffscreenTarget::create(*m_device, 8, 8);
    ASSERT_TRUE(target) << target.error().message;

    for (int frame = 0; frame < 2; ++frame) // twice: the second frame starts from a different state
    {
        Result<Rhi::IRhiCommandList*> list = m_device->beginFrame();
        ASSERT_TRUE(list);
        (*target)->begin(**list, Math::Color{.r = 1.0F, .g = 0.0F, .b = 0.0F, .a = 1.0F});
        (*target)->end(**list);
        ASSERT_TRUE(m_device->endFrame());
        m_device->waitIdle();
    }

    const std::span<const std::byte> pixels = (*target)->pixels();
    ASSERT_EQ(pixels.size(), std::size_t{8 * 8 * 4});
    EXPECT_EQ(pixels[0], std::byte{255});
    EXPECT_EQ(pixels[1], std::byte{0});
    EXPECT_EQ(m_device->validationErrorCount(), 0U);
}
} // namespace Meow2::Renderer
