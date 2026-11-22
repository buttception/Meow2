/// @file
/// @brief Tests for SceneModel's gatherInstances() that need no GPU: which entity each instance was gathered for.

#include <Meow2/Core/Id/UuidGenerator.h>
#include <Meow2/Graphics/Renderer/SceneModel.h>
#include <Meow2/Math/Types.h>
#include <Meow2/Scene/ComponentRegistry.h>
#include <Meow2/Scene/Scene.h>
#include <Meow2/Scene/TransformHierarchy.h>

#include <gtest/gtest.h>

#include <memory_resource>
#include <vector>

namespace
{
namespace Renderer = Meow2::Renderer;
namespace Scene = Meow2::Scene;
} // namespace

TEST(SceneModel, GatherReportsEntities)
{
    Meow2::UuidGenerator ids;
    Scene::Scene scene{ids, *std::pmr::new_delete_resource()};
    const Meow2::AssetId twoParts = ids.next<Meow2::IdKind::Asset>();
    const Meow2::AssetId onePart = ids.next<Meow2::IdKind::Asset>();
    Renderer::RenderMeshes meshes;
    const Meow2::Math::Aabb box{.center = {0.0F, 0.0F, 0.0F}, .extents = {1.0F, 1.0F, 1.0F}};
    meshes.add(twoParts, {{.mesh = 1, .material = 0, .bounds = box}, {.mesh = 2, .material = 1, .bounds = box}});
    meshes.add(onePart, {{.mesh = 3, .material = 0, .bounds = box}});

    const auto drawn = [&scene](Meow2::AssetId mesh)
    {
        const entt::entity entity = scene.create();
        Scene::WorldTransform world{};
        DirectX::XMStoreFloat4x4(&world.matrix, DirectX::XMMatrixIdentity());
        scene.registry().emplace<Scene::WorldTransform>(entity, world);
        scene.registry().emplace<Renderer::MeshRenderer>(entity, Renderer::MeshRenderer{.mesh = mesh});
        return entity;
    };
    const entt::entity a = drawn(twoParts);
    const entt::entity b = drawn(onePart);
    static_cast<void>(scene.create()); // no mesh: no instance

    std::vector<Renderer::InstanceDesc> instances;
    std::vector<entt::entity> entities{entt::null}; // cleared first
    ASSERT_TRUE(Renderer::gatherInstances(scene, meshes, instances, &entities));
    ASSERT_EQ(instances.size(), 3U);
    ASSERT_EQ(entities.size(), instances.size()) << "one entity per instance";
    for (std::size_t i = 0; i < instances.size(); ++i)
    {
        // Each instance's mesh says which entity drew it: parts 1 and 2 are a's, part 3 is b's.
        EXPECT_TRUE(entities[i] == (instances[i].mesh == 3 ? b : a)) << "instance " << i;
    }

    // Without the list it gathers the same instances.
    std::vector<Renderer::InstanceDesc> again;
    ASSERT_TRUE(Renderer::gatherInstances(scene, meshes, again));
    EXPECT_EQ(again.size(), instances.size());
}

// One mobility field: a movable transform makes its meshes movable instances (drawn into the shadows every frame);
// MeshRenderer has no flag of its own any more, and files of version 1 lose theirs on the way up.
TEST(SceneModel, MobilityComesFromTheTransform)
{
    Meow2::UuidGenerator ids;
    Scene::Scene scene{ids, *std::pmr::new_delete_resource()};
    const Meow2::AssetId mesh = ids.next<Meow2::IdKind::Asset>();
    Renderer::RenderMeshes meshes;
    meshes.add(mesh, {{.mesh = 1, .material = 0, .bounds = {.center = {}, .extents = {1.0F, 1.0F, 1.0F}}}});
    const auto drawn = [&scene, mesh](Scene::Mobility mobility)
    {
        const entt::entity entity = scene.create();
        Scene::WorldTransform world{};
        DirectX::XMStoreFloat4x4(&world.matrix, DirectX::XMMatrixIdentity());
        scene.registry().emplace<Scene::WorldTransform>(entity, world);
        scene.registry().emplace<Scene::LocalTransform>(entity,
                                                        Scene::LocalTransform{.value = {}, .mobility = mobility});
        scene.registry().emplace<Renderer::MeshRenderer>(entity, Renderer::MeshRenderer{.mesh = mesh});
        return entity;
    };
    const entt::entity moving = drawn(Scene::Mobility::Movable);
    static_cast<void>(drawn(Scene::Mobility::Static));
    static_cast<void>(drawn(Scene::Mobility::Auto));

    std::vector<Renderer::InstanceDesc> instances;
    std::vector<entt::entity> entities;
    ASSERT_TRUE(Renderer::gatherInstances(scene, meshes, instances, &entities));
    ASSERT_EQ(instances.size(), 3U);
    for (std::size_t i = 0; i < instances.size(); ++i)
    {
        EXPECT_EQ(instances[i].movable, entities[i] == moving) << "only Movable moves; Auto counts as static for now";
    }

    Scene::ComponentRegistry registry;
    Renderer::registerRenderComponents(registry);
    const Scene::ComponentDesc* renderer = registry.find("MeshRenderer");
    ASSERT_NE(renderer, nullptr);
    EXPECT_EQ(renderer->version, 2U);
    Scene::FieldTable old{{"mesh", Scene::FieldValue{std::string{"x"}}}, {"movable", Scene::FieldValue{true}}};
    ASSERT_TRUE(Scene::migrate(*renderer, old, 1, 2));
    EXPECT_FALSE(old.contains("movable")) << "the flag is gone in version 2";
    ASSERT_TRUE(Scene::migrate(*renderer, old, 2, 1));
    EXPECT_TRUE(old.contains("movable")) << "written for older builds again";
}
