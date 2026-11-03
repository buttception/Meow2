#pragma once

/// @file
/// @brief Engine input types: keys, mouse buttons, gamepad buttons and axes. No SDL or Qt types.
/// @see techstack.md §9, docs/wiki/Input.md

#include <cstddef>
#include <cstdint>

namespace Meow2::Input
{
/// @brief A physical keyboard key, by its USB HID usage id (page 0x07).
///
/// The value names the key's **position**, not the character it types: `Key::W` is the key where W is on a US
/// layout, which is Z on a French one. That's what games want for movement keys. Typed text arrives separately as
/// TextEntered. SDL's scancodes are the same numbers, so its source converts with a cast.
enum class Key : std::uint16_t
{
    Unknown = 0, ///< Not a key the engine knows.

    A = 4,  ///< A.
    B = 5,  ///< B.
    C = 6,  ///< C.
    D = 7,  ///< D.
    E = 8,  ///< E.
    F = 9,  ///< F.
    G = 10, ///< G.
    H = 11, ///< H.
    I = 12, ///< I.
    J = 13, ///< J.
    K = 14, ///< K.
    L = 15, ///< L.
    M = 16, ///< M.
    N = 17, ///< N.
    O = 18, ///< O.
    P = 19, ///< P.
    Q = 20, ///< Q.
    R = 21, ///< R.
    S = 22, ///< S.
    T = 23, ///< T.
    U = 24, ///< U.
    V = 25, ///< V.
    W = 26, ///< W.
    X = 27, ///< X.
    Y = 28, ///< Y.
    Z = 29, ///< Z.

    Num1 = 30, ///< 1 (top row).
    Num2 = 31, ///< 2 (top row).
    Num3 = 32, ///< 3 (top row).
    Num4 = 33, ///< 4 (top row).
    Num5 = 34, ///< 5 (top row).
    Num6 = 35, ///< 6 (top row).
    Num7 = 36, ///< 7 (top row).
    Num8 = 37, ///< 8 (top row).
    Num9 = 38, ///< 9 (top row).
    Num0 = 39, ///< 0 (top row).

    Return = 40,       ///< Enter on the main block.
    Escape = 41,       ///< Esc.
    Backspace = 42,    ///< Backspace.
    Tab = 43,          ///< Tab.
    Space = 44,        ///< Space bar.
    Minus = 45,        ///< - and _ (US layout).
    Equals = 46,       ///< = and + (US layout).
    LeftBracket = 47,  ///< [ and { (US layout).
    RightBracket = 48, ///< ] and } (US layout).
    Backslash = 49,    ///< \ and | (US layout).
    Semicolon = 51,    ///< ; and : (US layout).
    Apostrophe = 52,   ///< ' and " (US layout).
    Grave = 53,        ///< ` and ~ (US layout), left of 1.
    Comma = 54,        ///< , and < (US layout).
    Period = 55,       ///< . and > (US layout).
    Slash = 56,        ///< / and ? (US layout).
    CapsLock = 57,     ///< Caps Lock.

    F1 = 58,  ///< F1.
    F2 = 59,  ///< F2.
    F3 = 60,  ///< F3.
    F4 = 61,  ///< F4.
    F5 = 62,  ///< F5.
    F6 = 63,  ///< F6.
    F7 = 64,  ///< F7.
    F8 = 65,  ///< F8.
    F9 = 66,  ///< F9.
    F10 = 67, ///< F10.
    F11 = 68, ///< F11.
    F12 = 69, ///< F12.

    PrintScreen = 70, ///< Print Screen.
    ScrollLock = 71,  ///< Scroll Lock.
    Pause = 72,       ///< Pause / Break.
    Insert = 73,      ///< Insert.
    Home = 74,        ///< Home.
    PageUp = 75,      ///< Page Up.
    Delete = 76,      ///< Delete (forward delete).
    End = 77,         ///< End.
    PageDown = 78,    ///< Page Down.
    Right = 79,       ///< Right arrow.
    Left = 80,        ///< Left arrow.
    Down = 81,        ///< Down arrow.
    Up = 82,          ///< Up arrow.

    NumLock = 83,         ///< Num Lock.
    KeypadDivide = 84,    ///< Keypad /.
    KeypadMultiply = 85,  ///< Keypad *.
    KeypadMinus = 86,     ///< Keypad -.
    KeypadPlus = 87,      ///< Keypad +.
    KeypadEnter = 88,     ///< Keypad Enter.
    Keypad1 = 89,         ///< Keypad 1.
    Keypad2 = 90,         ///< Keypad 2.
    Keypad3 = 91,         ///< Keypad 3.
    Keypad4 = 92,         ///< Keypad 4.
    Keypad5 = 93,         ///< Keypad 5.
    Keypad6 = 94,         ///< Keypad 6.
    Keypad7 = 95,         ///< Keypad 7.
    Keypad8 = 96,         ///< Keypad 8.
    Keypad9 = 97,         ///< Keypad 9.
    Keypad0 = 98,         ///< Keypad 0.
    KeypadPeriod = 99,    ///< Keypad . (Delete).
    NonUsBackslash = 100, ///< The extra key next to left Shift on ISO keyboards.
    Application = 101,    ///< The context-menu key.

    LeftCtrl = 224,   ///< Left Ctrl.
    LeftShift = 225,  ///< Left Shift.
    LeftAlt = 226,    ///< Left Alt.
    LeftGui = 227,    ///< Left Windows / Command key.
    RightCtrl = 228,  ///< Right Ctrl.
    RightShift = 229, ///< Right Shift.
    RightAlt = 230,   ///< Right Alt (AltGr).
    RightGui = 231,   ///< Right Windows / Command key.
};

/// @brief Number of possible Key values (the HID page fits in 0..255), for state tables.
inline constexpr std::size_t c_keyCount = 256;

/// @brief A mouse button.
enum class MouseButton : std::uint8_t
{
    Left,   ///< Primary button.
    Middle, ///< Wheel click.
    Right,  ///< Secondary button.
    X1,     ///< First side button (usually "back").
    X2,     ///< Second side button (usually "forward").
};

/// @brief Number of MouseButton values.
inline constexpr std::size_t c_mouseButtonCount = 5;

/// @brief Identifies a connected gamepad. Stays the same while it's connected; 0 is never a gamepad.
using GamepadId = std::uint32_t;

/// @brief A gamepad button, by position (an Xbox layout's A is South, a PlayStation layout's Cross too).
enum class GamepadButton : std::uint8_t
{
    South,         ///< Bottom face button (Xbox A, PlayStation Cross).
    East,          ///< Right face button (Xbox B, PlayStation Circle).
    West,          ///< Left face button (Xbox X, PlayStation Square).
    North,         ///< Top face button (Xbox Y, PlayStation Triangle).
    Back,          ///< Back / View / Select / Share.
    Guide,         ///< Xbox / PS / Home button.
    Start,         ///< Start / Menu / Options.
    LeftStick,     ///< Left stick pressed in.
    RightStick,    ///< Right stick pressed in.
    LeftShoulder,  ///< Left bumper.
    RightShoulder, ///< Right bumper.
    DpadUp,        ///< D-pad up.
    DpadDown,      ///< D-pad down.
    DpadLeft,      ///< D-pad left.
    DpadRight,     ///< D-pad right.
};

/// @brief Number of GamepadButton values.
inline constexpr std::size_t c_gamepadButtonCount = 15;

/// @brief A gamepad axis.
enum class GamepadAxis : std::uint8_t
{
    LeftX,        ///< Left stick, -1 (left) .. 1 (right).
    LeftY,        ///< Left stick, -1 (up) .. 1 (down).
    RightX,       ///< Right stick, -1 (left) .. 1 (right).
    RightY,       ///< Right stick, -1 (up) .. 1 (down).
    LeftTrigger,  ///< Left trigger, 0 (released) .. 1 (fully pressed).
    RightTrigger, ///< Right trigger, 0 (released) .. 1 (fully pressed).
};

/// @brief Number of GamepadAxis values.
inline constexpr std::size_t c_gamepadAxisCount = 6;
} // namespace Meow2::Input
