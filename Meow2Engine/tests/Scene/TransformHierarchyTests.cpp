/// @file
/// @brief Tests for TransformHierarchy: world = local × parent world, dirty propagation, reparenting, orphans, render
///        interpolation, and a parallel update of 100k entities checked against a single-threaded reference.

#include <Meow2/Core/Id/UuidGenerator.h>
#include <Meow2/Core/Jobs/JobSystem.h>
#include <Meow2/Core/Log/LogSystem.h>
#include <Meow2/Core/Platform.h>
#include <Meow2/Math/Random.h>
#include <Meow2/Scene/Scene.h>
#include <Meow2/Scene/TransformHierarchy.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <format>
#include <memory_resource>
#include <optional>
#include <vector>

namespace Meow2::Scene
{
namespace
{
namespace fs = std::filesystem;

/// @brief A job system with 4 threads, a scene and its hierarchy.
class TransformHierarchyTest : public ::testing::Test
{
protected:
    TransformHierarchyTest()
        : m_logSystem({.appName = "Test", .directory = logDirectory(), .console = false})
        , m_log(m_logSystem.createLogger("Engine"))
    {
    }

    /// @brief World position of an entity (its matrix's translation row).
    Math::Vec3 worldPosition(entt::entity entity)
    {
        const Math::Mat4& m = m_scene.registry().get<WorldTransform>(entity).matrix;
        return Math::Vec3{m.m[3][0], m.m[3][1], m.m[3][2]};
    }

    /// @brief An entity with a transform at `position` under `parent`.
    entt::entity spawn(const Math::Vec3& position, entt::entity parent = entt::null)
    {
        const entt::entity entity = m_scene.createTransient();
        m_hierarchy.add(entity, Math::Transform{position, Math::Quat{}, {1.0F, 1.0F, 1.0F}}, parent);
        return entity;
    }

    Log::LogSystem m_logSystem;
    Log::Logger m_log;
    Jobs::JobSystem m_jobs{{.threadCount = 4}, m_log};
    UuidGenerator m_ids;
    Scene m_scene{m_ids, *std::pmr::new_delete_resource(), m_jobs.threadCount()};
    TransformHierarchy m_hierarchy{m_scene, m_jobs};

private:
    static fs::path logDirectory()
    {
        const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
        return fs::temp_directory_path() / std::format("meow2-hierarchy-{}-{}", c_configName, info->name());
    }
};
} // namespace

TEST_F(TransformHierarchyTest, WorldIsParentTimesLocal)
{
    const entt::entity root = m_scene.createTransient();
    m_hierarchy.add(
        root,
        Math::Transform{{10.0F, 0.0F, 0.0F}, Math::fromAxisAngle(Math::c_up, Math::c_halfPi), {2.0F, 2.0F, 2.0F}});
    const entt::entity child = spawn({0.0F, 0.0F, 1.0F}, root);
    const entt::entity grandchild = spawn({0.0F, 3.0F, 0.0F}, child);
    m_hierarchy.update();
    EXPECT_EQ(m_hierarchy.levelCount(), 3U);
    // The child is 1 forward of the root; the root is turned to face right and scaled 2: (10 + 2, 0, 0).
    EXPECT_TRUE(Math::nearlyEqual(worldPosition(child), Math::Vec3{12.0F, 0.0F, 0.0F}, 1e-5F))
        << std::format("{}", worldPosition(child));
    EXPECT_TRUE(Math::nearlyEqual(worldPosition(grandchild), Math::Vec3{12.0F, 6.0F, 0.0F}, 1e-5F))
        << "up 3, scaled 2 by the root";
    EXPECT_TRUE(Math::nearlyEqual(worldPosition(root), Math::Vec3{10.0F, 0.0F, 0.0F}));
}

TEST_F(TransformHierarchyTest, DirtyPropagatesToChildren)
{
    const entt::entity a = spawn({1.0F, 0.0F, 0.0F});
    const entt::entity aChild = spawn({0.0F, 1.0F, 0.0F}, a);
    const entt::entity b = spawn({-1.0F, 0.0F, 0.0F});
    const entt::entity bChild = spawn({0.0F, 1.0F, 0.0F}, b);
    m_hierarchy.update();

    m_hierarchy.setLocal(a, Math::Transform{{5.0F, 0.0F, 0.0F}, Math::Quat{}, {1.0F, 1.0F, 1.0F}});
    m_hierarchy.update();
    const auto& states = m_scene.registry().storage<TransformState>();
    EXPECT_TRUE(states.get(a).changed);
    EXPECT_TRUE(states.get(aChild).changed) << "follows its parent";
    EXPECT_FALSE(states.get(b).changed) << "untouched subtree isn't recomputed";
    EXPECT_FALSE(states.get(bChild).changed);
    EXPECT_TRUE(Math::nearlyEqual(worldPosition(aChild), Math::Vec3{5.0F, 1.0F, 0.0F}));

    // Writing LocalTransform directly + markDirty.
    m_scene.registry().get<LocalTransform>(bChild).value.position = Math::Vec3{0.0F, 2.0F, 0.0F};
    m_hierarchy.markDirty(bChild);
    m_hierarchy.update();
    EXPECT_FALSE(states.get(a).changed);
    EXPECT_TRUE(Math::nearlyEqual(worldPosition(bChild), Math::Vec3{-1.0F, 2.0F, 0.0F}));
}

TEST_F(TransformHierarchyTest, Reparenting)
{
    const entt::entity left = spawn({-10.0F, 0.0F, 0.0F});
    const entt::entity right = spawn({10.0F, 0.0F, 0.0F});
    const entt::entity item = spawn({0.0F, 1.0F, 0.0F}, left);
    const entt::entity itemChild = spawn({0.0F, 0.0F, 1.0F}, item);
    m_hierarchy.update();
    EXPECT_TRUE(Math::nearlyEqual(worldPosition(itemChild), Math::Vec3{-10.0F, 1.0F, 1.0F}));

    m_hierarchy.setParent(item, right);
    m_hierarchy.update();
    EXPECT_TRUE(Math::nearlyEqual(worldPosition(itemChild), Math::Vec3{10.0F, 1.0F, 1.0F})) << "the subtree moves";

    m_hierarchy.setParent(item, entt::null);
    m_hierarchy.update();
    EXPECT_TRUE(Math::nearlyEqual(worldPosition(item), Math::Vec3{0.0F, 1.0F, 0.0F})) << "now a root";
    EXPECT_EQ(m_hierarchy.levelCount(), 2U);
}

TEST_F(TransformHierarchyTest, CycleAsserts)
{
    if constexpr (!c_assertsEnabled)
    {
        GTEST_SKIP() << "asserts are compiled out in this configuration";
    }
    const entt::entity parent = spawn({0.0F, 0.0F, 0.0F});
    const entt::entity child = spawn({0.0F, 1.0F, 0.0F}, parent);
    EXPECT_DEATH(m_hierarchy.setParent(parent, child), "cycle");
}

TEST_F(TransformHierarchyTest, DestroyedParentOrphans)
{
    const entt::entity parent = spawn({5.0F, 0.0F, 0.0F});
    const entt::entity child = spawn({0.0F, 2.0F, 0.0F}, parent);
    const entt::entity grandchild = spawn({0.0F, 0.0F, 3.0F}, child);
    m_hierarchy.update();
    m_scene.destroy(parent);
    m_hierarchy.update();
    EXPECT_FALSE(m_scene.registry().all_of<Parent>(child)) << "the child is a root now";
    EXPECT_TRUE(Math::nearlyEqual(worldPosition(child), Math::Vec3{0.0F, 2.0F, 0.0F})) << "its local is its world";
    EXPECT_TRUE(Math::nearlyEqual(worldPosition(grandchild), Math::Vec3{0.0F, 2.0F, 3.0F})) << "still under the child";
    EXPECT_EQ(m_hierarchy.levelCount(), 2U);
}

TEST_F(TransformHierarchyTest, InterpolationForRenderAlpha)
{
    const entt::entity entity = spawn({0.0F, 0.0F, 0.0F});
    m_hierarchy.update();
    m_hierarchy.savePrevious();
    m_hierarchy.setLocal(
        entity, Math::Transform{{4.0F, 0.0F, 0.0F}, Math::fromAxisAngle(Math::c_up, 1.0F), {1.0F, 1.0F, 1.0F}});
    m_hierarchy.update();

    const auto positionOf = [](const Math::Mat4& m)
    {
        return Math::Vec3{m.m[3][0], m.m[3][1], m.m[3][2]};
    };
    EXPECT_TRUE(Math::nearlyEqual(positionOf(m_hierarchy.interpolatedWorld(entity, 0.0F)), Math::Vec3{}, 1e-5F));
    EXPECT_TRUE(Math::nearlyEqual(positionOf(m_hierarchy.interpolatedWorld(entity, 1.0F)), Math::Vec3{4.0F, 0.0F, 0.0F},
                                  1e-5F));
    const Math::Mat4 half = m_hierarchy.interpolatedWorld(entity, 0.5F);
    EXPECT_TRUE(Math::nearlyEqual(positionOf(half), Math::Vec3{2.0F, 0.0F, 0.0F}, 1e-5F));
    const std::optional<Math::Transform> halfTransform = Math::fromMatrix(Math::load(half));
    ASSERT_TRUE(halfTransform.has_value());
    EXPECT_NEAR(Math::angleBetween(Math::Quat{}, halfTransform->rotation), 0.5F, 1e-4F) << "rotation slerped";
}

TEST_F(TransformHierarchyTest, Parallel100k)
{
    // A random forest: each new entity picks a random earlier one as parent (or none), so depths vary.
    constexpr int c_count = 100000;
    Math::Random rng{2026U};
    std::vector<entt::entity> entities;
    std::vector<int> parentIndex;
    entities.reserve(c_count);
    for (int i = 0; i < c_count; ++i)
    {
        const int parent = (i > 0 && rng.chance(0.8F)) ? rng.range(std::max(0, i - 50), i - 1) : -1;
        const entt::entity entity = m_scene.createTransient();
        m_hierarchy.add(entity,
                        Math::Transform{{rng.range(-1.0F, 1.0F), rng.range(-1.0F, 1.0F), rng.range(-1.0F, 1.0F)},
                                        Math::fromAxisAngle(rng.unitVector(), rng.range(-0.3F, 0.3F)),
                                        {1.0F, 1.0F, 1.0F}},
                        parent >= 0 ? entities[static_cast<std::size_t>(parent)] : entt::null);
        entities.push_back(entity);
        parentIndex.push_back(parent);
    }
    m_hierarchy.update();
    EXPECT_GT(m_hierarchy.levelCount(), 10U) << "a deep hierarchy";

    // Single-threaded reference: parents always come before their children in creation order.
    std::vector<Math::Mat4> reference(c_count);
    for (std::size_t i = 0; i < entities.size(); ++i)
    {
        DirectX::XMMATRIX world = Math::toMatrix(m_scene.registry().get<LocalTransform>(entities[i]).value);
        if (parentIndex[i] >= 0)
        {
            world = DirectX::XMMatrixMultiply(world, Math::load(reference[static_cast<std::size_t>(parentIndex[i])]));
        }
        reference[i] = Math::storeMat4(world);
    }
    for (std::size_t i = 0; i < entities.size(); ++i)
    {
        ASSERT_TRUE(Math::nearlyEqual(m_scene.registry().get<WorldTransform>(entities[i]).matrix, reference[i], 1e-5F))
            << "entity " << i;
    }

    // Move a few roots; only their subtrees change, and the result still matches.
    m_hierarchy.setLocal(entities[0], Math::Transform{{100.0F, 0.0F, 0.0F}, Math::Quat{}, {1.0F, 1.0F, 1.0F}});
    m_hierarchy.update();
    const Math::Mat4& moved = m_scene.registry().get<WorldTransform>(entities[0]).matrix;
    EXPECT_EQ(moved.m[3][0], 100.0F);
}
} // namespace Meow2::Scene
