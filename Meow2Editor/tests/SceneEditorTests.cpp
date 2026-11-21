/// @file
/// @brief Tests for SceneEditor: entity and component commands with undo, and the queries (offscreen, no GPU).

#include "EditorTestRig.h"

#include <Meow2/Scene/Camera.h>
#include <Meow2/Scene/Environment.h>

#include <QJsonArray>
#include <QJsonObject>

namespace
{
using namespace Qt::StringLiterals;
using SceneEditorTest = Meow2::Tests::EditorTest;

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
} // namespace

TEST_F(SceneEditorTest, CreateDeleteUndoKeepsIds)
{
    const QString parent = ok(m_commands, u"entity.create", {{u"name"_s, u"Car"_s}}).value(u"id").toString();
    const QString child =
        ok(m_commands, u"entity.create", {{u"name"_s, u"Wheel"_s}, {u"parent"_s, parent}}).value(u"id").toString();
    ok(m_commands, u"component.add", {{u"id"_s, child}, {u"component"_s, u"Camera"_s}});
    ASSERT_TRUE(entityOf(m_world, child) != entt::null);
    const std::size_t before = m_world.scene().entityCount();

    ok(m_commands, u"entity.delete", {{u"id"_s, parent}});
    EXPECT_EQ(m_world.scene().entityCount(), before - 2) << "the child goes with its parent";
    EXPECT_TRUE(entityOf(m_world, child) == entt::null);

    ASSERT_TRUE(m_commands.execute(u"edit.undo"));
    const entt::entity wheel = entityOf(m_world, child);
    ASSERT_TRUE(wheel != entt::null) << "the same ID";
    EXPECT_EQ(m_world.scene().registry().get<Meow2::Scene::NameComponent>(wheel).name, "Wheel");
    EXPECT_TRUE(m_world.scene().registry().all_of<Meow2::Scene::Camera>(wheel)) << "its components";
    EXPECT_EQ(m_world.scene().registry().get<Meow2::Scene::Parent>(wheel).entity, entityOf(m_world, parent))
        << "and its parent";
    m_world.update(nullptr); // the hierarchy copes with what came back

    ASSERT_TRUE(m_commands.execute(u"edit.redo"));
    EXPECT_TRUE(entityOf(m_world, parent) == entt::null);
    ASSERT_TRUE(m_commands.execute(u"edit.undo")); // the delete
    ASSERT_TRUE(m_commands.execute(u"edit.undo")); // the camera
    ASSERT_TRUE(m_commands.execute(u"edit.undo")); // the child
    EXPECT_TRUE(entityOf(m_world, child) == entt::null);
    ASSERT_TRUE(m_commands.execute(u"edit.redo"));
    EXPECT_TRUE(entityOf(m_world, child) != entt::null) << "recreated with the ID it had";
}

TEST_F(SceneEditorTest, DuplicateCopiesTheSubtree)
{
    const QString parent = ok(m_commands, u"entity.create", {{u"name"_s, u"Tree"_s}}).value(u"id").toString();
    ok(m_commands, u"entity.create", {{u"name"_s, u"Branch"_s}, {u"parent"_s, parent}});
    ok(m_commands, u"component.setField",
       {{u"id"_s, parent},
        {u"component"_s, u"LocalTransform"_s},
        {u"field"_s, u"position"_s},
        {u"value"_s, QJsonArray{1.0, 2.0, 3.0}}});
    const std::size_t before = m_world.scene().entityCount();

    const QString copy = ok(m_commands, u"entity.duplicate", {{u"id"_s, parent}}).value(u"id").toString();
    EXPECT_NE(copy, parent);
    EXPECT_EQ(m_world.scene().entityCount(), before + 2);
    const QJsonObject copied = ok(m_commands, u"entity.get", {{u"id"_s, copy}});
    EXPECT_EQ(copied.value(u"name").toString(), u"Tree"_s);
    const QJsonObject transform = copied.value(u"components").toArray().at(0).toObject();
    EXPECT_EQ(transform.value(u"fields").toObject().value(u"position").toArray(), (QJsonArray{1.0, 2.0, 3.0}));

    // The copy's child hangs under the copy, not under the original.
    const QJsonArray roots = ok(m_commands, u"scene.hierarchy").value(u"entities").toArray();
    bool found = false;
    for (const QJsonValue& root : roots)
    {
        if (root.toObject().value(u"id").toString() == copy)
        {
            found = true;
            const QJsonArray children = root.toObject().value(u"children").toArray();
            ASSERT_EQ(children.size(), 1);
            EXPECT_EQ(children.at(0).toObject().value(u"name").toString(), u"Branch"_s);
        }
    }
    EXPECT_TRUE(found);
    ASSERT_TRUE(m_commands.execute(u"edit.undo"));
    EXPECT_EQ(m_world.scene().entityCount(), before);
}

TEST_F(SceneEditorTest, RenameAndReparent)
{
    const QString a = ok(m_commands, u"entity.create", {{u"name"_s, u"A"_s}}).value(u"id").toString();
    const QString b = ok(m_commands, u"entity.create", {{u"name"_s, u"B"_s}}).value(u"id").toString();
    ok(m_commands, u"entity.rename", {{u"id"_s, a}, {u"name"_s, u"Renamed"_s}});
    EXPECT_EQ(ok(m_commands, u"entity.get", {{u"id"_s, a}}).value(u"name").toString(), u"Renamed"_s);
    ASSERT_TRUE(m_commands.execute(u"edit.undo"));
    EXPECT_EQ(ok(m_commands, u"entity.get", {{u"id"_s, a}}).value(u"name").toString(), u"A"_s);

    ok(m_commands, u"entity.setParent", {{u"id"_s, b}, {u"parent"_s, a}});
    EXPECT_EQ(ok(m_commands, u"entity.get", {{u"id"_s, b}}).value(u"parent").toString(), a);
    EXPECT_FALSE(m_commands.execute(u"entity.setParent", {{u"id"_s, a}, {u"parent"_s, b}}))
        << "A can't hang under its own child";
    EXPECT_FALSE(m_commands.execute(u"entity.setParent", {{u"id"_s, a}, {u"parent"_s, a}}));
    ASSERT_TRUE(m_commands.execute(u"edit.undo"));
    EXPECT_TRUE(ok(m_commands, u"entity.get", {{u"id"_s, b}}).value(u"parent").toString().isEmpty())
        << "back at the root";
    EXPECT_FALSE(m_commands.execute(u"entity.rename", {{u"id"_s, u"not an id"_s}, {u"name"_s, u"X"_s}}));
}

TEST_F(SceneEditorTest, ComponentsAddRemoveSetField)
{
    const QString id = ok(m_commands, u"entity.create", {{u"name"_s, u"Thing"_s}}).value(u"id").toString();
    const entt::entity entity = entityOf(m_world, id);
    ok(m_commands, u"component.add", {{u"id"_s, id}, {u"component"_s, u"Camera"_s}});
    EXPECT_FALSE(m_commands.execute(u"component.add", {{u"id"_s, id}, {u"component"_s, u"Camera"_s}})) << "twice";
    EXPECT_FALSE(m_commands.execute(u"component.add", {{u"id"_s, id}, {u"component"_s, u"Parent"_s}}))
        << "parents through entity.setParent";
    EXPECT_FALSE(m_commands.execute(u"component.add", {{u"id"_s, id}, {u"component"_s, u"Nonsense"_s}}));

    ok(m_commands, u"component.setField",
       {{u"id"_s, id}, {u"component"_s, u"Camera"_s}, {u"field"_s, u"nearZ"_s}, {u"value"_s, 0.5}});
    EXPECT_FLOAT_EQ(m_world.scene().registry().get<Meow2::Scene::Camera>(entity).lens.nearZ, 0.5F);
    EXPECT_FALSE(m_commands.execute(
        u"component.setField",
        {{u"id"_s, id}, {u"component"_s, u"Camera"_s}, {u"field"_s, u"nearZ"_s}, {u"value"_s, u"far"_s}}))
        << "wrong shape";
    EXPECT_FALSE(m_commands.execute(
        u"component.setField",
        {{u"id"_s, id}, {u"component"_s, u"Camera"_s}, {u"field"_s, u"projection"_s}, {u"value"_s, u"fisheye"_s}}))
        << "not one of the enum's values";

    ok(m_commands, u"component.remove", {{u"id"_s, id}, {u"component"_s, u"Camera"_s}});
    EXPECT_FALSE(m_world.scene().registry().all_of<Meow2::Scene::Camera>(entity));
    ASSERT_TRUE(m_commands.execute(u"edit.undo"));
    ASSERT_TRUE(m_world.scene().registry().all_of<Meow2::Scene::Camera>(entity));
    EXPECT_FLOAT_EQ(m_world.scene().registry().get<Meow2::Scene::Camera>(entity).lens.nearZ, 0.5F)
        << "removed and put back with its values";
    ASSERT_TRUE(m_commands.execute(u"edit.undo")); // the near plane
    EXPECT_FLOAT_EQ(m_world.scene().registry().get<Meow2::Scene::Camera>(entity).lens.nearZ, 0.1F);

    // A transform edit moves the world transform at the next update.
    ok(m_commands, u"component.setField",
       {{u"id"_s, id},
        {u"component"_s, u"LocalTransform"_s},
        {u"field"_s, u"position"_s},
        {u"value"_s, QJsonArray{0.0, 7.0, 0.0}}});
    m_world.update(nullptr);
    EXPECT_FLOAT_EQ(m_world.scene().registry().get<Meow2::Scene::WorldTransform>(entity).matrix._42, 7.0F);

    // A transform with children stays.
    ok(m_commands, u"entity.create", {{u"name"_s, u"Child"_s}, {u"parent"_s, id}});
    EXPECT_FALSE(m_commands.execute(u"component.remove", {{u"id"_s, id}, {u"component"_s, u"LocalTransform"_s}}));
}

TEST_F(SceneEditorTest, QueriesDescribeTheScene)
{
    const QJsonArray components = ok(m_commands, u"component.list").value(u"components").toArray();
    bool sawTransform = false;
    for (const QJsonValue& component : components)
    {
        if (component.toObject().value(u"name").toString() != u"LocalTransform"_s)
        {
            continue;
        }
        sawTransform = true;
        const QJsonArray fields = component.toObject().value(u"fields").toArray();
        EXPECT_EQ(fields.at(0).toObject().value(u"type").toString(), u"vec3"_s);
        EXPECT_EQ(fields.at(1).toObject().value(u"type").toString(), u"quat"_s);
        EXPECT_EQ(fields.at(3).toObject().value(u"values").toArray(),
                  (QJsonArray{u"auto"_s, u"static"_s, u"movable"_s}));
    }
    EXPECT_TRUE(sawTransform);

    const QString id = ok(m_commands, u"entity.create", {{u"name"_s, u"Picked"_s}}).value(u"id").toString();
    ok(m_commands, u"selection.set", {{u"ids"_s, QJsonArray{id}}});
    EXPECT_EQ(ok(m_commands, u"selection.get").value(u"ids").toArray(), (QJsonArray{id}));
    EXPECT_FALSE(m_history.undoLabels().contains(u"selection.set"_s)) << "selecting isn't an undo step";
    EXPECT_FALSE(m_commands.execute(u"selection.set", {{u"ids"_s, QJsonArray{u"nope"_s}}}));
    ok(m_commands, u"entity.delete", {{u"id"_s, id}});
    EXPECT_TRUE(ok(m_commands, u"selection.get").value(u"ids").toArray().isEmpty())
        << "a deleted entity isn't selected";
}

// #228: the gizmos' command: previews while dragging aren't recorded; the last step undoes back to `from`.
TEST_F(SceneEditorTest, SetTransformCommand)
{
    const QString id = ok(m_commands, u"entity.create", {{u"name"_s, u"Box"_s}}).value(u"id").toString();
    const auto local = [this, &id]
    {
        return m_world.scene().registry().get<Meow2::Scene::LocalTransform>(entityOf(m_world, id)).value;
    };
    const qsizetype stepsBefore = m_history.undoLabels().size(); // Qt sizes are signed

    // Only what's given changes.
    ok(m_commands, u"entity.setTransform", {{u"id"_s, id}, {u"position"_s, QJsonArray{1.0, 2.0, 3.0}}});
    EXPECT_FLOAT_EQ(local().position.y, 2.0F);
    EXPECT_FLOAT_EQ(local().scale.x, 1.0F) << "scale kept";
    EXPECT_EQ(m_history.undoLabels().size(), stepsBefore + 1);

    // A drag: previews move it without steps; the last step names where it began.
    const QJsonObject start{{u"position"_s, QJsonArray{1.0, 2.0, 3.0}},
                            {u"rotation"_s, QJsonArray{0.0, 0.0, 0.0, 1.0}},
                            {u"scale"_s, QJsonArray{1.0, 1.0, 1.0}}};
    const std::uint64_t revision = m_world.revision();
    for (const double x : {1.5, 2.0, 2.5})
    {
        ok(m_commands, u"entity.setTransform",
           {{u"id"_s, id}, {u"position"_s, QJsonArray{x, 2.0, 3.0}}, {u"preview"_s, true}});
    }
    EXPECT_EQ(m_world.revision(), revision) << "previews don't rebuild the docks every frame";
    EXPECT_FLOAT_EQ(local().position.x, 2.5F);
    EXPECT_EQ(m_history.undoLabels().size(), stepsBefore + 1) << "previews aren't recorded";
    ok(m_commands, u"entity.setTransform",
       {{u"id"_s, id},
        {u"position"_s, QJsonArray{3.0, 2.0, 3.0}},
        {u"rotation"_s, QJsonArray{0.0, 0.0, 0.0, 2.0}},
        {u"from"_s, start}});
    EXPECT_FLOAT_EQ(local().position.x, 3.0F);
    EXPECT_FLOAT_EQ(local().rotation.w, 1.0F) << "rotations are normalised";
    EXPECT_EQ(m_history.undoLabels().size(), stepsBefore + 2) << "one step for the drag";
    ASSERT_TRUE(m_commands.execute(u"edit.undo"));
    EXPECT_FLOAT_EQ(local().position.x, 1.0F) << "undo goes back to where the drag began, not the last preview";
    ASSERT_TRUE(m_commands.execute(u"edit.redo"));
    EXPECT_FLOAT_EQ(local().position.x, 3.0F);

    // The world transform follows at the next update.
    m_world.update(nullptr);
    const Meow2::Math::Mat4& world =
        m_world.scene().registry().get<Meow2::Scene::WorldTransform>(entityOf(m_world, id)).matrix;
    EXPECT_FLOAT_EQ(world._41, 3.0F);

    // Wrong shapes are refused and change nothing.
    EXPECT_FALSE(m_commands.execute(u"entity.setTransform", {{u"id"_s, id}, {u"position"_s, QJsonArray{1.0, 2.0}}}));
    EXPECT_FALSE(
        m_commands.execute(u"entity.setTransform", {{u"id"_s, id}, {u"rotation"_s, QJsonArray{0.0, 0.0, 0.0, 0.0}}}));
    EXPECT_FALSE(
        m_commands.execute(u"entity.setTransform", {{u"id"_s, id}, {u"scale"_s, QJsonArray{1.0, u"a"_s, 1.0}}}));
    EXPECT_FALSE(
        m_commands.execute(u"entity.setTransform", {{u"id"_s, u"nope"_s}, {u"position"_s, QJsonArray{0.0, 0.0, 0.0}}}));
    EXPECT_FLOAT_EQ(local().position.x, 3.0F);
}

// Owner request (#229): the Environment is a scene setting, not an entity: its own commands, undoable, and never a
// component to add.
TEST_F(SceneEditorTest, EnvironmentIsASceneSetting)
{
    m_world.newScene();
    EXPECT_TRUE(Meow2::Scene::hasEnvironment(m_world.scene()));
    EXPECT_FALSE(Meow2::Scene::findEnvironment(m_world.scene())) << "no Environment entity in a new scene";

    const QJsonObject list = ok(m_commands, u"component.list");
    bool listedAsComponent = false;
    for (const QJsonValue& entry : list.value(u"components").toArray())
    {
        listedAsComponent = listedAsComponent || entry.toObject().value(u"name").toString() == u"Environment"_s;
    }
    EXPECT_FALSE(listedAsComponent);
    ASSERT_EQ(list.value(u"settings").toArray().size(), 1);
    EXPECT_EQ(list.value(u"settings").toArray().at(0).toObject().value(u"name").toString(), u"Environment"_s);

    const QString id = ok(m_commands, u"entity.create", {{u"name"_s, u"Box"_s}}).value(u"id").toString();
    EXPECT_FALSE(m_commands.execute(u"component.add", {{u"id"_s, id}, {u"component"_s, u"Environment"_s}}))
        << "a setting isn't added to entities";

    EXPECT_DOUBLE_EQ(ok(m_commands, u"scene.settings").value(u"Environment")[u"sunElevation"].toDouble(), 35.0);
    const std::uint64_t revision = m_world.revision();
    ok(m_commands, u"scene.setSetting",
       {{u"setting"_s, u"Environment"_s}, {u"field"_s, u"sunElevation"_s}, {u"value"_s, 60.0}});
    EXPECT_FLOAT_EQ(Meow2::Scene::environment(m_world.scene()).sunElevation, 60.0F);
    EXPECT_GT(m_world.revision(), revision) << "the docks and the viewports follow";
    ASSERT_TRUE(m_commands.execute(u"edit.undo"));
    EXPECT_FLOAT_EQ(Meow2::Scene::environment(m_world.scene()).sunElevation, 35.0F);
    EXPECT_FALSE(m_commands.execute(
        u"scene.setSetting",
        {{u"setting"_s, u"Environment"_s}, {u"field"_s, u"sunElevation"_s}, {u"value"_s, u"high"_s}}));
    EXPECT_FALSE(m_commands.execute(u"scene.setSetting",
                                    {{u"setting"_s, u"Weather"_s}, {u"field"_s, u"rain"_s}, {u"value"_s, 1.0}}));
}
