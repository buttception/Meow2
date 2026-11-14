/// @file
/// @brief Tests for SettingsDialog, the editor's preferences window.

#include "EditorTestRig.h"
#include "Panels/SettingsDialog.h"

#include <QCheckBox>
#include <QSpinBox>

namespace
{
using namespace Qt::StringLiterals;
using SettingsDialogTest = Meow2::Tests::EditorTest;
} // namespace

TEST_F(SettingsDialogTest, ChangeIssuesCommand)
{
    Meow2::Editor::SettingsDialog dialog{m_commands, m_settings};
    auto* threads = qobject_cast<QSpinBox*>(dialog.field(u"jobs.threadCount"_s));
    ASSERT_NE(threads, nullptr);
    EXPECT_EQ(threads->value(), 0) << "shows the default";
    EXPECT_EQ(threads->maximum(), 256);

    threads->setValue(6); // what the user does; the field runs settings.set
    EXPECT_EQ(m_config.getInt("jobs.threadCount", 0), 6);

    auto* follow = qobject_cast<QCheckBox*>(dialog.field(u"editor.console.followNewest"_s));
    ASSERT_NE(follow, nullptr);
    EXPECT_TRUE(follow->isChecked());
    follow->setChecked(false);
    EXPECT_FALSE(m_config.getBool("editor.console.followNewest", true));

    // A change from elsewhere (an agent over MCP) shows after refresh(), without running a command.
    ASSERT_TRUE(m_commands.execute(u"settings.set", {{u"key"_s, u"jobs.threadCount"_s}, {u"value"_s, 2}}));
    dialog.refresh();
    EXPECT_EQ(threads->value(), 2);
    EXPECT_EQ(dialog.field(u"editor.missing"_s), nullptr);
}
