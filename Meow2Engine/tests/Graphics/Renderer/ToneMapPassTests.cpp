/// @file
/// @brief Tests for ToneMapPass on a real GPU: the scene's HDR colour into an RGBA8 target, scaled up when the target
/// is
///        bigger (a game view at a lower render scale).

#include "Graphics/Rhi/RhiTestBackend.h"

#include <Meow2/Assets/AssetManager.h>
#include <Meow2/Core/Events/EventBus.h>
#include <Meow2/Core/Id/UuidGenerator.h>
#include <Meow2/Core/Jobs/JobSystem.h>
#include <Meow2/Core/Log/LogSystem.h>
#include <Meow2/Core/Memory/GlobalTracking.h>
#include <Meow2/Graphics/Renderer/FrameResources.h>
#include <Meow2/Graphics/Renderer/PipelineLibrary.h>
#include <Meow2/Graphics/Renderer/RenderDevice.h>
#include <Meow2/Graphics/Renderer/RenderGraph.h>
#include <Meow2/Graphics/Renderer/ToneMapPass.h>
#include <Meow2/Math/Packing.h>
#include <Meow2/Platform/FileSystem/FileSystem.h>

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

namespace Meow2::Renderer
{
namespace
{
/// @brief A headless device, the pipeline library and the tone-map pass.
class ToneMapTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        ASSERT_TRUE(m_files.mountDirectory("cooked", MEOW2_COOKED_DIR).has_value());
        Result<std::unique_ptr<RenderDevice>> device = RenderDevice::create(
            {.backend = Rhi::c_testBackend, .appName = "Meow2 tests", .validation = true, .presentation = false},
            m_log);
        ASSERT_TRUE(device) << device.error().message;
        m_render = std::move(*device);
        m_library = std::make_unique<PipelineLibrary>(PipelineLibraryDeps{*m_render, m_assets, m_events, m_log});
        const Result<AssetId> shader = Uuid::parse("01a1b5e2-3c17-8000-80a4-7b19d2e6c058")
                                           .and_then([](const Uuid& u) { return AssetId::fromUuid(u); });
        ASSERT_TRUE(shader);
        Result<std::unique_ptr<ToneMapPass>> pass = ToneMapPass::create(*m_library, *shader, OffscreenTarget::format());
        ASSERT_TRUE(pass) << pass.error().message;
        m_pass = std::move(*pass);
    }

    void TearDown() override
    {
        m_pass.reset();
        m_library.reset();
        m_render->device().waitIdle();
        EXPECT_EQ(m_render->device().validationErrorCount(), 0U);
    }

    Log::LogSystem m_logSystem{{.appName = "Test", .directory = {}, .console = true}};
    Log::Logger m_log{m_logSystem.createLogger("RHI")};
    Events::EventBus m_events{Memory::globalTracker()};
    FileSystem::FileSystem m_files{m_log};
    UuidGenerator m_ids;
    Assets::AssetManager m_assets{m_files, m_ids, m_log};
    std::unique_ptr<RenderDevice> m_render;
    std::unique_ptr<PipelineLibrary> m_library;
    std::unique_ptr<ToneMapPass> m_pass;
};
} // namespace

TEST_F(ToneMapTest, ScalesUpASmallerImage)
{
    constexpr std::uint32_t c_source = 32;
    constexpr std::uint32_t c_target = 64;
    Rhi::IRhiDevice& device = m_render->device();

    // A 32 × 32 HDR image: the left half red, the right half blue.
    Result<Rhi::TextureHandle> hdr = device.createTexture(
        {.width = c_source, .height = c_source, .format = Rhi::RhiFormat::Rgba16Float, .renderTarget = true});
    ASSERT_TRUE(hdr) << hdr.error().message;
    Result<Rhi::BufferHandle> upload = device.createBuffer(
        {.size = std::uint64_t{c_source} * c_source * 4 * sizeof(std::uint16_t), .memory = Rhi::RhiMemory::Upload});
    ASSERT_TRUE(upload);
    std::vector<std::uint16_t> texels;
    for (std::uint32_t y = 0; y < c_source; ++y)
    {
        for (std::uint32_t x = 0; x < c_source; ++x)
        {
            const bool left = x < c_source / 2;
            texels.insert(texels.end(), {Math::toHalf(left ? 1.0F : 0.0F), Math::toHalf(0.0F),
                                         Math::toHalf(left ? 0.0F : 1.0F), Math::toHalf(1.0F)});
        }
    }
    std::memcpy(device.mappedMemory(*upload).data(), texels.data(), texels.size() * sizeof(std::uint16_t));
    Result<std::unique_ptr<OffscreenTarget>> target = OffscreenTarget::create(device, c_target, c_target);
    ASSERT_TRUE(target) << target.error().message;

    Result<Rhi::IRhiCommandList*> list = device.beginFrame(); // runs first: the upload
    ASSERT_TRUE(list);
    (*list)->barrier(*hdr, Rhi::ResourceState::Undefined, Rhi::ResourceState::CopyTarget);
    (*list)->copyBufferToTexture(*upload, *hdr);
    (*list)->barrier(*hdr, Rhi::ResourceState::CopyTarget, Rhi::ResourceState::ShaderRead);
    RenderGraph graph;
    const GraphResource source = graph.import(*hdr, Rhi::ResourceState::ShaderRead, "HDR");
    const GraphResource output = graph.import((*target)->texture(), Rhi::ResourceState::Undefined, "Target");
    m_pass->addPass(graph, source, *hdr, output, (*target)->texture(), device);
    graph.compile();
    Jobs::JobSystem jobs{{.threadCount = 1}, m_log}; // one recording thread: the device was made with one
    Result<std::vector<Rhi::IRhiCommandList*>> lists = graph.execute(device, jobs);
    ASSERT_TRUE(lists) << lists.error().message;
    Result<Rhi::IRhiCommandList*> tail = device.commandList(0);
    ASSERT_TRUE(tail);
    (*tail)->barrier((*target)->texture(), Rhi::ResourceState::ColorTarget, Rhi::ResourceState::CopySource);
    (*target)->copyToCpu(**tail);
    lists->push_back(*tail);
    ASSERT_TRUE(device.endFrame(*lists));
    device.waitIdle();

    const auto pixel = [&](std::uint32_t x, std::uint32_t y)
    {
        std::array<std::uint8_t, 4> value{};
        std::memcpy(value.data(), (*target)->pixels().data() + (std::size_t{y} * c_target + x) * 4, 4);
        return value;
    };
    // Every pixel of the bigger target is filled, from the matching place in the smaller image.
    for (const auto [x, y] : {std::pair{2U, 2U}, std::pair{20U, 40U}, std::pair{30U, 63U}})
    {
        const auto red = pixel(x, y);
        EXPECT_GT(red[0], 100) << x << "," << y << " shows the red half";
        EXPECT_LT(red[2], 20);
    }
    for (const auto [x, y] : {std::pair{34U, 2U}, std::pair{50U, 40U}, std::pair{63U, 63U}})
    {
        const auto blue = pixel(x, y);
        EXPECT_GT(blue[2], 100) << x << "," << y << " shows the blue half";
        EXPECT_LT(blue[0], 20);
    }
    device.destroy(*upload);
    device.destroy(*hdr);
}
} // namespace Meow2::Renderer
