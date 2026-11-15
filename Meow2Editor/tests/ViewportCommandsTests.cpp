/// @file
/// @brief Tests for the viewport commands and the game viewport dock (without a GPU: nothing is drawn).

#include "EditorTestRig.h"
#include "Viewports/GameViewportDock.h"
#include "Viewports/ViewportController.h"

#include <Meow2/Scene/Camera.h>

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
