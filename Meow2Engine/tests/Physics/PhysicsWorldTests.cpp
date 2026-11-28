/// @file
/// @brief The physics contract tests: what every backend (`MEOW2_PHYSICS_BACKEND`) must do. In a build without
///        physics they check that creating a world says so, and skip the rest.

#include <Meow2/Core/Jobs/JobSystem.h>
#include <Meow2/Core/Log/LogSystem.h>
#include <Meow2/Core/Memory/GlobalTracking.h>
#include <Meow2/Core/Memory/MemTag.h>
#include <Meow2/Physics/PhysicsLibrary.h>
#include <Meow2/Physics/PhysicsWorld.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>
#include <vector>

namespace
{
using namespace Meow2;
using namespace Meow2::Physics;
using Math::Vec3;

constexpr float c_step = 1.0F / 60.0F;

/// @brief A library, a job system and a world, made fresh for each test.
class PhysicsWorldTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        if (!physicsAvailable())
        {
            Result<std::unique_ptr<PhysicsWorld>> none = makeWorld();
            ASSERT_FALSE(none);
            EXPECT_EQ(none.error().code, ErrorCode::Unsupported);
            GTEST_SKIP() << "this build has no physics (" << physicsBackendName() << ")";
        }
        Result<std::unique_ptr<PhysicsWorld>> world = makeWorld();
        ASSERT_TRUE(world) << world.error().message;
        m_world = std::move(*world);
    }

    void TearDown() override
    {
        m_world.reset(); // before the library
    }

    /// @brief A world with the given description.
    Result<std::unique_ptr<PhysicsWorld>> makeWorld(const PhysicsWorldDesc& desc = {})
    {
        return PhysicsWorld::create({.library = m_library, .jobs = m_jobs, .log = m_log}, desc);
    }

    /// @brief A static floor whose top is at y = 0.
    BodyId addFloor(PhysicsWorld& world, CollisionLayer layer = 0)
    {
        const Result<BodyId> floor = world.createBody({.shape = BoxShape{Vec3{50.0F, 0.5F, 50.0F}},
                                                       .position = Vec3{0.0F, -0.5F, 0.0F},
                                                       .motion = MotionType::Static,
                                                       .layer = layer});
        EXPECT_TRUE(floor) << floor.error().message;
        return floor.value_or(BodyId{});
    }

    /// @brief Steps `seconds` at 60 Hz.
    static void run(PhysicsWorld& world, float seconds)
    {
        for (int i = 0; i < static_cast<int>(std::lround(seconds / c_step)); ++i)
        {
            world.step(c_step);
        }
    }

    Log::LogSystem m_logSystem{{.appName = "Test", .directory = {}, .console = false}};
    Log::Logger m_log{m_logSystem.createLogger("Engine")};
    Jobs::JobSystem m_jobs{{.threadCount = 4}, m_log};
    PhysicsLibrary m_library{m_log};
    std::unique_ptr<PhysicsWorld> m_world;
};
} // namespace

// T1: a box dropped on a floor falls under gravity, lands and comes to rest on it.
TEST_F(PhysicsWorldTest, BodyFallsAndRests)
{
    addFloor(*m_world);
    const Result<BodyId> box = m_world->createBody(
        {.shape = BoxShape{Vec3{0.5F, 0.5F, 0.5F}}, .position = Vec3{0.0F, 5.0F, 0.0F}, .userData = 42});
    ASSERT_TRUE(box) << box.error().message;
    EXPECT_EQ(m_world->bodyCount(), 2U);
    EXPECT_EQ(m_world->userData(*box), 42U);

    // Free fall for 0.5 s: about g t² / 2 = 1.23 m (a little less with damping), falling at about g t.
    run(*m_world, 0.5F);
    EXPECT_NEAR(m_world->position(*box).y, 5.0F - 1.226F, 0.08F);
    EXPECT_NEAR(m_world->linearVelocity(*box).y, -4.9F, 0.2F);

    run(*m_world, 3.0F);
    EXPECT_NEAR(m_world->position(*box).y, 0.5F, 0.03F) << "resting on the floor, not in it";
    EXPECT_NEAR(m_world->linearVelocity(*box).y, 0.0F, 0.05F);
    run(*m_world, 2.0F);
    EXPECT_FALSE(m_world->isAwake(*box)) << "it went to sleep at rest";

    // An impulse wakes it and moves it.
    m_world->addImpulse(*box, Vec3{1000.0F, 0.0F, 0.0F}); // 1000 kg box: 1 m/s
    EXPECT_TRUE(m_world->isAwake(*box));
    m_world->step(c_step);
    EXPECT_GT(m_world->linearVelocity(*box).x, 0.5F);
}

// T2: every shape makes a body; static bodies stay put, kinematic ones follow moveKinematic, dynamic ones answer
// forces and velocities.
TEST_F(PhysicsWorldTest, ShapesAndMotionTypes)
{
    const std::vector<Vec3> tetrahedron{{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    const std::vector<ShapeDesc> shapes{
        SphereShape{0.5F},
        BoxShape{Vec3{0.5F, 0.25F, 1.0F}},
        CapsuleShape{0.5F, 0.25F},
        ConvexHullShape{tetrahedron},
        CompoundShape{{CompoundPart{.shape = BoxShape{}, .position = Vec3{-1.0F, 0.0F, 0.0F}, .rotation = {}},
                       CompoundPart{.shape = SphereShape{0.5F}, .position = Vec3{1.0F, 0.0F, 0.0F}, .rotation = {}}}},
    };
    for (std::size_t i = 0; i < shapes.size(); ++i)
    {
        const Result<BodyId> body = m_world->createBody(
            {.shape = shapes[i], .position = Vec3{static_cast<float>(i) * 5.0F, 10.0F, 0.0F}, .gravityScale = 0.0F});
        ASSERT_TRUE(body) << "shape " << i << ": " << body.error().message;
    }
    // A mesh: two triangles of a floor, on a static body.
    const Result<BodyId> mesh =
        m_world->createBody({.shape = MeshShape{.vertices = {{-5, 0, -5}, {-5, 0, 5}, {5, 0, 5}, {5, 0, -5}},
                                                .indices = {0, 1, 2, 0, 2, 3}},
                             .position = Vec3{0.0F, -20.0F, 0.0F},
                             .motion = MotionType::Static});
    ASSERT_TRUE(mesh) << mesh.error().message;

    // Static: doesn't move, even when stepped; never awake.
    const Result<BodyId> wall =
        m_world->createBody({.shape = BoxShape{}, .position = Vec3{0.0F, 0.0F, 20.0F}, .motion = MotionType::Static});
    ASSERT_TRUE(wall);
    // Kinematic: goes where it's told over a step, and has the velocity that takes it there.
    const Result<BodyId> platform = m_world->createBody(
        {.shape = BoxShape{}, .position = Vec3{0.0F, 0.0F, -20.0F}, .motion = MotionType::Kinematic});
    ASSERT_TRUE(platform);
    // Dynamic, without gravity: a set velocity carries it.
    const Result<BodyId> ball =
        m_world->createBody({.shape = SphereShape{0.25F}, .position = Vec3{0.0F, 30.0F, 0.0F}, .gravityScale = 0.0F});
    ASSERT_TRUE(ball);
    m_world->setLinearVelocity(*ball, Vec3{2.0F, 0.0F, 0.0F});

    m_world->moveKinematic(*platform, Vec3{1.0F, 0.0F, -20.0F}, Math::Quat{}, c_step);
    m_world->step(c_step);
    EXPECT_NEAR(m_world->position(*platform).x, 1.0F, 1e-4F);
    EXPECT_NEAR(m_world->linearVelocity(*platform).x, 60.0F, 1e-2F) << "1 m in 1/60 s";
    EXPECT_NEAR(m_world->position(*wall).z, 20.0F, 1e-6F);
    EXPECT_FALSE(m_world->isAwake(*wall));
    run(*m_world, 1.0F - c_step);
    EXPECT_NEAR(m_world->position(*ball).x, 2.0F * 0.95F, 0.1F) << "about 2 m in 1 s, less a little damping";

    // A teleport puts it there exactly.
    m_world->setTransform(*ball, Vec3{7.0F, 8.0F, 9.0F}, Math::Quat{});
    EXPECT_NEAR(m_world->position(*ball).z, 9.0F, 1e-6F);
    m_world->setGravity(Vec3{0.0F, -1.0F, 0.0F});
    EXPECT_FLOAT_EQ(m_world->gravity().y, -1.0F);
}

// T3: layers that don't collide pass through each other; queries see only the layers in their mask.
TEST_F(PhysicsWorldTest, LayersFilterCollisions)
{
    PhysicsWorldDesc desc;
    desc.layerCount = 3;
    desc.layerMasks[1] &= ~(1U << 2); // layer 1 ignores layer 2 (and so, for collisions, 2 ignores 1)
    Result<std::unique_ptr<PhysicsWorld>> made = makeWorld(desc);
    ASSERT_TRUE(made);
    PhysicsWorld& world = **made;
    addFloor(world, 1);
    const Result<BodyId> ghost =
        world.createBody({.shape = SphereShape{0.5F}, .position = Vec3{0.0F, 2.0F, 0.0F}, .layer = 2});
    const Result<BodyId> solid =
        world.createBody({.shape = SphereShape{0.5F}, .position = Vec3{3.0F, 2.0F, 0.0F}, .layer = 0});
    ASSERT_TRUE(ghost && solid);
    run(world, 2.0F);
    EXPECT_LT(world.position(*ghost).y, -5.0F) << "fell through the floor of a layer it ignores";
    EXPECT_NEAR(world.position(*solid).y, 0.5F, 0.03F) << "landed on it";

    // The ray down at x = 3 hits the solid ball, unless its layer is masked out: then the floor.
    const std::optional<RayHit> all = world.castRay(Vec3{3.0F, 10.0F, 0.0F}, Vec3{0.0F, -20.0F, 0.0F});
    ASSERT_TRUE(all);
    EXPECT_EQ(all->body, *solid);
    const std::optional<RayHit> floorOnly =
        world.castRay(Vec3{3.0F, 10.0F, 0.0F}, Vec3{0.0F, -20.0F, 0.0F}, {.layerMask = 1U << 1});
    ASSERT_TRUE(floorOnly);
    EXPECT_NE(floorOnly->body, *solid);
    EXPECT_NEAR(floorOnly->point.y, 0.0F, 1e-3F);

    EXPECT_FALSE(world.createBody({.shape = SphereShape{}, .layer = 3})) << "outside the world's 3 layers";
    PhysicsWorldDesc tooMany;
    tooMany.layerCount = c_maxCollisionLayers + 1;
    EXPECT_FALSE(makeWorld(tooMany));
}

// T4: rays, shape casts and overlaps find the right bodies, with points, normals and fractions.
TEST_F(PhysicsWorldTest, Queries)
{
    addFloor(*m_world);
    const Result<BodyId> near = m_world->createBody(
        {.shape = BoxShape{}, .position = Vec3{0.0F, 0.5F, 5.0F}, .motion = MotionType::Static, .userData = 7});
    const Result<BodyId> far =
        m_world->createBody({.shape = BoxShape{}, .position = Vec3{0.0F, 0.5F, 10.0F}, .motion = MotionType::Static});
    ASSERT_TRUE(near && far);

    // A ray along +Z at y = 0.5 hits the near box's front face (z = 4.5), 4.5 m along a 20 m ray.
    const std::optional<RayHit> hit = m_world->castRay(Vec3{0.0F, 0.5F, 0.0F}, Vec3{0.0F, 0.0F, 20.0F});
    ASSERT_TRUE(hit);
    EXPECT_EQ(hit->body, *near);
    EXPECT_EQ(hit->userData, 7U);
    EXPECT_NEAR(hit->fraction, 4.5F / 20.0F, 1e-4F);
    EXPECT_NEAR(hit->point.z, 4.5F, 1e-3F);
    EXPECT_NEAR(hit->normal.z, -1.0F, 1e-3F) << "the face looks back at the ray";
    // Ignoring it, the far one.
    const std::optional<RayHit> past =
        m_world->castRay(Vec3{0.0F, 0.5F, 0.0F}, Vec3{0.0F, 0.0F, 20.0F}, {.ignore = *near});
    ASSERT_TRUE(past);
    EXPECT_EQ(past->body, *far);
    // Too short, or pointing away: nothing.
    EXPECT_FALSE(m_world->castRay(Vec3{0.0F, 0.5F, 0.0F}, Vec3{0.0F, 0.0F, 4.0F}));
    EXPECT_FALSE(m_world->castRay(Vec3{0.0F, 0.5F, 0.0F}, Vec3{0.0F, 20.0F, 0.0F}));

    // A sphere of radius 0.25 swept along +Z touches the near box when its centre is 0.25 short of the face.
    const Result<std::optional<ShapeHit>> swept =
        m_world->castShape(SphereShape{0.25F}, Vec3{0.0F, 0.5F, 0.0F}, Math::Quat{}, Vec3{0.0F, 0.0F, 20.0F});
    ASSERT_TRUE(swept);
    ASSERT_TRUE(swept->has_value());
    EXPECT_EQ((*swept)->body, *near);
    EXPECT_NEAR((*swept)->fraction, 4.25F / 20.0F, 2e-3F);
    EXPECT_NEAR((*swept)->normal.z, -1.0F, 1e-2F);

    // A box overlapping both boxes and the floor finds the three, sorted and each once.
    const Result<std::vector<BodyId>> overlapping =
        m_world->overlap(BoxShape{Vec3{0.5F, 0.5F, 3.0F}}, Vec3{0.0F, 0.45F, 7.5F}, Math::Quat{}); // into the floor
    ASSERT_TRUE(overlapping);
    EXPECT_EQ(overlapping->size(), 3U);
    EXPECT_TRUE(std::ranges::is_sorted(*overlapping, [](BodyId a, BodyId b) { return a.value < b.value; }));
    // In the air between them, nothing.
    const Result<std::vector<BodyId>> empty = m_world->overlap(SphereShape{0.5F}, Vec3{0.0F, 3.0F, 7.5F}, {});
    ASSERT_TRUE(empty);
    EXPECT_TRUE(empty->empty());

    // Meshes and compounds aren't query shapes.
    EXPECT_FALSE(m_world->overlap(CompoundShape{{CompoundPart{}}}, Vec3{}, {}));
}

// T5: contact events begin and end once per pair; sensors report what enters and leaves them.
TEST_F(PhysicsWorldTest, ContactAndSensorEvents)
{
    const BodyId floor = addFloor(*m_world);
    const Result<BodyId> box = m_world->createBody({.shape = BoxShape{}, .position = Vec3{0.0F, 2.0F, 0.0F}});
    const Result<BodyId> sensor = m_world->createBody({.shape = BoxShape{Vec3{1.0F, 1.0F, 1.0F}},
                                                       .position = Vec3{5.0F, 1.0F, 0.0F},
                                                       .motion = MotionType::Static,
                                                       .sensor = true});
    const Result<BodyId> ball = m_world->createBody(
        {.shape = SphereShape{0.25F}, .position = Vec3{2.0F, 1.0F, 0.0F}, .gravityScale = 0.0F, .allowSleep = false});
    ASSERT_TRUE(box && sensor && ball);
    m_world->setLinearVelocity(*ball, Vec3{4.0F, 0.0F, 0.0F}); // through the sensor (x 4..6) in about 0.6 s

    std::vector<ContactEvent> contacts;
    std::vector<SensorEvent> sensors;
    for (int i = 0; i < 120; ++i)
    {
        m_world->step(c_step);
        contacts.insert(contacts.end(), m_world->contactEvents().begin(), m_world->contactEvents().end());
        sensors.insert(sensors.end(), m_world->sensorEvents().begin(), m_world->sensorEvents().end());
    }
    // The box landed: one Begin with the floor, the normal pointing from A to B (up or down by the id order).
    ASSERT_EQ(contacts.size(), 1U);
    EXPECT_EQ(contacts[0].type, EventType::Begin);
    const bool floorIsA = contacts[0].bodyA == floor;
    EXPECT_EQ(floorIsA ? contacts[0].bodyB : contacts[0].bodyA, *box);
    EXPECT_NEAR(contacts[0].normal.y, floorIsA ? 1.0F : -1.0F, 1e-3F);
    EXPECT_NEAR(contacts[0].point.y, 0.0F, 0.05F);
    // The ball entered and left the sensor, and didn't bounce off it.
    ASSERT_EQ(sensors.size(), 2U);
    EXPECT_EQ(sensors[0].type, EventType::Begin);
    EXPECT_EQ(sensors[1].type, EventType::End);
    EXPECT_EQ(sensors[0].sensor, *sensor);
    EXPECT_EQ(sensors[0].other, *ball);
    EXPECT_GT(m_world->position(*ball).x, 6.0F);

    // Destroying the box ends its touch with the floor, delivered with the next step.
    ASSERT_TRUE(m_world->destroyBody(*box));
    m_world->step(c_step);
    ASSERT_EQ(m_world->contactEvents().size(), 1U);
    EXPECT_EQ(m_world->contactEvents()[0].type, EventType::End);
    m_world->step(c_step);
    EXPECT_TRUE(m_world->contactEvents().empty()) << "and only once";
}

// T6: the same scene, built the same way, gives the same positions bit for bit (with 4 job threads).
TEST_F(PhysicsWorldTest, Determinism)
{
    const auto simulate = [this]
    {
        Result<std::unique_ptr<PhysicsWorld>> made = makeWorld();
        EXPECT_TRUE(made);
        PhysicsWorld& world = **made;
        addFloor(world);
        std::vector<BodyId> bodies;
        for (int i = 0; i < 64; ++i)
        {
            const float x = static_cast<float>(i % 4) * 0.6F;
            const float z = static_cast<float>((i / 4) % 4) * 0.6F;
            const float y = 1.0F + static_cast<float>(i / 16) * 1.1F;
            const Result<BodyId> body = world.createBody(
                {.shape = i % 2 == 0 ? ShapeDesc{BoxShape{Vec3{0.25F, 0.25F, 0.25F}}} : ShapeDesc{SphereShape{0.3F}},
                 .position = Vec3{x, y, z},
                 .rotation = Math::Quat{0.1F, 0.2F, 0.0F, 0.97F}});
            EXPECT_TRUE(body);
            bodies.push_back(body.value_or(BodyId{}));
        }
        run(world, 3.0F);
        std::vector<float> state;
        for (const BodyId body : bodies)
        {
            const Math::WorldPosition p = world.position(body);
            const Math::Quat q = world.rotation(body);
            state.insert(state.end(), {p.x, p.y, p.z, q.x, q.y, q.z, q.w});
        }
        return state;
    };
    const std::vector<float> first = simulate();
    const std::vector<float> second = simulate();
    ASSERT_EQ(first.size(), second.size());
    EXPECT_EQ(std::memcmp(first.data(), second.data(), first.size() * sizeof(float)), 0) << "bit-identical";
}

// T1 (#244): a mass override sets the mass whatever the shape's size: a 10 kg body takes 1 m/s from 10 kg·m/s.
TEST_F(PhysicsWorldTest, MassOverride)
{
    for (const float half : {0.1F, 2.0F})
    {
        const Result<BodyId> body = m_world->createBody({.shape = BoxShape{Vec3{half, half, half}},
                                                         .position = Vec3{half * 10.0F, 50.0F, 0.0F},
                                                         .mass = 10.0F,
                                                         .gravityScale = 0.0F});
        ASSERT_TRUE(body) << body.error().message;
        m_world->addImpulse(*body, Vec3{10.0F, 0.0F, 0.0F});
        EXPECT_NEAR(m_world->linearVelocity(*body).x, 1.0F, 1e-3F) << "half extent " << half;
    }
    EXPECT_FALSE(m_world->createBody({.shape = SphereShape{}, .mass = -1.0F})) << "a negative mass is refused";
}

// T7: bad input is refused with a message, not an assert or a crash; destroyed ids are refused.
TEST_F(PhysicsWorldTest, BadInput)
{
    const auto refused = [this](const BodyDesc& desc)
    {
        const Result<BodyId> body = m_world->createBody(desc);
        return !body && body.error().code == ErrorCode::InvalidArgument;
    };
    EXPECT_TRUE(refused({.shape = SphereShape{0.0F}}));
    EXPECT_TRUE(refused({.shape = BoxShape{Vec3{1.0F, -1.0F, 1.0F}}}));
    EXPECT_TRUE(refused({.shape = CapsuleShape{0.5F, 0.0F}}));
    EXPECT_TRUE(refused({.shape = ConvexHullShape{{{0, 0, 0}, {1, 0, 0}, {0, 1, 0}}}})) << "3 points";
    EXPECT_TRUE(refused({.shape = ConvexHullShape{{{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {1, 1, 0}}}})) << "flat";
    EXPECT_TRUE(refused({.shape = MeshShape{.vertices = {{0, 0, 0}}, .indices = {}}, .motion = MotionType::Static}));
    EXPECT_TRUE(refused(
        {.shape = MeshShape{.vertices = {{0, 0, 0}, {1, 0, 0}}, .indices = {0, 1, 2}}, .motion = MotionType::Static}))
        << "index out of range";
    EXPECT_TRUE(refused({.shape = MeshShape{.vertices = {{0, 0, 0}, {1, 0, 0}, {0, 0, 1}}, .indices = {0, 1, 2}},
                         .motion = MotionType::Dynamic}))
        << "a mesh can't be dynamic";
    EXPECT_TRUE(refused({.shape = CompoundShape{}}));
    EXPECT_TRUE(refused({.shape = SphereShape{}, .rotation = Math::Quat{0.0F, 0.0F, 0.0F, 0.0F}}));
    EXPECT_TRUE(refused({.shape = SphereShape{}, .density = 0.0F}));
    EXPECT_TRUE(refused({.shape = SphereShape{}, .restitution = 2.0F}));
    EXPECT_EQ(m_world->bodyCount(), 0U) << "nothing half-made was left behind";

    const Result<BodyId> body = m_world->createBody({.shape = SphereShape{}});
    ASSERT_TRUE(body);
    EXPECT_TRUE(m_world->contains(*body));
    ASSERT_TRUE(m_world->destroyBody(*body));
    EXPECT_FALSE(m_world->contains(*body));
    const Result<void> again = m_world->destroyBody(*body);
    ASSERT_FALSE(again);
    EXPECT_EQ(again.error().code, ErrorCode::NotFound);
    EXPECT_FALSE(m_world->contains(BodyId{}));
    // A new body may reuse the slot, but not the id.
    const Result<BodyId> next = m_world->createBody({.shape = SphereShape{}});
    ASSERT_TRUE(next);
    EXPECT_NE(*next, *body);
    EXPECT_FALSE(m_world->contains(*body));
}

// T8: the library's memory is counted under MemTag::Physics and given back when the world and library go.
TEST(PhysicsMemoryTest, MemoryTagged)
{
    if (!physicsAvailable() || !Memory::c_memoryTrackingAvailable)
    {
        GTEST_SKIP() << "needs physics and memory tracking (Debug, RelWithDebInfo)";
    }
    Log::LogSystem logSystem{{.appName = "Test", .directory = {}, .console = false}};
    Log::Logger log = logSystem.createLogger("Engine");
    Jobs::JobSystem jobs{{.threadCount = 2}, log};
    const std::uint64_t before = Memory::globalTracker().stats(Memory::MemTag::Physics).currentBytes;
    {
        PhysicsLibrary library{log};
        Result<std::unique_ptr<PhysicsWorld>> world =
            PhysicsWorld::create({.library = library, .jobs = jobs, .log = log}, {});
        ASSERT_TRUE(world);
        ASSERT_TRUE((*world)->createBody({.shape = BoxShape{}}));
        (*world)->step(c_step);
        EXPECT_GT(Memory::globalTracker().stats(Memory::MemTag::Physics).currentBytes, before + 1024U * 1024U)
            << "the world and its scratch memory";
    }
    EXPECT_EQ(Memory::globalTracker().stats(Memory::MemTag::Physics).currentBytes, before) << "all of it given back";
}
