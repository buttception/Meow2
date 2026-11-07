/// @file
/// @brief The GPU-driven path on a real GPU (`MEOW2_GPU_TESTS=ON`): compute culling, indirect drawing, vertex pulling,
///        reversed-Z depth, and the render graph recording on jobs.

#include "Graphics/Rhi/RhiTestBackend.h"

#include <Meow2/Assets/AssetManager.h>
#include <Meow2/Core/Events/EventBus.h>
#include <Meow2/Core/Id/UuidGenerator.h>
#include <Meow2/Core/Jobs/JobSystem.h>
#include <Meow2/Core/Log/LogSystem.h>
#include <Meow2/Core/Memory/GlobalTracking.h>
#include <Meow2/Graphics/Renderer/FrameResources.h>
#include <Meow2/Graphics/Renderer/GpuScene.h>
#include <Meow2/Math/Bounds.h>
#include <Meow2/Math/Camera.h>
#include <Meow2/Math/Transform.h>
#include <Meow2/Platform/FileSystem/FileSystem.h>

#include <gtest/gtest.h>

#include <array>
#include <cmath>
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
constexpr std::uint32_t c_size = 64;
constexpr std::uint32_t c_recordingThreads = 8;

/// @brief A headless device, jobs and a scene of cubes.
class GpuSceneTest : public ::testing::Test
{
protected:
    GpuSceneTest()
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
    }

    void TearDown() override
    {
        m_scene.reset();
        m_library.reset();
        if (m_depth.isSet())
        {
            m_render->device().destroy(m_depth);
        }
        m_target.reset();
        m_render->device().waitIdle();
        EXPECT_EQ(m_render->device().validationErrorCount(), 0U);
    }

    void makeScene(std::uint32_t capacity)
    {
        const Result<Uuid> uuid = Uuid::parse("01a1b5e2-3c12-8000-80a4-7b19d2e6c053");
        ASSERT_TRUE(uuid);
        const Result<AssetId> shader = AssetId::fromUuid(*uuid);
        ASSERT_TRUE(shader);
        Result<std::unique_ptr<GpuScene>> scene = GpuScene::create(
            *m_render, *m_library,
            {.shader = *shader, .instanceCapacity = capacity, .colorFormat = OffscreenTarget::format()});
        ASSERT_TRUE(scene) << scene.error().message;
        m_scene = std::move(*scene);
        ASSERT_TRUE(m_scene->addMesh(makeCube()));

        Result<std::unique_ptr<OffscreenTarget>> target = OffscreenTarget::create(m_render->device(), c_size, c_size);
        ASSERT_TRUE(target) << target.error().message;
        m_target = std::move(*target);
        Result<Rhi::TextureHandle> depth = m_render->device().createTexture({.width = c_size,
                                                                             .height = c_size,
                                                                             .format = Rhi::RhiFormat::D32Float,
                                                                             .renderTarget = true,
                                                                             .name = "Depth"});
        ASSERT_TRUE(depth) << depth.error().message;
        m_depth = *depth;
    }

    static Shader::InstanceData instanceAt(const Math::Vec3& position, std::uint32_t material = 1)
    {
        Math::Transform transform;
        transform.position = position;
        Shader::InstanceData instance{};
        instance.world = Math::toMat4(transform);
        instance.material = material;
        return instance;
    }

    /// @brief A camera at the origin looking down +Z.
    void setCamera(std::uint32_t slot, Math::Frustum& frustum)
    {
        const Math::CameraMatrices matrices = Math::cameraMatrices(Math::Transform{}, Math::CameraLens{}, 1.0F);
        frustum = Math::frustumFromCamera(matrices);
        Shader::FrameConstants frame{};
        frame.view = matrices.view;
        frame.projection = matrices.projection;
        frame.viewProjection = matrices.viewProjection;
        m_scene->setView(slot, frame, frustum);
    }

    /// @brief Runs one frame through the graph and reads the draw count back.
    std::uint32_t runFrame(Math::Frustum& frustum)
    {
        Result<Rhi::BufferHandle> readback =
            m_render->device().createBuffer({.size = sizeof(std::uint32_t), .memory = Rhi::RhiMemory::Readback});
        EXPECT_TRUE(readback);
        Result<Rhi::IRhiCommandList*> first = m_render->device().beginFrame();
        EXPECT_TRUE(first);
        setCamera(m_render->device().frameSlot(), frustum);

        RenderGraph sceneGraph;
        const Math::Color clear{.a = 1.0F};
        m_scene->addPasses(sceneGraph, m_render->device().frameSlot(), nullptr, m_target->texture(), m_depth, clear);
        sceneGraph.compile();
        Result<std::vector<Rhi::IRhiCommandList*>> lists = sceneGraph.execute(m_render->device(), m_jobs);
        EXPECT_TRUE(lists) << lists.error().message;
        m_scene->endFrame(sceneGraph);

        Result<Rhi::IRhiCommandList*> last = m_render->device().commandList(0);
        EXPECT_TRUE(last);
        lists->push_back(*last);
        Rhi::IRhiCommandList& tail = **last;
        tail.barrier(m_scene->countBuffer(), Rhi::ResourceState::IndirectArgs, Rhi::ResourceState::CopySource);
        tail.copyBuffer(m_scene->countBuffer(), *readback, sizeof(std::uint32_t));
        tail.barrier(m_scene->countBuffer(), Rhi::ResourceState::CopySource, Rhi::ResourceState::IndirectArgs);
        tail.barrier(*readback, Rhi::ResourceState::CopyTarget, Rhi::ResourceState::HostRead);
        tail.barrier(m_target->texture(), Rhi::ResourceState::ColorTarget, Rhi::ResourceState::CopySource);
        m_target->copyToCpu(tail);
        EXPECT_TRUE(m_render->device().endFrame(*lists));
        m_render->device().waitIdle();

        std::uint32_t result = 0;
        std::memcpy(&result, m_render->device().mappedMemory(*readback).data(), sizeof(result));
        m_render->device().destroy(*readback);
        return result;
    }

    [[nodiscard]] std::array<std::uint8_t, 4> pixelAt(std::uint32_t x, std::uint32_t y) const
    {
        std::array<std::uint8_t, 4> pixel{};
        std::memcpy(pixel.data(), m_target->pixels().data() + (std::size_t{y} * c_size + x) * 4, 4);
        return pixel;
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
    std::unique_ptr<GpuScene> m_scene;
    std::unique_ptr<OffscreenTarget> m_target;
    Rhi::TextureHandle m_depth;
};
} // namespace

TEST_F(GpuSceneTest, CubeIsVisible)
{
    makeScene(1);
    const std::vector<Shader::InstanceData> instances{instanceAt({0.0F, 0.0F, 3.0F})};
    m_scene->setInstances(instances);
    Math::Frustum frustum{};

    EXPECT_EQ(runFrame(frustum), 1U);
    const auto center = pixelAt(c_size / 2, c_size / 2);
    EXPECT_GT(center[0] + center[1] + center[2], 0) << "the cube";
    const auto corner = pixelAt(0, 0);
    EXPECT_EQ(corner[0] + corner[1] + corner[2], 0) << "the clear colour";
}

TEST_F(GpuSceneTest, HundredThousandInstancesCullLikeTheCpu)
{
    constexpr std::uint32_t c_count = 100'000;
    constexpr std::uint32_t c_side = 47; // 47³ is a little over 100k
    makeScene(c_count);
    std::vector<Shader::InstanceData> instances;
    instances.reserve(c_count);
    for (std::uint32_t i = 0; i < c_count; ++i)
    {
        const auto x = static_cast<float>(i % c_side) - 23.0F;
        const auto y = static_cast<float>((i / c_side) % c_side) - 23.0F;
        const auto z = static_cast<float>(i / (c_side * c_side)) - 23.0F;
        instances.push_back(instanceAt({x * 3.0F, y * 3.0F, z * 3.0F}, i));
    }
    m_scene->setInstances(instances);
    Math::Frustum frustum{};

    const std::uint32_t drawn = runFrame(frustum);

    const float radius = std::sqrt(0.75F);
    std::uint32_t expected = 0;
    for (const Shader::InstanceData& instance : instances)
    {
        const Math::Sphere sphere{.center = {instance.world.m[3][0], instance.world.m[3][1], instance.world.m[3][2]},
                                  .radius = radius};
        expected += Math::test(frustum, sphere) != Math::Containment::Outside ? 1U : 0U;
    }
    EXPECT_GT(drawn, 0U);
    EXPECT_LT(drawn, c_count);
    EXPECT_EQ(drawn, expected);
}

TEST_F(GpuSceneTest, ReversedDepthKeepsTheNearestCube)
{
    makeScene(2);
    // The far cube is drawn second, so only the depth test can keep the near one on top.
    const std::vector<Shader::InstanceData> instances{instanceAt({0.0F, 0.0F, 3.0F}, 1),
                                                      instanceAt({0.0F, 0.0F, 6.0F}, 2)};
    m_scene->setInstances(instances);
    Math::Frustum frustum{};
    EXPECT_EQ(runFrame(frustum), 2U);
    const auto both = pixelAt(c_size / 2, c_size / 2);

    const std::vector<Shader::InstanceData> nearOnly{instanceAt({0.0F, 0.0F, 3.0F}, 1)};
    m_scene->setInstances(nearOnly);
    EXPECT_EQ(runFrame(frustum), 1U);
    EXPECT_EQ(pixelAt(c_size / 2, c_size / 2), both);
}
} // namespace Meow2::Renderer
