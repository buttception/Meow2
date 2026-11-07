/// @file
/// @brief Tests for the extract step: the packet is camera relative and independent of the scene afterwards.

#include <Meow2/Core/Id/UuidGenerator.h>
#include <Meow2/Core/Memory/BlockArena.h>
#include <Meow2/Core/Memory/GlobalTracking.h>
#include <Meow2/Graphics/Renderer/FramePacket.h>
#include <Meow2/Math/Transform.h>
#include <Meow2/Scene/Scene.h>
#include <Meow2/Scene/TransformHierarchy.h>

#include <gtest/gtest.h>

#include <memory_resource>

namespace Meow2::Renderer
{
namespace
{
class FramePacketTest : public ::testing::Test
{
protected:
    FramePacketTest()
        : m_arena(*std::pmr::get_default_resource(), Memory::MemTag::Renderer)
        , m_scene(m_ids, *std::pmr::get_default_resource())
    {
        m_scene.registerComponent<Scene::WorldTransform>();
        m_scene.registerComponent<Renderable>();
    }

    entt::entity makeEntity(float x, Renderable renderable)
    {
        const entt::entity entity = m_scene.create();
        Math::Transform transform;
        transform.position = Math::WorldPosition{x, 0.0F, 0.0F};
        Scene::WorldTransform world;
        DirectX::XMStoreFloat4x4(&world.matrix, Math::toMatrix(transform));
        m_scene.registry().emplace<Scene::WorldTransform>(entity, world);
        m_scene.registry().emplace<Renderable>(entity, renderable);
        return entity;
    }

    UuidGenerator m_ids;
    Memory::BlockArena m_arena;
    Scene::Scene m_scene;
};
} // namespace

TEST_F(FramePacketTest, CopiesRenderables)
{
    makeEntity(1.0F, {.mesh = 7, .material = 9});
    const entt::entity notDrawn = m_scene.create();
    static_cast<void>(notDrawn);

    const FramePacket& packet = extractFrame(m_scene, {.width = 640, .height = 480}, m_arena);

    ASSERT_EQ(packet.instances.size(), 1U);
    EXPECT_EQ(packet.instances[0].mesh, 7U);
    EXPECT_EQ(packet.instances[0].material, 9U);
    EXPECT_FLOAT_EQ(packet.frame.viewport.x, 640.0F);
    EXPECT_FLOAT_EQ(packet.frame.viewport.w, 1.0F / 480.0F);
}

TEST_F(FramePacketTest, PositionsAreCameraRelative)
{
    makeEntity(100'000.0F, {});
    FrameView view;
    view.transform.position = Math::WorldPosition{99'999.0F, 0.0F, 0.0F};

    const FramePacket& packet = extractFrame(m_scene, view, m_arena);

    ASSERT_EQ(packet.instances.size(), 1U);
    EXPECT_FLOAT_EQ(packet.instances[0].world.m[3][0], 1.0F);
    EXPECT_FLOAT_EQ(packet.frame.cameraPosition.x, 99'999.0F);
}

TEST_F(FramePacketTest, SurvivesSceneChanges)
{
    const entt::entity entity = makeEntity(1.0F, {.mesh = 3});
    const FramePacket& packet = extractFrame(m_scene, {}, m_arena);

    m_scene.destroy(entity);

    ASSERT_EQ(packet.instances.size(), 1U);
    EXPECT_EQ(packet.instances[0].mesh, 3U);
}
} // namespace Meow2::Renderer
