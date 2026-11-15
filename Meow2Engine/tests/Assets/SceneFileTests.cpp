/// @file
/// @brief Tests for scene files (SceneFile.h): round trips, order and merging, unknown components, versions and
///        migrations, aliases, and bad input.

#include <Meow2/Assets/SceneFile.h>
#include <Meow2/Core/Id/UuidGenerator.h>
#include <Meow2/Core/Jobs/JobSystem.h>
#include <Meow2/Core/Log/LogSystem.h>
#include <Meow2/Scene/Camera.h>
#include <Meow2/Scene/ComponentRegistry.h>
#include <Meow2/Scene/Environment.h>

#include <gtest/gtest.h>

#include <format>
#include <memory>
#include <memory_resource>
#include <sstream>
#include <string>
#include <variant>
#include <vector>

namespace Meow2::Assets
{
namespace
{
/// @brief A test component at version 2; version 1 stored a whole number under `hp`.
struct Health
{
    float value = 100.0F; ///< Hit points.
};

/// @brief The engine's components plus Health (alias HitPoints, a migration from version 1).
Scene::ComponentRegistry makeRegistry()
{
    Scene::ComponentRegistry registry;
    Scene::registerSceneComponents(registry);
    Scene::ComponentDesc health = Scene::component<Health>(
        "Health", 2, {Scene::field<Health>("value", [](Health& c) -> auto& { return c.value; })});
    health.aliases = {"HitPoints"};
    health.migrations.push_back({.from = 1,
                                 .up =
                                     [](Scene::FieldTable& table)
                                 {
                                     const auto node = table.extract("hp");
                                     table.emplace("value", static_cast<double>(std::get<std::int64_t>(node.mapped())));
                                 },
                                 .down =
                                     [](Scene::FieldTable& table)
                                 {
                                     const auto node = table.extract("value");
                                     table.emplace("hp", static_cast<std::int64_t>(std::get<double>(node.mapped())));
                                 }});
    registry.add(std::move(health));
    return registry;
}

/// @brief A scene with its hierarchy, ready for the registry's components.
struct World
{
    World(UuidGenerator& ids, Jobs::JobSystem& jobs, const Scene::ComponentRegistry& registry)
        : scene(std::make_unique<Scene::Scene>(ids, *std::pmr::new_delete_resource(), jobs.threadCount()))
        , hierarchy(std::make_unique<Scene::TransformHierarchy>(*scene, jobs))
    {
        registry.registerAll(*scene);
    }

    std::unique_ptr<Scene::Scene> scene;                  ///< Behind a pointer: Scene can't move.
    std::unique_ptr<Scene::TransformHierarchy> hierarchy; ///< Likewise.
};

/// @brief The lines of a text.
std::vector<std::string> lines(const std::string& text)
{
    std::vector<std::string> out;
    std::istringstream stream{text};
    for (std::string line; std::getline(stream, line);)
    {
        out.push_back(line);
    }
    return out;
}

class SceneFileTest : public ::testing::Test
{
protected:
    /// @brief A camera at the root, a child with Health under it, an environment and a transient helper.
    void buildScene(World& world)
    {
        Scene::Scene& scene = *world.scene;
        m_root = scene.create("Camera rig");
        world.hierarchy->add(
            m_root, Math::Transform{.position = {0.0F, 2.0F, -8.0F}, .rotation = {}, .scale = {1.0F, 1.0F, 1.0F}});
        scene.registry().emplace<Scene::Camera>(m_root, Scene::Camera{.lens = {}, .main = true});
        m_child = scene.create("Target \"A\"");
        world.hierarchy->add(
            m_child, Math::Transform{.position = {1.5F, 0.0F, 0.25F}, .rotation = {}, .scale = {2.0F, 1.0F, 1.0F}},
            m_root);
        scene.registry().emplace<Health>(m_child, Health{.value = 42.5F});
        const entt::entity environment = scene.create("Environment");
        Scene::Environment sun;
        sun.sunElevation = 20.0F;
        sun.exposure = 1.5F;
        scene.registry().emplace<Scene::Environment>(environment, sun);
        const entt::entity helper = scene.createTransient("Helper");
        world.hierarchy->add(helper);
    }

    /// @brief Reads `text` into a new world, expecting success.
    std::unique_ptr<World> load(const std::string& text, SceneReadReport* report = nullptr)
    {
        auto world = std::make_unique<World>(m_ids, m_jobs, m_registry);
        Result<SceneReadReport> read = readScene(text, *world->scene, *world->hierarchy, m_registry, "test.m2scene");
        EXPECT_TRUE(read) << read.error().message;
        if (read && report != nullptr)
        {
            *report = *read;
        }
        return world;
    }

    /// @brief writeScene(), expecting success.
    std::string write(const World& world, const SceneWriteOptions& options = {})
    {
        Result<std::string> text = writeScene(*world.scene, m_registry, options);
        EXPECT_TRUE(text) << text.error().message;
        return text ? *text : std::string{};
    }

    /// @brief A file of one entity with one component table, the given versions and fields.
    std::string oneEntity(std::string_view component, std::string_view version, std::string_view fields)
    {
        return std::format("format = \"meow2.scene\"\nversion = 1\n\n[components]\n{} = {}\n\n[[entity]]\nid = \"{}\"\n"
                           "name = \"One\"\n\n[entity.{}]\n{}\n",
                           component, version, m_ids.next<IdKind::Entity>().uuid().toString(), component, fields);
    }

    Log::LogSystem m_logSystem{{.appName = "Test", .directory = {}, .console = false}};
    Log::Logger m_log{m_logSystem.createLogger("Engine")};
    Jobs::JobSystem m_jobs{{.threadCount = 2}, m_log};
    UuidGenerator m_ids;
    Scene::ComponentRegistry m_registry = makeRegistry();
    entt::entity m_root = entt::null;
    entt::entity m_child = entt::null;
};
} // namespace

TEST_F(SceneFileTest, RoundTripIsIdentical)
{
    World original{m_ids, m_jobs, m_registry};
    buildScene(original);
    const std::string text = write(original);

    SceneReadReport report;
    const std::unique_ptr<World> loaded = load(text, &report);
    EXPECT_EQ(report.entities, 3U) << "the transient helper isn't saved";
    EXPECT_TRUE(report.warnings.empty()) << report.warnings.front();
    EXPECT_EQ(write(*loaded), text) << "saving what was loaded gives the same file";

    // The entities keep their IDs, names, components and hierarchy.
    Scene::Scene& scene = *loaded->scene;
    const entt::entity root = *scene.find(original.scene->idOf(m_root));
    const entt::entity child = *scene.find(original.scene->idOf(m_child));
    EXPECT_EQ(scene.registry().get<Scene::NameComponent>(child).name, "Target \"A\"");
    EXPECT_EQ(scene.registry().get<Scene::Parent>(child).entity, root);
    EXPECT_FLOAT_EQ(scene.registry().get<Health>(child).value, 42.5F);
    EXPECT_TRUE(scene.registry().get<Scene::Camera>(root).main);
    loaded->hierarchy->update();
    const Math::Mat4& world = scene.registry().get<Scene::WorldTransform>(child).matrix;
    EXPECT_FLOAT_EQ(world._42, 2.0F) << "the child follows its parent once the hierarchy updates";
    const std::optional<entt::entity> environment = Scene::findEnvironment(scene);
    ASSERT_TRUE(environment);
    EXPECT_FLOAT_EQ(scene.registry().get<Scene::Environment>(*environment).exposure, 1.5F);
}

TEST_F(SceneFileTest, EntitiesSortedAndOneBlockEach)
{
    World world{m_ids, m_jobs, m_registry};
    buildScene(world);
    const std::string text = write(world);
    std::vector<std::string> ids;
    for (const std::string& line : lines(text))
    {
        if (line.starts_with("id = "))
        {
            ids.push_back(line);
        }
    }
    ASSERT_EQ(ids.size(), 3U);
    EXPECT_TRUE(std::ranges::is_sorted(ids)) << "IDs grow with creation time, so the text sorts the same way";
    EXPECT_NE(text.find("[components]\nCamera = 1\nEnvironment = 1\nHealth = 2\nLocalTransform = 1\nParent = 1\n"),
              std::string::npos)
        << text;
    EXPECT_NE(text.find("[entity.LocalTransform]\nposition = [1.5, 0.0, 0.25]\nrotation = [0.0, 0.0, 0.0, 1.0]\n"
                        "scale = [2.0, 1.0, 1.0]\nmobility = \"auto\"\n"),
              std::string::npos)
        << "fields in declared order, floats short\n"
        << text;
    EXPECT_EQ(text.find("Helper"), std::string::npos);
}

TEST_F(SceneFileTest, EditingOneEntityChangesOnlyItsLines)
{
    World world{m_ids, m_jobs, m_registry};
    buildScene(world);
    const std::vector<std::string> before = lines(write(world));
    world.scene->registry().get<Health>(m_child).value = 7.0F;
    const std::vector<std::string> after = lines(write(world));
    ASSERT_EQ(before.size(), after.size());

    // Where the child's block is: from its id line back to its [[entity]], on to the next [[entity]].
    const std::string childId = std::format("id = \"{}\"", world.scene->idOf(m_child).uuid().toString());
    const auto idLine = std::ranges::find(before, childId);
    ASSERT_NE(idLine, before.end());
    const std::size_t start = static_cast<std::size_t>(idLine - before.begin()) - 1;
    std::size_t end = start + 1;
    while (end < before.size() && before[end] != "[[entity]]")
    {
        ++end;
    }
    std::size_t changed = 0;
    for (std::size_t i = 0; i < before.size(); ++i)
    {
        if (before[i] != after[i])
        {
            ++changed;
            EXPECT_TRUE(i > start && i < end) << "line " << i << " changed outside the edited entity: " << after[i];
        }
    }
    EXPECT_EQ(changed, 1U);
}

TEST_F(SceneFileTest, UnknownComponentsSurvive)
{
    const std::string text =
        oneEntity("Script", "3", "path = \"scripts/door.lua\"\nspeed = 2.5\nwaypoints = [1.0, 2.0]\nenabled = true");
    SceneReadReport report;
    const std::unique_ptr<World> world = load(text, &report);
    EXPECT_EQ(report.unknown, 1U);
    const entt::entity entity = *world->scene->registry().view<Scene::IdComponent>().begin();
    const auto& kept = world->scene->registry().get<Scene::UnknownComponents>(entity).components;
    ASSERT_EQ(kept.size(), 1U);
    EXPECT_EQ(kept[0].name, "Script");
    EXPECT_EQ(kept[0].version, 3U);
    // Written back with its version and every field (sorted by name, the order they were kept in).
    const std::string saved = write(*world);
    EXPECT_NE(saved.find("[components]\nScript = 3\n"), std::string::npos) << saved;
    EXPECT_NE(
        saved.find(
            "[entity.Script]\nenabled = true\npath = \"scripts/door.lua\"\nspeed = 2.5\nwaypoints = [1.0, 2.0]\n"),
        std::string::npos)
        << saved;
    EXPECT_EQ(write(*load(saved)), saved) << "and again, unchanged";
}

TEST_F(SceneFileTest, OldVersionMigratesUp)
{
    SceneReadReport report;
    const std::unique_ptr<World> world = load(oneEntity("Health", "1", "hp = 50"), &report);
    EXPECT_EQ(report.migrated, 1U);
    const entt::entity entity = *world->scene->registry().view<Scene::IdComponent>().begin();
    EXPECT_FLOAT_EQ(world->scene->registry().get<Health>(entity).value, 50.0F);
    EXPECT_NE(write(*world).find("[entity.Health]\nvalue = 50.0\n"), std::string::npos) << "saved as version 2";
}

TEST_F(SceneFileTest, WritesOlderVersion)
{
    World world{m_ids, m_jobs, m_registry};
    buildScene(world);
    const std::string text = write(world, {.componentVersions = {{"Health", 1}}});
    EXPECT_NE(text.find("Health = 1\n"), std::string::npos);
    EXPECT_NE(text.find("[entity.Health]\nhp = 42\n"), std::string::npos) << text;
    // And it loads back through the up migration.
    const std::unique_ptr<World> loaded = load(text);
    EXPECT_FLOAT_EQ(loaded->scene->registry().get<Health>(*loaded->scene->find(world.scene->idOf(m_child))).value,
                    42.0F);
    EXPECT_FALSE(writeScene(*world.scene, m_registry, {.componentVersions = {{"Camera", 0}}}))
        << "no migration below version 1";
}

TEST_F(SceneFileTest, NewerComponentVersionIsKept)
{
    SceneReadReport report;
    const std::string text = oneEntity("Health", "3", "value = 80.0\narmour = 5");
    const std::unique_ptr<World> world = load(text, &report);
    EXPECT_EQ(report.unknown, 1U);
    ASSERT_EQ(report.warnings.size(), 1U);
    EXPECT_NE(report.warnings[0].find("newer"), std::string::npos) << report.warnings[0];
    const entt::entity entity = *world->scene->registry().view<Scene::IdComponent>().begin();
    EXPECT_FALSE(world->scene->registry().all_of<Health>(entity)) << "not read with the wrong layout";
    const std::string saved = write(*world);
    EXPECT_NE(saved.find("Health = 3\n"), std::string::npos);
    EXPECT_NE(saved.find("[entity.Health]\narmour = 5\nvalue = 80.0\n"), std::string::npos) << saved;
}

TEST_F(SceneFileTest, RenamedComponentReadByAlias)
{
    const std::unique_ptr<World> world = load(oneEntity("HitPoints", "2", "value = 12.0"));
    const entt::entity entity = *world->scene->registry().view<Scene::IdComponent>().begin();
    EXPECT_FLOAT_EQ(world->scene->registry().get<Health>(entity).value, 12.0F);
    const std::string saved = write(*world);
    EXPECT_NE(saved.find("[entity.Health]"), std::string::npos);
    EXPECT_EQ(saved.find("HitPoints"), std::string::npos);
}

TEST_F(SceneFileTest, BadInputIsAnError)
{
    World world{m_ids, m_jobs, m_registry};
    const auto read = [&](const std::string& text)
    {
        return readScene(text, *world.scene, *world.hierarchy, m_registry, "bad.m2scene");
    };

    EXPECT_EQ(read("format = \"meow2.scene\"\nversion = 2\n").error().code, ErrorCode::Unsupported);
    EXPECT_EQ(read("format = \"meow2.scene\"\nversion = [").error().code, ErrorCode::ParseError);
    EXPECT_EQ(read("version = 1\n").error().code, ErrorCode::ParseError) << "not a scene";
    EXPECT_EQ(read("format = \"meow2.scene\"\nversion = 1\n[[entity]]\nid = \"nope\"\n").error().code,
              ErrorCode::ParseError);
    const std::string id = m_ids.next<IdKind::Entity>().uuid().toString();
    const std::string twice = std::format(
        "format = \"meow2.scene\"\nversion = 1\n[[entity]]\nid = \"{0}\"\n[[entity]]\nid = \"{1}\"\n[[entity]]\nid = "
        "\"{0}\"\n",
        id, m_ids.next<IdKind::Entity>().uuid().toString());
    EXPECT_EQ(read(twice).error().code, ErrorCode::AlreadyExists);
    EXPECT_EQ(world.scene->entityCount(), 0U) << "a file that can't be read adds nothing";

    // Problems inside entities are skipped with a warning: an unknown parent, a cycle, a value of the wrong shape.
    const std::string a = m_ids.next<IdKind::Entity>().uuid().toString();
    const std::string b = m_ids.next<IdKind::Entity>().uuid().toString();
    const std::string c = m_ids.next<IdKind::Entity>().uuid().toString();
    const std::string transform = "[entity.LocalTransform]\nposition = [0.0, 0.0, 0.0]\n";
    const std::string text = std::format(
        "format = \"meow2.scene\"\nversion = 1\n"
        "[[entity]]\nid = \"{0}\"\n{3}[entity.Parent]\nentity = \"{1}\"\n"
        "[[entity]]\nid = \"{1}\"\n{3}[entity.Parent]\nentity = \"{0}\"\n"
        "[[entity]]\nid = \"{2}\"\n{3}[entity.Parent]\nentity = \"{4}\"\n[entity.Camera]\nnearZ = \"close\"\n",
        a, b, c, transform, m_ids.next<IdKind::Entity>().uuid().toString());
    Result<SceneReadReport> report = read(text);
    ASSERT_TRUE(report) << report.error().message;
    EXPECT_EQ(report->entities, 3U);
    EXPECT_GE(report->warnings.size(), 3U);
    std::size_t parents = 0;
    for (const entt::entity entity : world.scene->registry().view<Scene::Parent>())
    {
        static_cast<void>(entity);
        ++parents;
    }
    EXPECT_LE(parents, 1U) << "the cycle is broken; the unknown parent is dropped";
    world.hierarchy->update(); // and the hierarchy copes with what's left
}
} // namespace Meow2::Assets
