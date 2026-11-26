/// @file
/// @brief Tests for EditorMainWindow's menus, toolbar and panels.

#include "EditorTestRig.h"
#include "Panels/AssetsDock.h"
#include "Panels/ConsoleDock.h"
#include "Panels/RenderGraphDock.h"
#include "Panels/SettingsDialog.h"
#include "Panels/StatsDock.h"
#include "Viewports/GameViewportDock.h"

#include <Meow2/Scene/Environment.h>

#include <QAction>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileInfo>
#include <QPlainTextEdit>
#include <QTabWidget>

namespace
{
using namespace Qt::StringLiterals;
using MainWindowTest = Meow2::Tests::EditorTest;
} // namespace

TEST_F(MainWindowTest, ActionsIssueCommands)
{
    Meow2::Editor::EditorMainWindow window{windowDeps()};
    QAction* undo = window.actionFor(u"edit.undo"_s);
    QAction* redo = window.actionFor(u"edit.redo"_s);
    ASSERT_NE(undo, nullptr);
    ASSERT_NE(redo, nullptr);
    EXPECT_FALSE(undo->isEnabled()) << "nothing to undo";
    EXPECT_EQ(undo->shortcut(), QKeySequence{QKeySequence::Undo});

    pushValueStep(0, 9);
    window.tick();
    EXPECT_TRUE(undo->isEnabled());
    EXPECT_EQ(undo->text(), u"Undo Set value to 9"_s) << "the label names the step";

    undo->trigger(); // what a click or Ctrl+Z does
    EXPECT_EQ(m_value, 0);
    EXPECT_FALSE(undo->isEnabled()) << "refreshed right after the command ran";
    EXPECT_TRUE(redo->isEnabled());
    redo->trigger();
    EXPECT_EQ(m_value, 9);
}

TEST_F(MainWindowTest, PanelsShowThroughCommands)
{
    {
        Meow2::Editor::EditorMainWindow window{windowDeps()};
        window.show();
        EXPECT_TRUE(window.console().isVisible()) << "the console is docked and shown by default";

        ASSERT_TRUE(m_commands.execute(u"view.showPanel", {{u"panel"_s, u"console"_s}, {u"visible"_s, false}}));
        EXPECT_FALSE(window.console().isVisible());
        ASSERT_TRUE(m_commands.execute(u"view.showPanel", {{u"panel"_s, u"settings"_s}, {u"visible"_s, true}}));
        EXPECT_TRUE(window.settingsDialog().isVisible());
        EXPECT_FALSE(m_commands.execute(u"view.showPanel", {{u"panel"_s, u"timeline"_s}, {u"visible"_s, true}}))
            << "not a panel";

        m_console.add(Meow2::Log::LogLevel::Info, "10:00:00.000", "Editor", "Hello");
        window.tick();
        EXPECT_EQ(window.console().view().toPlainText(), u"[10:00:00.000]<info> Editor: Hello"_s)
            << "the window's tick feeds the console";
    }
    EXPECT_EQ(m_commands.find(u"view.showPanel"), nullptr) << "removed with the window";
}

TEST_F(MainWindowTest, LayoutRoundTrips)
{
    {
        Meow2::Editor::EditorMainWindow window{windowDeps()};
        window.show();
        EXPECT_TRUE(window.gameDock().isVisible()) << "the game view is docked and shown by default";
        ASSERT_TRUE(m_commands.execute(u"view.showPanel", {{u"panel"_s, u"game"_s}, {u"visible"_s, false}}));
        window.saveLayout();
    }
    EXPECT_FALSE(m_config.getString("editor.layout", "").empty());

    Meow2::Editor::EditorMainWindow again{windowDeps()};
    again.show();
    EXPECT_FALSE(again.gameDock().isVisible()) << "hidden as it was left";
    EXPECT_TRUE(again.console().isVisible());
}

TEST_F(MainWindowTest, UnsavedChangesAsk)
{
    Meow2::Editor::EditorMainWindow window{windowDeps()};
    int asked = 0;
    Meow2::Editor::UnsavedAnswer answer = Meow2::Editor::UnsavedAnswer::Cancel;
    window.setUnsavedPrompt(
        [&]
        {
            ++asked;
            return answer;
        });
    window.setSaveAsPrompt([] { return u"Asked.m2scene"_s; });
    window.tick();
    EXPECT_TRUE(window.windowTitle().startsWith(u"Untitled — "_s)) << window.windowTitle().toStdString();
    EXPECT_TRUE(window.newScene()) << "nothing unsaved: no question";
    EXPECT_EQ(asked, 0);

    pushValueStep(0, 1);
    window.tick();
    EXPECT_TRUE(window.windowTitle().startsWith(u"Untitled* — "_s)) << "a * while there are unsaved changes";
    EXPECT_FALSE(window.newScene()) << "Cancel keeps the scene";
    EXPECT_EQ(asked, 1);
    EXPECT_TRUE(m_history.canUndo());

    answer = Meow2::Editor::UnsavedAnswer::Save; // untitled: saving asks for a file first
    EXPECT_TRUE(window.newScene());
    EXPECT_TRUE(std::filesystem::exists(m_project / "Asked.m2scene")) << "saved before the new scene";
    EXPECT_TRUE(m_document->scenePath().isEmpty()) << "and then a new, untitled scene";

    pushValueStep(1, 2);
    answer = Meow2::Editor::UnsavedAnswer::Discard;
    EXPECT_TRUE(window.openScene(u"Asked.m2scene"_s));
    EXPECT_FALSE(m_history.canUndo()) << "discarded";
    window.tick();
    EXPECT_TRUE(window.windowTitle().startsWith(u"Asked.m2scene — "_s));
}

TEST_F(MainWindowTest, AssetsDockOpensScenes)
{
    ASSERT_TRUE(m_commands.execute(u"scene.saveAs", {{u"path"_s, u"Levels/Dock.m2scene"_s}}));
    ASSERT_TRUE(m_commands.execute(u"scene.new"));
    Meow2::Editor::EditorMainWindow window{windowDeps()};
    EXPECT_EQ(window.assetsDock().project().toStdString(), m_document->projectFolder().string());
    window.assetsDock().activate(QString::fromStdString((m_project / "Levels" / "Dock.m2scene").string()));
    EXPECT_EQ(m_document->scenePath(), u"Levels/Dock.m2scene"_s) << "a double-click opens the scene";
    ASSERT_TRUE(m_commands.execute(u"view.showPanel", {{u"panel"_s, u"assets"_s}, {u"visible"_s, false}}));
    EXPECT_FALSE(window.assetsDock().isVisible());
}

// #228 (owner request): docks stacked as tabs show their tabs on top.
TEST_F(MainWindowTest, DockTabsOnTop)
{
    Meow2::Editor::EditorMainWindow window{windowDeps()};
    for (const Qt::DockWidgetArea area :
         {Qt::LeftDockWidgetArea, Qt::RightDockWidgetArea, Qt::TopDockWidgetArea, Qt::BottomDockWidgetArea})
    {
        EXPECT_EQ(window.tabPosition(area), QTabWidget::North) << "area " << static_cast<int>(area);
    }
}

// Owner request: a dock stacked with others as tabs shows no title bar of its own (its tab names it); alone again,
// it gets it back.
TEST_F(MainWindowTest, TabbedDocksHideTheirTitle)
{
    Meow2::Editor::EditorMainWindow window{windowDeps()};
    QDockWidget& console = window.console();
    QDockWidget& assets = window.assetsDock();
    window.tick();
    EXPECT_NE(console.titleBarWidget(), nullptr) << "a tab beside the Stats and Render graph docks at first";
    window.statsDock().hide();
    window.renderGraphDock().hide();
    window.tick();
    EXPECT_EQ(console.titleBarWidget(), nullptr) << "alone at the bottom: its title shows";

    window.tabifyDockWidget(&console, &assets);
    window.tick();
    ASSERT_NE(console.titleBarWidget(), nullptr) << "tabbed: hidden behind an empty title bar";
    ASSERT_NE(assets.titleBarWidget(), nullptr);
    EXPECT_TRUE(console.titleBarWidget()->children().isEmpty()) << "an empty widget";

    // Closing the other tab leaves it alone: the title comes back.
    assets.hide();
    window.tick();
    EXPECT_EQ(console.titleBarWidget(), nullptr);

    // Split off again: titles back on both.
    assets.show();
    window.tick();
    EXPECT_NE(console.titleBarWidget(), nullptr);
    window.addDockWidget(Qt::LeftDockWidgetArea, &assets);
    window.tick();
    EXPECT_EQ(console.titleBarWidget(), nullptr);
    EXPECT_EQ(assets.titleBarWidget(), nullptr);
}

// Owner requests (#229): the scene's settings in a dock of their own (File → Scene settings…), edited through
// scene.setSetting; the Assets dock's icons.
TEST_F(MainWindowTest, SceneSettingsDockAndAssetIcons)
{
    m_world.newScene();
    Meow2::Editor::EditorMainWindow window{windowDeps()};
    ASSERT_TRUE(m_commands.execute(u"view.showPanel", {{u"panel"_s, u"sceneSettings"_s}, {u"visible"_s, true}}));
    window.tick();
    auto* dock = window.findChild<QDockWidget*>(u"SceneSettingsDock"_s);
    ASSERT_NE(dock, nullptr);
    auto* elevation = dock->findChild<QDoubleSpinBox*>(u"Environment.sunElevation"_s);
    ASSERT_NE(elevation, nullptr);
    EXPECT_DOUBLE_EQ(elevation->value(), static_cast<double>(Meow2::Editor::c_newSceneEnvironment.sunElevation)) << "a new scene's sun";
    elevation->setValue(50.0);
    emit elevation->editingFinished();
    EXPECT_FLOAT_EQ(Meow2::Scene::environment(m_world.scene()).sunElevation, 50.0F);
    EXPECT_TRUE(m_history.canUndo()) << "an undoable step";
    ASSERT_NE(dock->findChild<QDoubleSpinBox*>(u"Environment.sunColor.2"_s), nullptr) << "colours as four boxes";

    EXPECT_EQ(Meow2::Editor::assetIconName(QFileInfo{u"Level.m2scene"_s}), u"Scene"_s);
    EXPECT_EQ(Meow2::Editor::assetIconName(QFileInfo{u"Sponza.GLTF"_s}), u"Mesh"_s);
    EXPECT_EQ(Meow2::Editor::assetIconName(QFileInfo{u"bricks.png"_s}), u"Texture"_s);
    EXPECT_EQ(Meow2::Editor::assetIconName(QFileInfo{u"Lit.hlsl"_s}), u"Shader"_s);
    EXPECT_TRUE(Meow2::Editor::assetIconName(QFileInfo{u"notes.txt"_s}).isEmpty());
    EXPECT_FALSE(QFile::exists(u":/icons/Nope.svg"_s));
    EXPECT_TRUE(QFile::exists(u":/icons/Scene.svg"_s)) << "the icon set is built into the editor";
}
