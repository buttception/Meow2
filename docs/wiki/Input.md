# Input

`InputSystem` holds the engine's keyboard, mouse and gamepad state. **Input sources** (SDL in Player and Renderer, Qt in the editor's viewport) translate their backend's events into engine input and submit it. Gameplay **polls** the state, and code that cares about a *change* (UI, rebinding, "gamepad connected") subscribes to **input events** on the [EventBus](Events.md).

- Headers:
  - `#include <Meow2/Platform/Input/InputSystem.h>`: `InputSystem`
  - `#include <Meow2/Platform/Input/InputTypes.h>`: `Key`, `MouseButton`, `GamepadButton`, `GamepadAxis`, `GamepadId`
  - `#include <Meow2/Platform/Input/InputEvents.h>`: what sources submit, and the bus events
  - `#include <Meow2/Platform/Input/SdlInputSource.h>`, `SdlGamepadSource.h`: the SDL sources
- Namespace: `Meow2::Input`.
- Status: ✅ M2 (#89, SDL sources #91). Player and Renderer get keyboard, mouse and gamepads from SDL. The editor's viewport input (`QtInputSource`) comes with M9.

```
Player / Renderer:   SDL events ───► SdlInputSource   ─┐
Editor viewport:     Qt events  ───► QtInputSource    ─┼─► InputSystem::submit(InputEvent)   (QtInputSource: M9)
Player / Renderer:   SDL gamepads ─► SdlGamepadSource ─┘            │
                                                                    ├─► polling: isDown, wasPressed, mouseDelta, axis, …
                                                                    └─► bus events: KeyPressed, MouseMoved, GamepadConnected, …
```

## Polling (gameplay)

```cpp
using namespace Meow2::Input;

// fixedUpdate (physics, gameplay): use the latched edges, then end the step
if (input.fixedWasPressed(Key::Space)) { jump(); }
if (input.isDown(Key::W)) { moveForward(dt); }
input.endFixedStep();

// update (camera, UI): frame edges and per-frame deltas
camera.rotate(input.mouseDelta());
if (input.wasPressed(Key::Escape)) { openMenu(); }
for (const GamepadId pad : input.connectedGamepads())
{
    steer(input.axis(pad, GamepadAxis::LeftX));
}
```

| Query | Meaning |
|---|---|
| `isDown(Key)` / `isDown(MouseButton)` / `isDown(pad, GamepadButton)` | held now |
| `wasPressed(…)` / `wasReleased(…)` | went down / up **this frame** (for `update`) |
| `fixedWasPressed(…)` / `fixedWasReleased(Key)` | went down / up **since the last `endFixedStep()`** (for `fixedUpdate`) |
| `mousePosition()` | in the window's client area, desktop units |
| `mouseDelta()` / `wheelDelta()` | summed over the frame |
| `connectedGamepads()`, `axis(pad, axis)` | sticks -1..1, triggers 0..1, no dead zone |

**Keys are physical positions.** `Key` values are USB HID usage IDs (the numbers SDL's scancodes use): `Key::W` is the key where W is on a US keyboard, which is Z on a French one, so WASD movement works on every layout. Typed **text** comes separately, as `TextEntered`, after the layout and IME.

## Why there are two kinds of edges

`fixedUpdate` runs 0..n times per frame ([GameLoop](GameLoop.md)), so frame edges don't fit it:
- On a fast frame with **no** fixed step, a tap would be gone by the next frame and the jump lost.
- On a slow frame with **three** fixed steps, each step would see the same press, so three jumps.

The **fixed-step latch** solves both: `fixedWasPressed` stays set from the press until the next `endFixedStep()`, which the app calls after each `fixedUpdate`. So the first step after a press sees it exactly once. Tests `FixedLatchSurvivesZeroSteps` and `FixedLatchSeenOnce` check exactly these two cases.

## Bus events

`beginFrame()` enqueues these after updating the state; they arrive at the loop's first `dispatchQueued()`.

| Event | When |
|---|---|
| `KeyPressed{key, repeat}` | key down; `repeat` for the OS's auto-repeat while held (not a new press for polling) |
| `KeyReleased{key}` | key up |
| `MouseButtonChanged{button, down, x, y}` | button down/up, with the position |
| `MouseMoved{x, y, dx, dy}` | once per frame if the mouse moved, summed |
| `MouseWheelScrolled{x, y}` | once per frame if the wheel turned, summed |
| `TextEntered{text}` | typed text (UTF-8) |
| `GamepadConnected{gamepad, name}` / `GamepadDisconnected{gamepad}` | a pad appeared / went away (its state is cleared) |
| `GamepadButtonChanged{gamepad, button, down}` | pad button down/up |

## Using it in an app

```cpp
Meow2::Input::InputSystem input{events};
// after the Platform (and destroyed before it):
const Meow2::Input::SdlInputSource keyboardAndMouse{*platform, input};
Meow2::Result<Meow2::Input::SdlGamepadSource> gamepads = Meow2::Input::SdlGamepadSource::create(*platform, input, log);
if (!gamepads) { /* warn; the app runs without gamepads */ }
Meow2::Time::GameLoop loop{clock, events, loopDesc,
    {.pumpEvents = [&] { platform.pumpEvents(); input.beginFrame(); },   // sources submit during the pump
     .fixedUpdate = [&](double dt) { game.fixedUpdate(dt); input.endFixedStep(); },
     .update = [&](double dt) { game.update(dt); }}};
```

- **Focus loss:** when another window has focus, the OS doesn't send releases for keys let go meanwhile. Player and Renderer call `input.releaseAll()` on `WindowFocusChanged{focused = false}`, so no key stays stuck.
- **Main thread only.** Sources run on the main thread, inside the pump.

## How it works

- **Queue, then apply.** `submit()` only queues. `beginFrame()` applies the queue in order, so the state is the same for every system all frame, and a press + release in one frame (a quick tap) still sets both edges.
- **Bitsets.** Keys (256), mouse buttons and each gamepad's buttons are a set of bitsets: held, pressed, released, fixed pressed, fixed released. A press for a key that's already down, or a release for one that isn't, changes nothing.
- **Gamepads** are kept in a small vector by `GamepadId`. Input for an id that isn't connected is ignored, and disconnecting drops its state.
- **Raw input is a `std::variant`** of plain structs, applied with `std::visit`. No virtual interfaces: sources are translators that hold an `InputSystem&` (techstack.md §9).

## The SDL sources

`Platform::pumpEvents()` gives every SDL event that isn't a window event to the registered sources (each registers in its constructor and unregisters in its destructor), and they translate:

| SDL | Engine |
|---|---|
| `KEY_DOWN`/`KEY_UP` (scancode, `repeat`) | `KeyInput`: the scancode **is** the `Key` value (both are HID usage IDs; checked with `static_assert`) |
| `MOUSE_MOTION` (`x`, `y`, `xrel`, `yrel`) | `MouseMotionInput` |
| `MOUSE_BUTTON_*` 1..5 | `MouseButtonInput`: Left, Middle, Right, X1, X2 |
| `MOUSE_WHEEL` | `MouseWheelInput`, negated when SDL says `FLIPPED` (natural scrolling), so +y is always away from the user |
| `TEXT_INPUT` | `TextInput`. SDL only sends text while text input is switched on; that comes with the first text field |
| `GAMEPAD_ADDED`/`REMOVED` | the pad is opened/closed; `GamepadConnection` with its name. `GamepadId` = SDL's joystick instance id |
| `GAMEPAD_BUTTON_*`, `GAMEPAD_AXIS_MOTION` | `GamepadButtonInput`, `GamepadAxisInput` (same order as SDL's standard layout; sticks −1..1, triggers 0..1) |

- **Gamepads are optional.** `SdlGamepadSource::create()` starts SDL's gamepad subsystem and returns an error if it can't; Player and Renderer then log a warning and run without gamepads. Pads already plugged in when it starts are reported like new ones.
- **Destroy the sources before the Platform** (it asserts that no source is still registered). Declaring them after it in `main` does that.
- **Tests** push SDL events by hand (`SDL_PushEvent`) and attach an **SDL virtual gamepad** (`SDL_AttachVirtualJoystick`), so the gamepad path is tested without hardware, on CI too. A real pad plugged into the test machine shows up as well; the test looks for its virtual pad by id.

## Not yet

- `QtInputSource` for the editor viewport (M9); gamepads in the editor (it has no Platform yet).
- Switching text input on (`SDL_StartTextInput`), mouse capture (relative mode), rumble, an action layer ("Jump" = Space or A) with bindings in the config.

See also: techstack.md §9, [GameLoop](GameLoop.md), [Events](Events.md), [Window](Window.md).

## Implementation summary

- `SdlInputSource` (keyboard, mouse, text) and `SdlGamepadSource` (`create()` → `Result`, opens pads on `GAMEPAD_ADDED`, `GamepadId` = SDL joystick id) in `src/Platform/Input/Sdl/`; `Platform::pumpEvents()` passes non-window SDL events to registered handlers. Player and Renderer only (the editor gets `QtInputSource` in M9). Tested with pushed SDL events and an SDL virtual gamepad.
