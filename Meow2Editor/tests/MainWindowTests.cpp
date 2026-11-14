/// @file
/// @brief Tests for EditorMainWindow's menus, toolbar and panels.

#include "EditorTestRig.h"
#include "Panels/ConsoleDock.h"
#include "Panels/SettingsDialog.h"

#include <QAction>
#include <QPlainTextEdit>

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
    QAction* save = window.actionFor(u"scene.save"_s);
    ASSERT_NE(undo, nullptr);
    ASSERT_NE(redo, nullptr);
    ASSERT_NE(save, nullptr);
    EXPECT_FALSE(undo->isEnabled()) << "nothing to undo";
    EXPECT_FALSE(save->isEnabled()) << "scene.save doesn't exist until scenes do";
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

    // Save lights up as soon as the command exists: buttons follow the registry, not a hard-coded list.
    ASSERT_TRUE(m_commands.add({.name = u"scene.save"_s},
                               [](const QJsonObject&) -> Meow2::Result<Meow2::Editor::CommandOutcome>
                               { return Meow2::Editor::CommandOutcome{}; }));
    window.tick();
    EXPECT_TRUE(save->isEnabled());
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
        EXPECT_FALSE(m_commands.execute(u"view.showPanel", {{u"panel"_s, u"hierarchy"_s}, {u"visible"_s, true}}))
            << "not a panel (yet)";

        m_console.add(Meow2::Log::LogLevel::Info, "10:00:00.000", "Editor", "Hello");
        window.tick();
        EXPECT_EQ(window.console().view().toPlainText(), u"[10:00:00.000]<info> Editor: Hello"_s)
            << "the window's tick feeds the console";
    }
    EXPECT_EQ(m_commands.find(u"view.showPanel"), nullptr) << "removed with the window";
}
