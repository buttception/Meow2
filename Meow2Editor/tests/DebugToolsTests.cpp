/// @file
/// @brief Tests for the editor's debug tools: the `debug.*` commands and `stats.frame`, and the Debug and Stats docks
///        that run them (offscreen, no GPU).

#include "EditorTestRig.h"
#include "Panels/DebugDock.h"
#include "Panels/StatsDock.h"
#include "Viewports/DebugTools.h"

#include <QCheckBox>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QRadioButton>
#include <QTableWidget>
#include <QToolButton>

namespace
{
using namespace Qt::StringLiterals;
using DebugCommandsTest = Meow2::Tests::EditorTest;
using DebugDockTest = Meow2::Tests::EditorTest;
using StatsDockTest = Meow2::Tests::EditorTest;

QJsonObject debugState(Meow2::Editor::EditorCommandRegistry& commands)
{
    return commands.execute(u"debug.state", {}).value().toObject();
}
} // namespace

// The debug tools change how the scene is looked at, not the scene: nothing to undo, saved for the next run
// (except freezing, which only means something while the editor runs).
TEST_F(DebugCommandsTest, SettingsRoundTripAndSave)
{
    {
        Meow2::Editor::EditorMainWindow window{windowDeps()};
        EXPECT_EQ(debugState(m_commands).value(u"view").toString(), u"shaded"_s) << "the default";
        ASSERT_TRUE(m_commands.execute(u"debug.setView", {{u"view"_s, u"baseColor"_s}}));
        ASSERT_TRUE(m_commands.execute(u"debug.setWireframe", {{u"mode"_s, u"overlay"_s}}));
        ASSERT_TRUE(m_commands.execute(u"debug.setCulling", {{u"freeze"_s, true}, {u"showFrustum"_s, false}}));
        ASSERT_TRUE(m_commands.execute(u"debug.setGrid", {{u"on"_s, true}}));

        const QJsonObject state = debugState(m_commands);
        EXPECT_EQ(state.value(u"view").toString(), u"baseColor"_s);
        EXPECT_EQ(state.value(u"wireframe").toString(), u"overlay"_s);
        EXPECT_TRUE(state.value(u"freezeCulling").toBool());
        EXPECT_FALSE(state.value(u"showFrustum").toBool());
        EXPECT_TRUE(state.value(u"grid").toBool());
        EXPECT_FALSE(m_history.canUndo()) << "nothing to undo";

        EXPECT_EQ(m_config.getString(Meow2::Editor::c_debugViewKey, ""), "baseColor");
        EXPECT_EQ(m_config.getString(Meow2::Editor::c_debugWireframeKey, ""), "overlay");
        EXPECT_FALSE(m_config.getBool(Meow2::Editor::c_debugShowFrustumKey, true));
        EXPECT_TRUE(m_config.getBool(Meow2::Editor::c_debugGridKey, false));
    }
    // The next run starts from the saved choices, culling not frozen.
    Meow2::Editor::EditorMainWindow window{windowDeps()};
    const QJsonObject state = debugState(m_commands);
    EXPECT_EQ(state.value(u"view").toString(), u"baseColor"_s);
    EXPECT_EQ(state.value(u"wireframe").toString(), u"overlay"_s);
    EXPECT_FALSE(state.value(u"freezeCulling").toBool());
    EXPECT_FALSE(state.value(u"showFrustum").toBool());
    EXPECT_TRUE(state.value(u"grid").toBool());
}

TEST_F(DebugCommandsTest, RejectsUnknownValues)
{
    Meow2::Editor::EditorMainWindow window{windowDeps()};
    ASSERT_TRUE(m_commands.execute(u"debug.setView", {{u"view"_s, u"depth"_s}}));
    EXPECT_FALSE(m_commands.execute(u"debug.setView", {{u"view"_s, u"normals!"_s}}));
    EXPECT_FALSE(m_commands.execute(u"debug.setWireframe", {{u"mode"_s, u"dotted"_s}}));
    EXPECT_FALSE(m_commands.execute(u"debug.setGrid", {{u"on"_s, u"yes"_s}})) << "a boolean";
    const QJsonObject state = debugState(m_commands);
    EXPECT_EQ(state.value(u"view").toString(), u"depth"_s) << "unchanged";
    EXPECT_EQ(state.value(u"wireframe").toString(), u"off"_s);
    EXPECT_FALSE(state.value(u"grid").toBool());
}

// Every view has a button, and every click is a command (so MCP can do the same).
TEST_F(DebugDockTest, ButtonsIssueCommands)
{
    Meow2::Editor::EditorMainWindow window{windowDeps()};
    window.tick(0.016F);
    Meow2::Editor::DebugDock& dock = window.debugDock();
    for (const Meow2::Renderer::DebugViewEntry& entry : Meow2::Renderer::debugViewEntries())
    {
        const QString key = QString::fromUtf8(entry.key.data(), static_cast<qsizetype>(entry.key.size()));
        auto* button = dock.findChild<QToolButton*>(u"View."_s + key);
        ASSERT_NE(button, nullptr) << entry.key;
        EXPECT_EQ(button->property("generated").toBool(), entry.generated) << "dashed when a debug pass makes it";
    }
    EXPECT_TRUE(dock.findChild<QToolButton*>(u"View.shaded"_s)->isChecked());

    dock.findChild<QToolButton*>(u"View.geometryNormal"_s)->click();
    dock.findChild<QRadioButton*>(u"Wireframe.only"_s)->click();
    dock.findChild<QCheckBox*>(u"Grid"_s)->click();
    dock.findChild<QCheckBox*>(u"FreezeCulling"_s)->click();
    QJsonObject state = debugState(m_commands);
    EXPECT_EQ(state.value(u"view").toString(), u"geometryNormal"_s);
    EXPECT_EQ(state.value(u"wireframe").toString(), u"only"_s);
    EXPECT_TRUE(state.value(u"grid").toBool());
    EXPECT_TRUE(state.value(u"freezeCulling").toBool());
    EXPECT_TRUE(dock.findChild<QToolButton*>(u"View.geometryNormal"_s)->isChecked());
    EXPECT_FALSE(dock.findChild<QToolButton*>(u"View.shaded"_s)->isChecked()) << "one view at a time";

    // A change from elsewhere (an agent) shows at the next refresh, with the view's hint.
    ASSERT_TRUE(m_commands.execute(u"debug.setView", {{u"view"_s, u"overdraw"_s}}));
    window.tick(0.016F);
    EXPECT_TRUE(dock.findChild<QToolButton*>(u"View.overdraw"_s)->isChecked());
    const auto* hint = dock.findChild<QLabel*>(u"ViewHint"_s);
    ASSERT_NE(hint, nullptr);
    EXPECT_TRUE(hint->text().contains(u"Layers per pixel"_s)) << hint->text().toStdString();
}

TEST_F(StatsDockTest, ShowsFrameAndMemory)
{
    Meow2::Editor::EditorMainWindow window{windowDeps()};
    for (int i = 0; i < 5; ++i)
    {
        window.tick(0.020F);
    }
    const QJsonObject stats = m_commands.execute(u"stats.frame", {}).value().toObject();
    EXPECT_EQ(stats.value(u"frames").toArray().size(), 5);
    EXPECT_NEAR(stats.value(u"frameMs").toDouble(), 20.0, 0.01);
    EXPECT_NEAR(stats.value(u"fps").toDouble(), 50.0, 0.1);
    EXPECT_TRUE(stats.contains(u"memory"));
    EXPECT_TRUE(stats.contains(u"entities"));

    Meow2::Editor::StatsDock& dock = window.statsDock();
    dock.refresh(true);
    const auto* frame = dock.findChild<QLabel*>(u"FrameTime"_s);
    ASSERT_NE(frame, nullptr);
    EXPECT_TRUE(frame->text().contains(u"20.00 ms"_s)) << frame->text().toStdString();
    // findChild() needs Q_OBJECT, which FrameGraph has no use for: found as a widget by its name.
    const auto* graph = static_cast<const Meow2::Editor::FrameGraph*>(dock.findChild<QWidget*>(u"FrameGraph"_s));
    ASSERT_NE(graph, nullptr);
    EXPECT_EQ(graph->frames().size(), 5U);
    const auto* table = dock.findChild<QTableWidget*>(u"MemoryTable"_s);
    ASSERT_NE(table, nullptr);
    EXPECT_EQ(table->rowCount(), stats.value(u"memory").toArray().size());
}

TEST(StatsDockFormat, BytesForPeople)
{
    EXPECT_EQ(Meow2::Editor::formatBytes(512.0), u"512 B"_s);
    EXPECT_EQ(Meow2::Editor::formatBytes(3.0 * 1024.0), u"3.00 KiB"_s);
    EXPECT_EQ(Meow2::Editor::formatBytes(48.0 * 1024.0 * 1024.0), u"48.0 MiB"_s);
    EXPECT_EQ(Meow2::Editor::formatBytes(1.25 * 1024.0 * 1024.0 * 1024.0), u"1.25 GiB"_s);
}
