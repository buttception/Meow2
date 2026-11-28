/// @file
/// @brief StatsDock (StatsDock.h).

#include "Panels/StatsDock.h"

#include "EditorTheme.h"
#include "Panels/FittingScrollArea.h"

#include <algorithm>
#include <array>
#include <QHeaderView>
#include <QJsonArray>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QTableWidget>
#include <QVBoxLayout>

namespace Meow2::Editor
{
using namespace Qt::StringLiterals;

QString formatBytes(double bytes)
{
    constexpr std::array<const char*, 4> c_units{"B", "KiB", "MiB", "GiB"};
    std::size_t unit = 0;
    while (bytes >= 1024.0 && unit + 1 < c_units.size())
    {
        bytes /= 1024.0;
        ++unit;
    }
    if (unit == 0)
    {
        return u"%1 B"_s.arg(static_cast<qint64>(bytes));
    }
    return u"%1 %2"_s.arg(bytes, 0, 'f', bytes < 10.0 ? 2 : 1).arg(QLatin1StringView{c_units[unit]});
}

FrameGraph::FrameGraph(QWidget* parent)
    : QWidget(parent)
{
    setMinimumHeight(48);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void FrameGraph::setFrames(std::vector<float> milliseconds)
{
    m_frames = std::move(milliseconds);
    update();
}

void FrameGraph::paintEvent(QPaintEvent* /*event*/)
{
    QPainter painter(this);
    painter.fillRect(rect(), QColor{0x0C, 0x0C, 0x0D});
    if (m_frames.size() < 2)
    {
        return;
    }
    const float top = std::max(*std::ranges::max_element(m_frames), 1.0F) * 1.1F;
    const auto y = [this, top](float milliseconds)
    {
        return static_cast<qreal>(height()) * (1.0 - static_cast<qreal>(milliseconds / top));
    };
    constexpr float c_sixtyHz = 1000.0F / 60.0F;
    if (c_sixtyHz < top)
    {
        // The 60 frames a second line, so a glance says whether the editor keeps up.
        painter.setPen(QPen{QColor{0x34, 0x34, 0x3A}, 1.0, Qt::DashLine});
        painter.drawLine(QPointF{0.0, y(c_sixtyHz)}, QPointF{static_cast<qreal>(width()), y(c_sixtyHz)});
    }
    QPainterPath path;
    const qreal step = static_cast<qreal>(width()) / static_cast<qreal>(m_frames.size() - 1);
    path.moveTo(0.0, y(m_frames.front()));
    for (std::size_t i = 1; i < m_frames.size(); ++i)
    {
        path.lineTo(step * static_cast<qreal>(i), y(m_frames[i]));
    }
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen{QColor{EditorColors::c_accent}, 1.5});
    painter.drawPath(path);
}

StatsDock::StatsDock(EditorCommandRegistry& commands, QWidget* parent)
    : QDockWidget(u"Stats"_s, parent)
    , m_commands(commands)
{
    setObjectName(u"StatsDock"_s); // QMainWindow::saveState() keys docks by object name
    auto* scroll = new FittingScrollArea(this);
    auto* content = new QWidget();
    auto* layout = new QVBoxLayout(content);
    layout->setContentsMargins(8, 6, 8, 8);
    const QFont mono = editorMonospaceFont(); // figures line up in a fixed-width font
    const auto label = [content, &mono, layout](const QString& name)
    {
        auto* made = new QLabel(content);
        made->setObjectName(name);
        made->setFont(mono);
        made->setTextInteractionFlags(Qt::TextSelectableByMouse);
        made->setWordWrap(true);
        layout->addWidget(made);
        return made;
    };
    m_frame = label(u"FrameTime"_s);
    m_graph = new FrameGraph(content);
    m_graph->setObjectName(u"FrameGraph"_s);
    layout->addWidget(m_graph);
    m_worst = label(u"WorstFrame"_s);
    m_gpu = label(u"GpuInfo"_s);
    m_scene = label(u"SceneCounts"_s);
    m_memory = new QTableWidget(0, 3, content);
    m_memory->setObjectName(u"MemoryTable"_s);
    m_memory->setHorizontalHeaderLabels({u"Tag"_s, u"CPU"_s, u"GPU"_s});
    m_memory->verticalHeader()->hide();
    m_memory->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_memory->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_memory->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_memory->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_memory->setSelectionMode(QAbstractItemView::NoSelection);
    m_memory->setFont(mono);
    m_memory->setMinimumHeight(120);
    layout->addWidget(m_memory, 1);
    m_untracked = new QLabel(u"CPU memory isn't tracked in Release builds."_s, content);
    m_untracked->setStyleSheet(u"color: #8A8A90;"_s);
    m_untracked->hide();
    layout->addWidget(m_untracked);
    scroll->setWidget(content);
    scroll->contentChanged();
    setWidget(scroll);
}

void StatsDock::refresh(bool force)
{
    constexpr qint64 c_refreshMilliseconds = 250; // readable numbers, and no cost worth measuring
    if (!force && (!isVisible() || (m_sinceRefresh.isValid() && m_sinceRefresh.elapsed() < c_refreshMilliseconds)))
    {
        return;
    }
    m_sinceRefresh.start();
    if (const Result<QJsonValue> stats = m_commands.execute(u"stats.frame"_s, {}); stats)
    {
        show(stats->toObject());
    }
}

void StatsDock::show(const QJsonObject& stats)
{
    const double frameMs = stats.value(u"frameMs").toDouble();
    m_frame->setText(u"%1 ms  (%2 FPS)"_s.arg(frameMs, 0, 'f', 2).arg(stats.value(u"fps").toDouble(), 0, 'f', 0));
    std::vector<float> frames;
    for (const QJsonValue& frame : stats.value(u"frames").toArray())
    {
        frames.push_back(static_cast<float>(frame.toDouble()));
    }
    m_worst->setText(
        u"Worst %1 ms (last %2 frames)"_s.arg(stats.value(u"worstMs").toDouble(), 0, 'f', 2).arg(frames.size()));
    m_graph->setFrames(std::move(frames));

    if (stats.contains(u"backend"))
    {
        QString gpu = u"%1 on %2\nGPU memory: %3 of %4 budget"_s.arg(
            stats.value(u"backend").toString(), stats.value(u"gpu").toString(),
            formatBytes(stats.value(u"gpuMemoryUsed").toDouble()),
            formatBytes(stats.value(u"gpuMemoryBudget").toDouble()));
        if (const double errors = stats.value(u"validationErrors").toDouble(); errors > 0.0)
        {
            gpu += u"\nValidation errors: %1"_s.arg(static_cast<qint64>(errors));
        }
        m_gpu->setText(gpu);
        m_scene->setText(
            u"Entities %1   Instances %2\nMeshes %3   Materials %4   Textures %5\nDrawn through the %6 path"_s
                .arg(static_cast<qint64>(stats.value(u"entities").toDouble()))
                .arg(static_cast<qint64>(stats.value(u"instances").toDouble()))
                .arg(static_cast<qint64>(stats.value(u"meshes").toDouble()))
                .arg(static_cast<qint64>(stats.value(u"materials").toDouble()))
                .arg(static_cast<qint64>(stats.value(u"textures").toDouble()))
                .arg(stats.value(u"drawPath").toString()));
    }
    else
    {
        m_gpu->setText(u"No GPU (drawing is off)."_s);
        m_scene->setText(u"Entities %1"_s.arg(static_cast<qint64>(stats.value(u"entities").toDouble())));
    }

    const QJsonArray memory = stats.value(u"memory").toArray();
    m_memory->setRowCount(static_cast<int>(memory.size()));
    for (int row = 0; row < memory.size(); ++row)
    {
        const QJsonObject entry = memory.at(row).toObject();
        const auto cell = [this, row](int column, const QString& text, bool right)
        {
            auto* item = new QTableWidgetItem(text); // the table owns it
            item->setTextAlignment(right ? Qt::AlignRight | Qt::AlignVCenter : Qt::AlignLeft | Qt::AlignVCenter);
            m_memory->setItem(row, column, item);
        };
        cell(0, entry.value(u"tag").toString(), false);
        cell(1, formatBytes(entry.value(u"cpuBytes").toDouble()), true);
        cell(2, formatBytes(entry.value(u"gpuBytes").toDouble()), true);
    }
    m_untracked->setVisible(!stats.value(u"cpuMemoryTracked").toBool());
}
} // namespace Meow2::Editor
