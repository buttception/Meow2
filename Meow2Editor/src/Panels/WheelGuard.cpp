/// @file
/// @brief WheelGuard (WheelGuard.h).

#include "Panels/WheelGuard.h"

#include <QAbstractScrollArea>
#include <QAbstractSlider>
#include <QAbstractSpinBox>
#include <QApplication>
#include <QComboBox>
#include <QCoreApplication>
#include <QEvent>
#include <QScrollBar>
#include <QWidget>

namespace Meow2::Editor
{
WheelGuard::WheelGuard(QObject* parent)
    : QObject(parent)
{
}

bool WheelGuard::isValueWidget(const QObject* object)
{
    // Scroll bars are sliders too, but scrolling is their job (a scroll area also hands them its wheel events).
    if (qobject_cast<const QScrollBar*>(object) != nullptr)
    {
        return false;
    }
    return qobject_cast<const QAbstractSpinBox*>(object) != nullptr ||
           qobject_cast<const QComboBox*>(object) != nullptr || qobject_cast<const QAbstractSlider*>(object) != nullptr;
}

bool WheelGuard::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() == QEvent::Polish && isValueWidget(watched))
    {
        auto* widget = static_cast<QWidget*>(watched);
        if (widget->focusPolicy() == Qt::WheelFocus)
        {
            widget->setFocusPolicy(Qt::StrongFocus); // a click or Tab still focuses it; the wheel doesn't
        }
        return false;
    }
    if (event->type() == QEvent::Wheel && isValueWidget(watched))
    {
        auto* widget = static_cast<QWidget*>(watched);
        // A spin box's line edit has the focus when it's being edited; it counts as the spin box.
        if (widget->hasFocus() || widget->isAncestorOf(QApplication::focusWidget()) ||
            widget->parentWidget() == nullptr)
        {
            return false;
        }
        // To the panel that scrolls (the Inspector's scroll area). Not just to the parent: Qt passes a wheel event up
        // the parents only when it came from the OS, not when code sends it on.
        for (QWidget* above = widget->parentWidget(); above != nullptr; above = above->parentWidget())
        {
            if (auto* scroll = qobject_cast<QAbstractScrollArea*>(above))
            {
                QCoreApplication::sendEvent(scroll->viewport(), event);
                break;
            }
        }
        return true; // either way, not the value's
    }
    return false;
}
} // namespace Meow2::Editor
