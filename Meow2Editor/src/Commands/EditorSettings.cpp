/// @file
/// @brief EditorSettings (EditorSettings.h).

#include "Commands/EditorSettings.h"

#include "Commands/UndoStack.h"

#include <Meow2/Core/Time/GameLoopConfig.h>

#include <algorithm>
#include <cmath>
#include <format>

namespace Meow2::Editor
{
using namespace Qt::StringLiterals;

namespace
{
/// @brief The editor's preferences, in the order the settings window shows them.
std::vector<SettingInfo> editorSettingList()
{
    return {
        {.key = QString::fromLatin1(c_undoLimitKey),
         .label = u"Undo steps"_s,
         .description = u"How many steps Edit → Undo can go back."_s,
         .type = SettingType::Integer,
         .defaultValue = static_cast<std::int64_t>(UndoStack::c_defaultLimit),
         .minimum = 10,
         .maximum = 10000},
        {.key = u"editor.console.followNewest"_s,
         .label = u"Console follows new lines"_s,
         .description = u"Whether the console starts scrolled to the newest line and stays there."_s,
         .type = SettingType::Bool,
         .defaultValue = 1,
         .needsRestart = true},
        {.key = u"jobs.threadCount"_s,
         .label = u"Job threads"_s,
         .description = u"Threads that run engine jobs; 0 means one per hardware thread."_s,
         .type = SettingType::Integer,
         .defaultValue = 0,
         .minimum = 0,
         .maximum = 256,
         .needsRestart = true},
        {.key = u"loop.fixedStepHz"_s,
         .label = u"Fixed steps per second"_s,
         .description = u"How often physics and gameplay simulation step."_s,
         .type = SettingType::Integer,
         .defaultValue = Time::c_defaultFixedStepHz,
         .minimum = Time::c_minFixedStepHz,
         .maximum = Time::c_maxFixedStepHz,
         .needsRestart = true},
        {.key = u"loop.maxFixedSteps"_s,
         .label = u"Most fixed steps per frame"_s,
         .description = u"Caps the catch-up steps after a slow frame."_s,
         .type = SettingType::Integer,
         .defaultValue = Time::c_defaultMaxFixedSteps,
         .minimum = 1,
         .maximum = Time::c_maxMaxFixedSteps,
         .needsRestart = true},
    };
}
} // namespace

EditorSettings::EditorSettings(Config::Config& config)
    : m_config(config)
    , m_settings(editorSettingList())
{
}

const SettingInfo* EditorSettings::find(const QString& key) const
{
    const auto found = std::ranges::find(m_settings, key, &SettingInfo::key);
    return found == m_settings.end() ? nullptr : &*found;
}

QJsonValue EditorSettings::value(const QString& key) const
{
    const SettingInfo* info = find(key);
    if (info == nullptr)
    {
        return {};
    }
    if (info->type == SettingType::Bool)
    {
        return m_config.getBool(key.toStdString(), info->defaultValue != 0);
    }
    return static_cast<double>(intValue(key)); // JSON numbers are doubles; settings stay far below 2^53
}

std::int64_t EditorSettings::intValue(const QString& key) const
{
    const SettingInfo* info = find(key);
    if (info == nullptr)
    {
        return 0;
    }
    if (info->type == SettingType::Bool)
    {
        return m_config.getBool(key.toStdString(), info->defaultValue != 0) ? 1 : 0;
    }
    // A hand-edited file may hold anything; the editor only ever sees a value in range.
    return std::clamp(m_config.getInt(key.toStdString(), info->defaultValue), info->minimum, info->maximum);
}

Result<void> EditorSettings::set(const QString& key, const QJsonValue& value)
{
    const SettingInfo* info = find(key);
    if (info == nullptr)
    {
        return fail(ErrorCode::NotFound, std::format("'{}' isn't an editor setting", key.toStdString()));
    }
    const std::string keyText = key.toStdString();
    if (info->type == SettingType::Bool)
    {
        if (!value.isBool())
        {
            return fail(ErrorCode::InvalidArgument, std::format("{} takes true or false", keyText));
        }
        m_config.setBool(keyText, value.toBool());
    }
    else
    {
        const double number = value.toDouble(std::nan(""));
        if (!value.isDouble() || std::trunc(number) != number || number < static_cast<double>(info->minimum) ||
            number > static_cast<double>(info->maximum))
        {
            return fail(ErrorCode::InvalidArgument,
                        std::format("{} takes a whole number from {} to {}", keyText, info->minimum, info->maximum));
        }
        m_config.setInt(keyText, static_cast<std::int64_t>(number));
    }
    return m_config.save();
}

QJsonObject EditorSettings::toJson(const SettingInfo& info) const
{
    QJsonObject json{{u"key"_s, info.key},
                     {u"label"_s, info.label},
                     {u"description"_s, info.description},
                     {u"type"_s, info.type == SettingType::Bool ? u"boolean"_s : u"integer"_s},
                     {u"value"_s, value(info.key)},
                     {u"needsRestart"_s, info.needsRestart}};
    if (info.type == SettingType::Bool)
    {
        json.insert(u"default", info.defaultValue != 0);
    }
    else
    {
        json.insert(u"default", static_cast<double>(info.defaultValue));
        json.insert(u"minimum", static_cast<double>(info.minimum));
        json.insert(u"maximum", static_cast<double>(info.maximum));
    }
    return json;
}
} // namespace Meow2::Editor
