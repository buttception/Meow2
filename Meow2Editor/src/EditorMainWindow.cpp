/// @file
/// @brief EditorMainWindow implementation.

#include "EditorMainWindow.h"

#include "Commands/CommandSchema.h"
#include "Editing/PlayMode.h"
#include "Editing/SceneEditor.h"
#include "EditorTheme.h"
#include "Panels/AssetsDock.h"
#include "Panels/ConsoleDock.h"
#include "Panels/DebugDock.h"
#include "Panels/HierarchyDock.h"
#include "Panels/InspectorDock.h"
#include "Panels/RenderGraphDock.h"
#include "Panels/SceneSettingsDock.h"
#include "Panels/SettingsDialog.h"
#include "Panels/StatsDock.h"
#include "Panels/WheelGuard.h"
#include "Project/SceneDocument.h"
#include "Viewports/GameViewportDock.h"
#include "Viewports/QtInputSource.h"
#include "Viewports/ViewportController.h"

#include <Meow2/Core/Assert.h>

#include <algorithm>
#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QByteArray>
#include <QCloseEvent>
#include <QComboBox>
#include <QDockWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QJsonArray>
#include <QKeySequence>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include <QPolygonF>
#include <QToolBar>
#include <string>
#include <string_view>
#include <utility>

namespace Meow2::Editor
{
using namespace Qt::StringLiterals;

namespace
{
/// @brief The shapes on the Play, Stop and Pause buttons.
enum class TransportShape : std::uint8_t
{
    Play,  ///< A triangle pointing right.
    Stop,  ///< A square.
    Pause, ///< Two bars.
};

/// @brief A transport button's picture, drawn here so it needs no image files and follows the theme: light grey,
///        and red (the theme's accent) for the state that's on (playing shows Stop in red, paused shows Pause in red).
/// @param off The shape when the action is unchecked.
/// @param on  The shape when it's checked.
/// @return The icon, with a pixmap per state at 32 pixels (Qt scales it to the toolbar).
QIcon transportIcon(TransportShape off, TransportShape on)
{
    const auto draw = [](TransportShape shape, QColor colour)
    {
        constexpr int c_size = 32;
        QPixmap pixmap(c_size, c_size);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen);
        painter.setBrush(colour);
        switch (shape)
        {
            case TransportShape::Play:
                painter.drawPolygon(QPolygonF{{QPointF(10, 7), QPointF(10, 25), QPointF(25, 16)}});
                break;
            case TransportShape::Stop:
                painter.drawRect(QRectF(9, 9, 14, 14));
                break;
            case TransportShape::Pause:
                painter.drawRect(QRectF(9, 8, 5, 16));
                painter.drawRect(QRectF(18, 8, 5, 16));
                break;
        }
        return pixmap;
    };
    const QColor normal(0xE6, 0xE6, 0xE6);
    QIcon icon;
    icon.addPixmap(draw(off, normal), QIcon::Normal, QIcon::Off);
    icon.addPixmap(draw(on, QColor(EditorColors::c_accentBright)), QIcon::Normal, QIcon::On);
    icon.addPixmap(draw(off, QColor(0x55, 0x55, 0x5A)), QIcon::Disabled, QIcon::Off);
    return icon;
}

/// @brief `view.showPanel`'s panel names.
constexpr QLatin1StringView c_consolePanel{"console"};
constexpr QLatin1StringView c_settingsPanel{"settings"};
constexpr QLatin1StringView c_gamePanel{"game"};
constexpr QLatin1StringView c_assetsPanel{"assets"};
constexpr QLatin1StringView c_hierarchyPanel{"hierarchy"};
constexpr QLatin1StringView c_inspectorPanel{"inspector"};
constexpr QLatin1StringView c_sceneSettingsPanel{"sceneSettings"};
constexpr QLatin1StringView c_debugPanel{"debug"};
constexpr QLatin1StringView c_statsPanel{"stats"};
constexpr QLatin1StringView c_renderGraphPanel{"renderGraph"};
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
    , m_editor(deps.editor)
    , m_hierarchyDock(new HierarchyDock(deps.commands, deps.editor, deps.world, deps.log, this))
    , m_inspectorDock(new InspectorDock(deps.commands, deps.editor, deps.world, deps.log, this))
    , m_sceneSettingsDock(new SceneSettingsDock(deps.commands, deps.world, deps.log, this))
    , m_assetsDock(new AssetsDock(this))
    , m_debugDock(new DebugDock(deps.commands, deps.log, this))
    , m_statsDock(new StatsDock(deps.commands, this))
    , m_renderGraphDock(new RenderGraphDock(deps.commands, deps.log, this))
{
    m_viewport->setObjectName(u"EditorViewport"_s);
    setCentralWidget(m_viewport);
    // Tabs of docks stacked in one place go on top (owner request), as in most editors; Qt's default is below.
    setTabPosition(Qt::AllDockWidgetAreas, QTabWidget::North);
    // Scrolling a panel mustn't edit the fields it passes over (owner request): value widgets take the wheel only when
    // focused. On the application, so dialogs and every dock get it; removed when the window (its parent) goes.
    qApp->installEventFilter(new WheelGuard(this));
    addDockWidget(Qt::RightDockWidgetArea, m_gameDock);
    addDockWidget(Qt::BottomDockWidgetArea, m_console);
    addDockWidget(Qt::LeftDockWidgetArea, m_hierarchyDock);
    addDockWidget(Qt::LeftDockWidgetArea, m_assetsDock);
    addDockWidget(Qt::RightDockWidgetArea, m_inspectorDock);
    splitDockWidget(m_gameDock, m_inspectorDock, Qt::Vertical); // the game view above, the inspector below
    tabifyDockWidget(m_inspectorDock, m_sceneSettingsDock);     // scene settings a tab beside the Inspector
    tabifyDockWidget(m_sceneSettingsDock, m_debugDock);         // the debug tools there too
    tabifyDockWidget(m_console, m_statsDock);                   // and the stats beside the console
    tabifyDockWidget(m_statsDock, m_renderGraphDock);           // and the frame capture
    m_console->raise();
    m_inspectorDock->raise();
    // Minimum sizes (owner request): a panel can't be squeezed until it stops working. The Inspector and Scene
    // settings also keep their width to their fields (their build()).
    m_viewport->setMinimumSize(320, 200); // room for the gizmo and the view-axis corner
    m_gameDock->setMinimumSize(240, 160);
    m_hierarchyDock->setMinimumSize(180, 140);
    m_assetsDock->setMinimumSize(180, 140);
    m_inspectorDock->setMinimumHeight(160);
    m_sceneSettingsDock->setMinimumHeight(160);
    m_console->setMinimumSize(320, 110);
    m_debugDock->setMinimumHeight(160);
    m_statsDock->setMinimumSize(240, 110);
    m_renderGraphDock->setMinimumSize(320, 200);
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
        // Scenes go in the project's Scenes/ folder unless chosen otherwise.
        const QString file = QFileDialog::getSaveFileName(
            this, u"Save scene as"_s, QString::fromStdString((m_document.projectFolder() / "Scenes").string()),
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
                                                                              .sceneEditor = deps.editor,
                                                                              .input = deps.input,
                                                                              .inputSource = *m_inputSource,
                                                                              .renderer = deps.renderer,
                                                                              .editorViewport = *m_viewport,
                                                                              .gameViewport = m_gameDock->viewport(),
                                                                              .log = deps.log,
                                                                              .headlessSize = deps.headlessSize});
    m_playMode = std::make_unique<PlayMode>(PlayModeDeps{.commands = deps.commands,
                                                         .history = deps.commands.history(),
                                                         .world = deps.world,
                                                         .jobs = deps.world.jobs(),
                                                         .physics = deps.physics,
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
    m_playMode.reset();  // stops playing first: the scene goes back as it was
    m_viewports.reset(); // removes its commands while the widgets it uses still exist
    m_commands.remove(u"view.showPanel");
}

void EditorMainWindow::tick(float deltaSeconds)
{
    m_console->tick();
    m_hierarchyDock->refresh();
    m_inspectorDock->refresh();
    m_sceneSettingsDock->refresh();
    m_viewports->tick(deltaSeconds);
    m_debugDock->refresh(); // after the tick: a command run there shows at once
    m_statsDock->refresh();
    m_renderGraphDock->refresh();
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

PlayMode& EditorMainWindow::playMode()
{
    return *m_playMode;
}

ViewportController& EditorMainWindow::viewports()
{
    return *m_viewports;
}

void EditorMainWindow::updateDockTitles()
{
    for (QDockWidget* dock : findChildren<QDockWidget*>(Qt::FindDirectChildrenOnly))
    {
        // Tab mates the user hasn't closed (a dock behind another tab isn't hidden, only not current).
        const QList<QDockWidget*> mates = tabifiedDockWidgets(dock);
        const bool tabbed = !dock->isFloating() &&
                            std::ranges::any_of(mates, [](const QDockWidget* mate) { return !mate->isHidden(); });
        // An empty widget as the title bar hides it; the dock still moves by dragging its tab.
        const bool hidden = dock->titleBarWidget() != nullptr;
        if (tabbed && !hidden)
        {
            dock->setTitleBarWidget(new QWidget(dock));
        }
        else if (!tabbed && hidden)
        {
            QWidget* empty = dock->titleBarWidget();
            dock->setTitleBarWidget(nullptr);
            empty->deleteLater();
        }
    }
}

void EditorMainWindow::refreshActions()
{
    updateDockTitles();
    for (const CommandAction& bound : m_commandActions)
    {
        bound.action->setEnabled(m_commands.isEnabled(bound.command));
    }
    m_playAction->setChecked(m_playMode->playing());
    m_playAction->setText(m_playMode->playing() ? u"Stop"_s : u"Play"_s);
    m_playAction->setToolTip(m_playMode->playing() ? u"Stop: put the scene back as it was before Play (Ctrl+P)"_s
                                                   : u"Play the scene's physics; Stop puts the scene back (Ctrl+P)"_s);
    m_pauseAction->setEnabled(m_playMode->playing());
    m_pauseAction->setChecked(m_playMode->paused());
    m_consoleAction->setChecked(m_console->isVisible());
    m_gameAction->setChecked(m_gameDock->isVisible());
    m_assetsAction->setChecked(m_assetsDock->isVisible());
    m_hierarchyAction->setChecked(m_hierarchyDock->isVisible());
    m_inspectorAction->setChecked(m_inspectorDock->isVisible());
    m_sceneSettingsAction->setChecked(m_sceneSettingsDock->isVisible());
    m_debugAction->setChecked(m_debugDock->isVisible());
    m_statsAction->setChecked(m_statsDock->isVisible());
    m_renderGraphAction->setChecked(m_renderGraphDock->isVisible());
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
    const Gizmo& gizmo = m_viewports->gizmo();
    m_translateAction->setChecked(gizmo.mode() == GizmoMode::Translate);
    m_rotateAction->setChecked(gizmo.mode() == GizmoMode::Rotate);
    m_scaleAction->setChecked(gizmo.mode() == GizmoMode::Scale);
    m_localAction->setChecked(gizmo.space() == GizmoSpace::Local);
    m_snapAction->setChecked(m_viewports->snapping());
    // While the camera flies, W and E move it: the gizmo keys step aside so the viewport gets them.
    const bool flying = m_viewports->camera().flying();
    for (QAction* key : m_gizmoKeys)
    {
        key->setEnabled(!flying);
    }
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
                    if (const QString path = m_document.projectPath(file.toStdWString()); !path.isEmpty())
                    {
                        openScene(path);
                    }
                    else
                    {
                        MEOW2_LOG_WARN(m_log,
                                       "{} is outside the project folder ({}); scenes are opened from the project",
                                       file.toStdString(), m_document.projectFolder().string());
                    }
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
    auto* newProject = new QAction(u"New project…"_s, this);
    connect(newProject, &QAction::triggered, this,
            [this]
            {
                // A name in a folder: the project is made there, with Assets/, Scenes/ and its project file.
                const QString folder = QFileDialog::getSaveFileName(
                    this, u"New project: choose a folder and a name"_s,
                    QString::fromStdString(m_document.projectFolder().parent_path().string()), QString{}, nullptr,
                    QFileDialog::ShowDirsOnly);
                if (!folder.isEmpty() && resolveUnsaved())
                {
                    tryRun(u"project.open"_s, {{u"path"_s, folder}, {u"discard"_s, true}});
                }
            });
    auto* openProject = new QAction(u"Open project…"_s, this);
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
                if (file.isEmpty())
                {
                    return;
                }
                // A model outside the project is copied in first (with its folder): scenes only refer to what the
                // project holds, so it opens on another machine too.
                QString path = m_document.projectPath(file.toStdWString());
                if (path.isEmpty())
                {
                    Result<QString> copied = m_document.copyIntoProject(file.toStdWString());
                    if (!copied)
                    {
                        MEOW2_LOG_WARN(m_log, "Import model: {}", copied.error());
                        return;
                    }
                    path = *copied;
                }
                tryRun(u"asset.importModel"_s, {{u"path"_s, path}});
            });
    // Entity actions work on the selection; Delete and Ctrl+D act wherever focus is, unless a text box uses the key.
    auto* createEntity = new QAction(u"Create entity"_s, this);
    connect(createEntity, &QAction::triggered, this,
            [this]
            {
                const std::vector<EntityId> selected = m_editor.selection();
                tryRun(u"entity.create"_s,
                       {{u"name"_s, u"Entity"_s},
                        {u"parent"_s,
                         selected.empty() ? QString{} : QString::fromStdString(selected.front().uuid().toString())}});
            });
    auto* duplicate = new QAction(u"Duplicate"_s, this);
    duplicate->setShortcut(QKeySequence{u"Ctrl+D"_s});
    connect(duplicate, &QAction::triggered, this,
            [this]
            {
                for (const EntityId id : m_editor.selection())
                {
                    tryRun(u"entity.duplicate"_s, {{u"id"_s, QString::fromStdString(id.uuid().toString())}});
                }
            });
    auto* deleteEntity = new QAction(u"Delete"_s, this);
    deleteEntity->setShortcut(QKeySequence::Delete);
    connect(deleteEntity, &QAction::triggered, this,
            [this]
            {
                for (const EntityId id : m_editor.selection())
                {
                    if (m_editor.find(id) == entt::null)
                    {
                        continue; // gone with a selected parent
                    }
                    tryRun(u"entity.delete"_s, {{u"id"_s, QString::fromStdString(id.uuid().toString())}});
                }
            });
    m_hierarchyAction = new QAction(u"Hierarchy"_s, this);
    m_hierarchyAction->setCheckable(true);
    connect(m_hierarchyAction, &QAction::triggered, this,
            [this](bool shown) { run(u"view.showPanel"_s, {{u"panel"_s, c_hierarchyPanel}, {u"visible"_s, shown}}); });
    m_inspectorAction = new QAction(u"Inspector"_s, this);
    m_inspectorAction->setCheckable(true);
    connect(m_inspectorAction, &QAction::triggered, this,
            [this](bool shown) { run(u"view.showPanel"_s, {{u"panel"_s, c_inspectorPanel}, {u"visible"_s, shown}}); });
    m_sceneSettingsAction = new QAction(u"Scene settings"_s, this);
    m_sceneSettingsAction->setCheckable(true);
    connect(m_sceneSettingsAction, &QAction::triggered, this, [this](bool shown)
            { run(u"view.showPanel"_s, {{u"panel"_s, c_sceneSettingsPanel}, {u"visible"_s, shown}}); });
    m_debugAction = new QAction(u"Debug"_s, this);
    m_debugAction->setCheckable(true);
    connect(m_debugAction, &QAction::triggered, this,
            [this](bool shown) { run(u"view.showPanel"_s, {{u"panel"_s, c_debugPanel}, {u"visible"_s, shown}}); });
    m_statsAction = new QAction(u"Stats"_s, this);
    m_statsAction->setCheckable(true);
    connect(m_statsAction, &QAction::triggered, this,
            [this](bool shown) { run(u"view.showPanel"_s, {{u"panel"_s, c_statsPanel}, {u"visible"_s, shown}}); });
    m_renderGraphAction = new QAction(u"Render graph"_s, this);
    m_renderGraphAction->setCheckable(true);
    connect(m_renderGraphAction, &QAction::triggered, this, [this](bool shown)
            { run(u"view.showPanel"_s, {{u"panel"_s, c_renderGraphPanel}, {u"visible"_s, shown}}); });
    // File → Scene settings… opens the dock (the scene's sun and exposure live with the scene, not on an entity).
    QAction* sceneSettings = addCommandAction(u"Scene settings…"_s, u"view.showPanel"_s,
                                              {{u"panel"_s, c_sceneSettingsPanel}, {u"visible"_s, true}});
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
    QAction* focus = addCommandAction(u"Frame selection"_s, u"viewport.focus"_s); // or the scene, with nothing selected
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

    // Gizmo modes, as in Unity: W move, E rotate, R scale. The toolbar and menu items have no shortcut of their own:
    // a Qt shortcut takes its key before the viewport sees it, so W and E would never reach the flying camera (WASD,
    // QE). The keys are separate actions on the viewport instead, switched off while the camera flies
    // (refreshActions()).
    const auto gizmoAction = [this](const QString& text, const QString& mode, Qt::Key key)
    {
        auto* action = new QAction(text, this);
        action->setCheckable(true);
        action->setToolTip(u"%1 (%2)"_s.arg(text, QKeySequence{key}.toString()));
        connect(action, &QAction::triggered, this, [this, mode] { run(u"viewport.setGizmo"_s, {{u"mode"_s, mode}}); });
        auto* shortcut = new QAction(text + u" (key)"_s, this);
        shortcut->setObjectName(u"GizmoKey"_s + QKeySequence{key}.toString());
        shortcut->setShortcut(QKeySequence{key});
        shortcut->setShortcutContext(Qt::WidgetShortcut);
        connect(shortcut, &QAction::triggered, this,
                [this, mode] { run(u"viewport.setGizmo"_s, {{u"mode"_s, mode}}); });
        m_viewport->addAction(shortcut);
        m_gizmoKeys.push_back(shortcut);
        return action;
    };
    m_translateAction = gizmoAction(u"Move"_s, u"translate"_s, Qt::Key_W);
    m_rotateAction = gizmoAction(u"Rotate"_s, u"rotate"_s, Qt::Key_E);
    m_scaleAction = gizmoAction(u"Scale"_s, u"scale"_s, Qt::Key_R);
    m_localAction = new QAction(u"Local axes"_s, this);
    m_localAction->setCheckable(true);
    m_localAction->setToolTip(u"Move and rotate along the entity's own axes instead of the world's"_s);
    connect(m_localAction, &QAction::triggered, this,
            [this](bool local) { run(u"viewport.setGizmo"_s, {{u"space"_s, local ? u"local"_s : u"world"_s}}); });
    // Play / Stop (Ctrl+P): one button, which runs play.start or play.stop.
    m_playAction = new QAction(u"Play"_s, this);
    m_playAction->setCheckable(true);
    m_playAction->setIcon(transportIcon(TransportShape::Play, TransportShape::Stop));
    m_playAction->setShortcut(QKeySequence{Qt::CTRL | Qt::Key_P});
    m_playAction->setToolTip(u"Play the scene's physics; Stop puts the scene back as it was (Ctrl+P)"_s);
    connect(m_playAction, &QAction::triggered, this,
            [this] { run(m_playMode->playing() ? u"play.stop"_s : u"play.start"_s, {}); });
    addAction(m_playAction); // the shortcut works wherever the focus is in the window
    // Pause (Ctrl+Shift+P, as in Unity): checked while paused; only while playing.
    m_pauseAction = new QAction(u"Pause"_s, this);
    m_pauseAction->setCheckable(true);
    m_pauseAction->setIcon(transportIcon(TransportShape::Pause, TransportShape::Pause));
    m_pauseAction->setShortcut(QKeySequence{Qt::CTRL | Qt::SHIFT | Qt::Key_P});
    m_pauseAction->setToolTip(u"Pause the scene while it plays; press again to resume (Ctrl+Shift+P)"_s);
    connect(m_pauseAction, &QAction::triggered, this,
            [this](bool pause) { run(pause ? u"play.pause"_s : u"play.resume"_s, {}); });
    addAction(m_pauseAction);

    m_snapAction = new QAction(u"Snap"_s, this);
    m_snapAction->setCheckable(true);
    m_snapAction->setToolTip(u"Snap to the steps in Settings (or hold Ctrl while dragging)"_s);
    connect(m_snapAction, &QAction::triggered, this,
            [this](bool snap) { run(u"viewport.setGizmo"_s, {{u"snap"_s, snap}}); });

    QMenu* file = menuBar()->addMenu(u"&File"_s);
    file->addAction(newAction);
    file->addAction(open);
    m_recentMenu = file->addMenu(u"Open recent"_s);
    file->addAction(save);
    file->addAction(saveAs);
    file->addSeparator();
    file->addAction(newProject);
    file->addAction(openProject);
    file->addAction(importModel);
    file->addSeparator();
    file->addAction(sceneSettings);
    file->addAction(settings);
    QMenu* edit = menuBar()->addMenu(u"&Edit"_s);
    edit->addAction(m_undoAction);
    edit->addAction(m_redoAction);
    edit->addSeparator();
    edit->addAction(createEntity);
    edit->addAction(duplicate);
    edit->addAction(deleteEntity);
    QMenu* view = menuBar()->addMenu(u"&View"_s);
    view->addAction(focus);
    view->addSeparator();
    view->addAction(m_orbitAction);
    view->addAction(m_flyAction);
    view->addSeparator();
    view->addAction(m_translateAction);
    view->addAction(m_rotateAction);
    view->addAction(m_scaleAction);
    view->addAction(m_localAction);
    view->addAction(m_snapAction);
    view->addSeparator();
    // F9 anywhere in the window, as in Meow2Renderer: keeps the next frame (the Render graph dock shows it).
    QAction* captureFrame = addCommandAction(u"Capture frame"_s, u"capture.frame"_s);
    captureFrame->setShortcut(QKeySequence{Qt::Key_F9});
    addAction(captureFrame);
    view->addAction(captureFrame);
    view->addSeparator();
    view->addAction(m_playAction);
    view->addAction(m_pauseAction);
    QMenu* window = menuBar()->addMenu(u"&Window"_s);
    window->addAction(m_consoleAction);
    window->addAction(m_gameAction);
    window->addAction(m_assetsAction);
    window->addAction(m_hierarchyAction);
    window->addAction(m_inspectorAction);
    window->addAction(m_sceneSettingsAction);
    window->addAction(m_debugAction);
    window->addAction(m_statsAction);
    window->addAction(m_renderGraphAction);
    QMenu* help = menuBar()->addMenu(u"&Help"_s);
    // The line that connects Claude Code to this editor (its address and token); greyed out without --mcp.
    help->addAction(addCommandAction(u"Copy MCP setup"_s, u"mcp.copySetup"_s));

    QToolBar* toolbar = addToolBar(u"Main"_s);
    toolbar->setObjectName(u"MainToolbar"_s);
    toolbar->setMovable(false);
    toolbar->addAction(save);
    toolbar->addSeparator();
    toolbar->addAction(m_undoAction);
    toolbar->addAction(m_redoAction);
    toolbar->addSeparator();
    toolbar->addAction(m_translateAction);
    toolbar->addAction(m_rotateAction);
    toolbar->addAction(m_scaleAction);
    toolbar->addAction(m_localAction);
    toolbar->addAction(m_snapAction);
    toolbar->addSeparator();
    toolbar->addSeparator();
    toolbar->addAction(m_playAction);
    toolbar->addAction(m_pauseAction);
    toolbar->addSeparator();
    toolbar->addAction(settings);
}

void EditorMainWindow::registerViewCommands()
{
    QJsonObject panel = propertySchema(u"string"_s, u"Which panel."_s);
    panel.insert(u"enum",
                 QJsonArray{c_assetsPanel, c_consolePanel, c_debugPanel, c_gamePanel, c_hierarchyPanel,
                            c_inspectorPanel, c_renderGraphPanel, c_sceneSettingsPanel, c_settingsPanel, c_statsPanel});
    [[maybe_unused]] const Result<void> added = m_commands.add(
        {.name = u"view.showPanel"_s,
         .description = u"Shows or hides a panel: the Assets, console, Debug, game view, Hierarchy, Inspector, Render "
                        u"graph, Scene settings or Stats dock, or the settings window."_s,
         .parameters = objectSchema({{.name = u"panel"_s, .schema = panel, .required = true},
                                     {.name = u"visible"_s,
                                      .schema = propertySchema(u"boolean"_s, u"Shown or hidden."_s),
                                      .required = true}})},
        [this](const QJsonObject& parameters) -> Result<CommandOutcome>
        {
            const bool visible = parameters.value(u"visible").toBool();
            const QString name = parameters.value(u"panel").toString();
            QWidget* widget = name == c_consolePanel         ? static_cast<QWidget*>(m_console)
                              : name == c_gamePanel          ? static_cast<QWidget*>(m_gameDock)
                              : name == c_assetsPanel        ? static_cast<QWidget*>(m_assetsDock)
                              : name == c_hierarchyPanel     ? static_cast<QWidget*>(m_hierarchyDock)
                              : name == c_inspectorPanel     ? static_cast<QWidget*>(m_inspectorDock)
                              : name == c_sceneSettingsPanel ? static_cast<QWidget*>(m_sceneSettingsDock)
                              : name == c_debugPanel         ? static_cast<QWidget*>(m_debugDock)
                              : name == c_statsPanel         ? static_cast<QWidget*>(m_statsDock)
                              : name == c_renderGraphPanel   ? static_cast<QWidget*>(m_renderGraphDock)
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

HierarchyDock& EditorMainWindow::hierarchyDock()
{
    return *m_hierarchyDock;
}

InspectorDock& EditorMainWindow::inspectorDock()
{
    return *m_inspectorDock;
}

DebugDock& EditorMainWindow::debugDock()
{
    return *m_debugDock;
}

StatsDock& EditorMainWindow::statsDock()
{
    return *m_statsDock;
}

RenderGraphDock& EditorMainWindow::renderGraphDock()
{
    return *m_renderGraphDock;
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
