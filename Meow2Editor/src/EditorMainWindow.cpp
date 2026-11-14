/// @file
/// @brief EditorMainWindow implementation.

#include "EditorMainWindow.h"

#include "Commands/CommandSchema.h"
#include "Panels/ConsoleDock.h"
#include "Panels/SettingsDialog.h"

#include <Meow2/Core/Assert.h>

#include <QAction>
#include <QJsonArray>
#include <QKeySequence>
#include <QMenu>
#include <QMenuBar>
#include <QToolBar>
#include <utility>

namespace Meow2::Editor
{
using namespace Qt::StringLiterals;

namespace
{
/// @brief `view.showPanel`'s panel names.
constexpr QLatin1StringView c_consolePanel{"console"};
constexpr QLatin1StringView c_settingsPanel{"settings"};
} // namespace

EditorMainWindow::EditorMainWindow(const EditorMainWindowDeps& deps, QWidget* parent)
    : QMainWindow(parent)
    , m_commands(deps.commands)
    , m_log(deps.log)
    , m_viewport(new ViewportWidget(this)) // Qt parent–child: the window deletes them
    , m_console(new ConsoleDock(deps.console, deps.settings.value(u"editor.console.followNewest"_s).toBool(), this))
    , m_settingsDialog(new SettingsDialog(deps.commands, deps.settings, this))
{
    setCentralWidget(m_viewport);
    addDockWidget(Qt::BottomDockWidgetArea, m_console);
    registerViewCommands();
    createMenusAndToolbar();
    tick();
}

EditorMainWindow::~EditorMainWindow()
{
    m_commands.remove(u"view.showPanel");
}

void EditorMainWindow::tick()
{
    m_console->tick();
    for (const CommandAction& bound : m_commandActions)
    {
        bound.action->setEnabled(m_commands.isEnabled(bound.command));
    }
    m_consoleAction->setChecked(m_console->isVisible());

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
    tick(); // show the effect (labels, enabled state) at once, not a frame later
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

    QMenu* file = menuBar()->addMenu(u"&File"_s);
    file->addAction(save);
    file->addSeparator();
    file->addAction(settings);
    QMenu* edit = menuBar()->addMenu(u"&Edit"_s);
    edit->addAction(m_undoAction);
    edit->addAction(m_redoAction);
    QMenu* window = menuBar()->addMenu(u"&Window"_s);
    window->addAction(m_consoleAction);

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
    panel.insert(u"enum", QJsonArray{c_consolePanel, c_settingsPanel});
    [[maybe_unused]] const Result<void> added = m_commands.add(
        {.name = u"view.showPanel"_s,
         .description = u"Shows or hides a panel: the console dock or the settings window."_s,
         .parameters = objectSchema({{.name = u"panel"_s, .schema = panel, .required = true},
                                     {.name = u"visible"_s,
                                      .schema = propertySchema(u"boolean"_s, u"Shown or hidden."_s),
                                      .required = true}})},
        [this](const QJsonObject& parameters) -> Result<CommandOutcome>
        {
            const bool visible = parameters.value(u"visible").toBool();
            QWidget* widget = parameters.value(u"panel").toString() == c_consolePanel
                                  ? static_cast<QWidget*>(m_console)
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
