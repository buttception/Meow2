/// @file
/// @brief FittingScrollArea: a scroll area as wide as its content.

#include "Panels/FittingScrollArea.h"

#include <QScrollBar>

namespace Meow2::Editor
{
FittingScrollArea::FittingScrollArea(QWidget* parent)
    : QScrollArea(parent)
{
    setWidgetResizable(true);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
}

QSize FittingScrollArea::minimumSizeHint() const
{
    QSize size = QScrollArea::minimumSizeHint();
    if (const QWidget* content = widget(); content != nullptr)
    {
        size.setWidth(content->minimumSizeHint().width() + verticalScrollBar()->sizeHint().width() + 2 * frameWidth());
    }
    return size;
}

void FittingScrollArea::contentChanged()
{
    QMetaObject::invokeMethod(this, &QWidget::updateGeometry, Qt::QueuedConnection);
}
} // namespace Meow2::Editor
