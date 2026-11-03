/// @file
/// @brief Tests for Scene: entity IDs, lookup, destruction, loading by ID, views, up-front pools, memory tag.

#include <Meow2/Core/Id/UuidGenerator.h>
#include <Meow2/Core/Jobs/JobSystem.h>
#include <Meow2/Core/Log/LogSystem.h>
#include <Meow2/Core/Memory/MallocResource.h>
#include <Meow2/Core/Memory/TrackingAllocator.h>
#include <Meow2/Core/Platform.h>
#include <Meow2/Scene/Scene.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <format>
#include <memory_resource>
#include <optional>
#include <vector>

namespace fs = std::filesystem;

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

TEST_F(SceneTest, TransientHasNoId)
{
    const entt::entity saved = m_scene.create("Saved");
    const entt::entity bullet = m_scene.createTransient();
    EXPECT_TRUE(m_scene.contains(bullet));
    EXPECT_FALSE(m_scene.registry().all_of<IdComponent>(bullet));
    EXPECT_FALSE(m_scene.idOf(bullet).isValid()) << "transient entities have no saved ID";
    EXPECT_TRUE(m_scene.idOf(saved).isValid());
    EXPECT_EQ(m_scene.entityCount(), 2U);
    EXPECT_EQ(m_scene.persistentCount(), 1U) << "only the saved one is in the ID map";
}

TEST_F(SceneTest, RegistryDestroyKeepsMapInSync)
{
    const entt::entity entity = m_scene.create();
    const Meow2::EntityId id = m_scene.idOf(entity);
    m_scene.registry().destroy(entity); // bypassing Scene::destroy
    EXPECT_EQ(m_scene.find(id), std::nullopt) << "the IdComponent signal updated the map";
    EXPECT_EQ(m_scene.persistentCount(), 0U);
}

TEST_F(SceneTest, RemovingIdMakesEntityTransient)
{
    const entt::entity entity = m_scene.create();
    const Meow2::EntityId id = m_scene.idOf(entity);
    m_scene.registry().remove<IdComponent>(entity);
    EXPECT_TRUE(m_scene.contains(entity));
    EXPECT_EQ(m_scene.find(id), std::nullopt);
    EXPECT_FALSE(m_scene.idOf(entity).isValid());
}

TEST_F(SceneTest, IsRegistered)
{
    EXPECT_FALSE(m_scene.isRegistered<Health>());
    m_scene.registerComponent<Health>();
    EXPECT_TRUE(m_scene.isRegistered<Health>());
    EXPECT_TRUE(m_scene.isRegistered<IdComponent>()) << "the Scene registers its own components";
}

namespace
{
/// @brief A tag component for the command tests. Plain data.
struct Marker
{
    int frame = 0; ///< Which frame marked it.
};

/// @brief A scene with Health and Marker registered.
class SceneCommandsTest : public ::testing::Test
{
protected:
    SceneCommandsTest()
    {
        m_scene.registerComponent<Health>();
        m_scene.registerComponent<Marker>();
    }

    Meow2::UuidGenerator m_ids;
    Scene m_scene{m_ids, *std::pmr::new_delete_resource()};
};
} // namespace

TEST_F(SceneCommandsTest, SpawnAppearsOnApply)
{
    Meow2::Scene::SceneCommands& commands = m_scene.commands(0);
    const Meow2::EntityId id = commands.spawn("Spawned", Health{5});
    EXPECT_TRUE(id.isValid()) << "the ID exists right away";
    EXPECT_EQ(m_scene.find(id), std::nullopt) << "the entity doesn't, until apply";
    EXPECT_EQ(commands.size(), 1U);

    m_scene.applyCommands();
    const std::optional<entt::entity> entity = m_scene.find(id);
    ASSERT_TRUE(entity.has_value());
    EXPECT_EQ(m_scene.registry().get<Health>(*entity).value, 5);
    EXPECT_EQ(m_scene.registry().get<NameComponent>(*entity).name, "Spawned");
    EXPECT_EQ(commands.size(), 0U) << "applied commands are gone";

    commands.spawnTransient(Health{1}, Marker{2});
    m_scene.applyCommands();
    EXPECT_EQ(m_scene.entityCount(), 2U);
    EXPECT_EQ(m_scene.persistentCount(), 1U);
}

TEST_F(SceneCommandsTest, DestroyEmplaceRemove)
{
    const entt::entity a = m_scene.create();
    const entt::entity b = m_scene.create();
    m_scene.registry().emplace<Health>(b, 10);
    Meow2::Scene::SceneCommands& commands = m_scene.commands(0);
    commands.emplace(a, Marker{7});
    commands.remove<Health>(b);
    commands.destroy(b);
    EXPECT_TRUE(m_scene.contains(b)) << "nothing happens before apply";
    EXPECT_FALSE(m_scene.registry().all_of<Marker>(a));

    m_scene.applyCommands();
    EXPECT_EQ(m_scene.registry().get<Marker>(a).frame, 7);
    EXPECT_FALSE(m_scene.contains(b));

    commands.emplace(a, Marker{8});
    m_scene.applyCommands();
    EXPECT_EQ(m_scene.registry().get<Marker>(a).frame, 8) << "emplace replaces an existing component";
}

TEST_F(SceneCommandsTest, SkipsEntitiesDestroyedEarlier)
{
    const entt::entity entity = m_scene.create();
    Meow2::Scene::SceneCommands& commands = m_scene.commands(0);
    commands.destroy(entity);
    commands.emplace(entity, Marker{1}); // recorded by a job that didn't know it'd be destroyed first
    commands.remove<Health>(entity);
    commands.destroy(entity);
    m_scene.applyCommands();
    EXPECT_FALSE(m_scene.contains(entity));
    EXPECT_EQ(m_scene.entityCount(), 0U);
}

TEST(SceneCommandsJobs, JobsRecordInParallel)
{
    const fs::path directory = fs::temp_directory_path() / std::format("meow2-scene-jobs-{}", Meow2::c_configName);
    Meow2::Log::LogSystem logSystem{{.appName = "Test", .directory = directory, .console = false}};
    Meow2::Log::Logger log = logSystem.createLogger("Engine");
    Meow2::Jobs::JobSystem jobs{{.threadCount = 4}, log};
    Meow2::UuidGenerator ids;
    Scene scene{ids, *std::pmr::new_delete_resource(), jobs.threadCount()};
    scene.registerComponent<Health>();
    scene.registerComponent<Marker>();

    constexpr std::uint32_t c_count = 10000;
    std::vector<entt::entity> entities;
    for (std::uint32_t i = 0; i < c_count; ++i)
    {
        entities.push_back(scene.create());
    }
    // Every job thread records into its own buffer: even entities are destroyed, odd ones marked, and every 100th
    // spawns a transient bullet. Nothing changes structure while the jobs run.
    jobs.wait(jobs.parallelFor(c_count, 64,
                               [&scene, &entities](std::uint32_t begin, std::uint32_t end, std::uint32_t threadIndex)
                               {
                                   Meow2::Scene::SceneCommands& commands = scene.commands(threadIndex);
                                   for (std::uint32_t i = begin; i < end; ++i)
                                   {
                                       if (i % 2 == 0)
                                       {
                                           commands.destroy(entities[i]);
                                       }
                                       else
                                       {
                                           commands.emplace(entities[i], Marker{static_cast<int>(i)});
                                       }
                                       if (i % 100 == 0)
                                       {
                                           commands.spawnTransient(Health{1});
                                       }
                                   }
                               }));
    scene.applyCommands();

    EXPECT_EQ(scene.persistentCount(), c_count / 2);
    EXPECT_EQ(scene.entityCount(), c_count / 2 + c_count / 100);
    EXPECT_EQ(scene.registry().view<Marker>().size(), c_count / 2);
    EXPECT_EQ(scene.registry().view<Health>().size(), c_count / 100);
    for (std::uint32_t i = 0; i < c_count; ++i)
    {
        ASSERT_EQ(scene.contains(entities[i]), i % 2 == 1) << "entity " << i;
    }
}
