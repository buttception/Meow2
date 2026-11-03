/// @file
/// @brief InputSystem (InputSystem.h). Backend-independent: sources translate SDL/Qt input before submit().

#include "Meow2/Platform/Input/InputSystem.h"

#include "Meow2/Core/Profiling/Profile.h"

#include <algorithm>
#include <type_traits>
#include <utility>

namespace Meow2::Input
{
namespace
{
template <class... Ts>
struct Overloaded : Ts...
{
    using Ts::operator()...;
};

template <class E>
constexpr std::size_t indexOf(E value)
{
    return static_cast<std::size_t>(static_cast<std::underlying_type_t<E>>(value));
}
} // namespace

template <std::size_t N>
void InputSystem::ButtonSet<N>::set(std::size_t index, bool isDown)
{
    if (index >= N || down.test(index) == isDown)
    {
        return; // unknown key, or no change (e.g. a release for a key we never saw go down)
    }
    down.set(index, isDown);
    (isDown ? pressed : released).set(index);
    (isDown ? fixedPressed : fixedReleased).set(index);
}

template <std::size_t N>
void InputSystem::ButtonSet<N>::clearFrameEdges()
{
    pressed.reset();
    released.reset();
}

template <std::size_t N>
void InputSystem::ButtonSet<N>::clearFixedEdges()
{
    fixedPressed.reset();
    fixedReleased.reset();
}

InputSystem::InputSystem(Events::EventBus& events)
    : m_events(events)
{
}

void InputSystem::submit(InputEvent event)
{
    m_queue.push_back(std::move(event));
}

void InputSystem::beginFrame()
{
    MEOW2_PROFILE_ZONE();
    m_keys.clearFrameEdges();
    m_mouseButtons.clearFrameEdges();
    for (Gamepad& gamepad : m_gamepads)
    {
        gamepad.buttons.clearFrameEdges();
    }
    m_mouseDelta = {};
    m_wheelDelta = {};

    // Swap first: a handler can't run here (events are only enqueued), but this keeps submit() during apply safe.
    std::vector<InputEvent> queue;
    queue.swap(m_queue);
    for (const InputEvent& event : queue)
    {
        apply(event);
    }

    // Motion and scroll are summed over the frame and sent once: per-event messages would flood the bus.
    if (m_mouseDelta != Vec2{})
    {
        m_events.enqueue(
            MouseMoved{.x = m_mousePosition.x, .y = m_mousePosition.y, .dx = m_mouseDelta.x, .dy = m_mouseDelta.y});
    }
    if (m_wheelDelta != Vec2{})
    {
        m_events.enqueue(MouseWheelScrolled{.x = m_wheelDelta.x, .y = m_wheelDelta.y});
    }
}

void InputSystem::apply(const InputEvent& event)
{
    std::visit(
        Overloaded{
            [this](const KeyInput& input)
            {
                if (input.down && input.repeat)
                {
                    m_events.enqueue(KeyPressed{.key = input.key, .repeat = true}); // not a new press: no edge
                    return;
                }
                const std::size_t index = indexOf(input.key);
                if (index >= c_keyCount || m_keys.down.test(index) == input.down)
                {
                    return; // a key outside the HID page, or no change (e.g. a release we never saw go down)
                }
                m_keys.set(index, input.down);
                if (input.down)
                {
                    m_events.enqueue(KeyPressed{.key = input.key, .repeat = false});
                }
                else
                {
                    m_events.enqueue(KeyReleased{.key = input.key});
                }
            },
            [this](const MouseButtonInput& input)
            {
                const std::size_t index = indexOf(input.button);
                if (index >= c_mouseButtonCount || m_mouseButtons.down.test(index) == input.down)
                {
                    return;
                }
                m_mouseButtons.set(index, input.down);
                m_events.enqueue(MouseButtonChanged{
                    .button = input.button, .down = input.down, .x = m_mousePosition.x, .y = m_mousePosition.y});
            },
            [this](const MouseMotionInput& input)
            {
                m_mousePosition = {input.x, input.y};
                m_mouseDelta.x += input.dx;
                m_mouseDelta.y += input.dy;
            },
            [this](const MouseWheelInput& input)
            {
                m_wheelDelta.x += input.x;
                m_wheelDelta.y += input.y;
            },
            [this](const TextInput& input) { m_events.enqueue(TextEntered{.text = input.text}); },
            [this](const GamepadConnection& input)
            {
                if (input.connected)
                {
                    if (findGamepad(input.gamepad) == nullptr)
                    {
                        m_gamepads.push_back({.id = input.gamepad});
                        m_gamepadIds.push_back(input.gamepad);
                        m_events.enqueue(GamepadConnected{.gamepad = input.gamepad, .name = input.name});
                    }
                    return;
                }
                if (findGamepad(input.gamepad) != nullptr)
                {
                    std::erase_if(m_gamepads, [&input](const Gamepad& pad) { return pad.id == input.gamepad; });
                    std::erase(m_gamepadIds, input.gamepad);
                    m_events.enqueue(GamepadDisconnected{.gamepad = input.gamepad});
                }
            },
            [this](const GamepadButtonInput& input)
            {
                Gamepad* gamepad = findGamepad(input.gamepad);
                const std::size_t index = indexOf(input.button);
                if (gamepad == nullptr || index >= c_gamepadButtonCount ||
                    gamepad->buttons.down.test(index) == input.down)
                {
                    return; // input for a gamepad that isn't connected (yet), or no change
                }
                gamepad->buttons.set(index, input.down);
                m_events.enqueue(
                    GamepadButtonChanged{.gamepad = input.gamepad, .button = input.button, .down = input.down});
            },
            [this](const GamepadAxisInput& input)
            {
                Gamepad* gamepad = findGamepad(input.gamepad);
                const std::size_t index = indexOf(input.axis);
                if (gamepad != nullptr && index < c_gamepadAxisCount)
                {
                    gamepad->axes[index] = input.value;
                }
            },
        },
        event);
}

void InputSystem::endFixedStep()
{
    m_keys.clearFixedEdges();
    m_mouseButtons.clearFixedEdges();
    for (Gamepad& gamepad : m_gamepads)
    {
        gamepad.buttons.clearFixedEdges();
    }
}

void InputSystem::releaseAll()
{
    for (std::size_t i = 0; i < c_keyCount; ++i)
    {
        if (m_keys.down.test(i))
        {
            submit(KeyInput{.key = static_cast<Key>(i), .down = false});
        }
    }
    for (std::size_t i = 0; i < c_mouseButtonCount; ++i)
    {
        if (m_mouseButtons.down.test(i))
        {
            submit(MouseButtonInput{.button = static_cast<MouseButton>(i), .down = false});
        }
    }
}

bool InputSystem::isDown(Key key) const
{
    return indexOf(key) < c_keyCount && m_keys.down.test(indexOf(key));
}

bool InputSystem::wasPressed(Key key) const
{
    return indexOf(key) < c_keyCount && m_keys.pressed.test(indexOf(key));
}

bool InputSystem::wasReleased(Key key) const
{
    return indexOf(key) < c_keyCount && m_keys.released.test(indexOf(key));
}

bool InputSystem::fixedWasPressed(Key key) const
{
    return indexOf(key) < c_keyCount && m_keys.fixedPressed.test(indexOf(key));
}

bool InputSystem::fixedWasReleased(Key key) const
{
    return indexOf(key) < c_keyCount && m_keys.fixedReleased.test(indexOf(key));
}

bool InputSystem::isDown(MouseButton button) const
{
    return indexOf(button) < c_mouseButtonCount && m_mouseButtons.down.test(indexOf(button));
}

bool InputSystem::wasPressed(MouseButton button) const
{
    return indexOf(button) < c_mouseButtonCount && m_mouseButtons.pressed.test(indexOf(button));
}

bool InputSystem::wasReleased(MouseButton button) const
{
    return indexOf(button) < c_mouseButtonCount && m_mouseButtons.released.test(indexOf(button));
}

bool InputSystem::fixedWasPressed(MouseButton button) const
{
    return indexOf(button) < c_mouseButtonCount && m_mouseButtons.fixedPressed.test(indexOf(button));
}

Vec2 InputSystem::mousePosition() const
{
    return m_mousePosition;
}

Vec2 InputSystem::mouseDelta() const
{
    return m_mouseDelta;
}

Vec2 InputSystem::wheelDelta() const
{
    return m_wheelDelta;
}

std::span<const GamepadId> InputSystem::connectedGamepads() const
{
    return m_gamepadIds;
}

bool InputSystem::isDown(GamepadId gamepad, GamepadButton button) const
{
    const Gamepad* pad = findGamepad(gamepad);
    return pad != nullptr && indexOf(button) < c_gamepadButtonCount && pad->buttons.down.test(indexOf(button));
}

bool InputSystem::wasPressed(GamepadId gamepad, GamepadButton button) const
{
    const Gamepad* pad = findGamepad(gamepad);
    return pad != nullptr && indexOf(button) < c_gamepadButtonCount && pad->buttons.pressed.test(indexOf(button));
}

bool InputSystem::fixedWasPressed(GamepadId gamepad, GamepadButton button) const
{
    const Gamepad* pad = findGamepad(gamepad);
    return pad != nullptr && indexOf(button) < c_gamepadButtonCount && pad->buttons.fixedPressed.test(indexOf(button));
}

float InputSystem::axis(GamepadId gamepad, GamepadAxis axis) const
{
    const Gamepad* pad = findGamepad(gamepad);
    return pad != nullptr && indexOf(axis) < c_gamepadAxisCount ? pad->axes[indexOf(axis)] : 0.0F;
}

InputSystem::Gamepad* InputSystem::findGamepad(GamepadId id)
{
    const auto found = std::ranges::find(m_gamepads, id, &Gamepad::id);
    return found != m_gamepads.end() ? &*found : nullptr;
}

const InputSystem::Gamepad* InputSystem::findGamepad(GamepadId id) const
{
    const auto found = std::ranges::find(m_gamepads, id, &Gamepad::id);
    return found != m_gamepads.end() ? &*found : nullptr;
}
} // namespace Meow2::Input
