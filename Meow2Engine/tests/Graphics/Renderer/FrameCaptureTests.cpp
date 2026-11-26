/// @file
/// @brief Tests for FrameCapture on a real GPU: a captured frame keeps a copy of each texture a pass wrote, and a
///        copy comes back to the CPU as a small sRGB picture a frame or two after it's asked for.

#include "Graphics/Rhi/RhiTestBackend.h"

#include <Meow2/Assets/AssetManager.h>
#include <Meow2/Core/Events/EventBus.h>
#include <Meow2/Core/Id/UuidGenerator.h>
#include <Meow2/Core/Jobs/JobSystem.h>
#include <Meow2/Core/Log/LogSystem.h>
#include <Meow2/Core/Memory/GlobalTracking.h>
#include <Meow2/Graphics/Renderer/FrameCapture.h>
#include <Meow2/Graphics/Renderer/PipelineLibrary.h>
#include <Meow2/Graphics/Renderer/RenderDevice.h>
#include <Meow2/Graphics/Renderer/RenderGraph.h>
#include <Meow2/Platform/FileSystem/FileSystem.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <optional>

namespace Meow2::Renderer
{
namespace
{
constexpr std::uint32_t c_width = 64;
constexpr std::uint32_t c_height = 32;
constexpr std::uint32_t c_threads = 2; ///< Job threads, and so threads that may record (one command list each).

/// @brief A headless device, the pipeline library, a frame capture and a texture a pass clears to orange.
class FrameCaptureTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        ASSERT_TRUE(m_files.mountDirectory("cooked", MEOW2_COOKED_DIR).has_value());
        Result<std::unique_ptr<RenderDevice>> device = RenderDevice::create({.backend = Rhi::c_testBackend,
                                                                             .appName = "Meow2 tests",
                                                                             .validation = true,
                                                                             .presentation = false,
                                                                             .recordingThreads = c_threads},
                                                                            m_log);
        ASSERT_TRUE(device) << device.error().message;
        m_render = std::move(*device);
        m_library = std::make_unique<PipelineLibrary>(PipelineLibraryDeps{*m_render, m_assets, m_events, m_log});
        const Result<AssetId> shader = Uuid::parse("01a1b5e2-3c18-8000-80a4-7b19d2e6c059")
                                           .and_then([](const Uuid& u) { return AssetId::fromUuid(u); });
        ASSERT_TRUE(shader);
        m_capture = std::make_unique<FrameCapture>(*m_render, *m_library, *shader, m_log);
        Result<Rhi::TextureHandle> texture = m_render->device().createTexture({.width = c_width,
                                                                               .height = c_height,
                                                                               .format = Rhi::RhiFormat::Rgba16Float,
                                                                               .renderTarget = true,
                                                                               .name = "Scene colour"});
        ASSERT_TRUE(texture) << texture.error().message;
        m_texture = *texture;
    }

    void TearDown() override
    {
        m_render->device().waitIdle();
        m_capture.reset();
        m_render->device().destroy(m_texture);
        m_library.reset();
        m_render->device().waitIdle();
        EXPECT_EQ(m_render->device().validationErrorCount(), 0U);
    }

    /// @brief One frame: a pass clears the texture to orange, then the capture's steps.
    void frame()
    {
        Rhi::IRhiDevice& device = m_render->device();
        ASSERT_TRUE(device.beginFrame());
        const std::uint32_t slot = device.frameSlot();
        m_capture->beginFrame(slot);
        RenderGraph graph;
        const GraphResource colour = graph.import(m_texture, Rhi::ResourceState::Undefined, "Scene colour");
        const Rhi::TextureHandle texture = m_texture;
        graph.addPass(
            {.name = "Clear",
             .accesses = {{colour, Rhi::ResourceState::ColorTarget}},
             .record = [texture](Rhi::IRhiCommandList& list)
             {
                 list.beginRendering(Rhi::RenderTargets{
                     .color = texture, .clearColor = Math::Color{.r = 0.8F, .g = 0.4F, .b = 0.1F, .a = 1.0F}});
                 list.endRendering();
             }});
        m_capture->afterPasses(graph, slot);
        graph.compile();
        Result<std::vector<Rhi::IRhiCommandList*>> lists = graph.execute(device, m_jobs);
        ASSERT_TRUE(lists) << lists.error().message;
        m_capture->endFrame(graph);
        ASSERT_TRUE(device.endFrame(*lists));
    }

    Log::LogSystem m_logSystem{{.appName = "Test", .directory = {}, .console = true}};
    Log::Logger m_log{m_logSystem.createLogger("RHI")};
    Events::EventBus m_events{Memory::globalTracker()};
    FileSystem::FileSystem m_files{m_log};
    UuidGenerator m_ids;
    Assets::AssetManager m_assets{m_files, m_ids, m_log};
    Jobs::JobSystem m_jobs{{.threadCount = c_threads}, m_log};
    std::unique_ptr<RenderDevice> m_render;
    std::unique_ptr<PipelineLibrary> m_library;
    std::unique_ptr<FrameCapture> m_capture;
    Rhi::TextureHandle m_texture{};
};
} // namespace

TEST_F(FrameCaptureTest, SnapshotsAndImage)
{
    EXPECT_FALSE(m_capture->flow()) << "nothing captured yet";
    m_capture->request();
    frame();
    ASSERT_TRUE(m_capture->flow());
    EXPECT_FALSE(m_capture->requested());
    ASSERT_EQ(m_capture->flow()->passes.size(), 1U) << "the frame as it was, before the capture's own copies";
    EXPECT_EQ(m_capture->flow()->passes[0].name, "Clear");
    ASSERT_EQ(m_capture->snapshots().size(), 1U) << "one texture written";
    EXPECT_EQ(m_capture->snapshotFor(0, 0), 0U);
    EXPECT_FALSE(m_capture->snapshotFor(0, 1));
    EXPECT_FALSE(m_capture->snapshots()[0].depth);

    // The picture: at most 16 pixels a side keeps the 2:1 shape; it comes back a frame or two later.
    m_capture->requestImage(0, defaultDisplaySettings(false), 16);
    EXPECT_TRUE(m_capture->imagePending());
    std::optional<CapturedImage> image;
    for (std::uint32_t i = 0; i < Rhi::c_maxFramesInFlight + 2 && !image; ++i)
    {
        frame();
        image = m_capture->takeImage();
    }
    ASSERT_TRUE(image) << "came back";
    EXPECT_FALSE(m_capture->imagePending());
    EXPECT_EQ(image->width, 16U);
    EXPECT_EQ(image->height, 8U);
    ASSERT_EQ(image->pixels.size(), std::size_t{16} * 8 * 4);
    // Orange, sRGB-encoded: 0.8 → 231, 0.4 → 170, 0.1 → 89.
    const std::size_t middle = (std::size_t{4} * 16 + 8) * 4;
    EXPECT_NEAR(static_cast<int>(image->pixels[middle]), 231, 3);
    EXPECT_NEAR(static_cast<int>(image->pixels[middle + 1]), 170, 3);
    EXPECT_NEAR(static_cast<int>(image->pixels[middle + 2]), 89, 3);
    EXPECT_FALSE(m_capture->takeImage()) << "once";

    // One channel shows as grey: green alone is 0.4 everywhere.
    m_capture->requestImage(0, {.channels = 0b0010, .rangeMin = 0.0F, .rangeMax = 1.0F}, 16);
    image.reset();
    for (std::uint32_t i = 0; i < Rhi::c_maxFramesInFlight + 2 && !image; ++i)
    {
        frame();
        image = m_capture->takeImage();
    }
    ASSERT_TRUE(image);
    EXPECT_NEAR(static_cast<int>(image->pixels[middle]), 170, 3);
    EXPECT_NEAR(static_cast<int>(image->pixels[middle + 2]), 170, 3);

    m_capture->clear();
    EXPECT_FALSE(m_capture->flow());
    EXPECT_TRUE(m_capture->snapshots().empty());
    frame(); // the dropped copies are freed after a frame, with the validation layer quiet
}
} // namespace Meow2::Renderer
