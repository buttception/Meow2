#pragma once

/// @file
/// @brief A scroll area that keeps its panel at least as wide as its content (the Inspector, Scene settings).

#include <QScrollArea>

namespace Meow2::Editor
{
/// @brief A scroll area never narrower than its content plus the vertical scroll bar (owner request, #229: a panel
///        can't be squeezed until the bar covers its fields or they stop working). Asked each time Qt lays the
///        window out, so it follows the content as it's rebuilt; the horizontal bar is off.
/// @par Thread safety
/// GUI thread only, as every widget.
class FittingScrollArea final : public QScrollArea
{
public:
    /// @brief An empty scroll area that resizes its widget to fit.
    /// @param parent Its parent (which owns it).
    explicit FittingScrollArea(QWidget* parent);

    /// @brief The content's minimum width, the bar and the frame; Qt's own minimum is tiny (a scroll area can scroll).
    /// @return The minimum size.
    [[nodiscard]] QSize minimumSizeHint() const override;

    /// @brief Asks the window to lay out again once a rebuilt content's widgets are shown: widgets added to a shown
    ///        panel stay out of its layout's sizes until then, so measuring at once came out far too small.
    void contentChanged();
};
} // namespace Meow2::Editor
