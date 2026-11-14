#pragma once

/// @file
/// @brief EditorSettings: the editor preferences the settings window shows, kept in the editor's TOML config.

#include <Meow2/Core/Config/Config.h>
#include <Meow2/Core/Result.h>

#include <cstdint>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <vector>

namespace Meow2::Editor
{
/// @brief A setting's value type.
enum class SettingType
{
    Bool,   ///< On or off; a checkbox.
    Integer ///< A whole number in [minimum, maximum]; a spin box.
};

/// @brief One editor preference: its config key, how to show it, its range. Plain data.
struct SettingInfo
{
    QString key{};                           ///< Dotted config key, e.g. `editor.undoLimit`.
    QString label{};                         ///< What the settings window calls it.
    QString description{};                   ///< One sentence: what it changes (tooltip, MCP).
    SettingType type = SettingType::Integer; ///< Its type.
    std::int64_t defaultValue = 0;           ///< The value when the config has none (0 or 1 for a Bool).
    std::int64_t minimum = 0;                ///< Smallest allowed value (Integer only).
    std::int64_t maximum = 0;                ///< Largest allowed value (Integer only).
    bool needsRestart = false;               ///< Read at startup only, so a change shows after the editor restarts.
};

/// @brief The editor's preferences: a fixed list of SettingInfo, read from and written to the editor's Config.
///
/// The settings window and the `settings.*` commands both work through it, so a value set from the window, over MCP
/// or by hand in `Meow2Editor.toml` is checked the same way. set() saves the file at once: a preference shouldn't be
/// lost because the editor crashed before it exited.
/// @par Thread safety
/// Not thread-safe; the main thread only (Config isn't thread-safe either).
/// @par Lifetime
/// The Config must outlive it.
class EditorSettings
{
public:
    /// @brief The settings over a config.
    /// @param config The editor's settings file.
    explicit EditorSettings(Config::Config& config);

    /// @brief Every setting, in the order the window shows them.
    /// @return The list.
    [[nodiscard]] const std::vector<SettingInfo>& list() const
    {
        return m_settings;
    }

    /// @brief Looks a setting up.
    /// @param key Its config key.
    /// @return Its info, or null if it isn't an editor setting.
    [[nodiscard]] const SettingInfo* find(const QString& key) const;

    /// @brief A setting's current value: the config's, or its default.
    /// @param key Its config key.
    /// @return A bool or a number; null if the key isn't an editor setting.
    [[nodiscard]] QJsonValue value(const QString& key) const;

    /// @brief An integer setting's value, clamped to its range.
    /// @param key Its config key.
    /// @return The value; 0 if the key isn't an editor setting.
    [[nodiscard]] std::int64_t intValue(const QString& key) const;

    /// @brief Checks and stores a value, then saves the config file.
    /// @param key   The setting's config key.
    /// @param value A bool for a Bool setting, a whole number in range for an Integer one.
    /// @return Nothing, NotFound for a key that isn't an editor setting, InvalidArgument for a wrong type or a value
    ///         out of range (nothing is stored then), or the save's IoError (the value is still set for this run).
    Result<void> set(const QString& key, const QJsonValue& value);

    /// @brief A setting as JSON: key, label, description, type, value, default, range, needsRestart. For the
    ///        `settings.list` query.
    /// @param info The setting.
    /// @return The object.
    [[nodiscard]] QJsonObject toJson(const SettingInfo& info) const;

private:
    Config::Config& m_config;
    std::vector<SettingInfo> m_settings;
};

/// @brief `editor.undoLimit`: how many steps the undo history keeps.
inline constexpr const char* c_undoLimitKey = "editor.undoLimit";
} // namespace Meow2::Editor
