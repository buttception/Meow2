/// @file
/// @brief EditorCommandRegistry (EditorCommandRegistry.h).

#include "Commands/EditorCommandRegistry.h"

#include "Commands/CommandSchema.h"

#include <format>
#include <QJsonDocument>
#include <utility>

namespace Meow2::Editor
{
EditorCommandRegistry::EditorCommandRegistry(UndoStack& history, Log::Logger& log)
    : m_history(history)
    , m_log(log)
{
}

Result<void> EditorCommandRegistry::add(CommandInfo info, CommandHandler handler)
{
    if (info.name.isEmpty() || !handler)
    {
        return fail(ErrorCode::InvalidArgument, "a command needs a name and a handler");
    }
    if (m_commands.contains(info.name))
    {
        return fail(ErrorCode::AlreadyExists,
                    std::format("command '{}' is already registered", info.name.toStdString()));
    }
    QString name = info.name;
    m_commands.emplace(std::move(name), Entry{.info = std::move(info), .handler = std::move(handler)});
    return {};
}

Result<QJsonValue> EditorCommandRegistry::execute(QStringView name, const QJsonObject& parameters)
{
    const auto found = m_commands.find(name.toString());
    if (found == m_commands.end())
    {
        return fail(ErrorCode::NotFound, std::format("no command '{}'", name.toString().toStdString()));
    }
    const Entry& entry = found->second;
    const std::string commandName = entry.info.name.toStdString();
    if (entry.info.isEnabled && !entry.info.isEnabled())
    {
        return fail(ErrorCode::Unsupported, std::format("{} can't run now", commandName));
    }
    if (Result<void> valid = validateParameters(entry.info.parameters, parameters); !valid)
    {
        MEOW2_LOG_WARN(m_log, "Command {}: {}", commandName, valid.error());
        return fail(valid.error().code, std::format("{}: {}", commandName, valid.error().message));
    }

    Result<CommandOutcome> outcome = entry.handler(parameters);
    if (!outcome)
    {
        MEOW2_LOG_WARN(m_log, "Command {} failed: {}", commandName, outcome.error());
        return fail(outcome.error().code, outcome.error().message);
    }
    if (entry.info.kind == CommandKind::Action)
    {
        // Logged with its parameters, so the log reads as a history of what the user (or an agent) did.
        MEOW2_LOG_DEBUG(m_log, "Command {} {}", commandName,
                        QJsonDocument{parameters}.toJson(QJsonDocument::Compact).toStdString());
        if (outcome->undoStep)
        {
            m_history.push(std::move(*outcome->undoStep));
        }
    }
    return std::move(outcome->result);
}

bool EditorCommandRegistry::remove(QStringView name)
{
    return m_commands.erase(name.toString()) > 0;
}

const CommandInfo* EditorCommandRegistry::find(QStringView name) const
{
    const auto found = m_commands.find(name.toString());
    return found == m_commands.end() ? nullptr : &found->second.info;
}

bool EditorCommandRegistry::isEnabled(QStringView name) const
{
    const CommandInfo* info = find(name);
    return info != nullptr && (!info->isEnabled || info->isEnabled());
}

std::vector<CommandInfo> EditorCommandRegistry::commands() const
{
    std::vector<CommandInfo> infos;
    infos.reserve(m_commands.size());
    for (const auto& [name, entry] : m_commands)
    {
        infos.push_back(entry.info);
    }
    return infos;
}
} // namespace Meow2::Editor
