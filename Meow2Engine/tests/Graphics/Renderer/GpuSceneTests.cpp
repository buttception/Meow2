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
#include <Meow2/Graphics/Renderer/SceneModel.h>
#include <Meow2/Math/Bounds.h>
#include <Meow2/Math/Camera.h>
#include <Meow2/Math/Transform.h>
#include <Meow2/Platform/FileSystem/FileSystem.h>
#include <Meow2/Scene/Scene.h>
#include <Meow2/Scene/TransformHierarchy.h>

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
        // The camera looks along +Z from the origin, so the sun is put behind it, lighting what it sees.
        m_scene->setLighting({.sunDirection = {0.4F, 0.8F, -0.6F}});
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

    /// @brief Runs one frame through the graph and reads the draw counts back.
    /// @return The draws in every list together (blended instances aren't in any).
    std::uint32_t runFrame(Math::Frustum& frustum)
    {
        constexpr std::uint64_t c_countBytes = MEOW2_DRAW_LIST_COUNT * sizeof(std::uint32_t);
        Result<Rhi::BufferHandle> readback =
            m_render->device().createBuffer({.size = c_countBytes, .memory = Rhi::RhiMemory::Readback});
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
        tail.copyBuffer(m_scene->countBuffer(), *readback, c_countBytes);
        tail.barrier(m_scene->countBuffer(), Rhi::ResourceState::CopySource, Rhi::ResourceState::IndirectArgs);
        tail.barrier(*readback, Rhi::ResourceState::CopyTarget, Rhi::ResourceState::HostRead);
        tail.barrier(m_target->texture(), Rhi::ResourceState::ColorTarget, Rhi::ResourceState::CopySource);
        m_target->copyToCpu(tail);
        EXPECT_TRUE(m_render->device().endFrame(*lists));
        m_render->device().waitIdle();

        std::array<std::uint32_t, MEOW2_DRAW_LIST_COUNT> counts{};
        std::memcpy(counts.data(), m_render->device().mappedMemory(*readback).data(), c_countBytes);
        m_render->device().destroy(*readback);
        m_lastCounts = counts;
        return counts[0] + counts[1] + counts[2];
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
    std::array<std::uint32_t, MEOW2_DRAW_LIST_COUNT> m_lastCounts{}; ///< Per list, from the last runFrame().
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
TEST_F(GpuSceneTest, ModelDrawsWithItsTexture)
{
    makeScene(4);
    // A quad 3 units in front of the camera, facing it, with a red texture: the importer's output, made by hand.
    Assets::ImportedModel model;
    Assets::Image red{.width = 2, .height = 2, .pixels = {}};
    for (int i = 0; i < 4; ++i)
    {
        red.pixels.insert(red.pixels.end(), {std::byte{255}, std::byte{0}, std::byte{0}, std::byte{255}});
    }
    model.images.push_back(red);
    Assets::ImportedMaterial material;
    material.baseColor.image = 0;
    material.metallicFactor = 0.0F; // a dielectric shows its colour; a metal (glTF's default) only reflects
    model.materials.push_back(material);
    Assets::ImportedPrimitive quad;
    quad.positions = {{-1.0F, -1.0F, 0.0F}, {-1.0F, 1.0F, 0.0F}, {1.0F, 1.0F, 0.0F}, {1.0F, -1.0F, 0.0F}};
    quad.normals.assign(4, Math::Vec3{0.0F, 0.0F, -1.0F});
    quad.uvs = {{0.0F, 1.0F}, {0.0F, 0.0F}, {1.0F, 0.0F}, {1.0F, 1.0F}};
    quad.indices = {0, 1, 2, 0, 2, 3}; // clockwise seen from the camera
    quad.material = 0;
    quad.bounds = {.center = {0.0F, 0.0F, 0.0F}, .extents = {1.0F, 1.0F, 0.0F}};
    model.meshes.push_back({.name = "Quad", .primitives = {quad}});
    Assets::ImportedNode node;
    node.local.position = Math::WorldPosition{0.0F, 0.0F, 3.0F};
    node.mesh = 0;
    model.nodes.push_back(node);

    RenderMeshes meshes;
    const Result<std::vector<AssetId>> meshIds = uploadModel(*m_scene, model, m_ids, meshes, m_log);
    ASSERT_TRUE(meshIds) << meshIds.error().message;
    Scene::Scene world{m_ids, Memory::globalTracker()};
    Scene::TransformHierarchy hierarchy{world, m_jobs};
    const std::vector<entt::entity> entities = instantiateModel(world, hierarchy, model, *meshIds);
    ASSERT_EQ(entities.size(), 1U);
    hierarchy.update();
    std::vector<Shader::InstanceData> instances;
    const std::optional<Math::Aabb> bounds = gatherInstances(world, meshes, instances);
    ASSERT_EQ(instances.size(), 1U);
    ASSERT_TRUE(bounds);
    EXPECT_NEAR(bounds->center.z, 3.0F, 1e-5F);
    m_scene->setInstances(instances);
    Math::Frustum frustum{};

    EXPECT_EQ(runFrame(frustum), 1U);
    const auto center = pixelAt(c_size / 2, c_size / 2);
    EXPECT_GT(center[0], 100) << "red from the texture, lit";
    EXPECT_LT(center[1], 30);
    EXPECT_LT(center[2], 30);
}
namespace
{
/// @brief A 2 × 2 quad in the XY plane facing -Z (towards a camera on -Z), clockwise from there, U along +X.
MeshData makeQuad()
{
    const std::array<Math::Vec3, 4> positions{
        {{-1.0F, -1.0F, 0.0F}, {-1.0F, 1.0F, 0.0F}, {1.0F, 1.0F, 0.0F}, {1.0F, -1.0F, 0.0F}}};
    const std::array<Math::Vec3, 4> normals{
        {{0.0F, 0.0F, -1.0F}, {0.0F, 0.0F, -1.0F}, {0.0F, 0.0F, -1.0F}, {0.0F, 0.0F, -1.0F}}};
    // The bitangent, cross(normal, tangent) × w, must point to decreasing V (+Y here): cross(-Z, +X) = -Y, so w = -1.
    const std::array<Math::Vec4, 4> tangents{
        {{1.0F, 0.0F, 0.0F, -1.0F}, {1.0F, 0.0F, 0.0F, -1.0F}, {1.0F, 0.0F, 0.0F, -1.0F}, {1.0F, 0.0F, 0.0F, -1.0F}}};
    const std::array<Math::Vec2, 4> uvs{{{0.0F, 1.0F}, {0.0F, 0.0F}, {1.0F, 0.0F}, {1.0F, 1.0F}}};
    const std::array<std::uint32_t, 6> indices{0, 1, 2, 0, 2, 3};
    return makeMesh({.positions = positions, .normals = normals, .tangents = tangents, .uvs = uvs, .indices = indices},
                    {.center = {0.0F, 0.0F, 0.0F}, .extents = {1.0F, 1.0F, 0.01F}});
}

/// @brief A transform that only moves. (Built by assignment: Transform::rotation has no default member initialiser,
///        so a designated initialiser naming only some fields warns on Clang.)
Math::Transform at(const Math::Vec3& position)
{
    Math::Transform transform;
    transform.position = position;
    return transform;
}

/// @brief An instance of `mesh` with a transform.
Shader::InstanceData instanceOf(const Math::Transform& transform, std::uint32_t mesh, std::uint32_t material)
{
    Shader::InstanceData instance{};
    instance.world = Math::toMat4(transform);
    instance.mesh = mesh;
    instance.material = material;
    return instance;
}

/// @brief A 1 × 1 image of one colour.
Assets::Image solidImage(std::uint8_t r, std::uint8_t g, std::uint8_t b)
{
    return {.width = 1, .height = 1, .pixels = {std::byte{r}, std::byte{g}, std::byte{b}, std::byte{255}}};
}

/// @brief The sum of a pixel's colour channels.
int brightness(const std::array<std::uint8_t, 4>& pixel)
{
    return pixel[0] + pixel[1] + pixel[2];
}
} // namespace

TEST_F(GpuSceneTest, MirroredAndDoubleSidedInstancesDraw)
{
    makeScene(4);
    const Result<std::uint32_t> quad = m_scene->addMesh(makeQuad());
    ASSERT_TRUE(quad);
    const Result<std::uint32_t> oneSided = m_scene->addMaterial({.metallic = 0.0F});
    const Result<std::uint32_t> twoSided = m_scene->addMaterial({.metallic = 0.0F, .doubleSided = true});
    ASSERT_TRUE(oneSided && twoSided);
    Math::Frustum frustum{};

    // Turned around, the quad shows its back: culled when one-sided, drawn when double-sided.
    Math::Transform behind = at({0.0F, 0.0F, 3.0F});
    behind.rotation = Math::fromAxisAngle(Math::c_up, Math::c_pi);
    std::vector<Shader::InstanceData> instances{instanceOf(behind, *quad, *oneSided)};
    m_scene->setInstances(instances);
    EXPECT_EQ(runFrame(frustum), 1U);
    EXPECT_EQ(brightness(pixelAt(c_size / 2, c_size / 2)), 0) << "one-sided, seen from behind: culled";

    instances = {instanceOf(behind, *quad, *twoSided)};
    m_scene->setInstances(instances);
    EXPECT_EQ(runFrame(frustum), 1U);
    EXPECT_EQ(m_lastCounts[MEOW2_DRAW_LIST_DOUBLE_SIDED], 1U);
    EXPECT_GT(brightness(pixelAt(c_size / 2, c_size / 2)), 60) << "double-sided: drawn, and lit from its own side";

    // Mirrored by a negative scale, the front faces wind the other way; the mirrored list culls the right side.
    Math::Transform mirrored = at({0.0F, 0.0F, 3.0F});
    mirrored.scale = {-1.0F, 1.0F, 1.0F};
    instances = {instanceOf(mirrored, *quad, *oneSided)};
    m_scene->setInstances(instances);
    EXPECT_EQ(runFrame(frustum), 1U);
    EXPECT_EQ(m_lastCounts[MEOW2_DRAW_LIST_MIRRORED], 1U);
    EXPECT_GT(brightness(pixelAt(c_size / 2, c_size / 2)), 60) << "mirrored: its front face is visible";
}

TEST_F(GpuSceneTest, BlendedMaterialMixesWithWhatsBehind)
{
    makeScene(4);
    const Result<std::uint32_t> quad = m_scene->addMesh(makeQuad());
    ASSERT_TRUE(quad);
    const Result<std::uint32_t> green =
        m_scene->addMaterial({.baseColorFactor = {0.0F, 1.0F, 0.0F, 1.0F}, .metallic = 0.0F});
    const Result<std::uint32_t> glass =
        m_scene->addMaterial({.baseColorFactor = {1.0F, 0.0F, 0.0F, 0.5F}, .metallic = 0.0F, .blend = true});
    ASSERT_TRUE(green && glass);
    Math::Frustum frustum{};

    std::vector<Shader::InstanceData> instances{instanceOf(at({0.0F, 0.0F, 4.0F}), *quad, *green)};
    m_scene->setInstances(instances);
    EXPECT_EQ(runFrame(frustum), 1U);
    const auto opaque = pixelAt(c_size / 2, c_size / 2);
    EXPECT_GT(opaque[1], 60);

    instances.push_back(instanceOf(at({0.0F, 0.0F, 3.0F}), *quad, *glass));
    m_scene->setInstances(instances);
    EXPECT_EQ(runFrame(frustum), 1U) << "the blended quad isn't in the indirect lists";
    const auto mixed = pixelAt(c_size / 2, c_size / 2);
    EXPECT_GT(mixed[0], 60) << "red from the glass";
    EXPECT_GT(mixed[1], 30) << "green through it";
    EXPECT_LT(mixed[1], opaque[1]) << "but less of it";
}

TEST_F(GpuSceneTest, NormalMapChangesLighting)
{
    makeScene(4);
    const Result<std::uint32_t> quad = m_scene->addMesh(makeQuad());
    ASSERT_TRUE(quad);
    // A normal tilted 45° towards +U (+X), where the sun is: x = 0.707 encodes as 218, z as 218.
    const Result<std::uint32_t> tilted = m_scene->addTexture(solidImage(218, 128, 218), false, "Tilted normal");
    ASSERT_TRUE(tilted);
    const Result<std::uint32_t> flat = m_scene->addMaterial({.metallic = 0.0F});
    const Result<std::uint32_t> bumpy =
        m_scene->addMaterial({.metallic = 0.0F, .normal = {.texture = static_cast<std::int32_t>(*tilted)}});
    ASSERT_TRUE(flat && bumpy);
    m_scene->setLighting({.sunDirection = {1.0F, 0.0F, -1.0F}, .sunIntensity = 2.0F});
    Math::Frustum frustum{};

    std::vector<Shader::InstanceData> instances{instanceOf(at({0.0F, 0.0F, 3.0F}), *quad, *flat)};
    m_scene->setInstances(instances);
    EXPECT_EQ(runFrame(frustum), 1U);
    const int flatLight = brightness(pixelAt(c_size / 2, c_size / 2));

    instances = {instanceOf(at({0.0F, 0.0F, 3.0F}), *quad, *bumpy)};
    m_scene->setInstances(instances);
    EXPECT_EQ(runFrame(frustum), 1U);
    const int bumpyLight = brightness(pixelAt(c_size / 2, c_size / 2));
    EXPECT_GT(bumpyLight, flatLight + 20) << "the map turns the surface towards the sun";
}
} // namespace Meow2::Renderer
