#pragma once

/// @file
/// @brief FlowView: a captured frame's render graph drawn as a node graph in Qt.

#include <Meow2/Graphics/Renderer/FrameFlow.h>

#include <cstdint>
#include <functional>
#include <optional>
#include <QColor>
#include <QPointF>
#include <QWidget>

namespace Meow2::Editor
{
/// @brief A resource kind's colour for pins and wires, as in Meow2Renderer: colour textures orange, depth cyan,
///        buffers green, indirect arguments violet.
/// @param kind The kind.
/// @return The colour.
[[nodiscard]] QColor flowKindColor(Renderer::ResourceKind kind);

/// @brief Draws a FrameFlow as a node graph: a node per pass placed by Renderer::placeFlow() (levels top to bottom),
///        input pins on the left, outputs on the right, wires from each output to the inputs that read it, coloured
///        by resource kind. Debug passes have a dashed border; skipped ones are greyed. A click on a node or a pin is
///        reported (onPassClicked(), onPinClicked()); the selection is drawn highlighted.
///
/// Its size is the graph's: put it in a scroll area.
/// @par Thread safety
/// Not thread-safe; the GUI thread.
class FlowView final : public QWidget
{
public:
    /// @brief An empty view.
    /// @param parent Its parent (which owns it).
    explicit FlowView(QWidget* parent);

    /// @brief Shows a flow (and clears the selection).
    /// @param flow The flow.
    void setFlow(Renderer::FrameFlow flow);

    /// @brief The flow shown.
    /// @return It.
    [[nodiscard]] const Renderer::FrameFlow& flow() const
    {
        return m_flow;
    }

    /// @brief Where its nodes are.
    /// @return The placement.
    [[nodiscard]] const Renderer::FlowGeometry& geometry() const
    {
        return m_geometry;
    }

    /// @brief Highlights a pass and a pin.
    /// @param pass The pass, or nothing.
    /// @param pin  The pin, or nothing.
    void setSelection(std::optional<std::uint32_t> pass, std::optional<Renderer::FlowPin> pin);

    /// @brief Where a pin's dot is, in the view's coordinates.
    /// @param pin The pin.
    /// @return Its centre, or nothing if the pass has no such pin.
    [[nodiscard]] std::optional<QPointF> pinPosition(const Renderer::FlowPin& pin) const;

    /// @brief Called with a pass's index when its node (not a pin) is clicked.
    /// @param callback The callback.
    void onPassClicked(std::function<void(std::uint32_t)> callback)
    {
        m_onPass = std::move(callback);
    }

    /// @brief Called with a pin when it's clicked.
    /// @param callback The callback.
    void onPinClicked(std::function<void(const Renderer::FlowPin&)> callback)
    {
        m_onPin = std::move(callback);
    }

    /// @brief The graph's size.
    /// @return It.
    [[nodiscard]] QSize sizeHint() const override;

protected:
    /// @brief Draws the wires, then the nodes and their pins.
    /// @param event Unused.
    void paintEvent(QPaintEvent* event) override;

    /// @brief Reports a click on a pin (first) or a node.
    /// @param event The click.
    void mousePressEvent(QMouseEvent* event) override;

private:
    /// @brief Places the nodes again (a new flow, or a font change).
    void layOut();

    Renderer::FrameFlow m_flow;
    Renderer::FlowMetrics m_metrics;
    Renderer::FlowGeometry m_geometry;
    std::optional<std::uint32_t> m_selectedPass;
    std::optional<Renderer::FlowPin> m_selectedPin;
    std::function<void(std::uint32_t)> m_onPass;
    std::function<void(const Renderer::FlowPin&)> m_onPin;
};
} // namespace Meow2::Editor
