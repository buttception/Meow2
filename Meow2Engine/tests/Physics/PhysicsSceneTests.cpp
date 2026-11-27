/// @file
/// @brief Tests for PhysicsScene and the physics components: bodies built from RigidBody and Collider by Unity's
///        rules, stepped with the scene, and the components in scene files. Skipped in a build without physics.

#include <Meow2/Assets/SceneFile.h>
#include <Meow2/Core/Id/UuidGenerator.h>
#include <Meow2/Core/Jobs/JobSystem.h>
#include <Meow2/Core/Log/LogSystem.h>
#include <Meow2/Physics/CollisionMeshes.h>
#include <Meow2/Physics/PhysicsComponents.h>
#include <Meow2/Physics/PhysicsLibrary.h>
#include <Meow2/Physics/PhysicsScene.h>
#include <Meow2/Physics/PhysicsWorld.h>
#include <Meow2/Scene/ComponentRegistry.h>
#include <Meow2/Scene/Scene.h>
#include <Meow2/Scene/TransformHierarchy.h>

#include <gtest/gtest.h>

#include <memory>
#include <memory_resource>
#include <optional>

namespace
{
using namespace Meow2;
using namespace Meow2::Physics;
using Math::Vec3;

constexpr float c_step = 1.0F / 60.0F;

/// @brief A scene with its hierarchy, the physics components registered, a world, and a PhysicsScene over them.
class PhysicsSceneTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        if (!physicsAvailable())
        {
            GTEST_SKIP() << "this build has no physics";
        }
        Scene::registerSceneComponents(m_components);
        registerPhysicsComponents(m_components);
        m_components.registerAll(m_scene);
        Result<std::unique_ptr<PhysicsWorld>> world =
            PhysicsWorld::create({.library = m_library, .jobs = m_jobs, .log = m_log}, {});
        ASSERT_TRUE(world) << world.error().message;
        m_world = std::move(*world);
        m_physics = std::make_unique<PhysicsScene>(
            PhysicsSceneDeps{.scene = m_scene,
                             .transforms = m_transforms,
                             .world = *m_world,
                             .meshes = m_meshes,
                             .meshOf = [this](const Scene::Scene&, entt::entity entity) -> std::optional<AssetId>
                             {
                                 const auto found = m_meshOf.find(entity);
                                 return found != m_meshOf.end() ? std::optional<AssetId>{found->second} : std::nullopt;
                             },
                             .log = m_log});
    }

    void TearDown() override
    {
        m_physics.reset(); // its bodies before the world
        m_world.reset();
    }

    /// @brief An entity with a transform.
    entt::entity make(std::string_view name, const Math::Transform& local = {}, entt::entity parent = entt::null)
    {
        const entt::entity entity = m_scene.create(name);
        m_transforms.add(entity, local, parent);
        return entity;
    }

    /// @brief A static floor whose top is at y = 0: a Collider without a RigidBody.
    entt::entity addFloor()
    {
        const entt::entity floor = make("Floor", {.position = {0.0F, -0.5F, 0.0F}, .rotation = {}, .scale = {1, 1, 1}});
        m_scene.registry().emplace<Collider>(floor, Collider{.size = {40.0F, 1.0F, 40.0F}});
        return floor;
    }

    /// @brief Steps `seconds` at 60 Hz.
    void run(float seconds)
    {
        for (int i = 0; i < static_cast<int>(seconds / c_step + 0.5F); ++i)
        {
            m_physics->step(c_step);
        }
    }

    /// @brief An entity's world position.
    Vec3 worldPosition(entt::entity entity)
    {
        const Math::Mat4& m = m_scene.registry().get<Scene::WorldTransform>(entity).matrix;
        return {m._41, m._42, m._43};
    }

    Log::LogSystem m_logSystem{{.appName = "Test", .directory = {}, .console = false}};
    Log::Logger m_log{m_logSystem.createLogger("Engine")};
    Jobs::JobSystem m_jobs{{.threadCount = 2}, m_log};
    PhysicsLibrary m_library{m_log};
    UuidGenerator m_ids;
    Scene::ComponentRegistry m_components;
    Scene::Scene m_scene{m_ids, *std::pmr::new_delete_resource(), m_jobs.threadCount()};
    Scene::TransformHierarchy m_transforms{m_scene, m_jobs};
    CollisionMeshes m_meshes;
    std::map<entt::entity, AssetId> m_meshOf;
    std::unique_ptr<PhysicsWorld> m_world;
    std::unique_ptr<PhysicsScene> m_physics;
};
} // namespace

// T2: a Collider alone is static scenery; a RigidBody with a Collider falls onto it and rests there, and its
// LocalTransform follows.
TEST_F(PhysicsSceneTest, StaticAndDynamic)
{
    const entt::entity floor = addFloor();
    const entt::entity box = make("Box", {.position = {0.0F, 5.0F, 0.0F}, .rotation = {}, .scale = {1, 1, 1}});
    m_scene.registry().emplace<RigidBody>(box);
    m_scene.registry().emplace<Collider>(box);
    EXPECT_TRUE(m_physics->build().empty());
    EXPECT_EQ(m_physics->bodyCount(), 2U);
    ASSERT_TRUE(m_physics->bodyOf(floor));
    ASSERT_TRUE(m_physics->bodyOf(box));
    run(3.0F);
    EXPECT_NEAR(m_scene.registry().get<Scene::LocalTransform>(box).value.position.y, 0.5F, 0.03F) << "on the floor";
    EXPECT_NEAR(m_scene.registry().get<Scene::LocalTransform>(floor).value.position.y, -0.5F, 1e-6F) << "static";

    // A RigidBody without any Collider is skipped with a reason.
    m_scene.registry().emplace<RigidBody>(make("Empty"));
    const std::vector<std::string> warnings = m_physics->build();
    ASSERT_EQ(warnings.size(), 1U);
    EXPECT_NE(warnings[0].find("no Collider"), std::string::npos) << warnings[0];
}

// T3: a RigidBody's children's Colliders make one compound body; the children move with it.
TEST_F(PhysicsSceneTest, CompoundFromChildren)
{
    addFloor();
    const entt::entity chair = make("Chair", {.position = {0.0F, 3.0F, 0.0F}, .rotation = {}, .scale = {1, 1, 1}});
    m_scene.registry().emplace<RigidBody>(chair);
    const entt::entity left =
        make("Left", {.position = {-1.0F, 0.0F, 0.0F}, .rotation = {}, .scale = {1, 1, 1}}, chair);
    const entt::entity right =
        make("Right", {.position = {1.0F, 0.0F, 0.0F}, .rotation = {}, .scale = {1, 1, 1}}, chair);
    m_scene.registry().emplace<Collider>(left, Collider{.size = {0.5F, 0.5F, 0.5F}});
    m_scene.registry().emplace<Collider>(right, Collider{.size = {0.5F, 0.5F, 0.5F}});
    EXPECT_TRUE(m_physics->build().empty());
    EXPECT_EQ(m_physics->bodyCount(), 2U) << "the floor and one chair";
    EXPECT_FALSE(m_physics->bodyOf(left)) << "a child Collider is part of its RigidBody's body";
    run(3.0F);
    EXPECT_NEAR(worldPosition(chair).y, 0.25F, 0.03F) << "resting on its two boxes";
    EXPECT_NEAR(worldPosition(left).x, -1.0F, 0.02F) << "the children kept their places";
    EXPECT_NEAR(worldPosition(right).y, 0.25F, 0.03F);

    // A child Collider that can't be used (a mesh nothing shows) is left out with a reason; the body keeps the rest.
    m_scene.registry().emplace<Collider>(make("Broken", {}, chair), Collider{.shape = ColliderShape::Mesh});
    const std::vector<std::string> warnings = m_physics->build();
    ASSERT_EQ(warnings.size(), 1U);
    EXPECT_NE(warnings[0].find("Broken"), std::string::npos) << warnings[0];
    EXPECT_TRUE(m_physics->bodyOf(chair)) << "still a body, from its two boxes";
}

// T4: an entity's world scale scales its Collider, and the centre offset moves the shape.
TEST_F(PhysicsSceneTest, ScaleAndOffset)
{
    const entt::entity wide = make("Wide", {.position = {}, .rotation = {}, .scale = {4.0F, 1.0F, 4.0F}});
    m_scene.registry().emplace<Collider>(wide); // a 1 m box, 4 m wide by scale
    const entt::entity moved = make("Moved", {.position = {}, .rotation = {}, .scale = {1.0F, 1.0F, 1.0F}});
    m_scene.registry().emplace<Collider>(moved, Collider{.center = {0.0F, 0.0F, 10.0F}});
    EXPECT_TRUE(m_physics->build().empty());
    const Vec3 down{0.0F, -10.0F, 0.0F};
    EXPECT_TRUE(m_world->castRay(Vec3{1.9F, 5.0F, 0.0F}, down)) << "inside the scaled box";
    EXPECT_FALSE(m_world->castRay(Vec3{2.1F, 5.0F, 0.0F}, down)) << "outside it";
    const std::optional<RayHit> offset = m_world->castRay(Vec3{0.0F, 5.0F, 10.0F}, down);
    ASSERT_TRUE(offset) << "the shape is 10 m along Z from its entity";
    EXPECT_EQ(offset->userData, static_cast<std::uint64_t>(entt::to_integral(moved)));
}

// T5: a mesh Collider uses the entity's mesh: triangles on static scenery, a convex hull on a RigidBody.
TEST_F(PhysicsSceneTest, MeshColliders)
{
    const AssetId quad = m_ids.next<IdKind::Asset>();
    m_meshes.add(quad, CollisionMesh{.positions = {{-10, 0, -10}, {-10, 0, 10}, {10, 0, 10}, {10, 0, -10}},
                                     .indices = {0, 1, 2, 0, 2, 3}});
    const entt::entity ground = make("Ground");
    m_scene.registry().emplace<Collider>(ground, Collider{.shape = ColliderShape::Mesh});
    m_meshOf[ground] = quad;

    const AssetId rock = m_ids.next<IdKind::Asset>();
    m_meshes.add(rock, CollisionMesh{.positions = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}}, .indices = {0, 1, 2}});
    const entt::entity thrown = make("Rock", {.position = {3.0F, 4.0F, 0.0F}, .rotation = {}, .scale = {1, 1, 1}});
    m_scene.registry().emplace<RigidBody>(thrown);
    m_scene.registry().emplace<Collider>(thrown, Collider{.shape = ColliderShape::Mesh});
    m_meshOf[thrown] = rock;

    const entt::entity ball = make("Ball", {.position = {0.0F, 3.0F, 0.0F}, .rotation = {}, .scale = {1, 1, 1}});
    m_scene.registry().emplace<RigidBody>(ball);
    m_scene.registry().emplace<Collider>(ball, Collider{.shape = ColliderShape::Sphere, .radius = 0.25F});

    EXPECT_TRUE(m_physics->build().empty());
    EXPECT_EQ(m_physics->bodyCount(), 3U);
    run(3.0F);
    EXPECT_NEAR(worldPosition(ball).y, 0.25F, 0.03F) << "the ball rests on the mesh's triangles";
    EXPECT_GT(worldPosition(thrown).y, -0.1F) << "the hull landed on it too";
    EXPECT_LT(worldPosition(thrown).y, 1.0F);

    // A mesh Collider whose entity shows no known mesh is skipped with a reason.
    m_scene.registry().emplace<Collider>(make("Unknown"), Collider{.shape = ColliderShape::Mesh});
    const std::vector<std::string> warnings = m_physics->build();
    ASSERT_EQ(warnings.size(), 1U);
    EXPECT_NE(warnings[0].find("mesh"), std::string::npos) << warnings[0];
}

// T6: a kinematic body goes where its transform is moved, and pushes a dynamic body out of the way.
TEST_F(PhysicsSceneTest, KinematicFollowsTransform)
{
    const entt::entity pusher = make("Pusher");
    m_scene.registry().emplace<RigidBody>(pusher, RigidBody{.motion = MotionType::Kinematic});
    m_scene.registry().emplace<Collider>(pusher);
    const entt::entity pushed = make("Pushed", {.position = {1.5F, 0.0F, 0.0F}, .rotation = {}, .scale = {1, 1, 1}});
    m_scene.registry().emplace<RigidBody>(pushed, RigidBody{.gravityScale = 0.0F});
    m_scene.registry().emplace<Collider>(pushed);
    EXPECT_TRUE(m_physics->build().empty());
    for (int i = 1; i <= 60; ++i)
    {
        m_transforms.setLocal(
            pusher, {.position = {static_cast<float>(i) * 0.05F, 0.0F, 0.0F}, .rotation = {}, .scale = {1, 1, 1}});
        m_physics->step(c_step);
    }
    EXPECT_NEAR(m_world->position(*m_physics->bodyOf(pusher)).x, 3.0F, 1e-3F) << "the body followed its transform";
    EXPECT_GT(worldPosition(pushed).x, 3.9F) << "pushed ahead of the pusher (its centre ≥ 1 m in front)";
}

// T7: RigidBody and Collider survive a scene file round trip.
TEST_F(PhysicsSceneTest, ComponentsRoundTrip)
{
    const entt::entity entity = make("Crate");
    m_scene.registry().emplace<RigidBody>(
        entity, RigidBody{.motion = MotionType::Kinematic, .mass = 12.5F, .gravityScale = 0.5F, .continuous = true});
    m_scene.registry().emplace<Collider>(entity, Collider{.shape = ColliderShape::Capsule,
                                                          .center = {0.0F, 1.0F, 0.0F},
                                                          .radius = 0.3F,
                                                          .height = 1.8F,
                                                          .friction = 0.8F,
                                                          .sensor = true});
    const Result<std::string> text = Assets::writeScene(m_scene, m_components);
    ASSERT_TRUE(text) << text.error().message;
    EXPECT_NE(text->find("[entity.RigidBody]"), std::string::npos) << *text;
    EXPECT_NE(text->find("capsule"), std::string::npos) << *text;

    Scene::Scene copy{m_ids, *std::pmr::new_delete_resource(), m_jobs.threadCount()};
    Scene::TransformHierarchy copyTransforms{copy, m_jobs};
    m_components.registerAll(copy);
    const Result<Assets::SceneReadReport> read = Assets::readScene(*text, copy, copyTransforms, m_components);
    ASSERT_TRUE(read) << read.error().message;
    const std::optional<entt::entity> found = copy.find(m_scene.idOf(entity));
    ASSERT_TRUE(found);
    const RigidBody& body = copy.registry().get<RigidBody>(*found);
    EXPECT_EQ(body.motion, MotionType::Kinematic);
    EXPECT_FLOAT_EQ(body.mass, 12.5F);
    EXPECT_FLOAT_EQ(body.gravityScale, 0.5F);
    EXPECT_TRUE(body.continuous);
    const Collider& collider = copy.registry().get<Collider>(*found);
    EXPECT_EQ(collider.shape, ColliderShape::Capsule);
    EXPECT_FLOAT_EQ(collider.center.y, 1.0F);
    EXPECT_FLOAT_EQ(collider.height, 1.8F);
    EXPECT_FLOAT_EQ(collider.friction, 0.8F);
    EXPECT_TRUE(collider.sensor);
}
