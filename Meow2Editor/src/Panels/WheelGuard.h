#pragma once

/// @file
/// @brief WheelGuard: value widgets take the mouse wheel only while they have focus.

#include <QObject>

namespace Meow2::Editor
{
/// @brief Stops the mouse wheel from changing values by accident while scrolling a panel.
///
/// Qt's spin boxes, combo boxes and sliders change their value when the wheel turns over them, focused or not, and
/// take focus from the wheel (`Qt::WheelFocus`). Scrolling down the Inspector would then edit every field it passes.
/// Installed on the application, this filter:
/// - gives those widgets `Qt::StrongFocus` when they're polished (before they're first shown), so the wheel doesn't
///   focus them (Qt hands out wheel focus before any event filter sees the event);
/// - hands a wheel event over an unfocused one to the scroll area around it instead, so the panel scrolls.
///
/// A click or Tab focuses a field; then the wheel changes it, as before.
/// @par Thread safety
/// Not thread-safe; the GUI thread.
class WheelGuard final : public QObject
{
public:
    /// @brief A guard; install it with `QApplication::installEventFilter()`.
    /// @param parent Its owner (it's removed from the application's filters when destroyed).
    explicit WheelGuard(QObject* parent = nullptr);

    /// @brief Whether a widget is one whose value the wheel changes.
    /// @param object The object.
    /// @return True for spin boxes, combo boxes and sliders (not scroll bars).
    [[nodiscard]] static bool isValueWidget(const QObject* object);

protected:
    /// @brief Sets the focus policy on polish, and passes wheel events over unfocused value widgets to their parent.
    /// @param watched The object the event is for.
    /// @param event   The event.
    /// @return True if the event was handled here.
    bool eventFilter(QObject* watched, QEvent* event) override;
};
} // namespace Meow2::Editor
