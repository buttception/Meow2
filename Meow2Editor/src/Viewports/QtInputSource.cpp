/// @file
/// @brief QtInputSource (QtInputSource.h): Qt events → InputSystem, and the native scan code tables.

#include "Viewports/QtInputSource.h"

#include <algorithm>
#include <array>
#include <optional>
#include <QEvent>
#include <QFocusEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QWidget>
#include <utility>

namespace Meow2::Editor
{
namespace
{
using Input::Key;

/// @brief The codes 0x01..0x58, which Windows scan code set 1 and Linux evdev share (Linux numbered its key codes after
///        set 1). Index = code; Key::Unknown for gaps. 0x45 differs (Pause on Windows, Num Lock on Linux) and is left
///        to the callers.
constexpr std::array<Key, 0x59> c_sharedCodes{Key::Unknown,
                                              Key::Escape,
                                              Key::Num1,
                                              Key::Num2,
                                              Key::Num3,
                                              Key::Num4,
                                              Key::Num5,
                                              Key::Num6,
                                              Key::Num7,
                                              Key::Num8,
                                              Key::Num9,
                                              Key::Num0,
                                              Key::Minus,
                                              Key::Equals,
                                              Key::Backspace,
                                              Key::Tab,
                                              Key::Q,
                                              Key::W,
                                              Key::E,
                                              Key::R,
                                              Key::T,
                                              Key::Y,
                                              Key::U,
                                              Key::I,
                                              Key::O,
                                              Key::P,
                                              Key::LeftBracket,
                                              Key::RightBracket,
                                              Key::Return,
                                              Key::LeftCtrl,
                                              Key::A,
                                              Key::S,
                                              Key::D,
                                              Key::F,
                                              Key::G,
                                              Key::H,
                                              Key::J,
                                              Key::K,
                                              Key::L,
                                              Key::Semicolon,
                                              Key::Apostrophe,
                                              Key::Grave,
                                              Key::LeftShift,
                                              Key::Backslash,
                                              Key::Z,
                                              Key::X,
                                              Key::C,
                                              Key::V,
                                              Key::B,
                                              Key::N,
                                              Key::M,
                                              Key::Comma,
                                              Key::Period,
                                              Key::Slash,
                                              Key::RightShift,
                                              Key::KeypadMultiply,
                                              Key::LeftAlt,
                                              Key::Space,
                                              Key::CapsLock,
                                              Key::F1,
                                              Key::F2,
                                              Key::F3,
                                              Key::F4,
                                              Key::F5,
                                              Key::F6,
                                              Key::F7,
                                              Key::F8,
                                              Key::F9,
                                              Key::F10,
                                              Key::Unknown /*0x45*/,
                                              Key::ScrollLock,
                                              Key::Keypad7,
                                              Key::Keypad8,
                                              Key::Keypad9,
                                              Key::KeypadMinus,
                                              Key::Keypad4,
                                              Key::Keypad5,
                                              Key::Keypad6,
                                              Key::KeypadPlus,
                                              Key::Keypad1,
                                              Key::Keypad2,
                                              Key::Keypad3,
                                              Key::Keypad0,
                                              Key::KeypadPeriod,
                                              Key::Unknown,
                                              Key::Unknown,
                                              Key::NonUsBackslash,
                                              Key::F11,
                                              Key::F12};

/// @brief A sorted (code, key) table, looked up by binary search.
template <std::size_t N>
Key lookUp(const std::array<std::pair<std::uint32_t, Key>, N>& table, std::uint32_t code)
{
    const auto found = std::ranges::lower_bound(table, code, {}, &std::pair<std::uint32_t, Key>::first);
    return found != table.end() && found->first == code ? found->second : Key::Unknown;
}

/// @brief Windows' extended (E0-prefixed) keys, as Qt reports them: 0x100 | the code. Sorted by code.
constexpr std::array<std::pair<std::uint32_t, Key>, 19> c_windowsExtended{{
    {0x11C, Key::KeypadEnter}, {0x11D, Key::RightCtrl}, {0x135, Key::KeypadDivide}, {0x137, Key::PrintScreen},
    {0x138, Key::RightAlt},    {0x145, Key::NumLock},   {0x147, Key::Home},         {0x148, Key::Up},
    {0x149, Key::PageUp},      {0x14B, Key::Left},      {0x14D, Key::Right},        {0x14F, Key::End},
    {0x150, Key::Down},        {0x151, Key::PageDown},  {0x152, Key::Insert},       {0x153, Key::Delete},
    {0x15B, Key::LeftGui},     {0x15C, Key::RightGui},  {0x15D, Key::Application},
}};

/// @brief evdev codes above the shared range. Sorted by code.
constexpr std::array<std::pair<std::uint32_t, Key>, 19> c_evdevHigh{{
    {96, Key::KeypadEnter}, {97, Key::RightCtrl}, {98, Key::KeypadDivide}, {99, Key::PrintScreen},
    {100, Key::RightAlt},   {102, Key::Home},     {103, Key::Up},          {104, Key::PageUp},
    {105, Key::Left},       {106, Key::Right},    {107, Key::End},         {108, Key::Down},
    {109, Key::PageDown},   {110, Key::Insert},   {111, Key::Delete},      {119, Key::Pause},
    {125, Key::LeftGui},    {126, Key::RightGui}, {127, Key::Application},
}};

/// @brief The engine key for a Qt key, where there's no native code: layout-dependent, so only a fallback.
Key keyFromQtKey(int qtKey)
{
    if (qtKey >= Qt::Key_A && qtKey <= Qt::Key_Z)
    {
        return static_cast<Key>(static_cast<int>(Key::A) + (qtKey - Qt::Key_A));
    }
    if (qtKey >= Qt::Key_1 && qtKey <= Qt::Key_9)
    {
        return static_cast<Key>(static_cast<int>(Key::Num1) + (qtKey - Qt::Key_1));
    }
    if (qtKey >= Qt::Key_F1 && qtKey <= Qt::Key_F12)
    {
        return static_cast<Key>(static_cast<int>(Key::F1) + (qtKey - Qt::Key_F1));
    }
    switch (qtKey)
    {
        case Qt::Key_0:
            return Key::Num0;
        case Qt::Key_Space:
            return Key::Space;
        case Qt::Key_Return:
            return Key::Return;
        case Qt::Key_Enter:
            return Key::KeypadEnter;
        case Qt::Key_Escape:
            return Key::Escape;
        case Qt::Key_Tab:
            return Key::Tab;
        case Qt::Key_Backspace:
            return Key::Backspace;
        case Qt::Key_Left:
            return Key::Left;
        case Qt::Key_Right:
            return Key::Right;
        case Qt::Key_Up:
            return Key::Up;
        case Qt::Key_Down:
            return Key::Down;
        case Qt::Key_Shift:
            return Key::LeftShift;
        case Qt::Key_Control:
            return Key::LeftCtrl;
        case Qt::Key_Alt:
            return Key::LeftAlt;
        case Qt::Key_Delete:
            return Key::Delete;
        case Qt::Key_Insert:
            return Key::Insert;
        case Qt::Key_Home:
            return Key::Home;
        case Qt::Key_End:
            return Key::End;
        case Qt::Key_PageUp:
            return Key::PageUp;
        case Qt::Key_PageDown:
            return Key::PageDown;
        default:
            return Key::Unknown;
    }
}

/// @brief The engine button for a Qt mouse button, if it's one the engine names.
std::optional<Input::MouseButton> buttonFromQt(Qt::MouseButton button)
{
    switch (button)
    {
        case Qt::LeftButton:
            return Input::MouseButton::Left;
        case Qt::RightButton:
            return Input::MouseButton::Right;
        case Qt::MiddleButton:
            return Input::MouseButton::Middle;
        case Qt::BackButton:
            return Input::MouseButton::X1;
        case Qt::ForwardButton:
            return Input::MouseButton::X2;
        default:
            return std::nullopt;
    }
}
} // namespace

Input::Key keyFromWindowsScanCode(std::uint32_t scanCode)
{
    if ((scanCode & 0x100U) != 0)
    {
        return lookUp(c_windowsExtended, scanCode & 0x1FFU);
    }
    const std::uint32_t code = scanCode & 0xFFU;
    if (code == 0x45)
    {
        return Key::Pause; // Num Lock is the extended 0x45
    }
    return code < c_sharedCodes.size() ? c_sharedCodes[code] : Key::Unknown;
}

Input::Key keyFromEvdevCode(std::uint32_t code)
{
    if (code == 69)
    {
        return Key::NumLock; // KEY_NUMLOCK; Pause is KEY_PAUSE (119)
    }
    if (code < c_sharedCodes.size())
    {
        return c_sharedCodes[code];
    }
    return lookUp(c_evdevHigh, code);
}

Input::Key keyFromQt(std::uint32_t nativeScanCode, int qtKey)
{
    Key key = Key::Unknown;
    if (nativeScanCode != 0)
    {
#if defined(_WIN32)
        key = keyFromWindowsScanCode(nativeScanCode);
#else
        key = nativeScanCode >= 8 ? keyFromEvdevCode(nativeScanCode - 8) : Key::Unknown; // X11 key code = evdev + 8
#endif
    }
    return key != Key::Unknown ? key : keyFromQtKey(qtKey);
}

QtInputSource::QtInputSource(Input::InputSystem& input, QObject* parent)
    : QObject(parent)
    , m_input(input)
{
}

void QtInputSource::watch(QWidget* viewport)
{
    viewport->setFocusPolicy(Qt::StrongFocus); // a click or Tab focuses it
    viewport->setMouseTracking(true);          // motion without a button held, for hover later
    viewport->installEventFilter(this);
    m_viewports.push_back(viewport);
    connect(viewport, &QObject::destroyed, this,
            [this, viewport]
            {
                std::erase(m_viewports, viewport);
                if (m_active == viewport)
                {
                    m_active = nullptr;
                }
            });
}

bool QtInputSource::eventFilter(QObject* watched, QEvent* event)
{
    auto* const viewport = qobject_cast<QWidget*>(watched);
    if (viewport == nullptr || std::ranges::find(m_viewports, viewport) == m_viewports.end())
    {
        return false;
    }
    switch (event->type())
    {
        case QEvent::FocusIn:
            m_active = viewport;
            break;
        case QEvent::FocusOut:
            releaseAll();
            if (m_active == viewport)
            {
                m_active = nullptr;
            }
            m_haveMouse = false;
            break;
        case QEvent::MouseButtonPress:
            viewport->setFocus(Qt::MouseFocusReason); // a click gives the viewport the keyboard too
            m_active = viewport;
            break;
        default:
            break;
    }
    // The viewport just clicked counts as focused even before Qt says so: hasFocus() waits for its window to be
    // activated, which can come after the press, and the press would be lost.
    if (m_active != viewport && !viewport->hasFocus())
    {
        return false; // typing in a dock never reaches the game or the camera
    }

    switch (event->type())
    {
        case QEvent::KeyPress:
        case QEvent::KeyRelease:
        {
            const auto* key = static_cast<QKeyEvent*>(event);
            const bool down = event->type() == QEvent::KeyPress;
            const Input::Key engineKey = keyFromQt(key->nativeScanCode(), key->key());
            // A held key repeats as release + press pairs, both marked auto-repeat. The release isn't real: passed on,
            // it let go of the key a moment after the repeat began, and the fly camera stopped (owner report, #229).
            if (engineKey != Input::Key::Unknown && !(key->isAutoRepeat() && !down))
            {
                m_input.submit(Input::KeyInput{.key = engineKey, .down = down, .repeat = key->isAutoRepeat()});
                if (down && !key->isAutoRepeat())
                {
                    m_heldKeys.push_back(engineKey);
                }
                else if (!down && !key->isAutoRepeat())
                {
                    std::erase(m_heldKeys, engineKey);
                }
            }
            if (down && !key->text().isEmpty() && key->text().front().isPrint())
            {
                m_input.submit(Input::TextInput{.text = key->text().toStdString()});
            }
            break;
        }
        case QEvent::MouseButtonPress:
        case QEvent::MouseButtonRelease:
        {
            const auto* mouse = static_cast<QMouseEvent*>(event);
            // Where the click is: moves are ignored while the viewport isn't focused, so without this the first
            // click into it picked at wherever the cursor last moved while it was focused (owner report, #229).
            const QPointF position = mouse->position();
            const QPointF delta = m_haveMouse ? position - m_lastMouse : QPointF{};
            m_lastMouse = position;
            m_haveMouse = true;
            m_input.submit(Input::MouseMotionInput{.x = static_cast<float>(position.x()),
                                                   .y = static_cast<float>(position.y()),
                                                   .dx = static_cast<float>(delta.x()),
                                                   .dy = static_cast<float>(delta.y())});
            if (const std::optional<Input::MouseButton> button = buttonFromQt(mouse->button()))
            {
                const bool down = event->type() == QEvent::MouseButtonPress;
                m_input.submit(Input::MouseButtonInput{.button = *button, .down = down});
                if (down)
                {
                    m_heldButtons.push_back(*button);
                }
                else
                {
                    std::erase(m_heldButtons, *button);
                }
            }
            break;
        }
        case QEvent::MouseMove:
        {
            const QPointF position = static_cast<QMouseEvent*>(event)->position();
            const QPointF delta = m_haveMouse ? position - m_lastMouse : QPointF{};
            m_lastMouse = position;
            m_haveMouse = true;
            m_input.submit(Input::MouseMotionInput{.x = static_cast<float>(position.x()),
                                                   .y = static_cast<float>(position.y()),
                                                   .dx = static_cast<float>(delta.x()),
                                                   .dy = static_cast<float>(delta.y())});
            break;
        }
        case QEvent::Wheel:
        {
            // 120 units per notch (QWheelEvent's convention); a smooth wheel sends fractions.
            const QPoint angle = static_cast<QWheelEvent*>(event)->angleDelta();
            m_input.submit(Input::MouseWheelInput{.x = static_cast<float>(angle.x()) / 120.0F,
                                                  .y = static_cast<float>(angle.y()) / 120.0F});
            break;
        }
        default:
            break;
    }
    return false;
}

void QtInputSource::releaseAll()
{
    for (const Input::Key key : m_heldKeys)
    {
        m_input.submit(Input::KeyInput{.key = key, .down = false});
    }
    for (const Input::MouseButton button : m_heldButtons)
    {
        m_input.submit(Input::MouseButtonInput{.button = button, .down = false});
    }
    m_heldKeys.clear();
    m_heldButtons.clear();
}
} // namespace Meow2::Editor
