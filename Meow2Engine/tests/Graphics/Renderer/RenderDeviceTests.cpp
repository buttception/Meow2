/// @file
/// @brief RenderDevice: switching the backend while a scene is loaded, and getting the same image back.
///        Built into both GPU suites: RhiGpu (Vulkan) and RhiGpuD3D12 (D3D12, which also switches to Vulkan and back).

#include "Graphics/Rhi/RhiTestBackend.h"

#include <Meow2/Assets/AssetManager.h>
#include <Meow2/Core/Events/EventBus.h>
#include <Meow2/Core/Id/UuidGenerator.h>
#include <Meow2/Core/Jobs/JobSystem.h>
#include <Meow2/Core/Log/LogSystem.h>
#include <Meow2/Core/Memory/GlobalTracking.h>
#include <Meow2/Graphics/Renderer/FrameResources.h>
#include <Meow2/Graphics/Renderer/GpuScene.h>
#include <Meow2/Graphics/Renderer/RenderDevice.h>
#include <Meow2/Math/Camera.h>
#include <Meow2/Math/Transform.h>
#include <Meow2/Platform/FileSystem/FileSystem.h>

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace Meow2::Renderer
{
namespace
{
constexpr std::uint32_t c_size = 64;
constexpr std::uint32_t c_recordingThreads = 4;

/// @brief What one frame produced.
struct FrameResult
{
    std::uint32_t drawCount = 0;     ///< Instances that survived culling; meshlets drawn on the meshlet path.
    std::vector<std::byte> pixels{}; ///< RGBA8, top row first.
};

/// @brief A RenderDevice with a scene of cubes, and a test-owned target that's a device user like the scene.
class RenderDeviceTest : public ::testing::Test
{
protected:
    RenderDeviceTest()
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
    }

    void TearDown() override
    {
        m_targetUser.reset();
        m_scene.reset();
        m_library.reset();
        if (m_render && m_render->hasDevice())
        {
            releaseTargets(m_render->device());
            m_render->device().waitIdle();
            EXPECT_EQ(m_render->device().validationErrorCount(), 0U);
        }
    }

    /// @brief The pipelines, a scene of three cubes and the targets, in dependency order.
    void makeScene()
    {
        m_library = std::make_unique<PipelineLibrary>(PipelineLibraryDeps{*m_render, m_assets, m_events, m_log});
        const Result<Uuid> uuid = Uuid::parse("01a1b5e2-3c12-8000-80a4-7b19d2e6c053");
        ASSERT_TRUE(uuid);
        const Result<AssetId> shader = AssetId::fromUuid(*uuid);
        ASSERT_TRUE(shader);
        Result<std::unique_ptr<GpuScene>> scene =
            GpuScene::create(*m_render, *m_library,
                             {.shader = *shader, .instanceCapacity = 8, .colorFormat = OffscreenTarget::format()});
        ASSERT_TRUE(scene) << scene.error().message;
        m_scene = std::move(*scene);
        m_scene->setMeshlets(m_meshlets);
        // The camera looks along +Z from the origin, so the sun is put behind it, lighting what it sees.
        m_scene->setLighting({.sunDirection = {0.4F, 0.8F, -0.6F}});
        ASSERT_TRUE(m_scene->addMesh(makeCube()));
        std::vector<InstanceDesc> instances;
        for (const Math::Vec3 position : {Math::Vec3{0.0F, 0.0F, 3.0F}, Math::Vec3{-1.2F, 0.5F, 4.0F},
                                          Math::Vec3{0.0F, 0.0F, -3.0F}}) // the last is behind the camera
        {
            Math::Transform transform;
            transform.position = position;
            InstanceDesc instance{};
            instance.world = Math::toMat4(transform);
            instance.material = static_cast<std::uint32_t>(instances.size()) + 1;
            instances.push_back(instance);
        }
        // The nearest cube gets a green texture, so restoring textures and material records is checked too.
        Assets::Image green{.width = 2, .height = 2, .pixels = {}};
        for (int i = 0; i < 4; ++i)
        {
            green.pixels.insert(green.pixels.end(), {std::byte{0}, std::byte{255}, std::byte{0}, std::byte{255}});
        }
        const Result<std::uint32_t> texture = m_scene->addTexture(green, true, "Green");
        ASSERT_TRUE(texture) << texture.error().message;
        const Result<std::uint32_t> material =
            m_scene->addMaterial({.baseColor = {.texture = static_cast<std::int32_t>(*texture)}, .metallic = 0.0F});
        ASSERT_TRUE(material) << material.error().message;
        instances[0].material = *material;
        m_scene->setInstances(instances);

        // The target is the test's own: it lets go and comes back like any other device user.
        ASSERT_TRUE(makeTargets(m_render->device()));
        m_targetUser = m_render->addUser(
            "Test target", [this](Rhi::IRhiDevice& device) { releaseTargets(device); },
            [this](Rhi::IRhiDevice& device) { return makeTargets(device); });
    }

    Result<void> makeTargets(Rhi::IRhiDevice& device)
    {
        Result<std::unique_ptr<OffscreenTarget>> target = OffscreenTarget::create(device, c_size, c_size);
        if (!target)
        {
            return Unexpected<Error>{target.error()};
        }
        m_target = std::move(*target);
        Result<Rhi::TextureHandle> depth = device.createTexture({.width = c_size,
                                                                 .height = c_size,
                                                                 .format = Rhi::RhiFormat::D32Float,
                                                                 .renderTarget = true,
                                                                 .name = "Depth"});
        if (!depth)
        {
            m_target.reset();
            return Unexpected<Error>{depth.error()};
        }
        m_depth = *depth;
        return {};
    }

    void releaseTargets(Rhi::IRhiDevice& device)
    {
        m_target.reset();
        device.destroy(m_depth);
        m_depth = {};
    }

    /// @brief Draws one frame with a camera at the origin looking down +Z, and reads the image and count back.
    FrameResult drawFrame()
    {
        Rhi::IRhiDevice& device = m_render->device(); // fetched per frame: it changes with every switch
        const bool meshlets = m_scene->usesMeshlets();
        Result<Rhi::BufferHandle> readback =
            device.createBuffer({.size = sizeof(std::uint32_t), .memory = Rhi::RhiMemory::Readback});
        EXPECT_TRUE(readback);
        Result<Rhi::IRhiCommandList*> first = device.beginFrame();
        EXPECT_TRUE(first);

        const Math::CameraMatrices matrices = Math::cameraMatrices(Math::Transform{}, Math::CameraLens{}, 1.0F);
        Shader::FrameConstants frame{};
        frame.view = matrices.view;
        frame.projection = matrices.projection;
        frame.viewProjection = matrices.viewProjection;
        m_scene->setView(device.frameSlot(), frame, Math::frustumFromCamera(matrices));

        RenderGraph graph;
        m_scene->addPasses(graph, device.frameSlot(), nullptr, m_target->texture(), m_depth, Math::Color{.a = 1.0F});
        graph.compile();
        Result<std::vector<Rhi::IRhiCommandList*>> lists = graph.execute(device, m_jobs);
        EXPECT_TRUE(lists) << lists.error().message;
        m_scene->endFrame(graph);

        Result<Rhi::IRhiCommandList*> last = device.commandList(0);
        EXPECT_TRUE(last);
        lists->push_back(*last);
        Rhi::IRhiCommandList& tail = **last;
        if (meshlets)
        {
            // Left in CopySource by the scene's own readback pass.
            tail.copyBuffer(m_scene->meshletCountBuffer(), *readback, sizeof(std::uint32_t),
                            MEOW2_TASK_COUNT_MESHLETS * sizeof(std::uint32_t));
        }
        else
        {
            tail.barrier(m_scene->countBuffer(), Rhi::ResourceState::IndirectArgs, Rhi::ResourceState::CopySource);
            tail.copyBuffer(m_scene->countBuffer(), *readback, sizeof(std::uint32_t));
        }
        tail.barrier(*readback, Rhi::ResourceState::CopyTarget, Rhi::ResourceState::HostRead);
        tail.barrier(m_target->texture(), Rhi::ResourceState::ColorTarget, Rhi::ResourceState::CopySource);
        m_target->copyToCpu(tail);
        EXPECT_TRUE(device.endFrame(*lists));
        device.waitIdle();

        FrameResult result;
        std::memcpy(&result.drawCount, device.mappedMemory(*readback).data(), sizeof(result.drawCount));
        result.pixels.assign(m_target->pixels().begin(), m_target->pixels().end());
        device.destroy(*readback);
        return result;
    }

    /// @brief How many pixels differ by more than `tolerance` in any channel.
    static std::size_t differentPixels(const std::vector<std::byte>& a, const std::vector<std::byte>& b,
                                       int tolerance = 0)
    {
        EXPECT_EQ(a.size(), b.size());
        std::size_t count = 0;
        for (std::size_t i = 0; i + 3 < a.size() && i + 3 < b.size(); i += 4)
        {
            for (std::size_t c = 0; c < 4; ++c)
            {
                if (std::abs(std::to_integer<int>(a[i + c]) - std::to_integer<int>(b[i + c])) > tolerance)
                {
                    ++count;
                    break;
                }
            }
        }
        return count;
    }

    bool m_meshlets = false; ///< The meshlet path; off by default, since most tests check the indirect draw counts.
    Log::LogSystem m_logSystem;
    Log::Logger m_log;
    Events::EventBus m_events{Memory::globalTracker()};
    FileSystem::FileSystem m_files;
    UuidGenerator m_ids;
    Assets::AssetManager m_assets;
    Jobs::JobSystem m_jobs;
    std::unique_ptr<RenderDevice> m_render;
    std::unique_ptr<PipelineLibrary> m_library;
    std::unique_ptr<GpuScene> m_scene;
    std::unique_ptr<OffscreenTarget> m_target;
    Rhi::TextureHandle m_depth;
    DeviceUser m_targetUser;
};
} // namespace

TEST_F(RenderDeviceTest, RecreatingRestoresTheScene)
{
    makeScene();
    const FrameResult before = drawFrame();
    ASSERT_EQ(before.drawCount, 2U) << "two cubes in front of the camera";
    const std::size_t centre = (std::size_t{c_size} / 2 * c_size + c_size / 2) * 4;
    EXPECT_GT(std::to_integer<int>(before.pixels[centre + 1]), 100) << "the textured cube is green";
    EXPECT_LT(std::to_integer<int>(before.pixels[centre]), 30);
    EXPECT_EQ(m_render->device().validationErrorCount(), 0U);

    ASSERT_TRUE(m_render->swapTo(Rhi::c_testBackend));

    EXPECT_EQ(m_render->swapCount(), 1U);
    EXPECT_EQ(m_render->backend(), Rhi::c_testBackend);
    const FrameResult after = drawFrame();
    EXPECT_EQ(after.drawCount, before.drawCount);
    EXPECT_EQ(differentPixels(before.pixels, after.pixels), 0U);
}

TEST_F(RenderDeviceTest, FailedRestoreGoesBack)
{
    makeScene();
    const FrameResult before = drawFrame();
    int restores = 0;
    bool failNext = true;
    DeviceUser flaky = m_render->addUser(
        "Flaky", [](Rhi::IRhiDevice&) {},
        [&restores, &failNext](Rhi::IRhiDevice&) -> Result<void>
        {
            ++restores;
            if (std::exchange(failNext, false))
            {
                return fail(ErrorCode::Unsupported, "not on this device");
            }
            return {};
        });

    const Result<void> swapped = m_render->swapTo(Rhi::c_testBackend);

    ASSERT_FALSE(swapped);
    EXPECT_NE(swapped.error().message.find("Flaky: not on this device"), std::string::npos) << swapped.error().message;
    EXPECT_EQ(restores, 2) << "once on the new device, once on the way back";
    EXPECT_TRUE(m_render->hasDevice());
    EXPECT_EQ(m_render->backend(), Rhi::c_testBackend);
    EXPECT_EQ(m_render->swapCount(), 0U);
    const FrameResult after = drawFrame();
    EXPECT_EQ(after.drawCount, before.drawCount);
    EXPECT_EQ(differentPixels(before.pixels, after.pixels), 0U);
}

TEST_F(RenderDeviceTest, UsersReleaseInReverseOrder)
{
    std::vector<std::string> calls;
    const auto user = [this, &calls](const std::string& name)
    {
        return m_render->addUser(
            name, [&calls, name](Rhi::IRhiDevice&) { calls.push_back("release " + name); },
            [&calls, name](Rhi::IRhiDevice&) -> Result<void>
            {
                calls.push_back("restore " + name);
                return {};
            });
    };
    const DeviceUser a = user("a");
    const DeviceUser b = user("b");
    const DeviceUser c = user("c");

    ASSERT_TRUE(m_render->swapTo(Rhi::c_testBackend));

    EXPECT_EQ(calls,
              (std::vector<std::string>{"release c", "release b", "release a", "restore a", "restore b", "restore c"}));
}

TEST_F(RenderDeviceTest, UnregisteredUserIsNotCalled)
{
    int calls = 0;
    DeviceUser user = m_render->addUser(
        "Gone", [&calls](Rhi::IRhiDevice&) { ++calls; },
        [&calls](Rhi::IRhiDevice&) -> Result<void>
        {
            ++calls;
            return {};
        });
    DeviceUser moved = std::move(user);
    EXPECT_FALSE(user.active()); // NOLINT(bugprone-use-after-move): checking the moved-from state is the point
    moved.reset();

    ASSERT_TRUE(m_render->swapTo(Rhi::c_testBackend));

    EXPECT_EQ(calls, 0);
}

TEST_F(RenderDeviceTest, KeepsEachBackendsPipelineCache)
{
    constexpr Rhi::RhiBackend c_other =
        Rhi::c_testBackend == Rhi::RhiBackend::Vulkan ? Rhi::RhiBackend::D3D12 : Rhi::RhiBackend::Vulkan;
    const std::vector<std::byte> saved{std::byte{1}, std::byte{2}, std::byte{3}};
    m_render->setPipelineCache(c_other, saved);
    makeScene(); // compiles pipelines, so the running backend has something to save

    EXPECT_EQ(m_render->pipelineCache(c_other), saved);
    const std::vector<std::byte> running = m_render->pipelineCache(Rhi::c_testBackend);
    EXPECT_FALSE(running.empty());

    // Recreating hands the old device's cache to the new one, which compiles nothing new: the same pipelines are
    // cached.
    ASSERT_TRUE(m_render->swapTo(Rhi::c_testBackend));
    EXPECT_FALSE(m_render->pipelineCache(Rhi::c_testBackend).empty());
    EXPECT_EQ(m_render->pipelineCache(c_other), saved);
}

#if MEOW2_TEST_D3D12 && MEOW2_RHI_VULKAN // a D3D12-only build has nothing to switch to
TEST_F(RenderDeviceTest, SwapsToVulkanAndBack)
{
    makeScene();
    const FrameResult d3d12 = drawFrame();
    ASSERT_EQ(d3d12.drawCount, 2U);

    const Result<void> toVulkan = m_render->swapTo(Rhi::RhiBackend::Vulkan);
    ASSERT_TRUE(toVulkan) << toVulkan.error().message;
    EXPECT_EQ(m_render->backend(), Rhi::RhiBackend::Vulkan);
    EXPECT_FALSE(m_render->pipelineCache(Rhi::RhiBackend::D3D12).empty()) << "kept for the way back";
    const FrameResult vulkan = drawFrame();
    EXPECT_EQ(vulkan.drawCount, d3d12.drawCount);
    // Two APIs, two drivers: rasterisation rules match, but allow a few edge pixels and rounding steps.
    EXPECT_LE(differentPixels(d3d12.pixels, vulkan.pixels, 2), std::size_t{c_size * c_size / 100});
    EXPECT_EQ(m_render->device().validationErrorCount(), 0U);

    const Result<void> back = m_render->swapTo(Rhi::RhiBackend::D3D12);
    ASSERT_TRUE(back) << back.error().message;
    EXPECT_EQ(m_render->swapCount(), 2U);
    const FrameResult again = drawFrame();
    EXPECT_EQ(again.drawCount, d3d12.drawCount);
    EXPECT_EQ(differentPixels(d3d12.pixels, again.pixels), 0U);
}
#endif
} // namespace Meow2::Renderer

namespace Meow2::Renderer
{
TEST_F(RenderDeviceTest, MeshletsSurviveBackendSwitch)
{
    m_meshlets = true;
    makeScene();
    if (!m_scene->meshletsAvailable())
    {
        GTEST_SKIP() << "this GPU has no mesh shaders";
    }
    const FrameResult before = drawFrame();
    ASSERT_GT(before.drawCount, 0U) << "meshlets of the two cubes in front";
    EXPECT_EQ(m_render->device().validationErrorCount(), 0U);

    ASSERT_TRUE(m_render->swapTo(Rhi::c_testBackend));
    ASSERT_TRUE(m_scene->usesMeshlets()) << "the meshlet buffers and pipelines came back";
    const FrameResult after = drawFrame();
    EXPECT_EQ(after.drawCount, before.drawCount);
    EXPECT_EQ(differentPixels(before.pixels, after.pixels), 0U);
}
} // namespace Meow2::Renderer
