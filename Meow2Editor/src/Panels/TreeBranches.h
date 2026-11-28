#pragma once

/// @file
/// @brief Fold arrows for the editor's tree views, drawn by the views themselves.

#include <QColor>
#include <QPainter>
#include <QPolygonF>
#include <QTreeView>

namespace Meow2::Editor
{
/// @brief Draws a tree row's branch area: the selection colour when it's selected, and a fold arrow (pointing right
///        when folded, down when open) when the row has children.
///
/// Qt's style sheets draw the branch area themselves once any `QTreeView::branch` rule exists (the theme has one,
/// for the selection colour), and then draw no arrows at all unless the sheet names arrow images: rows with
/// children looked like rows without (owner report). Drawing them here needs no image files and follows the theme.
/// Clicking the arrow still folds the row: QTreeView finds clicks by the indentation, not by what's drawn.
/// @param view    The tree.
/// @param painter Paints the row.
/// @param rect    The branch area: everything left of the row's item, its indentation included.
/// @param index   The row.
inline void drawTreeBranch(const QTreeView& view, QPainter& painter, const QRect& rect, const QModelIndex& index)
{
    if (view.selectionModel() != nullptr && view.selectionModel()->isSelected(index))
    {
        painter.fillRect(rect, QColor(224, 30, 45, 51)); // the theme's selection (EditorTheme.cpp), 20 % red
    }
    if (!view.model()->hasChildren(index))
    {
        return;
    }
    // The arrow sits in the last indentation step, the one belonging to this row (the others are its parents').
    const int step = view.indentation();
    const QRectF area(rect.right() + 1 - step, rect.top(), step, rect.height());
    const QPointF centre = area.center();
    const qreal half = std::min(step, rect.height()) * 0.28; // about a third of the row: easy to see and to hit
    QPolygonF arrow;
    if (view.isExpanded(index))
    {
        arrow << QPointF(centre.x() - half * 1.2, centre.y() - half * 0.6)
              << QPointF(centre.x() + half * 1.2, centre.y() - half * 0.6)
              << QPointF(centre.x(), centre.y() + half * 0.9);
    }
    else
    {
        arrow << QPointF(centre.x() - half * 0.6, centre.y() - half * 1.2)
              << QPointF(centre.x() - half * 0.6, centre.y() + half * 1.2)
              << QPointF(centre.x() + half * 0.9, centre.y());
    }
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0xB4, 0xB4, 0xBA)); // a light grey: visible on the black panel, quieter than the text
    painter.drawPolygon(arrow);
    painter.restore();
}

/// @brief A QTreeView with the editor's fold arrows (drawTreeBranch()).
/// @par Thread safety
/// Not thread-safe: a widget, GUI thread only.
class ArrowTreeView final : public QTreeView
{
public:
    /// @brief An empty tree.
    /// @param parent Its Qt parent (which owns it).
    explicit ArrowTreeView(QWidget* parent)
        : QTreeView(parent)
    {
    }

protected:
    /// @brief Draws a row's selection and fold arrow (drawTreeBranch()).
    void drawBranches(QPainter* painter, const QRect& rect, const QModelIndex& index) const override
    {
        drawTreeBranch(*this, *painter, rect, index);
    }
};
} // namespace Meow2::Editor
