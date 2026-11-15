/// @file
/// @brief EditorMainWindow implementation.

#include "EditorMainWindow.h"

#include "Commands/CommandSchema.h"
#include "Panels/ConsoleDock.h"
#include "Panels/SettingsDialog.h"
#include "Viewports/GameViewportDock.h"
#include "Viewports/QtInputSource.h"
#include "Viewports/ViewportController.h"

#include <Meow2/Core/Assert.h>

#include <QAction>
#include <QActionGroup>
#include <QByteArray>
#include <QComboBox>
#include <QJsonArray>
#include <QKeySequence>
#include <QMenu>
#include <QMenuBar>
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
{
    m_viewport->setObjectName(u"EditorViewport"_s);
    setCentralWidget(m_viewport);
    addDockWidget(Qt::RightDockWidgetArea, m_gameDock);
    addDockWidget(Qt::BottomDockWidgetArea, m_console);
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
    QAction* save = addCommandAction(u"Save"_s, u"scene.save"_s); // enabled once scenes exist (PR 3)
    save->setShortcut(QKeySequence::Save);
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
    file->addAction(save);
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
    panel.insert(u"enum", QJsonArray{c_consolePanel, c_gamePanel, c_settingsPanel});
    [[maybe_unused]] const Result<void> added = m_commands.add(
        {.name = u"view.showPanel"_s,
         .description = u"Shows or hides a panel: the console dock, the game view dock or the settings window."_s,
         .parameters = objectSchema({{.name = u"panel"_s, .schema = panel, .required = true},
                                     {.name = u"visible"_s,
                                      .schema = propertySchema(u"boolean"_s, u"Shown or hidden."_s),
                                      .required = true}})},
        [this](const QJsonObject& parameters) -> Result<CommandOutcome>
        {
            const bool visible = parameters.value(u"visible").toBool();
            const QString name = parameters.value(u"panel").toString();
            QWidget* widget = name == c_consolePanel ? static_cast<QWidget*>(m_console)
                              : name == c_gamePanel  ? static_cast<QWidget*>(m_gameDock)
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
} // namespace Meow2::Editor
