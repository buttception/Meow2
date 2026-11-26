/// @file
/// @brief FlowView (FlowView.h).

#include "Panels/FlowView.h"

#include "EditorTheme.h"

#include <algorithm>
#include <cmath>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <string_view>

namespace Meow2::Editor
{
namespace
{
/// @brief A layout value (float, from the engine) as Qt's qreal (double), said out loud: GCC and Clang flag the
///        implicit widening (-Wdouble-promotion).
qreal q(float value)
{
    return static_cast<qreal>(value);
}
} // namespace

QColor flowKindColor(Renderer::ResourceKind kind)
{
    switch (kind)
    {
        case Renderer::ResourceKind::ColorTexture:
            return QColor::fromRgbF(0.96F, 0.62F, 0.22F); // orange
        case Renderer::ResourceKind::DepthTexture:
            return QColor::fromRgbF(0.30F, 0.78F, 0.92F); // cyan
        case Renderer::ResourceKind::Buffer:
            return QColor::fromRgbF(0.48F, 0.82F, 0.45F); // green
        case Renderer::ResourceKind::IndirectBuffer:
            return QColor::fromRgbF(0.74F, 0.50F, 0.98F); // violet
    }
    return Qt::white;
}

FlowView::FlowView(QWidget* parent)
    : QWidget(parent)
{
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
}

void FlowView::layOut()
{
    // The Renderer's spacing, scaled to this font: a pin row is a line of text with room around it.
    const float line = static_cast<float>(fontMetrics().height());
    m_metrics.titleHeight = line + 8.0F;
    m_metrics.pinRow = line + 4.0F;
    m_metrics.pad = 8.0F;
    const QFontMetrics metrics = fontMetrics();
    m_geometry = Renderer::placeFlow(m_flow, m_metrics,
                                     [&metrics](std::string_view text)
                                     {
                                         return static_cast<float>(metrics.horizontalAdvance(
                                             QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()))));
                                     });
    setFixedSize(sizeHint());
    update();
}

void FlowView::setFlow(Renderer::FrameFlow flow)
{
    m_flow = std::move(flow);
    m_selectedPass.reset();
    m_selectedPin.reset();
    layOut();
}

void FlowView::setSelection(std::optional<std::uint32_t> pass, std::optional<Renderer::FlowPin> pin)
{
    m_selectedPass = pass;
    m_selectedPin = pin;
    update();
}

QSize FlowView::sizeHint() const
{
    return {static_cast<int>(std::ceil(m_geometry.width)), static_cast<int>(std::ceil(m_geometry.height))};
}

std::optional<QPointF> FlowView::pinPosition(const Renderer::FlowPin& pin) const
{
    if (pin.pass >= m_geometry.nodes.size())
    {
        return std::nullopt;
    }
    const Renderer::FlowNode& node = m_geometry.nodes[pin.pass];
    const std::vector<std::uint32_t>& pins = pin.output ? node.pins.outputs : node.pins.inputs;
    const auto found = std::ranges::find(pins, pin.resource);
    if (found == pins.end())
    {
        return std::nullopt;
    }
    const auto row = static_cast<float>(found - pins.begin());
    return QPointF{q(pin.output ? node.x + node.width : node.x),
                   q(node.y + m_metrics.titleHeight + (row + 0.5F) * m_metrics.pinRow)};
}

void FlowView::paintEvent(QPaintEvent* /*event*/)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.fillRect(rect(), QColor{0x0C, 0x0C, 0x0D});
    const QColor accent{EditorColors::c_accent};

    // Wires first, under the nodes: from the pass that last wrote a resource to each pass that reads it.
    for (std::uint32_t pass = 0; pass < m_geometry.nodes.size(); ++pass)
    {
        for (const std::uint32_t resource : m_geometry.nodes[pass].pins.inputs)
        {
            const std::optional<std::uint32_t> writer = Renderer::writerBefore(m_flow, resource, pass);
            if (!writer)
            {
                continue; // from before the frame: no wire
            }
            const std::optional<QPointF> from = pinPosition({.pass = *writer, .resource = resource, .output = true});
            const std::optional<QPointF> to = pinPosition({.pass = pass, .resource = resource, .output = false});
            if (!from || !to)
            {
                continue;
            }
            const bool selected = m_selectedPass == pass || m_selectedPass == *writer;
            QColor colour = flowKindColor(Renderer::resourceKind(m_flow, resource));
            colour.setAlphaF(selected ? 1.0F : 0.55F);
            QPainterPath wire(*from);
            const qreal bend = std::max(40.0, std::abs(to->x() - from->x()) * 0.5);
            wire.cubicTo(QPointF{from->x() + bend, from->y()}, QPointF{to->x() - bend, to->y()}, *to);
            painter.setPen(
                QPen{colour, selected ? 2.5 : 1.5, m_flow.resources[resource].debug ? Qt::DashLine : Qt::SolidLine});
            painter.setBrush(Qt::NoBrush);
            painter.drawPath(wire);
        }
    }

    // Nodes: a title bar, then a row per pin.
    for (std::uint32_t pass = 0; pass < m_geometry.nodes.size(); ++pass)
    {
        const Renderer::FlowNode& node = m_geometry.nodes[pass];
        const Renderer::FlowPass& flowPass = m_flow.passes[pass];
        const QRectF box{q(node.x), q(node.y), q(node.width), q(node.height)};
        const bool selected = m_selectedPass == pass;
        painter.setPen(QPen{selected ? accent : QColor{0x34, 0x34, 0x3A}, selected ? 2.0 : 1.0,
                            flowPass.debug ? Qt::DashLine : Qt::SolidLine});
        painter.setBrush(QColor{0x16, 0x16, 0x19});
        painter.drawRoundedRect(box, 4.0, 4.0);
        const QRectF title{box.x(), box.y(), box.width(), q(m_metrics.titleHeight)};
        painter.fillRect(title.adjusted(1.0, 1.0, -1.0, 0.0), QColor{0x22, 0x22, 0x27});
        painter.setPen(flowPass.skipped ? QColor{0x70, 0x70, 0x76} : QColor{0xE6, 0xE6, 0xE6});
        painter.drawText(title.adjusted(q(m_metrics.pad), 0.0, -q(m_metrics.pad), 0.0),
                         Qt::AlignVCenter | Qt::AlignLeft, QString::fromStdString(node.title));

        const auto drawPins = [&](const std::vector<std::uint32_t>& resources, bool output)
        {
            for (const std::uint32_t resource : resources)
            {
                const Renderer::FlowPin pin{.pass = pass, .resource = resource, .output = output};
                const QPointF at = *pinPosition(pin);
                const bool pinSelected = m_selectedPin == pin;
                painter.setPen(pinSelected ? QPen{Qt::white, 2.0} : QPen{Qt::NoPen});
                painter.setBrush(flowKindColor(Renderer::resourceKind(m_flow, resource)));
                painter.drawEllipse(at, q(m_metrics.dot), q(m_metrics.dot));
                painter.setPen(pinSelected ? QColor{Qt::white} : QColor{0xB4, 0xB4, 0xBA});
                const QString name = QString::fromStdString(m_flow.resources[resource].name);
                const qreal textX = output ? at.x() - q(m_metrics.dot) * 2.0 - fontMetrics().horizontalAdvance(name)
                                           : at.x() + q(m_metrics.dot) * 2.0;
                painter.drawText(QPointF{textX, at.y() + fontMetrics().ascent() * 0.4}, name);
            }
        };
        drawPins(node.pins.inputs, false);
        drawPins(node.pins.outputs, true);
    }
}

void FlowView::mousePressEvent(QMouseEvent* event)
{
    const QPointF at = event->position();
    // Pins first: they sit on the nodes' edges, and a pin is the more precise target.
    const double reach = q(m_metrics.dot) * 2.5;
    for (std::uint32_t pass = 0; pass < m_geometry.nodes.size(); ++pass)
    {
        const Renderer::FlowNode& node = m_geometry.nodes[pass];
        for (const bool output : {false, true})
        {
            for (const std::uint32_t resource : output ? node.pins.outputs : node.pins.inputs)
            {
                const Renderer::FlowPin pin{.pass = pass, .resource = resource, .output = output};
                const QPointF dot = *pinPosition(pin);
                if (std::hypot(dot.x() - at.x(), dot.y() - at.y()) <= reach)
                {
                    if (m_onPin)
                    {
                        m_onPin(pin);
                    }
                    return;
                }
            }
        }
    }
    for (std::uint32_t pass = 0; pass < m_geometry.nodes.size(); ++pass)
    {
        const Renderer::FlowNode& node = m_geometry.nodes[pass];
        if (QRectF{q(node.x), q(node.y), q(node.width), q(node.height)}.contains(at))
        {
            if (m_onPass)
            {
                m_onPass(pass);
            }
            return;
        }
    }
}
} // namespace Meow2::Editor
