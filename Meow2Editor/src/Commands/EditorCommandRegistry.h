#pragma once

/// @file
/// @brief EditorCommandRegistry: every editor action and query, by name, with its parameter schema (techstack.md §20).

#include "Commands/UndoStack.h"

#include <Meow2/Core/Log/Logger.h>
#include <Meow2/Core/Result.h>

#include <functional>
#include <map>
#include <optional>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QStringView>
#include <vector>

namespace Meow2::Editor
{
/// @brief Whether a command changes the editor or only reads it.
enum class CommandKind
{
    Action, ///< Changes something; may return an UndoStep, which the registry records.
    Query   ///< Only reads (scene hierarchy, settings, log); never recorded. MCP lists both as tools.
};

/// @brief What a command is, for menus, the tool list of MCP (PR 6) and parameter checks. Plain data.
struct CommandInfo
{
    QString name{};                         ///< Unique, dotted: `<area>.<verb>`, e.g. `edit.undo`, `settings.set`.
    QString description{};                  ///< One sentence for people and AI agents: what it does.
    CommandKind kind = CommandKind::Action; ///< Action or query.
    QJsonObject parameters{};               ///< JSON Schema of its parameters (CommandSchema.h); empty: none.
    std::function<bool()> isEnabled{};      ///< Whether it can run now (e.g. undo needs a step); unset: always.
};

/// @brief What running a command produced. Plain data.
struct CommandOutcome
{
    QJsonValue result{};                ///< What it returns to the caller (a query's answer); null if nothing.
    std::optional<UndoStep> undoStep{}; ///< For an action that changed something undoable: how to undo and redo it.
};

/// @brief Runs a command with checked parameters. Returns its outcome, or an error if it couldn't do it (then it
///        must have changed nothing).
using CommandHandler = std::function<Result<CommandOutcome>(const QJsonObject& parameters)>;

/// @brief Every editor command by name, and the one way to run them.
///
/// Menus, toolbar buttons, shortcuts, the settings window and (PR 6) MCP all call execute() with a name and JSON
/// parameters, so anything a user can do in the UI an agent can do too. execute() checks the parameters against the
/// command's schema, runs it, and records the UndoStep an action returns in the UndoStack.
/// @par Thread safety
/// Not thread-safe; the editor's main thread only. MCP requests arrive on a network thread and are handed to the
/// main thread before they get here (techstack.md §20).
/// @par Lifetime
/// The UndoStack and the Logger must outlive it; handlers must not outlive what they capture.
class EditorCommandRegistry
{
public:
    /// @brief An empty registry.
    /// @param history Where actions' undo steps are recorded.
    /// @param log     Receives a line per action run and per failure.
    EditorCommandRegistry(UndoStack& history, Log::Logger& log);

    /// @brief Adds a command.
    /// @param info    What it is; `name` must be new and non-empty.
    /// @param handler What it does; must be set.
    /// @return Nothing, ErrorCode::AlreadyExists if the name is taken, or InvalidArgument for an empty name or
    ///         handler.
    Result<void> add(CommandInfo info, CommandHandler handler);

    /// @brief Removes a command, e.g. one a window registered, when the window goes away.
    /// @param name The command's name.
    /// @return `true` if it was registered.
    bool remove(QStringView name);

    /// @brief Runs a command: checks it exists, is enabled and gets valid parameters, then calls it and records its
    ///        undo step.
    /// @param name       The command's name.
    /// @param parameters Its parameters; checked against CommandInfo::parameters before the handler runs.
    /// @return The command's result, or NotFound (no such command), Unsupported (not enabled now),
    ///         InvalidArgument (bad parameters) or the handler's own error. On any error nothing is recorded.
    Result<QJsonValue> execute(QStringView name, const QJsonObject& parameters = {});

    /// @brief Looks a command up.
    /// @param name The command's name.
    /// @return Its info, or null if there's no such command. Valid until the registry is destroyed.
    [[nodiscard]] const CommandInfo* find(QStringView name) const;

    /// @brief Whether a command exists and can run now.
    /// @param name The command's name.
    /// @return `false` if it's missing or its isEnabled() says no.
    [[nodiscard]] bool isEnabled(QStringView name) const;

    /// @brief Every command, sorted by name: what MCP's `tools/list` will return.
    /// @return Copies of their infos.
    [[nodiscard]] std::vector<CommandInfo> commands() const;

    /// @brief The history actions are recorded in.
    /// @return The UndoStack given to the constructor.
    [[nodiscard]] UndoStack& history()
    {
        return m_history;
    }

private:
    /// @brief A registered command.
    struct Entry
    {
        CommandInfo info;       ///< What it is.
        CommandHandler handler; ///< What it does.
    };

    UndoStack& m_history;
    Log::Logger& m_log;
    std::map<QString, Entry> m_commands; ///< By name; sorted, so commands() needs no sort.
};
} // namespace Meow2::Editor
