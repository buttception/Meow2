/// @file
/// @brief Tests for Scene: entity IDs, lookup, destruction, loading by ID, views, up-front pools, memory tag.

#include <Meow2/Core/Id/UuidGenerator.h>
#include <Meow2/Core/Memory/MallocResource.h>
#include <Meow2/Core/Memory/TrackingAllocator.h>
#include <Meow2/Scene/Scene.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <format>
#include <memory_resource>
#include <optional>
#include <vector>

namespace
{
using Meow2::Scene::IdComponent;
using Meow2::Scene::NameComponent;
using Meow2::Scene::Scene;

/// @brief A component for the tests. Plain data.
struct Health
{
    int value = 0; ///< Hit points.
};

/// @brief A generator and a scene on the default heap.
class SceneTest : public ::testing::Test
{
protected:
    Meow2::UuidGenerator m_ids;
    Scene m_scene{m_ids, *std::pmr::new_delete_resource()};
};
} // namespace

TEST_F(SceneTest, CreateHasIdAndName)
{
    const entt::entity player = m_scene.create("Player");
    ASSERT_TRUE(m_scene.contains(player));
    const Meow2::EntityId id = m_scene.registry().get<IdComponent>(player).id;
    EXPECT_TRUE(id.isValid());
    EXPECT_EQ(m_scene.idOf(player), id);
    EXPECT_EQ(m_scene.registry().get<NameComponent>(player).name, "Player");

    const entt::entity unnamed = m_scene.create();
    EXPECT_FALSE(m_scene.registry().all_of<NameComponent>(unnamed)) << "no name, no NameComponent";
    EXPECT_NE(m_scene.idOf(unnamed), id);
    EXPECT_EQ(m_scene.entityCount(), 2U);
}

TEST_F(SceneTest, FindById)
{
    const entt::entity a = m_scene.create("A");
    const entt::entity b = m_scene.create("B");
    EXPECT_EQ(m_scene.find(m_scene.idOf(a)), a);
    EXPECT_EQ(m_scene.find(m_scene.idOf(b)), b);
    EXPECT_EQ(m_scene.find(m_ids.next<Meow2::IdKind::Entity>()), std::nullopt) << "an ID no entity has";
}

TEST_F(SceneTest, DestroyRemovesFromLookup)
{
    const entt::entity entity = m_scene.create("Doomed");
    const Meow2::EntityId id = m_scene.idOf(entity);
    m_scene.destroy(entity);
    EXPECT_FALSE(m_scene.contains(entity));
    EXPECT_EQ(m_scene.find(id), std::nullopt);
    EXPECT_EQ(m_scene.entityCount(), 0U);

    const entt::entity next = m_scene.create();
    EXPECT_NE(m_scene.idOf(next), id) << "the handle may be reused, the ID never is";
}

TEST_F(SceneTest, CreateWithIdForLoading)
{
    const Meow2::EntityId saved = m_ids.next<Meow2::IdKind::Entity>();
    const Meow2::Result<entt::entity> loaded = m_scene.createWithId(saved, "Loaded");
    ASSERT_TRUE(loaded.has_value()) << std::format("{}", loaded.error());
    EXPECT_EQ(m_scene.find(saved), *loaded);
    EXPECT_EQ(m_scene.idOf(*loaded), saved);

    const Meow2::Result<entt::entity> again = m_scene.createWithId(saved);
    ASSERT_FALSE(again.has_value());
    EXPECT_EQ(again.error().code, Meow2::ErrorCode::AlreadyExists);

    const Meow2::Result<entt::entity> invalid = m_scene.createWithId(Meow2::EntityId{});
    ASSERT_FALSE(invalid.has_value());
    EXPECT_EQ(invalid.error().code, Meow2::ErrorCode::InvalidArgument);
    EXPECT_EQ(m_scene.entityCount(), 1U);
}

TEST_F(SceneTest, ViewsOverComponents)
{
    m_scene.registerComponent<Health>();
    std::vector<entt::entity> withHealth;
    for (int i = 0; i < 10; ++i)
    {
        const entt::entity entity = m_scene.create();
        if (i % 3 == 0)
        {
            m_scene.registry().emplace<Health>(entity, i);
            withHealth.push_back(entity);
        }
    }
    std::vector<entt::entity> visited;
    int total = 0;
    for (auto [entity, health] : m_scene.registry().view<Health>().each())
    {
        visited.push_back(entity);
        total += health.value;
    }
    EXPECT_EQ(visited.size(), withHealth.size());
    EXPECT_TRUE(std::ranges::is_permutation(visited, withHealth));
    EXPECT_EQ(total, 0 + 3 + 6 + 9);
}

TEST_F(SceneTest, RegisteredPoolsExistUpFront)
{
    const Scene& readOnly = m_scene; // the const overload looks a pool up; the non-const one would create it
    EXPECT_EQ(readOnly.registry().storage<Health>(), nullptr) << "not registered yet";
    m_scene.registerComponent<Health>();
    const auto* storage = readOnly.registry().storage<Health>();
    ASSERT_NE(storage, nullptr) << "the pool exists before any entity has a Health";
    EXPECT_EQ(storage->size(), 0U);
}

TEST(SceneMemory, CountedUnderSceneTag)
{
    using Meow2::Memory::MemTag;
    Meow2::Memory::MallocResource malloc;
    Meow2::Memory::TrackingAllocator tracker{malloc};
    Meow2::UuidGenerator ids;
    {
        Scene scene{ids, tracker};
        scene.registerComponent<Health>();
        for (int i = 0; i < 1000; ++i)
        {
            scene.registry().emplace<Health>(scene.create(), i);
        }
        EXPECT_GT(tracker.stats(MemTag::Scene).currentBytes, 1000 * sizeof(Health)) << "registry, pools and ID map";
        EXPECT_EQ(tracker.stats(MemTag::General).liveAllocations, 0U) << "nothing leaks into another tag";
    }
    EXPECT_EQ(tracker.stats(MemTag::Scene).liveAllocations, 0U) << "all returned when the scene goes away";
}
