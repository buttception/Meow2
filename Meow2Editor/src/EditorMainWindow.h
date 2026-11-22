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
#include <functional>
#include <memory>
#include <QJsonObject>
#include <QMainWindow>
#include <QString>
#include <QStringList>
#include <vector>

class QAction;
class QCloseEvent;
class QMenu;

namespace Meow2::Editor
{
class AssetsDock;
class HierarchyDock;
class InspectorDock;
class SceneSettingsDock;
class SceneEditor;
class ConsoleDock;
class GameViewportDock;
class QtInputSource;
class SceneDocument;
class SettingsDialog;
class ViewportController;

/// @brief The answer to "save the changes?": what the window asks before dropping unsaved changes.
enum class UnsavedAnswer
{
    Save,    ///< Save first (asking for a file if the scene is untitled), then go on.
    Discard, ///< Drop the changes and go on.
    Cancel,  ///< Don't go on.
};

/// @brief What the main window works with. Plain data (references), filled by the composition root.
struct EditorMainWindowDeps
{
    EditorCommandRegistry& commands;   ///< Every menu item, toolbar button and shortcut runs a command here.
    EditorSettings& settings;          ///< Shown by the settings window.
    const Log::ConsoleBuffer& console; ///< Shown by the console dock.
    Log::Logger& log;                  ///< Receives commands that fail when the user triggers them.
    EditorWorld& world;                ///< The scene the viewports show.
    Input::InputSystem& input;         ///< Fed by the viewports' keyboard and mouse (QtInputSource).
    SceneDocument& document; ///< The project folder and the scene file: titles, recent files, the Assets dock.
    SceneEditor& editor;     ///< The selection and the edit commands: the Hierarchy and Inspector docks.
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
/// File → New, Open, Open Recent, Save, Save As, Open Project and Import Model run the SceneDocument's commands; before
/// dropping unsaved changes (New, Open, a double-click in the Assets dock, closing) it asks Save / Discard / Cancel.
/// The title shows the scene's file and a `*` while it has unsaved changes.
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

    /// @brief The Assets dock.
    /// @return It.
    [[nodiscard]] AssetsDock& assetsDock();

    /// @brief The Hierarchy dock.
    /// @return It.
    [[nodiscard]] HierarchyDock& hierarchyDock();

    /// @brief The Inspector dock.
    /// @return It.
    [[nodiscard]] InspectorDock& inspectorDock();

    /// @brief Starts a new scene, asking about unsaved changes first (File → New).
    /// @return True if a new scene was started.
    bool newScene();

    /// @brief Opens a scene file, asking about unsaved changes first (File → Open, the Assets dock).
    /// @param path Relative to the project.
    /// @return True if it opened.
    bool openScene(const QString& path);

    /// @brief Saves the scene (File → Save): to its file, or to one picked in a dialog when it's untitled.
    /// @return True if it was saved.
    bool saveScene();

    /// @brief Replaces how the window asks about unsaved changes (a message box by default); tests answer directly.
    /// @param ask Returns the answer.
    void setUnsavedPrompt(std::function<UnsavedAnswer()> ask)
    {
        m_askUnsaved = std::move(ask);
    }

    /// @brief Replaces how the window picks a file to save to (a file dialog by default); tests answer directly.
    /// @param pick Returns a path relative to the project, or empty for cancelled.
    void setSaveAsPrompt(std::function<QString()> pick)
    {
        m_pickSaveAs = std::move(pick);
    }

protected:
    /// @brief Asks about unsaved changes before the window closes; Cancel keeps it open.
    /// @param event The close event.
    void closeEvent(QCloseEvent* event) override;

private:
    /// @brief If there are unsaved changes, asks what to do and saves if told to.
    /// @return True to go on (saved, discarded or nothing to save); false for Cancel or a failed save.
    bool resolveUnsaved();

    /// @brief Runs a command; true if it succeeded (a failure is logged).
    bool tryRun(const QString& command, const QJsonObject& parameters = {});

    /// @brief What a double-click in the Assets dock does with a file: opens a scene, imports a model.
    void activateAsset(const QString& nativePath);

    /// @brief Rebuilds File → Open Recent from the document's list.
    void refreshRecent();

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
    /// @brief Hides the title bar of a dock that shares its place with other docks as tabs (its tab already names
    ///        it), and shows it again when it stands alone or floats. Run every tick; changes only what changed.
    void updateDockTitles();

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
    QAction* m_translateAction = nullptr;                ///< Gizmo: move (W).
    QAction* m_rotateAction = nullptr;                   ///< Gizmo: rotate (E).
    QAction* m_scaleAction = nullptr;                    ///< Gizmo: scale (R).
    QAction* m_localAction = nullptr;                    ///< Gizmo: checked for local axes.
    QAction* m_snapAction = nullptr;                     ///< Gizmo: checked while snapping is on.
    std::vector<QAction*> m_gizmoKeys;                   ///< W/E/R on the viewport; off while the camera flies.
    std::vector<CommandAction> m_commandActions;         ///< Every action bound to a command.
    QAction* m_undoAction = nullptr;                     ///< Its text names the step it undoes.
    QAction* m_redoAction = nullptr;                     ///< Its text names the step it redoes.
    QAction* m_consoleAction = nullptr;                  ///< Checked while the console dock is shown.
    std::uint64_t m_historyRevision = ~std::uint64_t{0}; ///< The history's revision the labels show.
    SceneDocument& m_document;
    SceneEditor& m_editor;
    HierarchyDock* m_hierarchyDock = nullptr;         ///< Owned by Qt.
    InspectorDock* m_inspectorDock = nullptr;         ///< Owned by Qt.
    SceneSettingsDock* m_sceneSettingsDock = nullptr; ///< Owned by Qt.
    QAction* m_hierarchyAction = nullptr;             ///< Checked while the Hierarchy dock is shown.
    QAction* m_inspectorAction = nullptr;             ///< Checked while the Inspector dock is shown.
    QAction* m_sceneSettingsAction = nullptr;         ///< Checked while the Scene settings dock is shown.
    AssetsDock* m_assetsDock = nullptr;               ///< Owned by Qt, never null after construction.
    QAction* m_assetsAction = nullptr;                ///< Checked while the Assets dock is shown.
    QMenu* m_recentMenu = nullptr;                    ///< File → Open Recent.
    QStringList m_recentShown;                        ///< What m_recentMenu lists.
    std::function<UnsavedAnswer()> m_askUnsaved;      ///< setUnsavedPrompt().
    std::function<QString()> m_pickSaveAs;            ///< setSaveAsPrompt().
};
} // namespace Meow2::Editor
