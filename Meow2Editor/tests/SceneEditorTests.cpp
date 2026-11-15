/// @file
/// @brief Tests for SceneEditor: entity and component commands with undo, and the queries (offscreen, no GPU).

#include "EditorTestRig.h"

#include <Meow2/Scene/Camera.h>

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
