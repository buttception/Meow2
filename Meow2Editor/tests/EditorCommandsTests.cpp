/// @file
/// @brief Tests for the built-in editor commands (EditorCommands.h).

#include "EditorTestRig.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <QJsonArray>
#include <string>

namespace
{
using namespace Qt::StringLiterals;
using EditorCommandsTest = Meow2::Tests::EditorTest;

/// @brief Runs `settings.set {key, value}`.
Meow2::Result<QJsonValue> setSetting(Meow2::Editor::EditorCommandRegistry& commands, const QString& key,
                                     const QJsonValue& value)
{
    return commands.execute(u"settings.set", {{u"key"_s, key}, {u"value"_s, value}});
}
} // namespace

TEST_F(EditorCommandsTest, SettingsSetValidatesAndSaves)
{
    const Meow2::Result<QJsonValue> set = setSetting(m_commands, u"jobs.threadCount"_s, 4);
    ASSERT_TRUE(set) << set.error().message;
    EXPECT_EQ(set->toInt(), 4) << "returns the stored value";
    EXPECT_EQ(m_config.getInt("jobs.threadCount", 0), 4);
    std::string saved;
    {
        // Closed again before the next save: Windows can't replace a file that's open.
        std::ifstream file(m_config.path());
        saved.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    }
    EXPECT_NE(saved.find("threadCount = 4"), std::string::npos) << "saved at once:\n" << saved;

    const Meow2::Result<QJsonValue> follow = setSetting(m_commands, u"editor.console.followNewest"_s, false);
    ASSERT_TRUE(follow) << follow.error().message;
    EXPECT_FALSE(m_config.getBool("editor.console.followNewest", true));

    const Meow2::Result<QJsonValue> unknown = setSetting(m_commands, u"editor.colour"_s, 1);
    ASSERT_FALSE(unknown);
    EXPECT_EQ(unknown.error().code, Meow2::ErrorCode::NotFound);
    for (const QJsonValue& bad : {QJsonValue{999}, QJsonValue{1.5}, QJsonValue{u"4"_s}})
    {
        const Meow2::Result<QJsonValue> refused = setSetting(m_commands, u"jobs.threadCount"_s, bad);
        ASSERT_FALSE(refused);
        EXPECT_EQ(refused.error().code, Meow2::ErrorCode::InvalidArgument);
    }
    EXPECT_FALSE(setSetting(m_commands, u"editor.console.followNewest"_s, 1)) << "a bool takes true or false";
    EXPECT_EQ(m_config.getInt("jobs.threadCount", 0), 4) << "a refused value changes nothing";
    EXPECT_FALSE(m_history.canUndo()) << "preferences aren't in the undo history";

    const Meow2::Result<QJsonValue> list = m_commands.execute(u"settings.list");
    ASSERT_TRUE(list);
    bool found = false;
    for (const QJsonValue entry : list->toArray())
    {
        if (entry[u"key"].toString() == u"jobs.threadCount")
        {
            found = true;
            EXPECT_EQ(entry[u"value"].toInt(), 4);
            EXPECT_EQ(entry[u"maximum"].toInt(), 256);
            EXPECT_TRUE(entry[u"needsRestart"].toBool());
        }
    }
    EXPECT_TRUE(found);
}

TEST_F(EditorCommandsTest, UndoLimitAppliesAtOnce)
{
    ASSERT_TRUE(setSetting(m_commands, u"editor.undoLimit"_s, 25));
    EXPECT_EQ(m_history.limit(), 25U);
    EXPECT_FALSE(setSetting(m_commands, u"editor.undoLimit"_s, 1)) << "below the minimum of 10";
    EXPECT_EQ(m_history.limit(), 25U);
}

TEST_F(EditorCommandsTest, UndoRedoCommands)
{
    const Meow2::Result<QJsonValue> nothing = m_commands.execute(u"edit.undo");
    ASSERT_FALSE(nothing) << "nothing to undo";
    EXPECT_EQ(nothing.error().code, Meow2::ErrorCode::Unsupported);
    EXPECT_FALSE(m_commands.isEnabled(u"edit.redo"));

    pushValueStep(0, 5);
    ASSERT_TRUE(m_commands.execute(u"edit.undo"));
    EXPECT_EQ(m_value, 0);
    EXPECT_TRUE(m_commands.isEnabled(u"edit.redo"));
    ASSERT_TRUE(m_commands.execute(u"edit.redo"));
    EXPECT_EQ(m_value, 5);
    EXPECT_FALSE(m_history.canRedo()) << "undo and redo themselves aren't recorded";

    const Meow2::Result<QJsonValue> history = m_commands.execute(u"history.get");
    ASSERT_TRUE(history);
    EXPECT_EQ((*history)[u"undo"].toArray(), QJsonArray{u"Set value to 5"_s});
    EXPECT_FALSE((*history)[u"clean"].toBool());
}

TEST_F(EditorCommandsTest, LogLinesQuery)
{
    m_console.add(Meow2::Log::LogLevel::Info, "10:00:00.000", "Engine", "Started");
    m_console.add(Meow2::Log::LogLevel::Warn, "10:00:01.000", "Editor", "Disk almost full");
    m_console.add(Meow2::Log::LogLevel::Error, "10:00:02.000", "Engine", "Shader failed");

    const Meow2::Result<QJsonValue> all = m_commands.execute(u"log.lines");
    ASSERT_TRUE(all) << all.error().message;
    EXPECT_EQ((*all)[u"lines"].toArray().size(), 3);
    EXPECT_EQ((*all)[u"next"].toInt(), 3);

    const Meow2::Result<QJsonValue> warnings =
        m_commands.execute(u"log.lines", {{u"levels"_s, QJsonArray{u"warn"_s, u"error"_s}}, {u"since"_s, 1}});
    ASSERT_TRUE(warnings) << warnings.error().message;
    const QJsonArray lines = (*warnings)[u"lines"].toArray();
    ASSERT_EQ(lines.size(), 2);
    EXPECT_EQ(lines[0][u"message"].toString(), u"Disk almost full"_s);
    EXPECT_EQ(lines[0][u"level"].toString(), u"warn"_s);
    EXPECT_EQ(lines[1][u"sequence"].toInt(), 2);

    const Meow2::Result<QJsonValue> text = m_commands.execute(u"log.lines", {{u"text"_s, u"SHADER"_s}});
    ASSERT_TRUE(text);
    EXPECT_EQ((*text)[u"lines"].toArray().size(), 1) << "the text filter ignores case";

    const Meow2::Result<QJsonValue> newest = m_commands.execute(u"log.lines", {{u"limit"_s, 1}});
    ASSERT_TRUE(newest);
    EXPECT_EQ((*newest)[u"lines"].toArray()[0][u"message"].toString(), u"Shader failed"_s) << "keeps the newest";

    const Meow2::Result<QJsonValue> none = m_commands.execute(u"log.lines", {{u"since"_s, 3}});
    ASSERT_TRUE(none);
    EXPECT_TRUE((*none)[u"lines"].toArray().isEmpty());
    EXPECT_EQ((*none)[u"next"].toInt(), 3);

    // Array items aren't checked by the schema subset (CommandSchema.h): an unknown level just matches nothing.
    const Meow2::Result<QJsonValue> unknown = m_commands.execute(u"log.lines", {{u"levels"_s, QJsonArray{u"loud"_s}}});
    ASSERT_TRUE(unknown);
    EXPECT_TRUE((*unknown)[u"lines"].toArray().isEmpty());
}
