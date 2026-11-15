#pragma once

/// @file
/// @brief EditorMainWindow: the editor's top-level window, its menus, toolbar and docks.

#include "Commands/EditorCommandRegistry.h"
#include "Commands/EditorSettings.h"
#include "Viewports/EditorRenderer.h"
#include "Viewports/EditorWorld.h"
#include "ViewportWidget.h"

#include <Meow2/Core/Log/ConsoleBuffer.h>
#include <Meow2/Core/Log/Logger.h>
#include <Meow2/Platform/Input/InputSystem.h>

#include <cstdint>
#include <memory>
#include <QJsonObject>
#include <QMainWindow>
#include <QString>
#include <vector>

class QAction;

namespace Meow2::Editor
{
class ConsoleDock;
class GameViewportDock;
class QtInputSource;
class SettingsDialog;
class ViewportController;

/// @brief What the main window works with. Plain data (references), filled by the composition root.
struct EditorMainWindowDeps
{
    EditorCommandRegistry& commands;    ///< Every menu item, toolbar button and shortcut runs a command here.
    EditorSettings& settings;           ///< Shown by the settings window.
    const Log::ConsoleBuffer& console;  ///< Shown by the console dock.
    Log::Logger& log;                   ///< Receives commands that fail when the user triggers them.
    EditorWorld& world;                 ///< The scene the viewports show.
    Input::InputSystem& input;          ///< Fed by the viewports' keyboard and mouse (QtInputSource).
    EditorRenderer* renderer = nullptr; ///< Draws the viewports; null without a GPU (offscreen platform, tests).
};

/// @brief The editor's main window: the editor viewport in the middle, the game viewport and console docks, menus
///        and a toolbar.
///
/// **It never changes editor state itself** (techstack.md §20): every menu item, toolbar button and shortcut runs a
/// command through the EditorCommandRegistry, so an agent can do the same over MCP. A button is enabled exactly when
/// its command exists and its isEnabled() says yes; tick() refreshes that every frame, so the Save button lights up by
/// itself once `scene.save` exists (PR 3).
///
/// It registers `view.showPanel {panel, visible}` (the console and game docks, the settings window), and its
/// ViewportController the `viewport.*` commands; both are removed when it's destroyed. The dock layout is restored
/// from `editor.layout` and saved there by saveLayout().
/// @par Thread safety
/// Not thread-safe; GUI thread only, like every QWidget.
/// @par Lifetime
/// Everything in EditorMainWindowDeps must outlive it.
class EditorMainWindow final : public QMainWindow
{
public:
    /// @brief Creates the window with its viewport, console dock, menus and toolbar. It isn't shown until
    ///        showWithDesc() (WindowPlacement.h).
    /// @param deps   What it works with.
    /// @param parent Owning widget; null for a top-level window.
    explicit EditorMainWindow(const EditorMainWindowDeps& deps, QWidget* parent = nullptr);

    /// @brief Removes `view.showPanel` from the registry.
    ~EditorMainWindow() override;

    EditorMainWindow(const EditorMainWindow&) = delete;            ///< A window isn't copied.
    EditorMainWindow& operator=(const EditorMainWindow&) = delete; ///< A window isn't copied.

    /// @brief Once per frame (the editor's frame timer): new console lines, the viewports (camera, scene, drawing),
    ///        and the actions' state.
    /// @param deltaSeconds The frame's length, for the camera.
    void tick(float deltaSeconds = 0.0F);

    /// @brief Stores the dock layout in the settings (`editor.layout`), to restore at the next start.
    void saveLayout();

    /// @brief The game viewport's dock.
    /// @return The dock, owned by this window.
    [[nodiscard]] GameViewportDock& gameDock();

    /// @brief The viewports' controller (camera, drawing, `viewport.*` commands).
    /// @return It, owned by this window.
    [[nodiscard]] ViewportController& viewports();

    /// @brief The viewport in the middle of the window.
    /// @return The viewport, owned by this window.
    [[nodiscard]] ViewportWidget& viewport();

    /// @brief The console dock.
    /// @return The dock, owned by this window.
    [[nodiscard]] ConsoleDock& console();

    /// @brief The settings window (hidden until opened).
    /// @return The window, owned by this one.
    [[nodiscard]] SettingsDialog& settingsDialog();

    /// @brief The action that runs a command, for tests and shortcuts.
    /// @param command The command's name, e.g. `edit.undo`.
    /// @return The first menu or toolbar action bound to it, or null.
    [[nodiscard]] QAction* actionFor(const QString& command) const;

private:
    /// @brief A menu or toolbar action and the command it runs.
    struct CommandAction
    {
        QAction* action = nullptr; ///< Owned by Qt (parent–child).
        QString command;           ///< The command's name.
    };

    /// @brief Creates an action that runs `command` with `parameters` when triggered.
    QAction* addCommandAction(const QString& text, const QString& command, const QJsonObject& parameters = {});

    /// @brief Runs a command for the user; a failure is logged (the UI has nowhere better to show it yet).
    void run(const QString& command, const QJsonObject& parameters);

    /// @brief Builds the menus and the toolbar from the command actions.
    void createMenusAndToolbar();

    /// @brief Registers `view.showPanel`.
    void registerViewCommands();

    /// @brief Refreshes the actions: enabled state, Undo/Redo labels, checked panels and camera mode, the render scale.
    void refreshActions();

    EditorCommandRegistry& m_commands;
    Log::Logger& m_log;
    ViewportWidget* m_viewport;       ///< Owned by Qt (parent–child), never null.
    ConsoleDock* m_console;           ///< Owned by Qt, never null.
    SettingsDialog* m_settingsDialog; ///< Owned by Qt, never null.
    GameViewportDock* m_gameDock;     ///< Owned by Qt, never null.
    QtInputSource* m_inputSource;     ///< Owned by Qt, never null.
    EditorSettings& m_settings;
    std::unique_ptr<ViewportController> m_viewports;     ///< After the widgets it drives.
    QAction* m_gameAction = nullptr;                     ///< Checked while the game dock is shown.
    QAction* m_orbitAction = nullptr;                    ///< Checked in Orbit mode.
    QAction* m_flyAction = nullptr;                      ///< Checked in Fly mode.
    std::vector<CommandAction> m_commandActions;         ///< Every action bound to a command.
    QAction* m_undoAction = nullptr;                     ///< Its text names the step it undoes.
    QAction* m_redoAction = nullptr;                     ///< Its text names the step it redoes.
    QAction* m_consoleAction = nullptr;                  ///< Checked while the console dock is shown.
    std::uint64_t m_historyRevision = ~std::uint64_t{0}; ///< The history's revision the labels show.
};
} // namespace Meow2::Editor
