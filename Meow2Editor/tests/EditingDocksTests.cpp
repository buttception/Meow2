/// @file
/// @brief Tests for the Hierarchy and Inspector docks: they show the scene and run commands for what the user does
///        (offscreen, no GPU).

#include "EditorTestRig.h"
#include "Panels/HierarchyDock.h"
#include "Panels/InspectorDock.h"
#include "Viewports/ViewportController.h"

#include <Meow2/Graphics/Renderer/SceneModel.h>
#include <Meow2/Math/Quaternion.h>
#include <Meow2/Scene/TransformHierarchy.h>

#include <cmath>
#include <QComboBox>
#include <QCompleter>
#include <QDoubleSpinBox>
#include <QFrame>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QToolButton>
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

// Owner request: mesh and entity fields show names to pick from, not IDs.
TEST_F(InspectorDockTest, MeshAndEntityFieldsShowNames)
{
    ASSERT_TRUE(m_world.buildDefaultScene(nullptr));
    m_world.update(nullptr);
    Meow2::Editor::EditorMainWindow window{windowDeps()};

    // A cube of the test scene, under a new parent.
    QString cube;
    QString otherMesh;
    for (const auto [entity, renderer] : m_world.scene().registry().view<Meow2::Renderer::MeshRenderer>().each())
    {
        const QString id = QString::fromStdString(m_world.scene().idOf(entity).uuid().toString());
        const QString mesh = QString::fromStdString(renderer.mesh.uuid().toString());
        if (m_world.meshName(renderer.mesh) == "Test scene: cube 1")
        {
            cube = id;
        }
        else if (m_world.meshName(renderer.mesh) == "Test scene: ground")
        {
            otherMesh = mesh;
        }
    }
    ASSERT_FALSE(cube.isEmpty());
    ASSERT_FALSE(otherMesh.isEmpty());
    const QString holder = create(m_commands, u"Holder"_s);
    ASSERT_TRUE(m_commands.execute(u"entity.setParent", {{u"id"_s, cube}, {u"parent"_s, holder}}));
    ASSERT_TRUE(m_commands.execute(u"selection.set", {{u"ids"_s, QJsonArray{cube}}}));
    window.tick();

    auto* mesh = window.inspectorDock().findChild<QComboBox*>(u"MeshRenderer.mesh"_s);
    ASSERT_NE(mesh, nullptr) << "a drop-down, not a text box";
    EXPECT_EQ(mesh->currentText(), u"Test scene: cube 1"_s);
    EXPECT_GE(mesh->findText(u"Test scene: ground"_s), 0) << "every loaded mesh, by name";
    EXPECT_GE(mesh->findText(u"(none)"_s), 0);
    auto* parent = window.inspectorDock().findChild<QComboBox*>(u"Parent.entity"_s);
    ASSERT_NE(parent, nullptr);
    EXPECT_EQ(parent->currentText(), u"Holder"_s);
    // Two entities share that name here (the new scene's camera and the test scene's): both are told apart by ID.
    EXPECT_GE(parent->findText(u"Main camera ("_s, Qt::MatchStartsWith), 0) << "every entity, by name";
    EXPECT_EQ(parent->findText(u"Main camera"_s), -1) << "a repeated name gets the start of its ID";

    // Picking a name sets the field to that one's ID.
    const int ground = mesh->findText(u"Test scene: ground"_s);
    mesh->setCurrentIndex(ground);
    emit mesh->activated(ground);
    const Meow2::Result<Meow2::Uuid> uuid = Meow2::Uuid::parse(cube.toStdString());
    ASSERT_TRUE(uuid);
    const entt::entity entity = m_world.scene().find(Meow2::EntityId{*uuid}).value_or(entt::entity{entt::null});
    ASSERT_TRUE(entity != entt::null);
    EXPECT_EQ(QString::fromStdString(
                  m_world.scene().registry().get<Meow2::Renderer::MeshRenderer>(entity).mesh.uuid().toString()),
              otherMesh);
    ASSERT_TRUE(m_commands.execute(u"edit.undo"));
    EXPECT_EQ(m_world.meshName(m_world.scene().registry().get<Meow2::Renderer::MeshRenderer>(entity).mesh),
              "Test scene: cube 1");
}

// Owner requests (#229): the ID in grey under the name, components as cards that fold, Add component as a search.
TEST_F(InspectorDockTest, CardsIdAndComponentSearch)
{
    Meow2::Editor::EditorMainWindow window{windowDeps()};
    const QString id = create(m_commands, u"Box"_s);
    ASSERT_TRUE(m_commands.execute(u"selection.set", {{u"ids"_s, QJsonArray{id}}}));
    window.tick();
    Meow2::Editor::InspectorDock& inspector = window.inspectorDock();

    auto* idLabel = inspector.findChild<QLabel*>(u"EntityId"_s);
    ASSERT_NE(idLabel, nullptr);
    EXPECT_EQ(idLabel->text(), id);

    auto* card = inspector.findChild<QFrame*>(u"LocalTransform"_s);
    ASSERT_NE(card, nullptr);
    EXPECT_TRUE(card->property("componentCard").toBool());
    auto* fold = inspector.findChild<QToolButton*>(u"Fold.LocalTransform"_s);
    ASSERT_NE(fold, nullptr);
    auto* position = inspector.findChild<QDoubleSpinBox*>(u"LocalTransform.position.0"_s);
    ASSERT_NE(position, nullptr);
    EXPECT_EQ(position->buttonSymbols(), QAbstractSpinBox::NoButtons) << "compact: no arrows";
    fold->click();
    EXPECT_FALSE(position->isVisibleTo(&inspector)) << "folded shut";
    // Kept across rebuilds (another edit rebuilds the panel).
    ASSERT_TRUE(m_commands.execute(u"entity.rename", {{u"id"_s, id}, {u"name"_s, u"Crate"_s}}));
    window.tick();
    position = inspector.findChild<QDoubleSpinBox*>(u"LocalTransform.position.0"_s);
    ASSERT_NE(position, nullptr);
    EXPECT_FALSE(position->isVisibleTo(&inspector));
    inspector.findChild<QToolButton*>(u"Fold.LocalTransform"_s)->click();

    // Add component: a search field; a typed name adds it, a partial one doesn't.
    auto* add = inspector.findChild<QLineEdit*>(u"AddComponent"_s);
    ASSERT_NE(add, nullptr);
    ASSERT_NE(add->completer(), nullptr);
    EXPECT_EQ(add->completer()->filterMode(), Qt::MatchContains);
    add->setText(u"Cam"_s);
    emit add->returnPressed();
    window.tick();
    EXPECT_EQ(inspector.findChild<QFrame*>(u"Camera"_s), nullptr) << "not a whole name";
    add = inspector.findChild<QLineEdit*>(u"AddComponent"_s);
    add->setText(u"camera"_s);
    emit add->returnPressed();
    window.tick();
    EXPECT_NE(inspector.findChild<QFrame*>(u"Camera"_s), nullptr) << "any case";
    EXPECT_EQ(inspector.findChild<QLineEdit*>(u"AddComponent"_s)->completer()->model()->rowCount() > 0, true);
}

// Owner reports (#229): typing in the Hierarchy jumped to an entity named with that letter; double-click frames it.
TEST_F(HierarchyDockTest, KeysDontSearchAndDoubleClickFrames)
{
    Meow2::Editor::EditorMainWindow window{windowDeps()};
    const QString wall = create(m_commands, u"Wall"_s);
    create(m_commands, u"Door"_s);
    window.tick();
    QTreeWidget& tree = window.hierarchyDock().tree();
    QTreeWidgetItem* door = itemNamed(tree, u"Door"_s);
    ASSERT_NE(door, nullptr);
    tree.setCurrentItem(door);
    tree.keyboardSearch(u"W"_s);
    EXPECT_EQ(tree.currentItem(), door) << "W is a shortcut, not a search for 'Wall'";
    EXPECT_EQ(tree.editTriggers(), QAbstractItemView::EditKeyPressed) << "F2 renames; a double-click doesn't";

    // A double-click frames the entity: the camera turns to it (its 1 m sphere, as it draws nothing).
    ASSERT_TRUE(
        m_commands.execute(u"entity.setTransform", {{u"id"_s, wall}, {u"position"_s, QJsonArray{40.0, 0.0, 0.0}}}));
    m_world.update(nullptr);
    window.tick();
    QTreeWidgetItem* wallItem = itemNamed(tree, u"Wall"_s);
    ASSERT_NE(wallItem, nullptr);
    emit tree.itemDoubleClicked(wallItem, 0);
    const Meow2::Math::Transform camera = window.viewports().camera().transform();
    const Meow2::Math::Vec3 forward = Meow2::Math::rotate(camera.rotation, Meow2::Math::c_forward);
    // The point it looks at, at the framing distance ahead, is the wall.
    const float distance = std::hypot(camera.position.x - 40.0F, camera.position.y, camera.position.z);
    EXPECT_NEAR(camera.position.x + forward.x * distance, 40.0F, 0.05F);
    EXPECT_LT(distance, 5.0F) << "close, for a 1 m sphere";
    EXPECT_FALSE(m_commands.execute(u"viewport.focus", {{u"id"_s, u"nope"_s}}));
}
