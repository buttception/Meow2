/// @file
/// @brief EditorMainWindow implementation.

#include "EditorMainWindow.h"

#include "Commands/CommandSchema.h"
#include "Panels/AssetsDock.h"
#include "Panels/ConsoleDock.h"
#include "Panels/SettingsDialog.h"
#include "Project/SceneDocument.h"
#include "Viewports/GameViewportDock.h"
#include "Viewports/QtInputSource.h"
#include "Viewports/ViewportController.h"

#include <Meow2/Core/Assert.h>

#include <QAction>
#include <QActionGroup>
#include <QByteArray>
#include <QCloseEvent>
#include <QComboBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QJsonArray>
#include <QKeySequence>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QToolBar>
#include <string>
#include <string_view>
#include <utility>

namespace Meow2::Editor
{
using namespace Qt::StringLiterals;

namespace
{
/// @brief `view.showPanel`'s panel names.
constexpr QLatin1StringView c_consolePanel{"console"};
constexpr QLatin1StringView c_settingsPanel{"settings"};
constexpr QLatin1StringView c_gamePanel{"game"};
constexpr QLatin1StringView c_assetsPanel{"assets"};
/// @brief Where the dock layout is kept (QMainWindow::saveState(), base64).
constexpr std::string_view c_layoutKey = "editor.layout";
} // namespace

EditorMainWindow::EditorMainWindow(const EditorMainWindowDeps& deps, QWidget* parent)
    : QMainWindow(parent)
    , m_commands(deps.commands)
    , m_log(deps.log)
    , m_viewport(new ViewportWidget(this)) // Qt parent–child: the window deletes them
    , m_console(new ConsoleDock(deps.console, deps.settings.value(u"editor.console.followNewest"_s).toBool(), this))
    , m_settingsDialog(new SettingsDialog(deps.commands, deps.settings, this))
    , m_gameDock(new GameViewportDock(this))
    , m_inputSource(new QtInputSource(deps.input, this))
    , m_settings(deps.settings)
    , m_document(deps.document)
    , m_assetsDock(new AssetsDock(this))
{
    m_viewport->setObjectName(u"EditorViewport"_s);
    setCentralWidget(m_viewport);
    addDockWidget(Qt::RightDockWidgetArea, m_gameDock);
    addDockWidget(Qt::BottomDockWidgetArea, m_console);
    addDockWidget(Qt::LeftDockWidgetArea, m_assetsDock);
    m_assetsDock->setProject(QString::fromStdString(m_document.projectFolder().string()));
    m_assetsDock->onActivate([this](const QString& path) { activateAsset(path); });
    // How the window asks; tests replace both (setUnsavedPrompt(), setSaveAsPrompt()).
    m_askUnsaved = [this]
    {
        const QMessageBox::StandardButton answer =
            QMessageBox::question(this, u"Unsaved changes"_s, u"Save the changes to the scene first?"_s,
                                  QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
        return answer == QMessageBox::Save      ? UnsavedAnswer::Save
               : answer == QMessageBox::Discard ? UnsavedAnswer::Discard
                                                : UnsavedAnswer::Cancel;
    };
    m_pickSaveAs = [this]
    {
        const QString file = QFileDialog::getSaveFileName(this, u"Save scene as"_s,
                                                          QString::fromStdString(m_document.projectFolder().string()),
                                                          u"Meow2 scene (*.m2scene)"_s);
        return file.isEmpty() ? QString{} : m_document.projectPath(file.toStdWString());
    };
    // A saved layout puts the docks back where the user left them; the defaults above are the first run's.
    if (const std::string layout = deps.settings.config().getString(c_layoutKey, ""); !layout.empty())
    {
        restoreState(QByteArray::fromBase64(QByteArray::fromStdString(layout)));
    }
    m_inputSource->watch(m_viewport);
    m_inputSource->watch(&m_gameDock->viewport());
    m_viewports = std::make_unique<ViewportController>(ViewportControllerDeps{.commands = deps.commands,
                                                                              .settings = deps.settings,
                                                                              .world = deps.world,
                                                                              .input = deps.input,
                                                                              .inputSource = *m_inputSource,
                                                                              .renderer = deps.renderer,
                                                                              .editorViewport = *m_viewport,
                                                                              .gameViewport = m_gameDock->viewport(),
                                                                              .log = deps.log});
    connect(
        &m_gameDock->renderScaleBox(), &QComboBox::activated, this, [this](int index)
        { run(u"viewport.setRenderScale"_s, {{u"percent"_s, m_gameDock->renderScaleBox().itemData(index).toInt()}}); });
    registerViewCommands();
    createMenusAndToolbar();
    refreshActions();
}

EditorMainWindow::~EditorMainWindow()
{
    m_viewports.reset(); // removes its commands while the widgets it uses still exist
    m_commands.remove(u"view.showPanel");
}

void EditorMainWindow::tick(float deltaSeconds)
{
    m_console->tick();
    m_viewports->tick(deltaSeconds);
    refreshActions();
}

void EditorMainWindow::saveLayout()
{
    m_settings.config().setString(c_layoutKey, saveState().toBase64().toStdString());
}

GameViewportDock& EditorMainWindow::gameDock()
{
    return *m_gameDock;
}

ViewportController& EditorMainWindow::viewports()
{
    return *m_viewports;
}

void EditorMainWindow::refreshActions()
{
    for (const CommandAction& bound : m_commandActions)
    {
        bound.action->setEnabled(m_commands.isEnabled(bound.command));
    }
    m_consoleAction->setChecked(m_console->isVisible());
    m_gameAction->setChecked(m_gameDock->isVisible());
    m_assetsAction->setChecked(m_assetsDock->isVisible());
    // The title names the scene's file, with a * while it has unsaved changes.
    const QString name = m_document.scenePath().isEmpty() ? u"Untitled"_s : m_document.scenePath();
    const QString title = name + (m_document.hasUnsavedChanges() ? u"*"_s : QString{}) + u" — Meow2Editor"_s;
    if (windowTitle() != title)
    {
        setWindowTitle(title);
    }
    m_assetsDock->setProject(QString::fromStdString(m_document.projectFolder().string()));
    refreshRecent();
    m_orbitAction->setChecked(m_viewports->camera().mode() == CameraMode::Orbit);
    m_flyAction->setChecked(m_viewports->camera().mode() == CameraMode::Fly);
    m_gameDock->showRenderScale(m_viewports->renderScalePercent());

    // The labels name the next step: "Undo Set position". Only rebuilt when the history changed.
    const UndoStack& history = m_commands.history();
    if (history.revision() != m_historyRevision)
    {
        m_historyRevision = history.revision();
        const QStringList undo = history.undoLabels();
        const QStringList redo = history.redoLabels();
        m_undoAction->setText(undo.isEmpty() ? u"Undo"_s : u"Undo "_s + undo.front());
        m_redoAction->setText(redo.isEmpty() ? u"Redo"_s : u"Redo "_s + redo.front());
    }
}

ViewportWidget& EditorMainWindow::viewport()
{
    return *m_viewport;
}

ConsoleDock& EditorMainWindow::console()
{
    return *m_console;
}

SettingsDialog& EditorMainWindow::settingsDialog()
{
    return *m_settingsDialog;
}

QAction* EditorMainWindow::actionFor(const QString& command) const
{
    for (const CommandAction& bound : m_commandActions)
    {
        if (bound.command == command)
        {
            return bound.action;
        }
    }
    return nullptr;
}

QAction* EditorMainWindow::addCommandAction(const QString& text, const QString& command, const QJsonObject& parameters)
{
    auto* action = new QAction(text, this);
    connect(action, &QAction::triggered, this, [this, command, parameters] { run(command, parameters); });
    m_commandActions.push_back({.action = action, .command = command});
    return action;
}

void EditorMainWindow::run(const QString& command, const QJsonObject& parameters)
{
    if (const Result<QJsonValue> ran = m_commands.execute(command, parameters); !ran)
    {
        MEOW2_LOG_WARN(m_log, "{}: {}", command.toStdString(), ran.error());
    }
    refreshActions(); // show the effect (labels, enabled state) at once, not a frame later
}

void EditorMainWindow::createMenusAndToolbar()
{
    // File actions that may ask first (unsaved changes, a file name) aren't plain command actions: they ask, then run
    // the SceneDocument's command.
    auto* newAction = new QAction(u"New scene"_s, this);
    newAction->setShortcut(QKeySequence::New);
    connect(newAction, &QAction::triggered, this, [this] { newScene(); });
    auto* open = new QAction(u"Open scene…"_s, this);
    open->setShortcut(QKeySequence::Open);
    connect(open, &QAction::triggered, this,
            [this]
            {
                const QString file = QFileDialog::getOpenFileName(
                    this, u"Open scene"_s, QString::fromStdString(m_document.projectFolder().string()),
                    u"Meow2 scene (*.m2scene)"_s);
                if (!file.isEmpty())
                {
                    openScene(m_document.projectPath(file.toStdWString()));
                }
            });
    auto* save = new QAction(u"Save"_s, this);
    save->setShortcut(QKeySequence::Save);
    connect(save, &QAction::triggered, this, [this] { saveScene(); });
    auto* saveAs = new QAction(u"Save as…"_s, this);
    saveAs->setShortcut(QKeySequence::SaveAs);
    connect(saveAs, &QAction::triggered, this,
            [this]
            {
                if (const QString path = m_pickSaveAs ? m_pickSaveAs() : QString{}; !path.isEmpty())
                {
                    tryRun(u"scene.saveAs"_s, {{u"path"_s, path}});
                }
            });
    auto* openProject = new QAction(u"Open project folder…"_s, this);
    connect(openProject, &QAction::triggered, this,
            [this]
            {
                const QString folder = QFileDialog::getExistingDirectory(
                    this, u"Open project folder"_s, QString::fromStdString(m_document.projectFolder().string()));
                if (!folder.isEmpty() && resolveUnsaved())
                {
                    tryRun(u"project.open"_s, {{u"path"_s, folder}, {u"discard"_s, true}});
                }
            });
    auto* importModel = new QAction(u"Import model…"_s, this);
    connect(importModel, &QAction::triggered, this,
            [this]
            {
                const QString file = QFileDialog::getOpenFileName(
                    this, u"Import model"_s, QString::fromStdString(m_document.projectFolder().string()),
                    u"glTF model (*.gltf *.glb)"_s);
                if (!file.isEmpty())
                {
                    tryRun(u"asset.importModel"_s, {{u"path"_s, m_document.projectPath(file.toStdWString())}});
                }
            });
    m_assetsAction = new QAction(u"Assets"_s, this);
    m_assetsAction->setCheckable(true);
    connect(m_assetsAction, &QAction::triggered, this,
            [this](bool shown) { run(u"view.showPanel"_s, {{u"panel"_s, c_assetsPanel}, {u"visible"_s, shown}}); });
    m_undoAction = addCommandAction(u"Undo"_s, u"edit.undo"_s);
    m_undoAction->setShortcut(QKeySequence::Undo);
    m_redoAction = addCommandAction(u"Redo"_s, u"edit.redo"_s);
    m_redoAction->setShortcut(QKeySequence::Redo);
    QAction* settings =
        addCommandAction(u"Settings…"_s, u"view.showPanel"_s, {{u"panel"_s, c_settingsPanel}, {u"visible"_s, true}});
    settings->setShortcut(QKeySequence{u"Ctrl+,"_s}); // QKeySequence::Preferences has no binding on Windows

    // The console toggle is checkable, so its parameter depends on the new state: it isn't a fixed command action.
    m_consoleAction = new QAction(u"Console"_s, this);
    m_consoleAction->setCheckable(true);
    connect(m_consoleAction, &QAction::triggered, this,
            [this](bool shown) { run(u"view.showPanel"_s, {{u"panel"_s, c_consolePanel}, {u"visible"_s, shown}}); });
    m_gameAction = new QAction(u"Game view"_s, this);
    m_gameAction->setCheckable(true);
    connect(m_gameAction, &QAction::triggered, this,
            [this](bool shown) { run(u"view.showPanel"_s, {{u"panel"_s, c_gamePanel}, {u"visible"_s, shown}}); });

    // F frames the scene, but only while the editor viewport has focus: typed into a dock, F is just a letter.
    QAction* focus = addCommandAction(u"Frame scene"_s, u"viewport.focus"_s);
    focus->setShortcut(QKeySequence{Qt::Key_F});
    focus->setShortcutContext(Qt::WidgetShortcut);
    m_viewport->addAction(focus);
    m_orbitAction = addCommandAction(u"Orbit camera"_s, u"viewport.setCameraMode"_s, {{u"mode"_s, u"orbit"_s}});
    m_flyAction = addCommandAction(u"Fly camera"_s, u"viewport.setCameraMode"_s, {{u"mode"_s, u"fly"_s}});
    auto* modes = new QActionGroup(this);
    for (QAction* mode : {m_orbitAction, m_flyAction})
    {
        mode->setCheckable(true);
        modes->addAction(mode);
    }

    QMenu* file = menuBar()->addMenu(u"&File"_s);
    file->addAction(newAction);
    file->addAction(open);
    m_recentMenu = file->addMenu(u"Open recent"_s);
    file->addAction(save);
    file->addAction(saveAs);
    file->addSeparator();
    file->addAction(openProject);
    file->addAction(importModel);
    file->addSeparator();
    file->addAction(settings);
    QMenu* edit = menuBar()->addMenu(u"&Edit"_s);
    edit->addAction(m_undoAction);
    edit->addAction(m_redoAction);
    QMenu* view = menuBar()->addMenu(u"&View"_s);
    view->addAction(focus);
    view->addSeparator();
    view->addAction(m_orbitAction);
    view->addAction(m_flyAction);
    QMenu* window = menuBar()->addMenu(u"&Window"_s);
    window->addAction(m_consoleAction);
    window->addAction(m_gameAction);
    window->addAction(m_assetsAction);

    QToolBar* toolbar = addToolBar(u"Main"_s);
    toolbar->setObjectName(u"MainToolbar"_s);
    toolbar->setMovable(false);
    toolbar->addAction(save);
    toolbar->addSeparator();
    toolbar->addAction(m_undoAction);
    toolbar->addAction(m_redoAction);
    toolbar->addSeparator();
    toolbar->addAction(settings);
}

void EditorMainWindow::registerViewCommands()
{
    QJsonObject panel = propertySchema(u"string"_s, u"Which panel."_s);
    panel.insert(u"enum", QJsonArray{c_assetsPanel, c_consolePanel, c_gamePanel, c_settingsPanel});
    [[maybe_unused]] const Result<void> added = m_commands.add(
        {.name = u"view.showPanel"_s,
         .description =
             u"Shows or hides a panel: the Assets dock, the console dock, the game view dock or the settings window."_s,
         .parameters = objectSchema({{.name = u"panel"_s, .schema = panel, .required = true},
                                     {.name = u"visible"_s,
                                      .schema = propertySchema(u"boolean"_s, u"Shown or hidden."_s),
                                      .required = true}})},
        [this](const QJsonObject& parameters) -> Result<CommandOutcome>
        {
            const bool visible = parameters.value(u"visible").toBool();
            const QString name = parameters.value(u"panel").toString();
            QWidget* widget = name == c_consolePanel  ? static_cast<QWidget*>(m_console)
                              : name == c_gamePanel   ? static_cast<QWidget*>(m_gameDock)
                              : name == c_assetsPanel ? static_cast<QWidget*>(m_assetsDock)
                                                      : static_cast<QWidget*>(m_settingsDialog);
            if (widget == m_settingsDialog && visible)
            {
                m_settingsDialog->refresh(); // an agent may have changed a setting since it was last open
            }
            widget->setVisible(visible);
            if (visible)
            {
                widget->raise();
            }
            return CommandOutcome{};
        });
    MEOW2_ASSERT(added, "view.showPanel is registered once, by the one main window");
}

AssetsDock& EditorMainWindow::assetsDock()
{
    return *m_assetsDock;
}

bool EditorMainWindow::tryRun(const QString& command, const QJsonObject& parameters)
{
    const Result<QJsonValue> ran = m_commands.execute(command, parameters);
    if (!ran)
    {
        MEOW2_LOG_WARN(m_log, "{}: {}", command.toStdString(), ran.error());
    }
    refreshActions();
    return ran.has_value();
}

bool EditorMainWindow::resolveUnsaved()
{
    if (!m_document.hasUnsavedChanges())
    {
        return true;
    }
    switch (m_askUnsaved ? m_askUnsaved() : UnsavedAnswer::Cancel)
    {
        case UnsavedAnswer::Save:
            return saveScene();
        case UnsavedAnswer::Discard:
            return true;
        case UnsavedAnswer::Cancel:
            break;
    }
    return false;
}

bool EditorMainWindow::newScene()
{
    // The user answered (or nothing was unsaved), so the command may drop what's left.
    return resolveUnsaved() && tryRun(u"scene.new"_s, {{u"discard"_s, true}});
}

bool EditorMainWindow::openScene(const QString& path)
{
    return !path.isEmpty() && resolveUnsaved() && tryRun(u"scene.open"_s, {{u"path"_s, path}, {u"discard"_s, true}});
}

bool EditorMainWindow::saveScene()
{
    if (!m_document.scenePath().isEmpty())
    {
        return tryRun(u"scene.save"_s);
    }
    const QString path = m_pickSaveAs ? m_pickSaveAs() : QString{};
    return !path.isEmpty() && tryRun(u"scene.saveAs"_s, {{u"path"_s, path}});
}

void EditorMainWindow::activateAsset(const QString& nativePath)
{
    const QString path = m_document.projectPath(nativePath.toStdWString());
    if (path.isEmpty())
    {
        return;
    }
    if (path.endsWith(u".m2scene"_s, Qt::CaseInsensitive))
    {
        openScene(path);
    }
    else
    {
        tryRun(u"asset.importModel"_s, {{u"path"_s, path}});
    }
}

void EditorMainWindow::refreshRecent()
{
    if (m_recentMenu == nullptr || m_document.recentScenes() == m_recentShown)
    {
        return;
    }
    m_recentShown = m_document.recentScenes();
    m_recentMenu->clear();
    for (const QString& path : m_recentShown)
    {
        QAction* entry = m_recentMenu->addAction(path);
        connect(entry, &QAction::triggered, this, [this, path] { openScene(path); });
    }
    m_recentMenu->setEnabled(!m_recentShown.isEmpty());
}

void EditorMainWindow::closeEvent(QCloseEvent* event)
{
    if (resolveUnsaved())
    {
        event->accept();
    }
    else
    {
        event->ignore();
    }
}
} // namespace Meow2::Editor
