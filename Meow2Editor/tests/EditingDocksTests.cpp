/// @file
/// @brief Tests for the Hierarchy and Inspector docks: they show the scene and run commands for what the user does
///        (offscreen, no GPU).

#include "EditorTestRig.h"
#include "Panels/HierarchyDock.h"
#include "Panels/InspectorDock.h"

#include <Meow2/Math/Quaternion.h>
#include <Meow2/Scene/TransformHierarchy.h>

#include <QDoubleSpinBox>
#include <QJsonArray>
#include <QJsonObject>
#include <QLineEdit>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>

namespace
{
using namespace Qt::StringLiterals;
using HierarchyDockTest = Meow2::Tests::EditorTest;
using InspectorDockTest = Meow2::Tests::EditorTest;

QString create(Meow2::Editor::EditorCommandRegistry& commands, const QString& name, const QString& parent = {})
{
    return commands.execute(u"entity.create", {{u"name"_s, name}, {u"parent"_s, parent}})
        .value()
        .toObject()
        .value(u"id")
        .toString();
}

QTreeWidgetItem* itemNamed(QTreeWidget& tree, const QString& name)
{
    for (QTreeWidgetItemIterator it{&tree}; *it != nullptr; ++it)
    {
        if ((*it)->text(0) == name)
        {
            return *it;
        }
    }
    return nullptr;
}
} // namespace

TEST_F(HierarchyDockTest, FollowsTheScene)
{
    Meow2::Editor::EditorMainWindow window{windowDeps()};
    const QString parent = create(m_commands, u"Parent"_s);
    const QString child = create(m_commands, u"Child"_s, parent);
    window.tick();
    QTreeWidget& tree = window.hierarchyDock().tree();
    QTreeWidgetItem* parentItem = itemNamed(tree, u"Parent"_s);
    ASSERT_NE(parentItem, nullptr);
    ASSERT_EQ(parentItem->childCount(), 1);
    EXPECT_EQ(parentItem->child(0)->text(0), u"Child"_s) << "children nest under their parent";
    EXPECT_NE(itemNamed(tree, u"Main camera"_s), nullptr) << "the new scene's own entities too";

    // Selecting an item selects the entity.
    parentItem->setSelected(true);
    EXPECT_EQ(m_commands.execute(u"selection.get").value().toObject().value(u"ids").toArray(), (QJsonArray{parent}));

    // Renaming an item renames the entity.
    itemNamed(tree, u"Child"_s)->setText(0, u"Renamed"_s);
    EXPECT_EQ(m_commands.execute(u"entity.get", {{u"id"_s, child}}).value().toObject().value(u"name").toString(),
              u"Renamed"_s);

    // A drop on empty space moves the entity to the root; the tree follows at the next tick.
    window.hierarchyDock().reparent({child}, {});
    window.tick();
    EXPECT_EQ(itemNamed(window.hierarchyDock().tree(), u"Parent"_s)->childCount(), 0);
    EXPECT_NE(itemNamed(window.hierarchyDock().tree(), u"Renamed"_s), nullptr);
    EXPECT_EQ(m_history.undoLabels().front(), u"Move Renamed"_s);

    // A selection made elsewhere (the viewport, an agent) shows in the tree.
    ASSERT_TRUE(m_commands.execute(u"selection.set", {{u"ids"_s, QJsonArray{child}}}));
    window.tick();
    EXPECT_TRUE(itemNamed(window.hierarchyDock().tree(), u"Renamed"_s)->isSelected());
    EXPECT_FALSE(itemNamed(window.hierarchyDock().tree(), u"Parent"_s)->isSelected());
}

TEST_F(InspectorDockTest, EditsIssueCommands)
{
    Meow2::Editor::EditorMainWindow window{windowDeps()};
    const QString id = create(m_commands, u"Box"_s);
    window.tick();
    EXPECT_FALSE(window.inspectorDock().shown()) << "nothing selected";
    ASSERT_TRUE(m_commands.execute(u"selection.set", {{u"ids"_s, QJsonArray{id}}}));
    window.tick();
    ASSERT_TRUE(window.inspectorDock().shown());

    // A position edit runs component.setField when it's finished.
    auto* y = window.inspectorDock().findChild<QDoubleSpinBox*>(u"LocalTransform.position.1"_s);
    ASSERT_NE(y, nullptr);
    y->setValue(4.0);
    emit y->editingFinished();
    const Meow2::Result<Meow2::Uuid> uuid = Meow2::Uuid::parse(id.toStdString());
    const entt::entity box = *m_world.scene().find(Meow2::EntityId{*uuid});
    EXPECT_FLOAT_EQ(m_world.scene().registry().get<Meow2::Scene::LocalTransform>(box).value.position.y, 4.0F);
    EXPECT_EQ(m_history.undoLabels().front(), u"Set LocalTransform.position"_s);
    emit y->editingFinished(); // focus leaving again without a change records nothing
    EXPECT_EQ(m_history.undoLabels().size(), 2);

    // Rotations are typed as Euler angles in degrees, and stay as typed.
    window.tick();
    auto* yaw = window.inspectorDock().findChild<QDoubleSpinBox*>(u"LocalTransform.rotation.1"_s);
    ASSERT_NE(yaw, nullptr);
    yaw->setValue(90.0);
    emit yaw->editingFinished();
    const Meow2::Math::Quat rotation = m_world.scene().registry().get<Meow2::Scene::LocalTransform>(box).value.rotation;
    const Meow2::Math::Vec3 forward = Meow2::Math::rotate(rotation, Meow2::Math::Vec3{0.0F, 0.0F, 1.0F});
    EXPECT_NEAR(forward.x, 1.0F, 1e-4F) << "90° of yaw turns +Z to +X";
    window.tick();
    yaw = window.inspectorDock().findChild<QDoubleSpinBox*>(u"LocalTransform.rotation.1"_s);
    ASSERT_NE(yaw, nullptr);
    EXPECT_DOUBLE_EQ(yaw->value(), 90.0) << "shown as typed after the rebuild";

    // The name field renames.
    auto* name = window.inspectorDock().findChild<QLineEdit*>(u"Name"_s);
    ASSERT_NE(name, nullptr);
    name->setText(u"Crate"_s);
    emit name->editingFinished();
    EXPECT_EQ(m_commands.execute(u"entity.get", {{u"id"_s, id}}).value().toObject().value(u"name").toString(),
              u"Crate"_s);
}
