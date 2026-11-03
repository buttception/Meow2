#pragma once

/// @file
/// @brief InputSystem: the engine's input state. Sources submit raw input; gameplay polls; changes go on the bus.
///
/// @code
/// Meow2::Input::InputSystem input{events};
/// // per frame (in the game loop's pumpEvents phase):
/// platform.pumpEvents();   // SDL events -> SdlInputSource -> input.submit(...)
/// input.beginFrame();      // applies them; bus events are dispatched right after
/// // in fixedUpdate:
/// if (input.fixedWasPressed(Meow2::Input::Key::Space)) { jump(); }
/// input.endFixedStep();
/// // in update:
/// camera.rotate(input.mouseDelta());
/// @endcode
/// @see techstack.md §9, docs/wiki/Input.md

#include <Meow2/Core/Events/EventBus.h>
#include <Meow2/Platform/Input/InputEvents.h>
#include <Meow2/Platform/Input/InputTypes.h>

#include <array>
#include <bitset>
#include <span>
#include <vector>

namespace Meow2::Input
{
/// @brief A 2D value: a position or movement in desktop units, or scroll in notches. Plain data.
struct Vec2
{
    float x = 0.0F; ///< Horizontal component.
    float y = 0.0F; ///< Vertical component.

    /// @brief Equal when both components are.
    friend bool operator==(const Vec2&, const Vec2&) = default;
};

/// @brief Keeps the current state of keys, mouse and gamepads, built from the raw input that sources submit.
///
/// **Per frame:** input sources call submit() while the platform pumps; beginFrame() then applies everything in
/// order, updates the state, records the frame's edges (wasPressed/wasReleased) and enqueues the bus events
/// (InputEvents.h). Nothing changes between two beginFrame() calls, so every system sees the same input all frame.
///
/// **Two kinds of edges:**
/// - wasPressed()/wasReleased(): happened since the last beginFrame(); for `update` (variable step).
/// - fixedWasPressed()/fixedWasReleased(): **latched** since the last endFixedStep(); for `fixedUpdate`. The app
///   calls endFixedStep() after each fixedUpdate, so a press made in a frame with no fixed step waits for the next
///   one (never lost), and when a frame runs several steps only the first sees it (never doubled).
///
/// A press and release within the same frame (a quick tap) shows up as both edges, while isDown() is `false`.
/// Auto-repeat key-downs are sent as KeyPressed with `repeat` set but don't create a new edge.
/// @par Thread safety
/// Not thread-safe; main thread only (input sources run on the main thread).
/// @par Lifetime
/// The EventBus must outlive the InputSystem.
class InputSystem
{
public:
    /// @brief Creates an input state with nothing held and no gamepads.
    /// @param events Where beginFrame() enqueues the bus events. Must outlive the InputSystem.
    explicit InputSystem(Events::EventBus& events);

    /// @brief Queues raw input from a source; applied by the next beginFrame().
    /// @param event What the source saw.
    void submit(InputEvent event);

    /// @brief Applies the queued input in order, updates the state and edges, and enqueues the bus events.
    ///
    /// Call once per frame, after the platform pump and before EventBus::dispatchQueued().
    void beginFrame();

    /// @brief Clears the fixed-step edges. Call after each fixedUpdate step.
    void endFixedStep();

    /// @brief Queues a release for every held key and mouse button, e.g. when the window loses focus (the OS
    ///        doesn't send releases for keys let go while another window has focus).
    void releaseAll();

    /// @brief Whether a key is held.
    /// @param key The key.
    /// @return `true` while it's down.
    [[nodiscard]] bool isDown(Key key) const;

    /// @brief Whether a key went down since the last beginFrame() (auto-repeat doesn't count).
    /// @param key The key.
    /// @return `true` for the one frame it was pressed in.
    [[nodiscard]] bool wasPressed(Key key) const;

    /// @brief Whether a key went up since the last beginFrame().
    /// @param key The key.
    /// @return `true` for the one frame it was released in.
    [[nodiscard]] bool wasReleased(Key key) const;

    /// @brief Whether a key went down since the last endFixedStep(); use in fixedUpdate.
    /// @param key The key.
    /// @return `true` until the next endFixedStep().
    [[nodiscard]] bool fixedWasPressed(Key key) const;

    /// @brief Whether a key went up since the last endFixedStep(); use in fixedUpdate.
    /// @param key The key.
    /// @return `true` until the next endFixedStep().
    [[nodiscard]] bool fixedWasReleased(Key key) const;

    /// @brief Whether a mouse button is held.
    /// @param button The button.
    /// @return `true` while it's down.
    [[nodiscard]] bool isDown(MouseButton button) const;

    /// @brief Whether a mouse button went down since the last beginFrame().
    /// @param button The button.
    /// @return `true` for the one frame it was pressed in.
    [[nodiscard]] bool wasPressed(MouseButton button) const;

    /// @brief Whether a mouse button went up since the last beginFrame().
    /// @param button The button.
    /// @return `true` for the one frame it was released in.
    [[nodiscard]] bool wasReleased(MouseButton button) const;

    /// @brief Whether a mouse button went down since the last endFixedStep(); use in fixedUpdate.
    /// @param button The button.
    /// @return `true` until the next endFixedStep().
    [[nodiscard]] bool fixedWasPressed(MouseButton button) const;

    /// @brief The mouse position after the last beginFrame().
    /// @return Position in the window's client area, in desktop units.
    [[nodiscard]] Vec2 mousePosition() const;

    /// @brief Total mouse movement in the last frame.
    /// @return Movement in desktop units; (0, 0) when the mouse didn't move.
    [[nodiscard]] Vec2 mouseDelta() const;

    /// @brief Total wheel scroll in the last frame.
    /// @return Notches; positive y is away from the user.
    [[nodiscard]] Vec2 wheelDelta() const;

    /// @brief The gamepads connected now, in the order they appeared.
    /// @return Their ids.
    [[nodiscard]] std::span<const GamepadId> connectedGamepads() const;

    /// @brief Whether a gamepad button is held.
    /// @param gamepad The gamepad; an unknown id has nothing held.
    /// @param button  The button.
    /// @return `true` while it's down.
    [[nodiscard]] bool isDown(GamepadId gamepad, GamepadButton button) const;

    /// @brief Whether a gamepad button went down since the last beginFrame().
    /// @param gamepad The gamepad.
    /// @param button  The button.
    /// @return `true` for the one frame it was pressed in.
    [[nodiscard]] bool wasPressed(GamepadId gamepad, GamepadButton button) const;

    /// @brief Whether a gamepad button went down since the last endFixedStep(); use in fixedUpdate.
    /// @param gamepad The gamepad.
    /// @param button  The button.
    /// @return `true` until the next endFixedStep().
    [[nodiscard]] bool fixedWasPressed(GamepadId gamepad, GamepadButton button) const;

    /// @brief A gamepad axis's value.
    /// @param gamepad The gamepad; an unknown id reads 0.
    /// @param axis    The axis.
    /// @return -1..1 for sticks, 0..1 for triggers (no dead zone applied).
    [[nodiscard]] float axis(GamepadId gamepad, GamepadAxis axis) const;

private:
    /// @brief Held state and edges for one set of buttons (keys, mouse buttons, one gamepad's buttons).
    template <std::size_t N>
    struct ButtonSet
    {
        std::bitset<N> down;          ///< Held now.
        std::bitset<N> pressed;       ///< Went down this frame.
        std::bitset<N> released;      ///< Went up this frame.
        std::bitset<N> fixedPressed;  ///< Went down since the last fixed step.
        std::bitset<N> fixedReleased; ///< Went up since the last fixed step.

        void set(std::size_t index, bool isDown);
        void clearFrameEdges();
        void clearFixedEdges();
    };

    /// @brief One connected gamepad.
    struct Gamepad
    {
        GamepadId id = 0;                             ///< Its id.
        ButtonSet<c_gamepadButtonCount> buttons{};    ///< Button state.
        std::array<float, c_gamepadAxisCount> axes{}; ///< Axis values.
    };

    void apply(const InputEvent& event);
    [[nodiscard]] Gamepad* findGamepad(GamepadId id);
    [[nodiscard]] const Gamepad* findGamepad(GamepadId id) const;

    Events::EventBus& m_events;
    std::vector<InputEvent> m_queue;
    ButtonSet<c_keyCount> m_keys;
    ButtonSet<c_mouseButtonCount> m_mouseButtons;
    Vec2 m_mousePosition;
    Vec2 m_mouseDelta;
    Vec2 m_wheelDelta;
    std::vector<Gamepad> m_gamepads;
    std::vector<GamepadId> m_gamepadIds;
};
} // namespace Meow2::Input
