/// @file
/// @brief Tests for PlayMode (play.start / play.stop) and the collider outlines (offscreen, no GPU).

#include "Editing/ColliderLines.h"
#include "Editing/PlayMode.h"
#include "EditorTestRig.h"

#include <Meow2/Physics/PhysicsLibrary.h>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace
{
using namespace Qt::StringLiterals;
using PlayModeTest = Meow2::Tests::EditorTest;

/// @brief Runs a command that must succeed and returns its result.
QJsonObject ok(Meow2::Editor::EditorCommandRegistry& commands, QStringView name, const QJsonObject& parameters = {})
{
    const Meow2::Result<QJsonValue> ran = commands.execute(name, parameters);
    EXPECT_TRUE(ran) << name.toString().toStdString() << ": " << (ran ? "" : ran.error().message);
    return ran ? ran->toObject() : QJsonObject{};
}

/// @brief The entity with an ID.
entt::entity entityOf(Meow2::Editor::EditorWorld& world, const QString& id)
{
    const Meow2::Result<Meow2::Uuid> uuid = Meow2::Uuid::parse(id.toStdString());
    if (!uuid)
    {
        return entt::null;
    }
    return world.scene().find(Meow2::EntityId{*uuid}).value_or(entt::entity{entt::null});
}

/// @brief Sets a field through component.setField.
void setField(Meow2::Editor::EditorCommandRegistry& commands, const QString& id, const QString& component,
              const QString& field, const QJsonValue& value)
{
    ok(commands, u"component.setField",
       {{u"id"_s, id}, {u"component"_s, component}, {u"field"_s, field}, {u"value"_s, value}});
}

/// @brief A floor (a static box collider) and a box with a RigidBody 3 m above it, made through the commands.
struct BoxOnFloor
{
    QString floor;
    QString box;
};

BoxOnFloor makeBoxOnFloor(Meow2::Editor::EditorCommandRegistry& commands)
{
    BoxOnFloor made;
    made.floor = ok(commands, u"entity.create", {{u"name"_s, u"Floor"_s}}).value(u"id").toString();
    ok(commands, u"component.add", {{u"id"_s, made.floor}, {u"component"_s, u"Collider"_s}});
    setField(commands, made.floor, u"Collider"_s, u"size"_s, QJsonArray{20.0, 1.0, 20.0});
    setField(commands, made.floor, u"LocalTransform"_s, u"position"_s, QJsonArray{0.0, -0.5, 0.0});

    made.box = ok(commands, u"entity.create", {{u"name"_s, u"Box"_s}}).value(u"id").toString();
    ok(commands, u"component.add", {{u"id"_s, made.box}, {u"component"_s, u"Collider"_s}});
    ok(commands, u"component.add", {{u"id"_s, made.box}, {u"component"_s, u"RigidBody"_s}});
    setField(commands, made.box, u"LocalTransform"_s, u"position"_s, QJsonArray{0.0, 3.0, 0.0});
    return made;
}
} // namespace

TEST_F(PlayModeTest, PlayStepsAndStopRestores)
{
    Meow2::Physics::PhysicsLibrary physics{m_log};
    Meow2::Editor::PlayMode play{{.commands = m_commands,
                                  .history = m_history,
                                  .world = m_world,
                                  .jobs = m_jobs,
                                  .physics = &physics,
                                  .log = m_log}};
    const BoxOnFloor scene = makeBoxOnFloor(m_commands);
    m_world.update(nullptr);
    const entt::entity box = entityOf(m_world, scene.box);
    ASSERT_TRUE(box != entt::null);

    EXPECT_FALSE(m_commands.isEnabled(u"play.stop"));
    const Meow2::Result<QJsonValue> started = m_commands.execute(u"play.start");
    if (!started && started.error().code == Meow2::ErrorCode::Unsupported)
    {
        GTEST_SKIP() << "built without physics (MEOW2_PHYSICS_BACKEND=None)";
    }
    ASSERT_TRUE(started) << started.error().message;
    EXPECT_EQ(started->toObject().value(u"bodies").toInt(), 2);
    EXPECT_TRUE(started->toObject().value(u"warnings").toArray().isEmpty());
    EXPECT_TRUE(play.playing());
    EXPECT_FALSE(m_commands.isEnabled(u"play.start")) << "already playing";
    EXPECT_FALSE(m_commands.isEnabled(u"scene.saveAs")) << "the played scene isn't saved";
    EXPECT_FALSE(m_commands.isEnabled(u"edit.undo")) << "undo is off while playing";

    // Two seconds: the box falls onto the floor and rests on it (its half-height, 0.5 m, above y = 0).
    for (int i = 0; i < 120; ++i)
    {
        play.fixedUpdate(1.0F / 60.0F);
    }
    const float fallen = m_world.scene().registry().get<Meow2::Scene::LocalTransform>(box).value.position.y;
    EXPECT_NEAR(fallen, 0.5F, 0.05F);
    const QJsonObject state = ok(m_commands, u"play.state");
    EXPECT_TRUE(state.value(u"playing").toBool());
    EXPECT_NEAR(state.value(u"seconds").toDouble(), 2.0, 1e-3);

    ok(m_commands, u"play.stop");
    EXPECT_FALSE(play.playing());
    const entt::entity restored = entityOf(m_world, scene.box);
    ASSERT_TRUE(restored != entt::null) << "the same entity IDs after Stop";
    EXPECT_FLOAT_EQ(m_world.scene().registry().get<Meow2::Scene::LocalTransform>(restored).value.position.y, 3.0F)
        << "Stop puts the scene back as it was at Play";
    // The last step is still the edit before Play (the box raised to 3 m): playing recorded none.
    ok(m_commands, u"edit.undo");
    EXPECT_FLOAT_EQ(m_world.scene().registry().get<Meow2::Scene::LocalTransform>(restored).value.position.y, 0.0F);

    EXPECT_TRUE(m_commands.isEnabled(u"scene.saveAs"));
    EXPECT_FALSE(m_commands.execute(u"play.stop")) << "not playing";
}

TEST_F(PlayModeTest, RefusedWithoutPhysics)
{
    Meow2::Editor::PlayMode play{{.commands = m_commands,
                                  .history = m_history,
                                  .world = m_world,
                                  .jobs = m_jobs,
                                  .physics = nullptr,
                                  .log = m_log}};
    EXPECT_FALSE(m_commands.execute(u"play.start"));
    EXPECT_FALSE(play.playing());
    EXPECT_FALSE(ok(m_commands, u"play.state").value(u"playing").toBool());
}

TEST_F(PlayModeTest, PhysicsComponentsThroughCommands)
{
    const QString id = ok(m_commands, u"entity.create", {{u"name"_s, u"Ball"_s}}).value(u"id").toString();
    ok(m_commands, u"component.add", {{u"id"_s, id}, {u"component"_s, u"Collider"_s}});
    setField(m_commands, id, u"Collider"_s, u"shape"_s, u"sphere"_s);
    setField(m_commands, id, u"Collider"_s, u"radius"_s, 0.25);
    ok(m_commands, u"component.add", {{u"id"_s, id}, {u"component"_s, u"RigidBody"_s}});
    setField(m_commands, id, u"RigidBody"_s, u"mass"_s, 2.0);
    const entt::entity entity = entityOf(m_world, id);
    const auto& collider = m_world.scene().registry().get<Meow2::Physics::Collider>(entity);
    EXPECT_EQ(collider.shape, Meow2::Physics::ColliderShape::Sphere);
    EXPECT_FLOAT_EQ(collider.radius, 0.25F);
    EXPECT_FLOAT_EQ(m_world.scene().registry().get<Meow2::Physics::RigidBody>(entity).mass, 2.0F);
    EXPECT_FALSE(m_commands.execute(
        u"component.setField",
        {{u"id"_s, id}, {u"component"_s, u"Collider"_s}, {u"field"_s, u"shape"_s}, {u"value"_s, u"cone"_s}}))
        << "not a shape";

    // component.list (what the Inspector and MCP offer) names both.
    const Meow2::Result<QJsonValue> listed = m_commands.execute(u"component.list");
    ASSERT_TRUE(listed);
    const QString text = QString::fromUtf8(QJsonDocument{listed->toObject()}.toJson());
    EXPECT_TRUE(text.contains(u"RigidBody"_s));
    EXPECT_TRUE(text.contains(u"Collider"_s));
}

TEST_F(PlayModeTest, ColliderLinesForTheSelection)
{
    const BoxOnFloor scene = makeBoxOnFloor(m_commands);
    const QString ball =
        ok(m_commands, u"entity.create", {{u"name"_s, u"Ball"_s}, {u"parent"_s, scene.box}}).value(u"id").toString();
    ok(m_commands, u"component.add", {{u"id"_s, ball}, {u"component"_s, u"Collider"_s}});
    setField(m_commands, ball, u"Collider"_s, u"shape"_s, u"sphere"_s);
    m_world.update(nullptr);
    const auto meshOf = [](entt::entity) -> std::optional<Meow2::AssetId>
    {
        return std::nullopt;
    };
    const Meow2::Physics::CollisionMeshes& meshes = m_world.collisionMeshes();

    EXPECT_TRUE(Meow2::Editor::colliderLines(m_world.scene(), {}, meshes, meshOf).empty()) << "nothing selected";

    const std::vector<entt::entity> floor{entityOf(m_world, scene.floor)};
    const std::vector<Meow2::Renderer::DebugLine> box =
        Meow2::Editor::colliderLines(m_world.scene(), floor, meshes, meshOf);
    ASSERT_EQ(box.size(), 12U) << "a box's twelve edges";
    float lowest = 1e9F;
    float widest = 0.0F;
    for (const Meow2::Renderer::DebugLine& line : box)
    {
        lowest = std::min({lowest, line.from.y, line.to.y});
        widest = std::max({widest, line.from.x, line.to.x});
    }
    EXPECT_NEAR(lowest, -1.0F, 1e-4F) << "the floor's bottom: at y = -0.5, 1 m high";
    EXPECT_NEAR(widest, 10.0F, 1e-4F);

    // Selecting the box shows its collider and its child's sphere (three circles).
    const std::vector<entt::entity> parent{entityOf(m_world, scene.box)};
    EXPECT_GT(Meow2::Editor::colliderLines(m_world.scene(), parent, meshes, meshOf).size(), 12U + 3U * 30U);
}
