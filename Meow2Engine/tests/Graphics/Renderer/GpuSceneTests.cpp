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
#include <Meow2/Graphics/Renderer/SkyAtmosphere.h>
#include <Meow2/Math/Bounds.h>
#include <Meow2/Math/Camera.h>
#include <Meow2/Math/Quaternion.h>
#include <Meow2/Math/Scalar.h>
#include <Meow2/Math/Transform.h>
#include <Meow2/Platform/FileSystem/FileSystem.h>
#include <Meow2/Scene/Scene.h>
#include <Meow2/Scene/TransformHierarchy.h>

#include <DirectXMath.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <format>
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
        m_sky.reset(); // after the scene, which refers to it
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
        m_scene->setMeshlets(m_meshlets);
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

    /// @brief Gives the scene a sky (shaders/Sky.hlsl) to light it and be drawn behind it.
    void makeSky()
    {
        const Result<Uuid> uuid = Uuid::parse("01a1b5e2-3c16-8000-80a4-7b19d2e6c057");
        ASSERT_TRUE(uuid);
        const Result<AssetId> shader = AssetId::fromUuid(*uuid);
        ASSERT_TRUE(shader);
        Result<std::unique_ptr<SkyAtmosphere>> sky =
            SkyAtmosphere::create(*m_render, *m_library, {.shader = *shader, .colorFormat = OffscreenTarget::format()});
        ASSERT_TRUE(sky) << sky.error().message;
        m_sky = std::move(*sky);
        m_scene->setSky(m_sky.get());
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
        Math::Transform camera;
        camera.rotation = m_cameraRotation;
        const Math::CameraMatrices matrices = Math::cameraMatrices(camera, Math::CameraLens{}, 1.0F);
        frustum = m_cullFrustum.value_or(Math::frustumFromCamera(matrices)); // a frozen one, or the camera's
        Shader::FrameConstants frame{};
        frame.debugView = m_debugView;
        frame.view = matrices.view;
        frame.projection = matrices.projection;
        frame.viewProjection = matrices.viewProjection;
        if (m_sky)
        {
            m_sky->setView(slot, {.sunDirection = m_sun}, matrices.viewProjection);
        }
        m_scene->setView(slot, frame, frustum);
    }

    /// @brief Runs one frame through the graph and reads the counts back: the draw counts on the indirect path, the
    ///        meshlet counters (m_lastMeshletCounts) on the meshlet path.
    /// @return The draws in every list together (blended instances aren't in any), or on the meshlet path the
    ///         meshlets drawn.
    std::uint32_t runFrame(Math::Frustum& frustum)
    {
        const bool meshlets = m_scene->usesMeshlets();
        const std::uint64_t countBytes =
            meshlets ? MEOW2_TASK_COUNT_SIZE * sizeof(std::uint32_t) : MEOW2_DRAW_LIST_COUNT * sizeof(std::uint32_t);
        Result<Rhi::BufferHandle> readback =
            m_render->device().createBuffer({.size = countBytes, .memory = Rhi::RhiMemory::Readback});
        EXPECT_TRUE(readback);
        Result<Rhi::IRhiCommandList*> first = m_render->device().beginFrame();
        EXPECT_TRUE(first);
        setCamera(m_render->device().frameSlot(), frustum);

        RenderGraph sceneGraph;
        if (m_sky)
        {
            m_sky->addPasses(sceneGraph, m_render->device().frameSlot());
        }
        const GraphResource colour = m_scene->addPasses(sceneGraph, m_render->device().frameSlot(), nullptr,
                                                        m_target->texture(), m_depth, m_clear);
        if (m_overlay)
        {
            m_scene->addOverlayPass(sceneGraph, "Overlay", *m_overlay, colour, m_target->texture(), m_depth,
                                    m_render->device().frameSlot());
        }
        sceneGraph.compile();
        Result<std::vector<Rhi::IRhiCommandList*>> lists = sceneGraph.execute(m_render->device(), m_jobs);
        EXPECT_TRUE(lists) << lists.error().message;
        m_scene->endFrame(sceneGraph);
        if (m_sky)
        {
            m_sky->endFrame();
        }

        Result<Rhi::IRhiCommandList*> last = m_render->device().commandList(0);
        EXPECT_TRUE(last);
        lists->push_back(*last);
        Rhi::IRhiCommandList& tail = **last;
        if (meshlets)
        {
            // The scene's own readback pass leaves the counters in CopySource; overlays may have counted since.
            const Rhi::ResourceState counted =
                m_overlay ? Rhi::ResourceState::ShaderWrite : Rhi::ResourceState::CopySource;
            tail.barrier(m_scene->meshletCountBuffer(), counted, Rhi::ResourceState::CopySource);
            tail.copyBuffer(m_scene->meshletCountBuffer(), *readback, countBytes);
            tail.barrier(m_scene->meshletCountBuffer(), Rhi::ResourceState::CopySource, counted);
        }
        else
        {
            tail.barrier(m_scene->countBuffer(), Rhi::ResourceState::IndirectArgs, Rhi::ResourceState::CopySource);
            tail.copyBuffer(m_scene->countBuffer(), *readback, countBytes);
            tail.barrier(m_scene->countBuffer(), Rhi::ResourceState::CopySource, Rhi::ResourceState::IndirectArgs);
        }
        tail.barrier(*readback, Rhi::ResourceState::CopyTarget, Rhi::ResourceState::HostRead);
        tail.barrier(m_target->texture(), Rhi::ResourceState::ColorTarget, Rhi::ResourceState::CopySource);
        m_target->copyToCpu(tail);
        EXPECT_TRUE(m_render->device().endFrame(*lists));
        m_render->device().waitIdle();

        if (meshlets)
        {
            std::memcpy(m_lastMeshletCounts.data(), m_render->device().mappedMemory(*readback).data(), countBytes);
            m_render->device().destroy(*readback);
            return m_lastMeshletCounts[MEOW2_TASK_COUNT_MESHLETS];
        }
        std::array<std::uint32_t, MEOW2_DRAW_LIST_COUNT> counts{};
        std::memcpy(counts.data(), m_render->device().mappedMemory(*readback).data(), countBytes);
        m_render->device().destroy(*readback);
        m_lastCounts = counts;
        return counts[0] + counts[1] + counts[2];
    }

    /// @brief The target's pixels after the last runFrame(), RGBA8.
    [[nodiscard]] std::vector<std::uint8_t> image() const
    {
        std::vector<std::uint8_t> pixels(std::size_t{c_size} * c_size * 4);
        std::memcpy(pixels.data(), m_target->pixels().data(), pixels.size());
        return pixels;
    }

    [[nodiscard]] std::array<std::uint8_t, 4> pixelAt(std::uint32_t x, std::uint32_t y) const
    {
        std::array<std::uint8_t, 4> pixel{};
        std::memcpy(pixel.data(), m_target->pixels().data() + (std::size_t{y} * c_size + x) * 4, 4);
        return pixel;
    }

    bool m_meshlets = false; ///< The meshlet path; off by default, since most tests check the indirect draw counts.
    std::array<std::uint32_t, MEOW2_TASK_COUNT_SIZE> m_lastMeshletCounts{}; ///< From the last meshlet-path frame.
    std::uint32_t m_debugView = 0;              ///< FrameConstants::debugView, for the DEBUG_VIEW = 1 pipelines.
    std::optional<Math::Frustum> m_cullFrustum; ///< Culls with this instead of the camera's (frozen culling).
    Math::Quat m_cameraRotation{};              ///< The camera at the origin; identity looks down +Z.
    std::optional<DrawPipelines> m_overlay;     ///< Drawn over the scene after its passes.
    Math::Color m_clear{.a = 1.0F};             ///< The colour the scene clears to.
    Log::LogSystem m_logSystem;
    Log::Logger m_log;
    Events::EventBus m_events{Memory::globalTracker()};
    FileSystem::FileSystem m_files;
    UuidGenerator m_ids;
    Assets::AssetManager m_assets;
    Jobs::JobSystem m_jobs;
    std::unique_ptr<RenderDevice> m_render;
    std::unique_ptr<PipelineLibrary> m_library;
    std::unique_ptr<SkyAtmosphere> m_sky; ///< Optional: declared before the scene, which refers to it.
    Math::Vec3 m_sun{0.4F, 0.8F, -0.6F};  ///< The sky's sun: the same as the fixture's scene lighting.
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

TEST_F(GpuSceneTest, TextureTransformMovesTheTexture)
{
    makeScene(4);
    const Result<std::uint32_t> quad = m_scene->addMesh(makeQuad());
    ASSERT_TRUE(quad);
    // Four texels across: red, green, green, red. The quad's centre is U = 0.5, between the two greens; offset by half
    // the texture it's U = 1.0, which wraps to between the two reds.
    Assets::Image stripes{.width = 4, .height = 1, .pixels = {}};
    for (const std::uint8_t red : std::array<std::uint8_t, 4>{255, 0, 0, 255})
    {
        stripes.pixels.insert(stripes.pixels.end(), {std::byte{red}, std::byte{static_cast<std::uint8_t>(255 - red)},
                                                     std::byte{0}, std::byte{255}});
    }
    const Result<std::uint32_t> texture = m_scene->addTexture(stripes, true, "Stripes");
    ASSERT_TRUE(texture);
    const auto textureIndex = static_cast<std::int32_t>(*texture);
    const Result<std::uint32_t> plain =
        m_scene->addMaterial({.baseColor = {.texture = textureIndex}, .metallic = 0.0F});
    const Result<std::uint32_t> moved =
        m_scene->addMaterial({.baseColor = {.texture = textureIndex, .uvOffset = {0.5F, 0.0F}}, .metallic = 0.0F});
    ASSERT_TRUE(plain && moved);
    Math::Frustum frustum{};

    std::vector<Shader::InstanceData> instances{instanceOf(at({0.0F, 0.0F, 3.0F}), *quad, *plain)};
    m_scene->setInstances(instances);
    EXPECT_EQ(runFrame(frustum), 1U);
    const auto before = pixelAt(c_size / 2, c_size / 2);
    EXPECT_GT(before[1], before[0] + 50) << "green in the middle";

    instances = {instanceOf(at({0.0F, 0.0F, 3.0F}), *quad, *moved)};
    m_scene->setInstances(instances);
    EXPECT_EQ(runFrame(frustum), 1U);
    const auto after = pixelAt(c_size / 2, c_size / 2);
    EXPECT_GT(after[0], after[1] + 50) << "red in the middle once the texture moves by half";
}

TEST_F(GpuSceneTest, SmoothMetalReflectsTheSky)
{
    makeScene(4);
    makeSky();
    const Result<std::uint32_t> quad = m_scene->addMesh(makeQuad());
    const Result<std::uint32_t> mirror = m_scene->addMaterial({.metallic = 1.0F, .roughness = 0.05F});
    ASSERT_TRUE(quad && mirror);
    Math::Frustum frustum{};
    // The camera looks along +Z. Tilted back 45°, the quad reflects the view straight up, into the sky; tilted
    // forward, straight down, at the ground. (The target is linear, so the values are small: the zenith is darker
    // than ground lit by a sun 53° up.)
    std::array<std::array<std::uint8_t, 4>, 2> seen{};
    for (const float tilt : {45.0F, -45.0F})
    {
        Math::Transform transform = at({0.0F, 0.0F, 3.0F});
        transform.rotation = Math::fromAxisAngle({1.0F, 0.0F, 0.0F}, Math::toRadians(tilt));
        const std::vector<Shader::InstanceData> instances{instanceOf(transform, *quad, *mirror)};
        m_scene->setInstances(instances);
        EXPECT_EQ(runFrame(frustum), 1U);
        seen[tilt > 0.0F ? 0 : 1] = pixelAt(c_size / 2, c_size / 2);
    }
    const auto& sky = seen[0];
    const auto& ground = seen[1];
    const auto values =
        std::format("sky {} {} {}, ground {} {} {}", sky[0], sky[1], sky[2], ground[0], ground[1], ground[2]);
    EXPECT_GT(sky[2], sky[0] * 2 + 4) << "blue from the sky overhead: " << values;
    EXPECT_GT(ground[0], ground[2]) << "the sunlit ground is a warm grey: " << values;
    EXPECT_GT(ground[0], sky[0] + 10) << "the two reflections differ: " << values;
    const auto corner = pixelAt(0, 0);
    EXPECT_GT(corner[0] + corner[1] + corner[2], 0) << "the sky is drawn behind the scene";
}

namespace
{
/// @brief The pixel a point of the plane z = 3 lands on, for the test camera (origin, +Z, 60° vertical, square).
std::pair<std::uint32_t, std::uint32_t> pixelFor(float x, float y)
{
    const float perUnit = 1.0F / (3.0F * std::tan(Math::toRadians(30.0F))); // NDC per world unit at z = 3
    const auto toPixel = [](float ndc)
    {
        return static_cast<std::uint32_t>((ndc + 1.0F) * 0.5F * c_size);
    };
    return {toPixel(x * perUnit), toPixel(-y * perUnit)};
}

int channel(const std::array<std::uint8_t, 4>& pixel, std::size_t c)
{
    return pixel[c];
}
} // namespace

TEST_F(GpuSceneTest, DebugViewsShowTheirChannel)
{
    makeScene(4);
    const Result<std::uint32_t> quad = m_scene->addMesh(makeQuad());
    ASSERT_TRUE(quad);
    const Result<std::uint32_t> material =
        m_scene->addMaterial({.baseColorFactor = {0.2F, 0.6F, 0.9F, 1.0F}, .metallic = 0.3F, .roughness = 0.7F});
    ASSERT_TRUE(material);
    const Result<DrawPipelines> views = m_scene->makeDrawPipelines({.options = {{"DEBUG_VIEW", "1"}}, .name = "Views"});
    ASSERT_TRUE(views) << views.error().message;
    m_scene->setPipelineOverride(*views);
    const std::vector<Shader::InstanceData> instances{instanceOf(at({0.0F, 0.0F, 3.0F}), *quad, *material)};
    m_scene->setInstances(instances);
    Math::Frustum frustum{};
    const auto [x, y] = pixelFor(0.4F, 0.3F); // inside the quad, off its diagonal

    // The target is linear RGBA8: a value v reads back as v × 255.
    const auto expectView = [&](std::uint32_t view, std::array<int, 3> expected, const char* name)
    {
        m_debugView = view;
        EXPECT_EQ(runFrame(frustum), 1U);
        const std::array<std::uint8_t, 4> pixel = pixelAt(x, y);
        for (std::size_t c = 0; c < 3; ++c)
        {
            EXPECT_NEAR(channel(pixel, c), expected[c], 3) << name << ", channel " << c;
        }
    };
    expectView(MEOW2_DEBUG_VIEW_BASE_COLOR, {51, 153, 230}, "base colour");
    expectView(MEOW2_DEBUG_VIEW_METALLIC, {77, 77, 77}, "metallic");
    expectView(MEOW2_DEBUG_VIEW_ROUGHNESS, {179, 179, 179}, "roughness");
    expectView(MEOW2_DEBUG_VIEW_GEOMETRY_NORMAL, {128, 128, 0}, "normal (0, 0, -1), towards the camera");
    // Depth: 1 - log2(distance / near) / 16 at distance 3 (the plane) with the default near plane of 0.1.
    const int shade = static_cast<int>((1.0F - std::log2(3.0F / 0.1F) / 16.0F) * 255.0F + 0.5F);
    expectView(MEOW2_DEBUG_VIEW_DEPTH, {shade, shade, shade}, "depth");
}

TEST_F(GpuSceneTest, WireframeDrawsOnlyEdges)
{
    makeScene(4);
    const Result<std::uint32_t> quad = m_scene->addMesh(makeQuad());
    ASSERT_TRUE(quad);
    const Result<std::uint32_t> material = m_scene->addMaterial({.metallic = 0.0F});
    ASSERT_TRUE(material);
    // Wireframe only: black surfaces (they still write depth), then the edges over them.
    const Result<DrawPipelines> views = m_scene->makeDrawPipelines({.options = {{"DEBUG_VIEW", "1"}}, .name = "Views"});
    const Result<DrawPipelines> wire = m_scene->makeDrawPipelines(
        {.options = {{"DEBUG_VIEW", "2"}}, .fill = Rhi::FillMode::Wireframe, .depthWrite = false, .name = "Wire"});
    ASSERT_TRUE(views && wire);
    m_scene->setPipelineOverride(*views);
    m_debugView = MEOW2_DEBUG_VIEW_BLACK;
    m_overlay = *wire;
    m_clear = {.r = 0.0F, .g = 0.0F, .b = 0.0F, .a = 1.0F};
    const std::vector<Shader::InstanceData> instances{instanceOf(at({0.0F, 0.0F, 3.0F}), *quad, *material)};
    m_scene->setInstances(instances);
    Math::Frustum frustum{};
    EXPECT_EQ(runFrame(frustum), 1U);

    // The edge colour is the bright accent (1, 0.353, 0.4).
    const auto isEdge = [](const std::array<std::uint8_t, 4>& pixel)
    {
        return pixel[0] > 200 && pixel[1] < 130;
    };
    const auto [insideX, insideY] = pixelFor(0.5F, -0.5F); // inside, away from every edge and the diagonal
    const std::array<std::uint8_t, 4> inside = pixelAt(insideX, insideY);
    EXPECT_FALSE(isEdge(inside)) << "a face's inside stays black";
    EXPECT_LT(channel(inside, 0) + channel(inside, 1) + channel(inside, 2), 10);
    // Across the middle row: the left and right edges and the diagonal.
    int edges = 0;
    for (std::uint32_t column = 0; column < c_size; ++column)
    {
        edges += isEdge(pixelAt(column, c_size / 2)) ? 1 : 0;
    }
    EXPECT_GE(edges, 3) << "left edge, diagonal, right edge";
    EXPECT_LE(edges, 9) << "edges are lines, not filled";
}

TEST_F(GpuSceneTest, OverdrawCountsLayers)
{
    makeScene(4);
    const Result<std::uint32_t> quad = m_scene->addMesh(makeQuad());
    ASSERT_TRUE(quad);
    const Result<DrawPipelines> overdraw = m_scene->makeDrawPipelines({.options = {{"DEBUG_VIEW", "1"}},
                                                                       .blend = Rhi::BlendMode::Additive,
                                                                       .depthTest = false,
                                                                       .depthWrite = false,
                                                                       .name = "Overdraw"});
    ASSERT_TRUE(overdraw) << overdraw.error().message;
    m_scene->setPipelineOverride(*overdraw);
    m_debugView = MEOW2_DEBUG_VIEW_OVERDRAW;
    m_clear = {.r = 0.0F, .g = 0.0F, .b = 0.0F, .a = 1.0F};
    // Two quads side by side, overlapping between x = -0.5 and 1.
    const std::vector<Shader::InstanceData> instances{instanceOf(at({0.0F, 0.0F, 3.0F}), *quad, 0),
                                                      instanceOf(at({0.5F, 0.0F, 3.0F}), *quad, 0)};
    m_scene->setInstances(instances);
    Math::Frustum frustum{};
    EXPECT_EQ(runFrame(frustum), 2U);

    // 1/16 per layer: 16 for one, 32 for two (of 255).
    const auto valueAt = [&](float x)
    {
        const auto [px, py] = pixelFor(x, 0.4F);
        return channel(pixelAt(px, py), 0);
    };
    EXPECT_NEAR(valueAt(-0.75F), 16, 2) << "only the first quad";
    EXPECT_NEAR(valueAt(0.25F), 32, 2) << "both";
    EXPECT_NEAR(valueAt(1.25F), 16, 2) << "only the second quad";
}

TEST_F(GpuSceneTest, FrozenCullingKeepsTheOldFrustum)
{
    makeScene(4);
    const std::vector<Shader::InstanceData> instances{instanceAt({0.0F, 0.0F, 3.0F})};
    m_scene->setInstances(instances);
    Math::Frustum frustum{};
    m_cameraRotation = Math::fromAxisAngle(Math::Vec3{0.0F, 1.0F, 0.0F}, Math::c_pi); // looking down -Z, away from it
    EXPECT_EQ(runFrame(frustum), 0U) << "behind the camera: culled";

    // Frozen while the camera looked down +Z: the cube is still in that frustum, so it's still drawn.
    m_cullFrustum = Math::frustumFromCamera(Math::cameraMatrices(Math::Transform{}, Math::CameraLens{}, 1.0F));
    EXPECT_EQ(runFrame(frustum), 1U);
}

namespace
{
/// @brief Mesh arrays to build test meshes from. Plain data.
struct TestMesh
{
    std::vector<Math::Vec3> positions;
    std::vector<Math::Vec3> normals;
    std::vector<Math::Vec2> uvs;
    std::vector<std::uint32_t> indices;
};

/// @brief Adds a triangle wound clockwise seen from the side its first vertex's normal faces (the engine's front
///        faces: the cross product of two edges points out of a front face). Skips degenerate ones.
void addTriangle(TestMesh& mesh, std::uint32_t a, std::uint32_t b, std::uint32_t c)
{
    using namespace DirectX;
    const XMVECTOR p0 = Math::load(mesh.positions[a]);
    const XMVECTOR facing = XMVector3Cross(XMVectorSubtract(Math::load(mesh.positions[b]), p0),
                                           XMVectorSubtract(Math::load(mesh.positions[c]), p0));
    if (XMVectorGetX(XMVector3LengthSq(facing)) < 1e-12F)
    {
        return;
    }
    const bool outwards = XMVectorGetX(XMVector3Dot(facing, Math::load(mesh.normals[a]))) > 0.0F;
    mesh.indices.insert(mesh.indices.end(), {a, outwards ? b : c, outwards ? c : b});
}

/// @brief Packs a test mesh.
MeshData build(const TestMesh& mesh, const Math::Aabb& bounds)
{
    return makeMesh({.positions = mesh.positions, .normals = mesh.normals, .uvs = mesh.uvs, .indices = mesh.indices},
                    bounds);
}

/// @brief A unit sphere around the origin, `rings` × `segments` quads, facing outwards.
MeshData makeSphere(std::uint32_t rings, std::uint32_t segments)
{
    TestMesh mesh;
    for (std::uint32_t r = 0; r <= rings; ++r)
    {
        const float theta = Math::c_pi * static_cast<float>(r) / static_cast<float>(rings);
        for (std::uint32_t s = 0; s <= segments; ++s)
        {
            const float phi = 2.0F * Math::c_pi * static_cast<float>(s) / static_cast<float>(segments);
            const Math::Vec3 point{std::sin(theta) * std::cos(phi), std::cos(theta), std::sin(theta) * std::sin(phi)};
            mesh.positions.push_back(point);
            mesh.normals.push_back(point);
            mesh.uvs.push_back({static_cast<float>(s) / static_cast<float>(segments),
                                static_cast<float>(r) / static_cast<float>(rings)});
        }
    }
    for (std::uint32_t r = 0; r < rings; ++r)
    {
        for (std::uint32_t s = 0; s < segments; ++s)
        {
            const std::uint32_t a = r * (segments + 1) + s;
            const std::uint32_t c = a + segments + 1;
            addTriangle(mesh, a, c, c + 1);
            addTriangle(mesh, a, c + 1, a + 1);
        }
    }
    return build(mesh, {.center = {0.0F, 0.0F, 0.0F}, .extents = {1.0F, 1.0F, 1.0F}});
}

/// @brief A 2 × 2 grid of `cells` × `cells` quads in the XY plane, facing -Z like makeQuad().
MeshData makeGrid(std::uint32_t cells)
{
    TestMesh mesh;
    for (std::uint32_t y = 0; y <= cells; ++y)
    {
        for (std::uint32_t x = 0; x <= cells; ++x)
        {
            const float u = static_cast<float>(x) / static_cast<float>(cells);
            const float v = static_cast<float>(y) / static_cast<float>(cells);
            mesh.positions.push_back({u * 2.0F - 1.0F, v * 2.0F - 1.0F, 0.0F});
            mesh.normals.push_back({0.0F, 0.0F, -1.0F});
            mesh.uvs.push_back({u, 1.0F - v});
        }
    }
    for (std::uint32_t y = 0; y < cells; ++y)
    {
        for (std::uint32_t x = 0; x < cells; ++x)
        {
            const std::uint32_t a = y * (cells + 1) + x;
            const std::uint32_t c = a + cells + 1;
            addTriangle(mesh, a, c, c + 1);
            addTriangle(mesh, a, c + 1, a + 1);
        }
    }
    return build(mesh, {.center = {0.0F, 0.0F, 0.0F}, .extents = {1.0F, 1.0F, 0.01F}});
}

/// @brief How many pixels of two images differ by more than `tolerance` in a colour channel.
std::size_t differentPixels(const std::vector<std::uint8_t>& a, const std::vector<std::uint8_t>& b, int tolerance = 1)
{
    std::size_t count = 0;
    for (std::size_t i = 0; i + 3 < a.size() && i + 3 < b.size(); i += 4)
    {
        for (std::size_t c = 0; c < 3; ++c)
        {
            if (std::abs(int{a[i + c]} - int{b[i + c]}) > tolerance)
            {
                ++count;
                break;
            }
        }
    }
    return count;
}
} // namespace

TEST_F(GpuSceneTest, MeshletPathMatchesIndirect)
{
    m_meshlets = true;
    makeScene(8);
    if (!m_scene->meshletsAvailable())
    {
        GTEST_SKIP() << "this GPU has no mesh shaders";
    }
    const Result<std::uint32_t> sphere = m_scene->addMesh(makeSphere(24, 48));
    const Result<std::uint32_t> quad = m_scene->addMesh(makeQuad());
    const Result<std::uint32_t> oneSided = m_scene->addMaterial({.baseColorFactor = {0.8F, 0.4F, 0.2F, 1.0F}});
    const Result<std::uint32_t> twoSided = m_scene->addMaterial({.metallic = 0.0F, .doubleSided = true});
    ASSERT_TRUE(sphere && quad && oneSided && twoSided);

    // Every list: a turned sphere, a mirrored one, a double-sided quad seen from behind, and the cube.
    Math::Transform turned = at({-1.2F, 0.3F, 5.0F});
    turned.rotation = Math::fromAxisAngle(Math::Vec3{0.3F, 1.0F, 0.0F}, 0.8F);
    Math::Transform mirrored = at({1.2F, -0.4F, 5.0F});
    mirrored.scale = {-1.0F, 1.0F, 1.0F};
    Math::Transform behind = at({0.0F, 1.6F, 6.0F});
    behind.rotation = Math::fromAxisAngle(Math::c_up, Math::c_pi);
    behind.scale = {0.6F, 0.6F, 0.6F};
    const std::vector<Shader::InstanceData> instances{
        instanceOf(turned, *sphere, *oneSided), instanceOf(mirrored, *sphere, *oneSided),
        instanceOf(behind, *quad, *twoSided), instanceOf(at({0.0F, -1.6F, 4.0F}), 0, 9)};
    m_scene->setInstances(instances);
    Math::Frustum frustum{};

    m_scene->setMeshlets(false);
    EXPECT_EQ(runFrame(frustum), 4U) << "the indirect path draws all four";
    const std::vector<std::uint8_t> indirect = image();

    m_scene->setMeshlets(true);
    ASSERT_TRUE(m_scene->usesMeshlets());
    EXPECT_GT(runFrame(frustum), 0U);
    EXPECT_EQ(differentPixels(indirect, image()), 0U) << "the same triangles, shaded the same way";
}

TEST_F(GpuSceneTest, MeshletCullingSkipsHiddenClusters)
{
    m_meshlets = true;
    makeScene(2);
    if (!m_scene->meshletsAvailable())
    {
        GTEST_SKIP() << "this GPU has no mesh shaders";
    }
    const Result<std::uint32_t> grid = m_scene->addMesh(makeGrid(32));
    const Result<std::uint32_t> sphere = m_scene->addMesh(makeSphere(24, 48));
    const Result<std::uint32_t> oneSided = m_scene->addMaterial({.metallic = 0.0F});
    const Result<std::uint32_t> twoSided = m_scene->addMaterial({.metallic = 0.0F, .doubleSided = true});
    ASSERT_TRUE(grid && sphere && oneSided && twoSided);
    Math::Frustum frustum{};

    // Draws one instance through both paths: the same image, and how many meshlets the meshlet path drew.
    const auto drawBoth = [&](const Shader::InstanceData& instance)
    {
        const std::vector<Shader::InstanceData> instances{instance};
        m_scene->setInstances(instances);
        m_scene->setMeshlets(false);
        EXPECT_EQ(runFrame(frustum), 1U);
        const std::vector<std::uint8_t> indirect = image();
        m_scene->setMeshlets(true);
        const std::uint32_t drawn = runFrame(frustum);
        EXPECT_EQ(differentPixels(indirect, image()), 0U) << "culling removed only what can't be seen";
        return drawn;
    };

    // A wide grid reaching far to the left of the view: its instance is drawn, most of its meshlets aren't.
    Math::Transform wide = at({-7.0F, 0.0F, 3.0F});
    wide.scale = {8.0F, 8.0F, 8.0F};
    std::uint32_t drawn = drawBoth(instanceOf(wide, *grid, *oneSided));
    const std::uint32_t gridMeshlets = m_scene->meshletStats().meshlets;
    EXPECT_GT(drawn, 0U);
    EXPECT_LT(drawn, gridMeshlets / 2) << "meshlets outside the frustum are culled";

    // A sphere: the meshlets on its far side face away, so their cones cull them.
    drawn = drawBoth(instanceOf(at({0.0F, 0.0F, 4.0F}), *sphere, *oneSided));
    const std::uint32_t sphereMeshlets = m_scene->meshletStats().meshlets;
    EXPECT_GT(drawn, 0U);
    EXPECT_LT(drawn, sphereMeshlets) << "back-facing meshlets are culled";

    // Double-sided, both sides show: nothing is culled by its cone.
    drawn = drawBoth(instanceOf(at({0.0F, 0.0F, 4.0F}), *sphere, *twoSided));
    EXPECT_EQ(drawn, sphereMeshlets);
    EXPECT_EQ(m_lastMeshletCounts[MEOW2_TASK_COUNT_TRIANGLES], m_scene->meshletStats().triangles);
}

TEST_F(GpuSceneTest, MeshletViewColoursClusters)
{
    m_meshlets = true;
    makeScene(1);
    if (!m_scene->meshletsAvailable())
    {
        GTEST_SKIP() << "this GPU has no mesh shaders";
    }
    const Result<std::uint32_t> sphere = m_scene->addMesh(makeSphere(24, 48));
    const Result<std::uint32_t> material = m_scene->addMaterial({.metallic = 0.0F});
    ASSERT_TRUE(sphere && material);
    Result<DrawPipelines> views = m_scene->makeDrawPipelines({.options = {{"DEBUG_VIEW", "1"}}, .name = "Views"});
    ASSERT_TRUE(views) << views.error().message;
    ASSERT_TRUE(views->hasMeshLists);
    m_scene->setPipelineOverride(*views);
    const std::vector<Shader::InstanceData> instances{instanceOf(at({0.0F, 0.0F, 3.0F}), *sphere, *material)};
    m_scene->setInstances(instances);
    m_debugView = MEOW2_DEBUG_VIEW_MESHLETS;
    Math::Frustum frustum{};

    EXPECT_GT(runFrame(frustum), 0U);
    std::vector<std::uint32_t> colours;
    for (std::uint32_t y = c_size / 4; y < c_size * 3 / 4; ++y)
    {
        for (std::uint32_t x = c_size / 4; x < c_size * 3 / 4; ++x)
        {
            const std::array<std::uint8_t, 4> pixel = pixelAt(x, y);
            // The hue, without the shading: the largest channel scaled to 255.
            const int top = std::max({int{pixel[0]}, int{pixel[1]}, int{pixel[2]}, 1});
            const std::uint32_t hue = (static_cast<std::uint32_t>(pixel[0] * 7 / top) << 8U) |
                                      (static_cast<std::uint32_t>(pixel[1] * 7 / top) << 4U) |
                                      static_cast<std::uint32_t>(pixel[2] * 7 / top);
            if (std::find(colours.begin(), colours.end(), hue) == colours.end())
            {
                colours.push_back(hue);
            }
        }
    }
    EXPECT_GE(colours.size(), 4U) << "neighbouring meshlets get different colours";

    // The same view on the indirect path: no meshlets to colour, so grey (0.25).
    DrawPipelines indirectViews = *views;
    indirectViews.hasMeshLists = false;
    m_scene->setPipelineOverride(indirectViews);
    EXPECT_FALSE(m_scene->usesMeshlets());
    EXPECT_EQ(runFrame(frustum), 1U);
    const std::array<std::uint8_t, 4> centre = pixelAt(c_size / 2, c_size / 2);
    for (std::size_t c = 0; c < 3; ++c)
    {
        EXPECT_NEAR(int{centre[c]}, 64, 2) << "channel " << c;
    }
}
} // namespace Meow2::Renderer
