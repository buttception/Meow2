#pragma once

/// @file
/// @brief DebugDock: the editor viewport's rendering debug tools (debug views, wireframes, frozen culling, the grid).

#include "Commands/EditorCommandRegistry.h"

#include <Meow2/Core/Log/Logger.h>

#include <QDockWidget>
#include <QJsonObject>
#include <QString>
#include <utility>
#include <vector>

class QCheckBox;
class QLabel;
class QRadioButton;
class QToolButton;

namespace Meow2::Editor
{
/// @brief A dock with the editor viewport's debug tools: a button per debug view, grouped (Geometry, Surface,
///        Material, Lighting), the wireframe mode, frozen culling and its frustum, and the ground grid.
///
/// Every click runs a `debug.*` command, and refresh() shows what `debug.state` says, so a change from MCP shows here
/// too. A view made by a debug-only pass (Position) has a dashed outline, as in Meow2Renderer.
/// @par Thread safety
/// Not thread-safe; the GUI thread.
class DebugDock final : public QDockWidget
{
public:
    /// @brief The dock and its buttons.
    /// @param commands Runs the `debug.*` commands and the `debug.state` query.
    /// @param log      Receives failed commands.
    /// @param parent   The main window.
    DebugDock(EditorCommandRegistry& commands, Log::Logger& log, QWidget* parent = nullptr);

    /// @brief Shows the current state, if it changed since the last call.
    void refresh();

private:
    /// @brief Runs a command; a failure is logged.
    void run(const QString& command, const QJsonObject& parameters);

    /// @brief Sets the buttons from a `debug.state` answer.
    void show(const QJsonObject& state);

    EditorCommandRegistry& m_commands;
    Log::Logger& m_log;
    QJsonObject m_shown;                                        ///< The state the buttons show.
    std::vector<std::pair<QString, QToolButton*>> m_views;      ///< By view key; Qt owns the buttons.
    std::vector<std::pair<QString, QRadioButton*>> m_wireframe; ///< By mode name.
    QCheckBox* m_freeze = nullptr;
    QCheckBox* m_showFrustum = nullptr;
    QCheckBox* m_grid = nullptr;
    QLabel* m_hint = nullptr;   ///< How to read the chosen view.
    QLabel* m_failed = nullptr; ///< Shown when a debug pipeline couldn't be built.
};
} // namespace Meow2::Editor
