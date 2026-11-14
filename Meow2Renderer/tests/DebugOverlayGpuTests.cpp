/// @file
/// @brief The debug overlays on the GPU, on each backend: the infinite grid below the horizon and hidden behind
///        geometry, and the frozen frustum's lines. Drawn headlessly into a small HDR target and read back.

#include "DebugLines.h"
#include "InfiniteGrid.h"

#include <Meow2/Assets/AssetManager.h>
#include <Meow2/Core/Events/EventBus.h>
#include <Meow2/Core/Id/UuidGenerator.h>
#include <Meow2/Core/Jobs/JobSystem.h>
#include <Meow2/Core/Log/LogSystem.h>
#include <Meow2/Core/Memory/GlobalTracking.h>
#include <Meow2/Graphics/Renderer/PipelineLibrary.h>
#include <Meow2/Graphics/Renderer/RenderDevice.h>
#include <Meow2/Graphics/Renderer/RenderGraph.h>
#include <Meow2/Math/Camera.h>
#include <Meow2/Math/Quaternion.h>
#include <Meow2/Platform/FileSystem/FileSystem.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <ostream>
#include <string_view>
#include <vector>

namespace Meow2::RendererApp
{
namespace
{
/// @brief The target's width and height in pixels: enough for grid lines, small enough to be quick.
constexpr std::uint32_t c_size = 128;

/// @brief The scene colour's format, as in Meow2Renderer.
constexpr Rhi::RhiFormat c_hdrFormat = Rhi::RhiFormat::Rgba16Float;

std::string_view backendName(Rhi::RhiBackend backend)
{
    return backend == Rhi::RhiBackend::D3D12 ? "d3d12" : "vulkan";
}

/// @brief One run: a backend. Plain data; a struct of this namespace, so gtest finds PrintTo() for it.
struct OverlayCase
{
    Rhi::RhiBackend backend = Rhi::RhiBackend::Vulkan; ///< The API it's drawn with.
};

/// @brief Prints a case as its backend's name, so the CTest names read `.../vulkan`.
void PrintTo(const OverlayCase& overlay, std::ostream* out)
{
    *out << backendName(overlay.backend);
}

std::vector<OverlayCase> backends()
{
    std::vector<OverlayCase> out{{.backend = Rhi::RhiBackend::Vulkan}};
#if MEOW2_RHI_D3D12
    out.push_back({.backend = Rhi::RhiBackend::D3D12});
#endif
    return out;
}

AssetId assetId(std::string_view text)
{
    return Uuid::parse(text).and_then([](const Uuid& uuid) { return AssetId::fromUuid(uuid); }).value_or(AssetId{});
}

/// @brief A camera's frame constants, from a position and a direction to look in.
Shader::FrameConstants cameraAt(const Math::Vec3& position, const Math::Vec3& forward)
{
    Math::Transform camera{};
    camera.position = position;
    camera.rotation = Math::lookRotation(forward);
    const Math::CameraMatrices matrices = Math::cameraMatrices(camera, Math::CameraLens{}, 1.0F);
    Shader::FrameConstants frame{};
    frame.view = matrices.view;
    frame.projection = matrices.projection;
    frame.viewProjection = matrices.viewProjection;
    frame.cameraPosition = {position.x, position.y, position.z, 0.0F};
    return frame;
}

/// @brief The read-back image: RGBA16F, 8 bytes per pixel.
struct HdrImage
{
    std::vector<std::uint16_t> halves; ///< Four per pixel.

    /// @brief Whether anything was drawn at a pixel (the target is cleared to all zero bits).
    [[nodiscard]] bool drawn(std::uint32_t x, std::uint32_t y) const
    {
        const std::size_t at = (std::size_t{y} * c_size + x) * 4;
        return halves[at] != 0 || halves[at + 1] != 0 || halves[at + 2] != 0;
    }

    /// @brief How many pixels were drawn in rows [top, bottom).
    [[nodiscard]] std::size_t drawnIn(std::uint32_t top, std::uint32_t bottom) const
    {
        std::size_t count = 0;
        for (std::uint32_t y = top; y < bottom; ++y)
        {
            for (std::uint32_t x = 0; x < c_size; ++x)
            {
                count += drawn(x, y) ? 1 : 0;
            }
        }
        return count;
    }
};

/// @brief A headless device with the grid and the debug lines, drawing into a cleared HDR target and depth.
class DebugOverlayTest : public ::testing::TestWithParam<OverlayCase>
{
protected:
    DebugOverlayTest()
        : m_logSystem({.appName = "Test", .directory = {}, .console = true})
        , m_log(m_logSystem.createLogger("Renderer"))
        , m_files(m_log)
        , m_assets(m_files, m_ids, m_log)
        , m_jobs({.threadCount = 2}, m_log)
    {
    }

    void SetUp() override
    {
        ASSERT_TRUE(m_files.mountDirectory("cooked", MEOW2_COOKED_DIR).has_value());
        Result<std::unique_ptr<Renderer::RenderDevice>> device =
            Renderer::RenderDevice::create({.backend = GetParam().backend,
                                            .appName = "Meow2 debug overlay tests",
                                            .validation = true,
                                            .presentation = false,
                                            .recordingThreads = 2},
                                           m_log);
        ASSERT_TRUE(device) << device.error().message;
        m_render = std::move(*device);
        m_pipelines = std::make_unique<Renderer::PipelineLibrary>(
            Renderer::PipelineLibraryDeps{*m_render, m_assets, m_events, m_log});
        m_grid = std::make_unique<InfiniteGrid>(*m_render, *m_pipelines,
                                                assetId("01a1b5e2-3c1b-8000-80a4-7b19d2e6c05c"), c_hdrFormat, m_log);
        m_lines = std::make_unique<DebugLines>(*m_render, *m_pipelines, assetId("01a1b5e2-3c1a-8000-80a4-7b19d2e6c05b"),
                                               c_hdrFormat, m_log);
        ASSERT_TRUE(m_grid->ready());
        Rhi::IRhiDevice& rhi = m_render->device();
        const auto texture = [&rhi](Rhi::RhiFormat format, const char* name)
        {
            Result<Rhi::TextureHandle> made = rhi.createTexture(
                {.width = c_size, .height = c_size, .format = format, .renderTarget = true, .name = name});
            EXPECT_TRUE(made) << made.error().message;
            return made.value_or(Rhi::TextureHandle{});
        };
        m_hdr = texture(c_hdrFormat, "Scene colour");
        m_depth = texture(Rhi::RhiFormat::D32Float, "Depth");
        Result<Rhi::BufferHandle> readback = rhi.createBuffer(
            {.size = std::uint64_t{c_size} * c_size * 8, .memory = Rhi::RhiMemory::Readback, .name = "Readback"});
        ASSERT_TRUE(readback) << readback.error().message;
        m_readback = *readback;
    }

    void TearDown() override
    {
        if (!m_render)
        {
            return;
        }
        m_render->device().waitIdle();
        m_lines.reset();
        m_grid.reset();
        m_pipelines.reset();
        m_render->device().destroy(m_hdr);
        m_render->device().destroy(m_depth);
        m_render->device().destroy(m_readback);
        EXPECT_EQ(m_render->device().validationErrorCount(), 0U);
    }

    /// @brief One frame: clear colour to zero and depth to `depth`, then the grid (if asked) and the lines; read back.
    HdrImage draw(const Shader::FrameConstants& frame, float depth, bool grid)
    {
        Rhi::IRhiDevice& rhi = m_render->device();
        Result<Rhi::IRhiCommandList*> first = rhi.beginFrame();
        EXPECT_TRUE(first);
        const std::uint32_t slot = rhi.frameSlot();
        Renderer::RenderGraph graph;
        const Renderer::GraphResource color = graph.import(m_hdr, Rhi::ResourceState::Undefined, "Scene colour");
        const Renderer::GraphResource depthResource = graph.import(m_depth, Rhi::ResourceState::Undefined, "Depth");
        graph.addPass(
            {.name = "Clear",
             .accesses = {{color, Rhi::ResourceState::ColorTarget}, {depthResource, Rhi::ResourceState::DepthTarget}},
             .record = [this, depth](Rhi::IRhiCommandList& list)
             {
                 list.beginRendering(Rhi::RenderTargets{
                     .color = m_hdr, .clearColor = Math::Color{}, .depth = m_depth, .clearDepth = depth});
                 list.endRendering();
             }});
        if (grid)
        {
            m_grid->addPasses(graph, slot, frame, color, m_hdr, depthResource, m_depth, c_size, c_size);
        }
        m_lines->addPass(graph, slot, frame.viewProjection, color, m_hdr);
        graph.compile();
        Result<std::vector<Rhi::IRhiCommandList*>> lists = graph.execute(rhi, m_jobs);
        EXPECT_TRUE(lists) << lists.error().message;
        const Rhi::ResourceState finalState = graph.finalState(color);
        Result<Rhi::IRhiCommandList*> last = rhi.commandList(0);
        EXPECT_TRUE(last);
        lists->push_back(*last);
        (*last)->barrier(m_hdr, finalState, Rhi::ResourceState::CopySource);
        (*last)->copyTextureToBuffer(m_hdr, m_readback);
        (*last)->barrier(m_readback, Rhi::ResourceState::CopyTarget, Rhi::ResourceState::HostRead);
        EXPECT_TRUE(rhi.endFrame(*lists));
        rhi.waitIdle();

        HdrImage image;
        image.halves.resize(std::size_t{c_size} * c_size * 4);
        std::memcpy(image.halves.data(), rhi.mappedMemory(m_readback).data(), image.halves.size() * 2);
        return image;
    }

    Log::LogSystem m_logSystem;
    Log::Logger m_log;
    Events::EventBus m_events{Memory::globalTracker()};
    FileSystem::FileSystem m_files;
    UuidGenerator m_ids;
    Assets::AssetManager m_assets;
    Jobs::JobSystem m_jobs;
    std::unique_ptr<Renderer::RenderDevice> m_render;
    std::unique_ptr<Renderer::PipelineLibrary> m_pipelines;
    std::unique_ptr<InfiniteGrid> m_grid;
    std::unique_ptr<DebugLines> m_lines;
    Rhi::TextureHandle m_hdr;
    Rhi::TextureHandle m_depth;
    Rhi::BufferHandle m_readback;
};
} // namespace

TEST_P(DebugOverlayTest, GridShowsBelowTheHorizon)
{
    // Two metres up, looking level: the horizon is the middle row; the ground below it, the sky above.
    const HdrImage image = draw(cameraAt({0.3F, 2.0F, 0.0F}, {0.0F, 0.0F, 1.0F}), 0.0F, true);
    EXPECT_EQ(image.drawnIn(0, c_size / 2 - 1), 0U) << "nothing above the horizon";
    EXPECT_GT(image.drawnIn(c_size / 2 + 4, c_size), std::size_t{c_size} * 4) << "grid lines on the ground";
}

TEST_P(DebugOverlayTest, GridHidesBehindGeometry)
{
    // Depth 1 is the near plane (reversed Z): something right in front of the camera everywhere.
    const HdrImage image = draw(cameraAt({0.3F, 2.0F, 0.0F}, {0.0F, -0.5F, 1.0F}), 1.0F, true);
    EXPECT_EQ(image.drawnIn(0, c_size), 0U);
}

TEST_P(DebugOverlayTest, FrustumLinesAreDrawn)
{
    // A frozen camera at the origin looking along +Z, seen from 30 m behind it.
    const Shader::FrameConstants frozen = cameraAt({0.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 1.0F});
    for (const DebugLine& line : frustumLines(frozen, 5.0F, {.r = 1.0F, .g = 0.85F, .b = 0.1F, .a = 1.0F}))
    {
        m_lines->add(line);
    }
    const HdrImage image = draw(cameraAt({0.0F, 0.0F, -30.0F}, {0.0F, 0.0F, 1.0F}), 0.0F, false);
    // The far rectangle, 5 m out with a 60° view, is 2.9 m from the centre to its edge; 35 m from the viewer that's
    // about 9 pixels of 64. Everything drawn stays within it (the sides run from its corners to the centre).
    EXPECT_GT(image.drawnIn(c_size / 2 - 12, c_size / 2 + 12), 20U) << "the far rectangle and the sides show";
    EXPECT_EQ(image.drawnIn(0, c_size / 2 - 14), 0U);
    EXPECT_EQ(image.drawnIn(c_size / 2 + 14, c_size), 0U);
}

INSTANTIATE_TEST_SUITE_P(Backends, DebugOverlayTest, ::testing::ValuesIn(backends()),
                         [](const ::testing::TestParamInfo<OverlayCase>& info)
                         { return std::string{backendName(info.param.backend)}; });
} // namespace Meow2::RendererApp
