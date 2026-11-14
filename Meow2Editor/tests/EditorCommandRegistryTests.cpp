/// @file
/// @brief Tests for EditorCommandRegistry and parameter validation.

#include "Commands/CommandSchema.h"
#include "Commands/EditorCommandRegistry.h"
#include "QtTestApplication.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <string>
#include <vector>

namespace
{
using namespace Qt::StringLiterals;
using namespace Meow2::Editor;

/// @brief A registry of its own (no built-in commands), with a value an action changes.
class EditorCommandRegistryTest : public Meow2::Tests::QtTest
{
protected:
    /// @brief Adds `test.set {value: integer 0..100, mode: "a"|"b"}`, an undoable action on m_value.
    void addSetCommand()
    {
        QJsonObject value = propertySchema(u"integer"_s, u"The new value."_s);
        value.insert(u"minimum", 0);
        value.insert(u"maximum", 100);
        QJsonObject mode = propertySchema(u"string"_s, u"A mode."_s);
        mode.insert(u"enum", QJsonArray{u"a"_s, u"b"_s});
        const Meow2::Result<void> added =
            m_registry.add({.name = u"test.set"_s,
                            .description = u"Sets the value."_s,
                            .parameters = objectSchema({{.name = u"value"_s, .schema = value, .required = true},
                                                        {.name = u"mode"_s, .schema = mode}})},
                           [this](const QJsonObject& parameters) -> Meow2::Result<CommandOutcome>
                           {
                               ++m_calls;
                               const int from = m_value;
                               const int to = parameters.value(u"value").toInt();
                               m_value = to;
                               return CommandOutcome{.result = to,
                                                     .undoStep = UndoStep{.label = u"Set value"_s,
                                                                          .undo = [this, from] { m_value = from; },
                                                                          .redo =
                                                                              [this, to]
                                                                          {
                                                                              m_value = to;
                                                                          }}};
                           });
        ASSERT_TRUE(added) << added.error().message;
    }

    UndoStack m_history;
    EditorCommandRegistry m_registry{m_history, m_log};
    int m_value = 0;
    int m_calls = 0;
};
} // namespace

TEST_F(EditorCommandRegistryTest, RegistersAndLists)
{
    addSetCommand();
    ASSERT_TRUE(m_registry.add({.name = u"a.query"_s, .kind = CommandKind::Query},
                               [](const QJsonObject&) -> Meow2::Result<CommandOutcome> { return CommandOutcome{}; }));
    const std::vector<CommandInfo> commands = m_registry.commands();
    ASSERT_EQ(commands.size(), 2U);
    EXPECT_EQ(commands[0].name, u"a.query"_s) << "sorted by name";
    EXPECT_EQ(commands[1].name, u"test.set"_s);
    EXPECT_EQ(commands[1].parameters.value(u"type").toString(), u"object"_s);
    EXPECT_TRUE(commands[1].parameters.value(u"properties").toObject().contains(u"value"));
    EXPECT_EQ(commands[1].parameters.value(u"required").toArray(), QJsonArray{u"value"_s});
    EXPECT_NE(m_registry.find(u"test.set"), nullptr);
    EXPECT_EQ(m_registry.find(u"test.missing"), nullptr);
}

TEST_F(EditorCommandRegistryTest, RejectsDuplicateName)
{
    addSetCommand();
    const Meow2::Result<void> again = m_registry.add(
        {.name = u"test.set"_s}, [](const QJsonObject&) -> Meow2::Result<CommandOutcome> { return CommandOutcome{}; });
    ASSERT_FALSE(again);
    EXPECT_EQ(again.error().code, Meow2::ErrorCode::AlreadyExists);
    EXPECT_FALSE(m_registry.add({}, {})) << "a command needs a name and a handler";
}

TEST_F(EditorCommandRegistryTest, UnknownCommandIsAnError)
{
    const Meow2::Result<QJsonValue> ran = m_registry.execute(u"test.missing");
    ASSERT_FALSE(ran);
    EXPECT_EQ(ran.error().code, Meow2::ErrorCode::NotFound);
}

TEST_F(EditorCommandRegistryTest, ValidatesParameters)
{
    addSetCommand();
    const QJsonObject bad[] = {
        {},                                         // value is required
        {{u"value"_s, u"ten"_s}},                   // wrong type
        {{u"value"_s, 2.5}},                        // not a whole number
        {{u"value"_s, 101}},                        // above the maximum
        {{u"value"_s, -1}},                         // below the minimum
        {{u"value"_s, 1}, {u"mode"_s, u"c"_s}},     // not in the enum
        {{u"value"_s, 1}, {u"colour"_s, u"red"_s}}, // not a parameter
    };
    for (const QJsonObject& parameters : bad)
    {
        const Meow2::Result<QJsonValue> ran = m_registry.execute(u"test.set", parameters);
        ASSERT_FALSE(ran) << QJsonDocument{parameters}.toJson().toStdString();
        EXPECT_EQ(ran.error().code, Meow2::ErrorCode::InvalidArgument);
    }
    EXPECT_EQ(m_calls, 0) << "the handler never ran with bad parameters";
    EXPECT_TRUE(m_registry.execute(u"test.set", {{u"value"_s, 3.0}, {u"mode"_s, u"b"_s}})) << "3.0 is a whole number";
}

TEST_F(EditorCommandRegistryTest, ExecuteUndoRedoRoundTrip)
{
    addSetCommand();
    const Meow2::Result<QJsonValue> ran = m_registry.execute(u"test.set", {{u"value"_s, 42}});
    ASSERT_TRUE(ran) << ran.error().message;
    EXPECT_EQ(ran->toInt(), 42) << "the handler's result comes back";
    EXPECT_EQ(m_value, 42);
    ASSERT_TRUE(m_history.canUndo());
    EXPECT_EQ(m_history.undoLabels().front(), u"Set value"_s);

    m_history.undo();
    EXPECT_EQ(m_value, 0);
    m_history.redo();
    EXPECT_EQ(m_value, 42);
}

TEST_F(EditorCommandRegistryTest, FailedCommandLeavesNoUndoStep)
{
    ASSERT_TRUE(m_registry.add({.name = u"test.fail"_s}, [](const QJsonObject&) -> Meow2::Result<CommandOutcome>
                               { return Meow2::fail(Meow2::ErrorCode::IoError, "disk full"); }));
    const auto revision = m_history.revision();
    const Meow2::Result<QJsonValue> ran = m_registry.execute(u"test.fail");
    ASSERT_FALSE(ran);
    EXPECT_EQ(ran.error().code, Meow2::ErrorCode::IoError) << "the handler's own error comes back";
    EXPECT_EQ(m_history.revision(), revision);
    EXPECT_NE(logText().find("test.fail failed"), std::string::npos) << "failures are logged";
}

TEST_F(EditorCommandRegistryTest, QueriesAreNotRecorded)
{
    // A query that (wrongly) returns an undo step still isn't recorded: queries never change the history.
    ASSERT_TRUE(m_registry.add({.name = u"test.read"_s, .kind = CommandKind::Query},
                               [](const QJsonObject&) -> Meow2::Result<CommandOutcome>
                               {
                                   return CommandOutcome{.result = 7,
                                                         .undoStep = UndoStep{.undo = [] {}, .redo = [] {}}};
                               }));
    const Meow2::Result<QJsonValue> ran = m_registry.execute(u"test.read");
    ASSERT_TRUE(ran);
    EXPECT_EQ(ran->toInt(), 7);
    EXPECT_FALSE(m_history.canUndo());
}

TEST_F(EditorCommandRegistryTest, DisabledCommandIsRefused)
{
    bool enabled = false;
    ASSERT_TRUE(m_registry.add({.name = u"test.gated"_s,
                                .isEnabled =
                                    [&enabled]
                                {
                                    return enabled;
                                }},
                               [this](const QJsonObject&) -> Meow2::Result<CommandOutcome>
                               {
                                   ++m_calls;
                                   return CommandOutcome{};
                               }));
    EXPECT_FALSE(m_registry.isEnabled(u"test.gated"));
    const Meow2::Result<QJsonValue> ran = m_registry.execute(u"test.gated");
    ASSERT_FALSE(ran);
    EXPECT_EQ(ran.error().code, Meow2::ErrorCode::Unsupported);
    EXPECT_EQ(m_calls, 0);

    enabled = true;
    EXPECT_TRUE(m_registry.isEnabled(u"test.gated"));
    EXPECT_TRUE(m_registry.execute(u"test.gated"));
    EXPECT_EQ(m_calls, 1);
    EXPECT_FALSE(m_registry.isEnabled(u"test.missing"));
    EXPECT_TRUE(m_registry.remove(u"test.gated"));
    EXPECT_FALSE(m_registry.remove(u"test.gated"));
}
