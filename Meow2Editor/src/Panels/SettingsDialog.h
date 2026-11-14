#pragma once

/// @file
/// @brief SettingsDialog: the editor's preferences window.

#include "Commands/EditorCommandRegistry.h"
#include "Commands/EditorSettings.h"

#include <map>
#include <QDialog>
#include <QJsonValue>
#include <QString>

class QLabel;
class QWidget;

namespace Meow2::Editor
{
/// @brief A non-modal window with one field per editor setting (EditorSettings::list()).
///
/// It never writes a setting itself: a changed field runs the `settings.set` command, which checks the value and
/// saves the file, so the window and an agent over MCP change settings the same way. refresh() reads the values back
/// (after an agent changed one, or a value was refused).
/// @par Thread safety
/// Not thread-safe; GUI thread only, like every QWidget.
/// @par Lifetime
/// The registry and the settings must outlive it.
class SettingsDialog final : public QDialog
{
public:
    /// @brief Builds the fields from the settings' list.
    /// @param commands Where `settings.set` runs.
    /// @param settings What the fields show.
    /// @param parent   Owning widget, usually the main window.
    SettingsDialog(EditorCommandRegistry& commands, const EditorSettings& settings, QWidget* parent = nullptr);

    /// @brief Shows every setting's current value without running any command.
    void refresh();

    /// @brief A setting's field, for tests: a QSpinBox for an integer, a QCheckBox for a bool.
    /// @param key The setting's key.
    /// @return The field, or null if there's no such setting.
    [[nodiscard]] QWidget* field(const QString& key) const;

private:
    /// @brief Runs `settings.set` for one field; shows the error under the fields if it's refused.
    void apply(const QString& key, const QJsonValue& value);

    EditorCommandRegistry& m_commands;
    const EditorSettings& m_settings;
    std::map<QString, QWidget*> m_fields; ///< By key; owned by Qt (parent–child).
    QLabel* m_status = nullptr;           ///< The last error, or a restart reminder. Owned by Qt.
};
} // namespace Meow2::Editor
