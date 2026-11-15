/// @file
/// @brief GameViewportDock (GameViewportDock.h).

#include "Viewports/GameViewportDock.h"

#include "ViewportWidget.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QSignalBlocker>
#include <QVBoxLayout>

namespace Meow2::Editor
{
using namespace Qt::StringLiterals;

GameViewportDock::GameViewportDock(QWidget* parent)
    : QDockWidget(u"Game"_s, parent)
{
    setObjectName(u"GameDock"_s);      // QMainWindow::saveState() keys docks by object name
    auto* content = new QWidget(this); // Qt parent–child ownership throughout
    auto* layout = new QVBoxLayout(content);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    auto* toolbar = new QHBoxLayout();
    toolbar->setContentsMargins(6, 4, 6, 4);
    toolbar->addWidget(new QLabel(u"Render scale"_s, content));
    m_scale = new QComboBox(content);
    m_scale->setObjectName(u"RenderScale"_s);
    m_scale->setToolTip(u"Draw the game smaller and scale it up, for less GPU load."_s);
    for (const int percent : c_scales)
    {
        m_scale->addItem(QString::number(percent) + u" %"_s, percent);
    }
    toolbar->addWidget(m_scale);
    toolbar->addStretch(1);
    layout->addLayout(toolbar);
    m_viewport = new ViewportWidget(content);
    m_viewport->setObjectName(u"GameViewport"_s);
    layout->addWidget(m_viewport, 1);
    setWidget(content);
}

void GameViewportDock::showRenderScale(int percent)
{
    const QSignalBlocker block{m_scale};
    int index = m_scale->findData(percent);
    if (index < 0)
    {
        m_scale->addItem(QString::number(percent) + u" %"_s, percent); // a value set from the settings window
        index = m_scale->count() - 1;
    }
    m_scale->setCurrentIndex(index);
}
} // namespace Meow2::Editor
