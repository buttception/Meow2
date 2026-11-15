#pragma once

/// @file
/// @brief GameViewportDock: the dock that shows the game through its main camera, with a render-scale drop-down.

#include <QDockWidget>

class QComboBox;

namespace Meow2::Editor
{
class ViewportWidget;

/// @brief The game viewport's dock: a native viewport widget under a small toolbar with the render scale (100, 75,
///        50 or 25 %).
///
/// The drop-down only *asks*: choosing a value runs `viewport.setRenderScale` (the main window connects it), and
/// showRenderScale() puts the setting back into it, so an agent's change over MCP shows here too.
/// @par Thread safety
/// Not thread-safe; GUI thread only, like every QWidget.
class GameViewportDock final : public QDockWidget
{
public:
    /// @brief The render scales the drop-down offers, in percent.
    static constexpr int c_scales[] = {100, 75, 50, 25};

    /// @brief Creates the dock with its viewport and drop-down.
    /// @param parent Owning widget, usually the main window.
    explicit GameViewportDock(QWidget* parent = nullptr);

    /// @brief The game viewport.
    /// @return The widget, owned by the dock.
    [[nodiscard]] ViewportWidget& viewport()
    {
        return *m_viewport;
    }

    /// @brief The render-scale drop-down (each item's data is its percentage).
    /// @return The combo box, owned by the dock.
    [[nodiscard]] QComboBox& renderScaleBox()
    {
        return *m_scale;
    }

    /// @brief Shows a render scale in the drop-down without emitting a change.
    /// @param percent 25 to 100; a value the list doesn't offer is added.
    void showRenderScale(int percent);

private:
    ViewportWidget* m_viewport; ///< Owned by Qt (parent–child).
    QComboBox* m_scale;         ///< Owned by Qt.
};
} // namespace Meow2::Editor
