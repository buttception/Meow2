/// @file
/// @brief DebugDock (DebugDock.h).

#include "Panels/DebugDock.h"

#include "Panels/FittingScrollArea.h"

#include <Meow2/Graphics/Renderer/RenderDebug.h>

#include <algorithm>
#include <QButtonGroup>
#include <QCheckBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QRadioButton>
#include <QToolButton>
#include <QVBoxLayout>
#include <string_view>

namespace Meow2::Editor
{
using namespace Qt::StringLiterals;

namespace
{
/// @brief A string as a QString.
QString toQString(std::string_view text)
{
    return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

/// @brief A grey section heading, as the Renderer's separators.
QLabel* heading(const QString& text, QWidget* parent)
{
    auto* label = new QLabel(text, parent);
    label->setProperty("debugHeading", true);
    return label;
}

constexpr int c_viewColumns = 3; ///< View buttons per row: wide enough for "Geometry normal" at the dock's width.
} // namespace

DebugDock::DebugDock(EditorCommandRegistry& commands, Log::Logger& log, QWidget* parent)
    : QDockWidget(u"Debug"_s, parent)
    , m_commands(commands)
    , m_log(log)
{
    setObjectName(u"DebugDock"_s); // QMainWindow::saveState() keys docks by object name
    // The chosen view in the theme's red; a view made by a debug-only pass has a dashed outline.
    setStyleSheet(u"QToolButton[debugView=\"true\"] { padding: 3px 6px; }"
                  u"QToolButton[debugView=\"true\"]:checked { background: #E01E2D; color: #FFFFFF; }"
                  u"QToolButton[generated=\"true\"] { border-style: dashed; }"
                  u"QLabel[debugHeading=\"true\"] { color: #8A8A90; font-weight: 600; margin-top: 6px; }"_s);
    auto* scroll = new FittingScrollArea(this);
    auto* content = new QWidget();
    auto* layout = new QVBoxLayout(content);
    layout->setContentsMargins(8, 6, 8, 8);

    layout->addWidget(heading(u"VIEW"_s, content));
    QGridLayout* grid = nullptr;
    std::string_view group;
    int index = 0;
    for (const Renderer::DebugViewEntry& entry : Renderer::debugViewEntries())
    {
        if (entry.group != group)
        {
            group = entry.group;
            auto* groupLabel = new QLabel(toQString(group), content);
            groupLabel->setStyleSheet(u"color: #8A8A90;"_s);
            layout->addWidget(groupLabel);
            grid = new QGridLayout();
            grid->setSpacing(4);
            layout->addLayout(grid);
            index = 0;
        }
        const QString key = toQString(entry.key);
        auto* button = new QToolButton(content);
        button->setObjectName(u"View."_s + key);
        button->setText(toQString(entry.label));
        button->setCheckable(true);
        button->setProperty("debugView", true);
        button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        if (entry.generated)
        {
            button->setProperty("generated", true);
            button->setToolTip(u"Shown from a buffer made only for this view (dashed): a debug pass rebuilds it, and "
                               u"nothing in the frame uses it."_s);
        }
        connect(button, &QToolButton::clicked, this, [this, key] { run(u"debug.setView"_s, {{u"view"_s, key}}); });
        grid->addWidget(button, index / c_viewColumns, index % c_viewColumns);
        m_views.emplace_back(key, button);
        ++index;
    }
    m_hint = new QLabel(content);
    m_hint->setObjectName(u"ViewHint"_s);
    m_hint->setWordWrap(true);
    m_hint->setStyleSheet(u"color: #8A8A90;"_s);
    layout->addWidget(m_hint);

    layout->addWidget(heading(u"WIREFRAME"_s, content));
    auto* wireframeRow = new QHBoxLayout();
    auto* wireframeGroup = new QButtonGroup(this); // one checked at a time
    for (const auto& [mode, label] :
         {std::pair{u"off"_s, u"Off"_s}, std::pair{u"overlay"_s, u"Overlay"_s}, std::pair{u"only"_s, u"Only"_s}})
    {
        auto* radio = new QRadioButton(label, content);
        radio->setObjectName(u"Wireframe."_s + mode);
        wireframeGroup->addButton(radio);
        connect(radio, &QRadioButton::clicked, this,
                [this, mode] { run(u"debug.setWireframe"_s, {{u"mode"_s, mode}}); });
        wireframeRow->addWidget(radio);
        m_wireframe.emplace_back(mode, radio);
    }
    wireframeRow->addStretch(1);
    layout->addLayout(wireframeRow);

    layout->addWidget(heading(u"CULLING"_s, content));
    m_freeze = new QCheckBox(u"Freeze"_s, content);
    m_freeze->setObjectName(u"FreezeCulling"_s);
    m_freeze->setToolTip(u"Keep culling with the camera's view of now: fly away and see what was culled."_s);
    connect(m_freeze, &QCheckBox::clicked, this, [this](bool on) { run(u"debug.setCulling"_s, {{u"freeze"_s, on}}); });
    m_showFrustum = new QCheckBox(u"Show frustum"_s, content);
    m_showFrustum->setObjectName(u"ShowFrustum"_s);
    m_showFrustum->setToolTip(u"While frozen, draw the frozen camera's view volume (yellow), through the scene."_s);
    connect(m_showFrustum, &QCheckBox::clicked, this,
            [this](bool on) { run(u"debug.setCulling"_s, {{u"showFrustum"_s, on}}); });
    auto* cullingRow = new QHBoxLayout();
    cullingRow->addWidget(m_freeze);
    cullingRow->addWidget(m_showFrustum);
    cullingRow->addStretch(1);
    layout->addLayout(cullingRow);

    layout->addWidget(heading(u"GRID"_s, content));
    m_grid = new QCheckBox(u"Ground grid"_s, content);
    m_grid->setObjectName(u"Grid"_s);
    m_grid->setToolTip(u"An endless grid on the ground (y = 0): the X axis red, the Z axis blue."_s);
    connect(m_grid, &QCheckBox::clicked, this, [this](bool on) { run(u"debug.setGrid"_s, {{u"on"_s, on}}); });
    layout->addWidget(m_grid);

    m_failed = new QLabel(u"Debug pipelines failed to build; see the console."_s, content);
    m_failed->setStyleSheet(u"color: #FF5A66;"_s);
    m_failed->setWordWrap(true);
    m_failed->hide();
    layout->addWidget(m_failed);
    layout->addStretch(1);
    scroll->setWidget(content);
    scroll->contentChanged();
    setWidget(scroll);
}

void DebugDock::run(const QString& command, const QJsonObject& parameters)
{
    if (const Result<QJsonValue> ran = m_commands.execute(command, parameters); !ran)
    {
        MEOW2_LOG_WARN(m_log, "{}: {}", command.toStdString(), ran.error());
    }
    refresh(); // show the result at once, not at the next tick
}

void DebugDock::refresh()
{
    const Result<QJsonValue> state = m_commands.execute(u"debug.state"_s, {});
    if (!state || state->toObject() == m_shown)
    {
        return;
    }
    show(state->toObject());
}

void DebugDock::show(const QJsonObject& state)
{
    m_shown = state;
    const QString view = state.value(u"view").toString();
    for (const auto& [key, button] : m_views)
    {
        button->setChecked(key == view); // setChecked doesn't emit clicked: no command runs from here
    }
    const Renderer::DebugViewEntry* entry = Renderer::findDebugView(std::string_view{view.toStdString()});
    m_hint->setText(entry != nullptr ? toQString(entry->hint) : QString{});
    m_hint->setVisible(!m_hint->text().isEmpty());
    const QString wireframe = state.value(u"wireframe").toString();
    for (const auto& [mode, radio] : m_wireframe)
    {
        radio->setChecked(mode == wireframe);
    }
    m_freeze->setChecked(state.value(u"freezeCulling").toBool());
    m_showFrustum->setChecked(state.value(u"showFrustum").toBool());
    m_grid->setChecked(state.value(u"grid").toBool());
    m_failed->setVisible(state.value(u"failed").toBool());
}
} // namespace Meow2::Editor
