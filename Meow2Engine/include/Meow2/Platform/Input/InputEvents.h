#pragma once

/// @file
/// @brief Input as data: what input sources submit (InputEvent) and what InputSystem puts on the EventBus.
///
/// Two sets of plain data structs:
/// - **Raw input** (`…Input`, combined in the InputEvent variant): what a source (SDL, Qt) saw, handed to
///   InputSystem::submit(). Only sources create these.
/// - **Bus events** (`KeyPressed`, `MouseMoved`, …): what InputSystem::beginFrame() enqueues on the EventBus after
///   updating its state. Code that cares about a *change* subscribes to these; gameplay polls InputSystem instead.
/// @see techstack.md §9, §22, docs/wiki/Input.md

#include <Meow2/Platform/Input/InputTypes.h>

#include <string>
#include <variant>

namespace Meow2::Input
{
/// @brief Raw input: a key went down or up. Plain data.
struct KeyInput
{
    Key key = Key::Unknown; ///< Which key.
    bool down = false;      ///< `true` for pressed, `false` for released.
    bool repeat = false;    ///< The OS's auto-repeat while held (only with `down`); not a new press.
};

/// @brief Raw input: a mouse button went down or up. Plain data.
struct MouseButtonInput
{
    MouseButton button = MouseButton::Left; ///< Which button.
    bool down = false;                      ///< `true` for pressed, `false` for released.
};

/// @brief Raw input: the mouse moved. Plain data.
struct MouseMotionInput
{
    float x = 0.0F;  ///< New position in the window's client area, in desktop units (0,0 = top-left).
    float y = 0.0F;  ///< New position in the window's client area, in desktop units.
    float dx = 0.0F; ///< Movement since the previous motion input, in desktop units (also valid in relative mode).
    float dy = 0.0F; ///< Movement since the previous motion input, in desktop units.
};

/// @brief Raw input: the wheel turned. Plain data.
struct MouseWheelInput
{
    float x = 0.0F; ///< Horizontal scroll, positive to the right, in notches (fractions on smooth wheels).
    float y = 0.0F; ///< Vertical scroll, positive away from the user, in notches.
};

/// @brief Raw input: typed text, after the keyboard layout and IME. Plain data.
struct TextInput
{
    std::string text{}; ///< UTF-8; one or more characters.
};

/// @brief Raw input: a gamepad was connected or disconnected. Plain data.
struct GamepadConnection
{
    GamepadId gamepad = 0;  ///< Which gamepad.
    bool connected = false; ///< `true` when it appeared, `false` when it went away.
    std::string name{};     ///< The device's name, e.g. "Xbox Series X Controller" (connections only).
};

/// @brief Raw input: a gamepad button went down or up. Plain data.
struct GamepadButtonInput
{
    GamepadId gamepad = 0;                       ///< Which gamepad.
    GamepadButton button = GamepadButton::South; ///< Which button.
    bool down = false;                           ///< `true` for pressed, `false` for released.
};

/// @brief Raw input: a gamepad axis moved. Plain data.
struct GamepadAxisInput
{
    GamepadId gamepad = 0;                 ///< Which gamepad.
    GamepadAxis axis = GamepadAxis::LeftX; ///< Which axis.
    float value = 0.0F;                    ///< New value: -1..1 for sticks, 0..1 for triggers (see GamepadAxis).
};

/// @brief Any raw input a source can submit to InputSystem.
using InputEvent = std::variant<KeyInput, MouseButtonInput, MouseMotionInput, MouseWheelInput, TextInput,
                                GamepadConnection, GamepadButtonInput, GamepadAxisInput>;

/// @brief Bus event: a key was pressed (or auto-repeated while held). Plain data.
struct KeyPressed
{
    Key key = Key::Unknown; ///< Which key.
    bool repeat = false;    ///< The OS's auto-repeat, e.g. for moving a text cursor; not a new press.
};

/// @brief Bus event: a key was released. Plain data.
struct KeyReleased
{
    Key key = Key::Unknown; ///< Which key.
};

/// @brief Bus event: a mouse button was pressed or released. Plain data.
struct MouseButtonChanged
{
    MouseButton button = MouseButton::Left; ///< Which button.
    bool down = false;                      ///< `true` for pressed, `false` for released.
    float x = 0.0F;                         ///< Mouse position at the time, in desktop units.
    float y = 0.0F;                         ///< Mouse position at the time, in desktop units.
};

/// @brief Bus event: the mouse moved during the frame. Sent once per frame, summed. Plain data.
struct MouseMoved
{
    float x = 0.0F;  ///< Position at the end of the frame, in desktop units.
    float y = 0.0F;  ///< Position at the end of the frame, in desktop units.
    float dx = 0.0F; ///< Total movement during the frame, in desktop units.
    float dy = 0.0F; ///< Total movement during the frame, in desktop units.
};

/// @brief Bus event: the wheel turned during the frame. Sent once per frame, summed. Plain data.
struct MouseWheelScrolled
{
    float x = 0.0F; ///< Total horizontal scroll in notches, positive to the right.
    float y = 0.0F; ///< Total vertical scroll in notches, positive away from the user.
};

/// @brief Bus event: text was typed (UTF-8). Plain data.
struct TextEntered
{
    std::string text{}; ///< The characters, after the keyboard layout and IME.
};

/// @brief Bus event: a gamepad appeared. Plain data.
struct GamepadConnected
{
    GamepadId gamepad = 0; ///< Its id for polling.
    std::string name{};    ///< The device's name.
};

/// @brief Bus event: a gamepad went away; its state is cleared. Plain data.
struct GamepadDisconnected
{
    GamepadId gamepad = 0; ///< The id it had.
};

/// @brief Bus event: a gamepad button was pressed or released. Plain data.
struct GamepadButtonChanged
{
    GamepadId gamepad = 0;                       ///< Which gamepad.
    GamepadButton button = GamepadButton::South; ///< Which button.
    bool down = false;                           ///< `true` for pressed, `false` for released.
};
} // namespace Meow2::Input
