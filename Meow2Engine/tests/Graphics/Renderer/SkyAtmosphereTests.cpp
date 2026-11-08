/// @file
/// @brief SkyAtmosphere on a real GPU (`MEOW2_GPU_TESTS=ON`): the atmosphere LUTs read back and checked against what
///        the physics says, and a device switch.

#include "Graphics/Rhi/RhiTestBackend.h"

#include <Meow2/Assets/AssetManager.h>
#include <Meow2/Core/Events/EventBus.h>
#include <Meow2/Core/Id/UuidGenerator.h>
#include <Meow2/Core/Jobs/JobSystem.h>
#include <Meow2/Core/Log/LogSystem.h>
#include <Meow2/Core/Memory/GlobalTracking.h>
#include <Meow2/Graphics/Renderer/PipelineLibrary.h>
#include <Meow2/Graphics/Renderer/RenderDevice.h>
#include <Meow2/Graphics/Renderer/RenderGraph.h>
#include <Meow2/Graphics/Renderer/SkyAtmosphere.h>
#include <Meow2/Math/Camera.h>
#include <Meow2/Math/Packing.h>
#include <Meow2/Math/Transform.h>
#include <Meow2/Platform/FileSystem/FileSystem.h>

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

namespace Meow2::Renderer
{
namespace
{
constexpr std::uint32_t c_recordingThreads = 4;
constexpr std::uint32_t c_transmittanceWidth = 256; // shaders/Atmosphere.hlsli
constexpr std::uint32_t c_transmittanceHeight = 64;
constexpr std::uint32_t c_skyViewWidth = 192;
constexpr std::uint32_t c_skyViewHeight = 108;

/// @brief A linear RGB colour read back from a half-float texture.
struct Rgb
{
    float r = 0.0F;
    float g = 0.0F;
    float b = 0.0F;
};

/// @brief A headless device and a sky.
class SkyAtmosphereTest : public ::testing::Test
{
protected:
    SkyAtmosphereTest()
        : m_logSystem({.appName = "Test", .directory = {}, .console = true})
        , m_log(m_logSystem.createLogger("RHI"))
        , m_files(m_log)
        , m_assets(m_files, m_ids, m_log)
        , m_jobs({.threadCount = c_recordingThreads}, m_log)
    {
    }

    void SetUp() override
    {
        ASSERT_TRUE(m_files.mountDirectory("cooked", MEOW2_COOKED_DIR).has_value());
        Result<std::unique_ptr<RenderDevice>> device = RenderDevice::create({.backend = Rhi::c_testBackend,
                                                                             .appName = "Meow2 tests",
                                                                             .validation = true,
                                                                             .presentation = false,
                                                                             .recordingThreads = c_recordingThreads},
                                                                            m_log);
        ASSERT_TRUE(device) << device.error().message;
        m_render = std::move(*device);
        m_library = std::make_unique<PipelineLibrary>(PipelineLibraryDeps{*m_render, m_assets, m_events, m_log});
        const Result<Uuid> uuid = Uuid::parse("01a1b5e2-3c16-8000-80a4-7b19d2e6c057"); // shaders/Sky.hlsl
        ASSERT_TRUE(uuid);
        const Result<AssetId> shader = AssetId::fromUuid(*uuid);
        ASSERT_TRUE(shader);
        Result<std::unique_ptr<SkyAtmosphere>> sky =
            SkyAtmosphere::create(*m_render, *m_library, {.shader = *shader, .colorFormat = Rhi::RhiFormat::Rgba8Srgb});
        ASSERT_TRUE(sky) << sky.error().message;
        m_sky = std::move(*sky);
    }

    void TearDown() override
    {
        m_sky.reset();
        m_library.reset();
        if (m_render && m_render->hasDevice())
        {
            m_render->device().waitIdle();
            EXPECT_EQ(m_render->device().validationErrorCount(), 0U);
        }
    }

    /// @brief Runs the sky's passes for a sun direction and reads both LUTs back.
    void runFrame(const Math::Vec3& sunDirection)
    {
        Rhi::IRhiDevice& device = m_render->device(); // fetched per frame: a switch replaces it
        const Result<Rhi::BufferHandle> transmittance =
            device.createBuffer({.size = std::uint64_t{c_transmittanceWidth} * c_transmittanceHeight * 8,
                                 .memory = Rhi::RhiMemory::Readback});
        const Result<Rhi::BufferHandle> skyView = device.createBuffer(
            {.size = std::uint64_t{c_skyViewWidth} * c_skyViewHeight * 8, .memory = Rhi::RhiMemory::Readback});
        ASSERT_TRUE(transmittance && skyView);
        Result<Rhi::IRhiCommandList*> first = device.beginFrame();
        ASSERT_TRUE(first);
        const Math::CameraMatrices matrices = Math::cameraMatrices(Math::Transform{}, Math::CameraLens{}, 1.0F);
        m_sky->setView(device.frameSlot(), {.sunDirection = sunDirection}, matrices.viewProjection);

        RenderGraph graph;
        m_sky->addPasses(graph, device.frameSlot());
        graph.compile();
        Result<std::vector<Rhi::IRhiCommandList*>> lists = graph.execute(device, m_jobs);
        ASSERT_TRUE(lists) << lists.error().message;
        m_sky->endFrame();

        Result<Rhi::IRhiCommandList*> last = device.commandList(0);
        ASSERT_TRUE(last);
        lists->push_back(*last);
        Rhi::IRhiCommandList& tail = **last;
        for (const auto& [texture, buffer] :
             {std::pair{m_sky->transmittanceLut(), *transmittance}, std::pair{m_sky->skyViewLut(), *skyView}})
        {
            tail.barrier(texture, Rhi::ResourceState::ShaderRead, Rhi::ResourceState::CopySource);
            tail.copyTextureToBuffer(texture, buffer);
            tail.barrier(texture, Rhi::ResourceState::CopySource, Rhi::ResourceState::ShaderRead);
            tail.barrier(buffer, Rhi::ResourceState::CopyTarget, Rhi::ResourceState::HostRead);
        }
        ASSERT_TRUE(device.endFrame(*lists));
        device.waitIdle();
        m_transmittance = read(device, *transmittance, c_transmittanceWidth * c_transmittanceHeight);
        m_skyView = read(device, *skyView, c_skyViewWidth * c_skyViewHeight);
        device.destroy(*transmittance);
        device.destroy(*skyView);
    }

    static std::vector<Rgb> read(Rhi::IRhiDevice& device, Rhi::BufferHandle buffer, std::uint32_t texels)
    {
        std::vector<std::uint16_t> halves(std::size_t{texels} * 4);
        std::memcpy(halves.data(), device.mappedMemory(buffer).data(), halves.size() * sizeof(std::uint16_t));
        std::vector<Rgb> colors(texels);
        for (std::size_t i = 0; i < texels; ++i)
        {
            colors[i] = {Math::fromHalf(halves[i * 4]), Math::fromHalf(halves[i * 4 + 1]),
                         Math::fromHalf(halves[i * 4 + 2])};
        }
        return colors;
    }

    [[nodiscard]] Rgb transmittanceAt(std::uint32_t x, std::uint32_t y) const
    {
        return m_transmittance[std::size_t{y} * c_transmittanceWidth + x];
    }

    [[nodiscard]] Rgb skyViewAt(std::uint32_t x, std::uint32_t y) const
    {
        return m_skyView[std::size_t{y} * c_skyViewWidth + x];
    }

    Log::LogSystem m_logSystem;
    Log::Logger m_log;
    Events::EventBus m_events{Memory::globalTracker()};
    FileSystem::FileSystem m_files;
    UuidGenerator m_ids;
    Assets::AssetManager m_assets;
    Jobs::JobSystem m_jobs;
    std::unique_ptr<RenderDevice> m_render;
    std::unique_ptr<PipelineLibrary> m_library;
    std::unique_ptr<SkyAtmosphere> m_sky;
    std::vector<Rgb> m_transmittance;
    std::vector<Rgb> m_skyView;
};
} // namespace

TEST_F(SkyAtmosphereTest, TransmittanceFallsTowardsTheHorizon)
{
    runFrame({0.0F, 1.0F, 0.0F});
    // Bottom row: at the ground. Left: looking straight up (the shortest path); right: along the horizon.
    const Rgb zenith = transmittanceAt(0, 0);
    const Rgb horizon = transmittanceAt(c_transmittanceWidth - 1, 0);
    EXPECT_GT(zenith.r, 0.8F) << "most red light gets straight through";
    EXPECT_GT(zenith.r, horizon.r);
    EXPECT_GT(zenith.b, horizon.b);
    // Rayleigh scattering removes blue far more than red: along the horizon little blue is left (sunsets are red).
    EXPECT_GT(horizon.r, horizon.b * 2.0F);
    EXPECT_GT(zenith.r, zenith.b) << "even straight up, blue is scattered most";
}

TEST_F(SkyAtmosphereTest, SkyIsBlueAtNoonAndRedAtSunset)
{
    runFrame({0.0F, 1.0F, 0.0F}); // the sun overhead
    const Rgb noonZenith = skyViewAt(c_skyViewWidth / 2, 0);
    EXPECT_GT(noonZenith.b, noonZenith.r * 1.5F) << "a blue sky";
    EXPECT_GT(noonZenith.b, 0.0F);

    runFrame({0.0F, 0.02F, 1.0F}); // the sun just above the horizon
    // Just above the horizon (the row above the middle), towards the sun (the left column).
    const Rgb sunset = skyViewAt(0, c_skyViewHeight / 2 - 2);
    EXPECT_GT(sunset.r, sunset.b) << "red-orange towards a setting sun";
    const Rgb awayFromSun = skyViewAt(c_skyViewWidth - 1, c_skyViewHeight / 2 - 2);
    EXPECT_GT(sunset.r, awayFromSun.r) << "brighter towards the sun";
}

TEST_F(SkyAtmosphereTest, SurvivesABackendSwitch)
{
    runFrame({0.4F, 0.8F, 0.6F});
    const Rgb before = skyViewAt(c_skyViewWidth / 2, 10);
    ASSERT_TRUE(m_render->swapTo(Rhi::c_testBackend));
    runFrame({0.4F, 0.8F, 0.6F});
    const Rgb after = skyViewAt(c_skyViewWidth / 2, 10);
    EXPECT_NEAR(after.r, before.r, before.r * 0.01F + 1e-4F) << "the LUTs were rebuilt on the new device";
    EXPECT_NEAR(after.b, before.b, before.b * 0.01F + 1e-4F);
    EXPECT_EQ(m_render->device().validationErrorCount(), 0U);
}
} // namespace Meow2::Renderer
