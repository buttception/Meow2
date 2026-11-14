/// @file
/// @brief The built-in commands (EditorCommands.h).

#include "Commands/EditorCommands.h"

#include "Commands/CommandSchema.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <QJsonArray>
#include <QStringList>
#include <string_view>
#include <vector>

namespace Meow2::Editor
{
using namespace Qt::StringLiterals;

namespace
{
/// @brief Level names as the log file writes them, in Log::LogLevel's order.
constexpr std::array<std::string_view, 5> c_levelNames{"debug", "info", "warn", "error", "fatal"};

/// @brief The most lines one `log.lines` call returns.
constexpr std::int64_t c_maxLogLines = 2000;

/// @brief A level's name as a QString.
QString levelName(Log::LogLevel level)
{
    const std::string_view name = c_levelNames[static_cast<std::size_t>(level)];
    return QString::fromLatin1(name.data(), static_cast<qsizetype>(name.size()));
}

/// @brief The level names as a JSON array, for the `log.lines` schema's enum.
QJsonArray levelNameArray()
{
    QJsonArray names;
    for (std::string_view name : c_levelNames)
    {
        names.append(QString::fromLatin1(name.data(), static_cast<qsizetype>(name.size())));
    }
    return names;
}

Result<void> addUndoRedo(const EditorCommandsDeps& deps)
{
    UndoStack& history = deps.history;
    if (Result<void> added = deps.commands.add({.name = u"edit.undo"_s,
                                                .description = u"Undoes the last change."_s,
                                                .isEnabled =
                                                    [&history]
                                                {
                                                    return history.canUndo();
                                                }},
                                               [&history](const QJsonObject&) -> Result<CommandOutcome>
                                               {
                                                   history.undo();
                                                   return CommandOutcome{};
                                               });
        !added)
    {
        return added;
    }
    return deps.commands.add({.name = u"edit.redo"_s,
                              .description = u"Redoes the last undone change."_s,
                              .isEnabled =
                                  [&history]
                              {
                                  return history.canRedo();
                              }},
                             [&history](const QJsonObject&) -> Result<CommandOutcome>
                             {
                                 history.redo();
                                 return CommandOutcome{};
                             });
}

Result<void> addSettings(const EditorCommandsDeps& deps)
{
    EditorSettings& settings = deps.settings;
    UndoStack& history = deps.history;
    if (Result<void> added = deps.commands.add(
            {.name = u"settings.set"_s,
             .description = u"Changes an editor setting (settings.list shows them) and saves the settings file."_s,
             .parameters = objectSchema(
                 {{.name = u"key"_s, .schema = propertySchema(u"string"_s, u"The setting's key."_s), .required = true},
                  {.name = u"value"_s,
                   .schema = propertySchema({}, u"Its new value: true/false or a number."_s),
                   .required = true}})},
            [&settings, &history](const QJsonObject& parameters) -> Result<CommandOutcome>
            {
                const QString key = parameters.value(u"key").toString();
                if (Result<void> stored = settings.set(key, parameters.value(u"value")); !stored)
                {
                    return fail(stored.error().code, stored.error().message);
                }
                if (key == QLatin1StringView{c_undoLimitKey})
                {
                    history.setLimit(static_cast<std::size_t>(settings.intValue(key)));
                }
                return CommandOutcome{.result = settings.value(key)};
            });
        !added)
    {
        return added;
    }
    return deps.commands.add({.name = u"settings.list"_s,
                              .description = u"Lists the editor settings with their values, defaults and ranges."_s,
                              .kind = CommandKind::Query},
                             [&settings](const QJsonObject&) -> Result<CommandOutcome>
                             {
                                 QJsonArray list;
                                 for (const SettingInfo& info : settings.list())
                                 {
                                     list.append(settings.toJson(info));
                                 }
                                 return CommandOutcome{.result = list};
                             });
}

Result<void> addHistory(const EditorCommandsDeps& deps)
{
    UndoStack& history = deps.history;
    return deps.commands.add(
        {.name = u"history.get"_s,
         .description = u"The undo history: what can be undone and redone (next first), and whether it's saved."_s,
         .kind = CommandKind::Query},
        [&history](const QJsonObject&) -> Result<CommandOutcome>
        {
            return CommandOutcome{.result = QJsonObject{{u"undo"_s, QJsonArray::fromStringList(history.undoLabels())},
                                                        {u"redo"_s, QJsonArray::fromStringList(history.redoLabels())},
                                                        {u"clean"_s, history.isClean()}}};
        });
}

Result<void> addLog(const EditorCommandsDeps& deps)
{
    const Log::ConsoleBuffer& console = deps.console;
    QJsonObject levelsSchema = propertySchema(u"array"_s, u"Only these levels; all if left out."_s);
    levelsSchema.insert(u"items", QJsonObject{{u"type"_s, u"string"_s}, {u"enum"_s, levelNameArray()}});
    QJsonObject sinceSchema =
        propertySchema(u"integer"_s, u"The first sequence to return: 0, or `next` of the last call."_s);
    sinceSchema.insert(u"minimum", 0);
    QJsonObject limitSchema =
        propertySchema(u"integer"_s, u"The most lines to return (the newest); 200 if left out."_s);
    limitSchema.insert(u"minimum", 1);
    limitSchema.insert(u"maximum", static_cast<double>(c_maxLogLines));
    return deps.commands.add(
        {.name = u"log.lines"_s,
         .description = u"Recent log lines, oldest first, with the sequence to pass as `since` next time."_s,
         .kind = CommandKind::Query,
         .parameters = objectSchema(
             {{.name = u"since"_s, .schema = sinceSchema},
              {.name = u"levels"_s, .schema = levelsSchema},
              {.name = u"text"_s,
               .schema = propertySchema(u"string"_s, u"Only lines whose message or logger contain it (any case)."_s)},
              {.name = u"limit"_s, .schema = limitSchema}})},
        [&console](const QJsonObject& parameters) -> Result<CommandOutcome>
        {
            std::vector<Log::LogLine> lines;
            const auto since = static_cast<std::uint64_t>(parameters.value(u"since").toDouble(0));
            const std::uint64_t missed = console.linesSince(since, lines);
            const QStringList levels = parameters.value(u"levels").toVariant().toStringList();
            const QString text = parameters.value(u"text").toString();
            const auto limit = static_cast<qsizetype>(parameters.value(u"limit").toDouble(200));

            QJsonArray result;
            for (const Log::LogLine& line : lines)
            {
                const QString level = levelName(line.level);
                const QString message = QString::fromStdString(line.message);
                const QString logger = QString::fromStdString(line.logger);
                if ((!levels.isEmpty() && !levels.contains(level)) ||
                    (!message.contains(text, Qt::CaseInsensitive) && !logger.contains(text, Qt::CaseInsensitive)))
                {
                    continue;
                }
                result.append(QJsonObject{{u"sequence"_s, static_cast<double>(line.sequence)},
                                          {u"time"_s, QString::fromStdString(line.time)},
                                          {u"level"_s, level},
                                          {u"logger"_s, logger},
                                          {u"message"_s, message}});
            }
            while (result.size() > limit)
            {
                result.removeFirst(); // keep the newest
            }
            const std::uint64_t next = lines.empty() ? since + missed : lines.back().sequence + 1;
            return CommandOutcome{.result = QJsonObject{{u"lines"_s, result},
                                                        {u"next"_s, static_cast<double>(next)},
                                                        {u"missed"_s, static_cast<double>(missed)}}};
        });
}
} // namespace

Result<void> registerEditorCommands(const EditorCommandsDeps& deps)
{
    for (Result<void> (*add)(const EditorCommandsDeps&) : {addUndoRedo, addSettings, addHistory, addLog})
    {
        if (Result<void> added = add(deps); !added)
        {
            return added;
        }
    }
    return {};
}
} // namespace Meow2::Editor
