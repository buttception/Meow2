/// @file
/// @brief Tests for ComponentRegistry: fields read and written through descriptors, enums, references, names and
///        aliases, migrations.

#include <Meow2/Core/Id/UuidGenerator.h>
#include <Meow2/Core/Jobs/JobSystem.h>
#include <Meow2/Core/Log/LogSystem.h>
#include <Meow2/Scene/Camera.h>
#include <Meow2/Scene/ComponentRegistry.h>
#include <Meow2/Scene/Environment.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <memory_resource>
#include <variant>

namespace Meow2::Scene
{
namespace
{
/// @brief A scene with the engine's components registered.
class ComponentRegistryTest : public ::testing::Test
{
protected:
    ComponentRegistryTest()
    {
        registerSceneComponents(m_registry);
        m_registry.registerAll(m_scene);
    }

    /// @brief A field of a component by name.
    const FieldDesc& fieldOf(std::string_view component, std::string_view name) const
    {
        const ComponentDesc* desc = m_registry.find(component);
        EXPECT_NE(desc, nullptr);
        const auto found =
            std::ranges::find_if(desc->fields, [name](const FieldDesc& field) { return field.name == name; });
        EXPECT_NE(found, desc->fields.end());
        return *found;
    }

    Log::LogSystem m_logSystem{{.appName = "Test", .directory = {}, .console = false}};
    Log::Logger m_log{m_logSystem.createLogger("Engine")};
    Jobs::JobSystem m_jobs{{.threadCount = 2}, m_log};
    UuidGenerator m_ids;
    Scene m_scene{m_ids, *std::pmr::new_delete_resource(), m_jobs.threadCount()};
    TransformHierarchy m_hierarchy{m_scene, m_jobs};
    ComponentRegistry m_registry;
};
} // namespace

TEST_F(ComponentRegistryTest, FieldsReadAndWriteMembers)
{
    LocalTransform transform{.value = {.position = {1.0F, 2.0F, 3.0F}, .rotation = {}, .scale = {1.0F, 1.0F, 1.0F}},
                             .mobility = Mobility::Auto};
    const FieldDesc& position = fieldOf("LocalTransform", "position");
    EXPECT_EQ(position.type, FieldType::Vec3);
    EXPECT_EQ(position.get(&transform, m_scene), (FieldValue{std::vector<double>{1.0, 2.0, 3.0}}));
    ASSERT_TRUE(position.set(&transform, std::vector<double>{4.0, 5.0, 6.0}, m_scene));
    EXPECT_FLOAT_EQ(transform.value.position.z, 6.0F);
    EXPECT_FALSE(position.set(&transform, std::vector<double>{1.0}, m_scene)) << "three numbers, not one";
    EXPECT_FALSE(position.set(&transform, true, m_scene));

    // Floats are kept in their shortest form, so files say 0.1 rather than 0.10000000149011612.
    Environment environment;
    environment.exposure = 0.1F;
    EXPECT_EQ(fieldOf("Environment", "exposure").get(&environment, m_scene), FieldValue{0.1});
    ASSERT_TRUE(fieldOf("Environment", "exposure").set(&environment, std::int64_t{2}, m_scene)) << "1 for 1.0 is fine";
    EXPECT_FLOAT_EQ(environment.exposure, 2.0F);

    // Enums by name.
    const FieldDesc& mobility = fieldOf("LocalTransform", "mobility");
    EXPECT_EQ(mobility.type, FieldType::Enum);
    EXPECT_EQ(mobility.enumNames, (std::vector<std::string>{"auto", "static", "movable"}));
    ASSERT_TRUE(mobility.set(&transform, std::string{"movable"}, m_scene));
    EXPECT_EQ(transform.mobility, Mobility::Movable);
    EXPECT_EQ(mobility.get(&transform, m_scene), FieldValue{std::string{"movable"}});
    EXPECT_FALSE(mobility.set(&transform, std::string{"flying"}, m_scene));
}

TEST_F(ComponentRegistryTest, EntityReferencesUseIds)
{
    const entt::entity saved = m_scene.create("Saved");
    const entt::entity transient = m_scene.createTransient();
    Parent parent{.entity = saved};
    const FieldDesc& entity = fieldOf("Parent", "entity");
    EXPECT_EQ(entity.get(&parent, m_scene), FieldValue{m_scene.idOf(saved).uuid().toString()});
    parent.entity = transient;
    EXPECT_EQ(entity.get(&parent, m_scene), FieldValue{std::string{}}) << "a transient entity can't be named";

    ASSERT_TRUE(entity.set(&parent, m_scene.idOf(saved).uuid().toString(), m_scene));
    EXPECT_EQ(parent.entity, saved);
    ASSERT_TRUE(entity.set(&parent, std::string{}, m_scene));
    EXPECT_EQ(parent.entity, entt::entity{entt::null});
    const std::string stranger = m_ids.next<IdKind::Entity>().uuid().toString();
    EXPECT_EQ(entity.set(&parent, stranger, m_scene).error().code, ErrorCode::NotFound);
}

TEST_F(ComponentRegistryTest, FindsByNameAndAlias)
{
    struct Health
    {
        float value = 100.0F;
    };
    ComponentDesc health =
        component<Health>("Health", 1, {field<Health>("value", [](Health& c) -> auto& { return c.value; })});
    health.aliases = {"HitPoints"};
    m_registry.add(std::move(health));
    ASSERT_NE(m_registry.find("Health"), nullptr);
    EXPECT_EQ(m_registry.find("HitPoints"), m_registry.find("Health"));
    EXPECT_EQ(m_registry.find("Mana"), nullptr);
    EXPECT_NE(m_registry.find("Camera"), nullptr);
    EXPECT_EQ(m_registry.components().size(), 5U);
}

TEST_F(ComponentRegistryTest, MigratesUpAndDown)
{
    struct Speed
    {
        float metresPerSecond = 0.0F;
    };
    ComponentDesc speed = component<Speed>(
        "Speed", 2, {field<Speed>("metresPerSecond", [](Speed& c) -> auto& { return c.metresPerSecond; })});
    // Version 1 stored kilometres per hour under another name.
    speed.migrations.push_back({.from = 1,
                                .up =
                                    [](FieldTable& table)
                                {
                                    const double kmh = std::get<double>(table.at("kmh"));
                                    table.erase("kmh");
                                    table.emplace("metresPerSecond", kmh / 3.6);
                                },
                                .down =
                                    [](FieldTable& table)
                                {
                                    const double ms = std::get<double>(table.at("metresPerSecond"));
                                    table.erase("metresPerSecond");
                                    table.emplace("kmh", ms * 3.6);
                                }});
    FieldTable table{{"kmh", 36.0}};
    ASSERT_TRUE(migrate(speed, table, 1, 2));
    EXPECT_DOUBLE_EQ(std::get<double>(table.at("metresPerSecond")), 10.0);
    ASSERT_TRUE(migrate(speed, table, 2, 1));
    EXPECT_DOUBLE_EQ(std::get<double>(table.at("kmh")), 36.0);
    EXPECT_EQ(migrate(speed, table, 1, 3).error().code, ErrorCode::Unsupported) << "no step from 2 to 3";
}

TEST_F(ComponentRegistryTest, SunDirectionFollowsTheAngles)
{
    Environment environment;
    environment.sunElevation = 90.0F;
    EXPECT_NEAR(sunDirection(environment).y, 1.0F, 1e-5F);
    environment.sunElevation = 0.0F;
    environment.sunAzimuth = 90.0F;
    EXPECT_NEAR(sunDirection(environment).x, 1.0F, 1e-5F);
    EXPECT_FALSE(findEnvironment(m_scene));
    const entt::entity entity = m_scene.create("Environment");
    m_scene.registry().emplace<Environment>(entity);
    EXPECT_EQ(findEnvironment(m_scene), entity);
}
} // namespace Meow2::Scene
