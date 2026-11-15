#pragma once

/// @file
/// @brief QtInputSource: Qt's keyboard and mouse events on the editor's viewports → the engine's InputSystem.

#include <Meow2/Platform/Input/InputSystem.h>
#include <Meow2/Platform/Input/InputTypes.h>

#include <cstdint>
#include <QObject>
#include <QPointF>
#include <vector>

class QWidget;

namespace Meow2::Editor
{
/// @brief The engine key for a Windows scan code (set 1), as Qt reports it in QKeyEvent::nativeScanCode(): the code in
///        the low byte, 0x100 for the extended (E0-prefixed) keys.
/// @param scanCode The native scan code.
/// @return The key (USB HID usage), or Key::Unknown for one the engine doesn't name.
[[nodiscard]] Input::Key keyFromWindowsScanCode(std::uint32_t scanCode);

/// @brief The engine key for a Linux evdev key code (`KEY_*` in `linux/input-event-codes.h`). X11 reports these plus
///        8 as its key codes, which is what Qt's xcb platform puts in QKeyEvent::nativeScanCode().
/// @param code The evdev code (the X11 key code minus 8).
/// @return The key, or Key::Unknown.
[[nodiscard]] Input::Key keyFromEvdevCode(std::uint32_t code);

/// @brief The engine key for a Qt key event's native scan code on this platform (Windows set 1, or X11's evdev + 8),
///        falling back to the Qt key for platforms without native codes (offscreen, Wayland without them).
/// @param nativeScanCode QKeyEvent::nativeScanCode(); 0 when there's none.
/// @param qtKey          QKeyEvent::key(), a `Qt::Key`.
/// @return The key; the physical key where a native code exists, so WASD stays WASD on any layout.
[[nodiscard]] Input::Key keyFromQt(std::uint32_t nativeScanCode, int qtKey);

/// @brief Forwards keyboard and mouse events from the editor's viewports to the InputSystem (techstack.md §9).
///
/// It's an event filter on each viewport widget: keys, text, mouse buttons, motion and the wheel reach the
/// InputSystem **only while that viewport has focus**, so typing in a dock never moves the camera. A click on a
/// viewport gives it focus. When a viewport loses focus, every key and button still held is released, so nothing
/// stays stuck down. Events aren't consumed: Qt's shortcuts (Ctrl+Z, F) still work, and editor actions stay editor
/// commands rather than input (techstack.md §9, "editor rule").
/// @par Thread safety
/// Not thread-safe; GUI thread only, like every QObject.
/// @par Lifetime
/// The InputSystem must outlive it; watched widgets may be destroyed first (Qt removes the filter with them).
class QtInputSource final : public QObject
{
public:
    /// @brief A source that forwards to `input`.
    /// @param input  Receives the events (InputSystem::submit()).
    /// @param parent Qt owner, usually the main window.
    explicit QtInputSource(Input::InputSystem& input, QObject* parent = nullptr);

    /// @brief Starts forwarding a viewport's events.
    /// @param viewport The viewport widget; it gets a strong focus policy so a click or Tab focuses it.
    void watch(QWidget* viewport);

    /// @brief The viewport that had focus at the last event (or null): whose camera the input drives.
    /// @return The widget, or null.
    [[nodiscard]] QWidget* activeViewport() const
    {
        return m_active;
    }

    /// @brief Qt's hook: translates the watched viewports' events.
    /// @param watched The object the event is for.
    /// @param event   The event.
    /// @return Always false: the event goes on to Qt.
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    /// @brief Submits a release for every key and button still down (focus lost).
    void releaseAll();

    Input::InputSystem& m_input;
    std::vector<QWidget*> m_viewports;             ///< Watched; not owned.
    QWidget* m_active = nullptr;                   ///< The focused viewport, or null.
    std::vector<Input::Key> m_heldKeys;            ///< Keys pressed and not yet released, to release on focus loss.
    std::vector<Input::MouseButton> m_heldButtons; ///< Buttons likewise.
    QPointF m_lastMouse;                           ///< The last mouse position, for the motion delta.
    bool m_haveMouse = false;                      ///< m_lastMouse is valid.
};
} // namespace Meow2::Editor
