/// @file
/// @brief Tests for the viewport commands and the game viewport dock (without a GPU: nothing is drawn).

#include "EditorTestRig.h"
#include "Viewports/GameViewportDock.h"
#include "Viewports/ViewportController.h"

#include <Meow2/Math/Quaternion.h>
#include <Meow2/Scene/Camera.h>

#include <QAction>
#include <QComboBox>
#include <QJsonArray>

namespace
{
using namespace Qt::StringLiterals;
using ViewportCommandsTest = Meow2::Tests::EditorTest;
} // namespace

TEST_F(ViewportCommandsTest, RenderScaleAndModes)
{
    ASSERT_TRUE(m_world.buildDefaultScene(nullptr));
    Meow2::Editor::EditorMainWindow window{windowDeps()};

    // Render scale: 25..100, saved as a preference, shown in the game dock's drop-down.
    const Meow2::Result<QJsonValue> scaled = m_commands.execute(u"viewport.setRenderScale", {{u"percent"_s, 50}});
    ASSERT_TRUE(scaled) << scaled.error().message;
    EXPECT_EQ(scaled->toInt(), 50);
    EXPECT_EQ(m_config.getInt("editor.gameRenderScale", 100), 50);
    window.tick();
    EXPECT_EQ(window.gameDock().renderScaleBox().currentData().toInt(), 50);
    EXPECT_FALSE(m_commands.execute(u"viewport.setRenderScale", {{u"percent"_s, 20}})) << "below 25";
    EXPECT_FALSE(m_commands.execute(u"viewport.setRenderScale", {{u"percent"_s, 110}})) << "above 100";
    EXPECT_FALSE(m_history.canUndo()) << "a preference, not a document change";

    // The drop-down runs the command.
    QComboBox& box = window.gameDock().renderScaleBox();
    box.setCurrentIndex(box.findData(25));
    emit box.activated(box.currentIndex());
    EXPECT_EQ(m_config.getInt("editor.gameRenderScale", 100), 25);

    // Camera mode.
    ASSERT_TRUE(m_commands.execute(u"viewport.setCameraMode", {{u"mode"_s, u"fly"_s}}));
    EXPECT_EQ(window.viewports().camera().mode(), Meow2::Editor::CameraMode::Fly);
    EXPECT_FALSE(m_commands.execute(u"viewport.setCameraMode", {{u"mode"_s, u"walk"_s}}));

    // Focus frames the scene.
    ASSERT_TRUE(m_commands.execute(u"viewport.focus"));
    const Meow2::Result<QJsonValue> info = m_commands.execute(u"viewport.info");
    ASSERT_TRUE(info) << info.error().message;
    EXPECT_EQ((*info)[u"cameraMode"].toString(), u"fly"_s);
    EXPECT_EQ((*info)[u"renderScale"].toInt(), 25);
    EXPECT_FALSE((*info)[u"game"][u"drawn"].toBool()) << "no GPU in the tests, so nothing is drawn";
    EXPECT_EQ((*info)[u"cameraPosition"].toArray().size(), 3);
}

TEST_F(ViewportCommandsTest, DefaultSceneHasAMainCamera)
{
    ASSERT_TRUE(m_world.buildDefaultScene(nullptr));
    m_world.update(nullptr);
    const std::optional<entt::entity> camera = Meow2::Scene::findMainCamera(m_world.scene());
    ASSERT_TRUE(camera);
    const std::optional<Meow2::Math::Transform> transform = Meow2::Scene::cameraTransform(m_world.scene(), *camera);
    ASSERT_TRUE(transform);
    EXPECT_LT(transform->position.z, 0.0F) << "behind the cubes, looking at them";
    EXPECT_GT(m_world.bounds().radius, 1.0F);
}

// #228: the gizmo's mode, axes and snapping through viewport.setGizmo, shown by viewport.info and the toolbar.
TEST_F(ViewportCommandsTest, GizmoCommand)
{
    Meow2::Editor::EditorMainWindow window{windowDeps()};
    const auto actionNamed = [&window](const QString& text)
    {
        for (QAction* action : window.findChildren<QAction*>())
        {
            if (action->text() == text)
            {
                return action;
            }
        }
        return static_cast<QAction*>(nullptr);
    };
    QAction* move = actionNamed(u"Move"_s);
    QAction* rotate = actionNamed(u"Rotate"_s);
    QAction* local = actionNamed(u"Local axes"_s);
    QAction* snap = actionNamed(u"Snap"_s);
    ASSERT_TRUE(move && rotate && local && snap);
    window.tick();
    EXPECT_TRUE(move->isChecked()) << "translate in world space, without snapping, at first";
    EXPECT_FALSE(local->isChecked());

    ASSERT_TRUE(m_commands.execute(u"viewport.setGizmo",
                                   {{u"mode"_s, u"rotate"_s}, {u"space"_s, u"local"_s}, {u"snap"_s, true}}));
    EXPECT_EQ(window.viewports().gizmo().mode(), Meow2::Editor::GizmoMode::Rotate);
    EXPECT_EQ(window.viewports().gizmo().space(), Meow2::Editor::GizmoSpace::Local);
    EXPECT_TRUE(window.viewports().snapping());
    const Meow2::Result<QJsonValue> info = m_commands.execute(u"viewport.info");
    ASSERT_TRUE(info);
    EXPECT_EQ((*info)[u"gizmo"][u"mode"].toString(), u"rotate"_s);
    EXPECT_EQ((*info)[u"gizmo"][u"space"].toString(), u"local"_s);
    EXPECT_TRUE((*info)[u"gizmo"][u"snap"].toBool());
    window.tick();
    EXPECT_TRUE(rotate->isChecked()) << "the toolbar follows";
    EXPECT_FALSE(move->isChecked());
    EXPECT_TRUE(local->isChecked());
    EXPECT_TRUE(snap->isChecked());

    // Only what's given changes; bad values are refused; nothing goes into the history.
    ASSERT_TRUE(m_commands.execute(u"viewport.setGizmo", {{u"snap"_s, false}}));
    EXPECT_EQ(window.viewports().gizmo().mode(), Meow2::Editor::GizmoMode::Rotate);
    EXPECT_FALSE(window.viewports().snapping());
    EXPECT_FALSE(m_commands.execute(u"viewport.setGizmo", {{u"mode"_s, u"shear"_s}}));
    EXPECT_FALSE(m_commands.execute(u"viewport.setGizmo", {{u"space"_s, u"screen"_s}}));
    EXPECT_FALSE(m_history.canUndo());

    // The toolbar runs the command.
    move->trigger();
    EXPECT_EQ(window.viewports().gizmo().mode(), Meow2::Editor::GizmoMode::Translate);
    local->trigger();
    EXPECT_EQ(window.viewports().gizmo().space(), Meow2::Editor::GizmoSpace::World);
}

// Owner report: W and E didn't reach the flying camera. The gizmo keys are viewport-only actions that step aside
// while the camera flies; the toolbar items have no shortcut that could take the keys.
TEST_F(ViewportCommandsTest, GizmoKeysStepAsideWhileFlying)
{
    Meow2::Editor::EditorMainWindow window{windowDeps()};
    auto* keyW = window.findChild<QAction*>(u"GizmoKeyW"_s);
    auto* keyE = window.findChild<QAction*>(u"GizmoKeyE"_s);
    ASSERT_TRUE(keyW && keyE);
    for (QAction* action : window.findChildren<QAction*>())
    {
        if (action->text() == u"Move"_s || action->text() == u"Rotate"_s || action->text() == u"Scale"_s)
        {
            EXPECT_TRUE(action->shortcut().isEmpty()) << action->text().toStdString() << " takes no key";
        }
    }
    window.tick();
    EXPECT_TRUE(keyW->isEnabled()) << "orbiting: W picks the move gizmo";

    ASSERT_TRUE(m_commands.execute(u"viewport.setCameraMode", {{u"mode"_s, u"fly"_s}}));
    window.tick();
    EXPECT_FALSE(keyW->isEnabled()) << "flying: W moves the camera";
    EXPECT_FALSE(keyE->isEnabled()) << "and E lifts it";

    ASSERT_TRUE(m_commands.execute(u"viewport.setCameraMode", {{u"mode"_s, u"orbit"_s}}));
    window.tick();
    EXPECT_TRUE(keyW->isEnabled());
    keyE->trigger();
    EXPECT_EQ(window.viewports().gizmo().mode(), Meow2::Editor::GizmoMode::Rotate);
}

// Owner request (#229): the view-axis gizmo's command turns the camera to look from a side.
TEST_F(ViewportCommandsTest, ViewFromASide)
{
    Meow2::Editor::EditorMainWindow window{windowDeps()};
    ASSERT_TRUE(m_commands.execute(u"viewport.viewFrom", {{u"side"_s, u"+x"_s}}));
    const Meow2::Math::Vec3 forward =
        Meow2::Math::rotate(window.viewports().camera().transform().rotation, Meow2::Math::c_forward);
    EXPECT_NEAR(forward.x, -1.0F, 1e-3F) << "from +X, looking along -X";
    ASSERT_TRUE(m_commands.execute(u"viewport.viewFrom", {{u"side"_s, u"-z"_s}}));
    EXPECT_NEAR(Meow2::Math::rotate(window.viewports().camera().transform().rotation, Meow2::Math::c_forward).z, 1.0F,
                1e-3F);
    EXPECT_FALSE(m_commands.execute(u"viewport.viewFrom", {{u"side"_s, u"top"_s}}));
    EXPECT_FALSE(m_history.canUndo()) << "how the scene is viewed, not an edit";
}
