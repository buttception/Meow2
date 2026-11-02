# Game loop

`GameLoop` runs one frame of the engine per `tick()`, always in the same order: collect events, simulate at a **fixed rate**, update once with the real frame time, then render with an **interpolation factor**. All three apps use it: Player and Renderer in a `while` loop, the editor from a Qt timer.

- Headers:
  - `#include <Meow2/Core/Time/GameLoop.h>`: `GameLoop`, `GameLoopDesc`, `GameLoopCallbacks`
  - `#include <Meow2/Core/Time/FixedTimestep.h>`: the accumulator on its own
  - `#include <Meow2/Core/Time/GameLoopConfig.h>`: `gameLoopDescFromConfig`
- Namespace: `Meow2::Time`.
- Status: ✅ M2 (#87). Nothing is simulated or drawn yet; physics (M8) and the renderer (M5) plug into the callbacks.

## One frame

```
tick()
 ├─ FrameClock::tick()          frame delta, clamped to 0.25 s
 ├─ pumpEvents()                OS events → window events enqueued
 ├─ events.dispatchQueued()     window (later input) events reach their handlers
 ├─ fixedUpdate(step) × n       n = whole steps that fit in the accumulated time, at most maxFixedSteps
 ├─ update(frameDelta)          once, with the real (clamped) delta
 ├─ events.dispatchQueued()     events raised by the simulation and update
 ├─ render(alpha)               alpha = how far we are into the next fixed step, 0..1
 └─ profiler frame mark
```

## Using it

```cpp
using namespace Meow2;

Time::FrameClock clock;                                   // create right before the loop
Time::GameLoop loop{clock, events, Time::gameLoopDescFromConfig(config, engineLog),
                    {.pumpEvents = [&] { platform.pumpEvents(); },
                     .fixedUpdate = [&](double dt) { physics.step(dt); },  // dt is always 1/60 (by default)
                     .update = [&](double dt) { camera.update(dt); },      // dt is this frame's time
                     .render = [&](double alpha) { renderer.render(alpha); }}};
while (running)
{
    loop.tick();
}
```

- Any callback can be left out; the loop skips it.
- `loop.fixedStepCount()`: steps run so far. `loop.droppedTime()`: the last frame hit the step cap (below).
- In the editor, Qt pumps the OS events itself, so it has no `pumpEvents`. A `QTimer` calls `loop.tick()`.

Settings in `Meow2<App>.toml` (see [Config](Config.md)):

```toml
[loop]
fixedStepHz = 60     # fixedUpdate calls per second of game time (1..1000)
maxFixedSteps = 8    # most fixedUpdate calls in one frame (1..64)
```

A value out of range logs a warning and uses the default.

## Why a fixed timestep

Frames don't all take the same time: 7 ms, then 23 ms, then 16 ms. If physics stepped by each frame's delta, the results would depend on the frame rate. Collisions could be missed on a long frame (the object moves through a wall in one step), springs could explode, and two runs with the same input would diverge. A **fixed step** makes the simulation stable and repeatable.

**The accumulator** (`FixedTimestep`) connects the two clocks:

1. Each frame, its delta is added to the accumulator.
2. While the accumulator holds at least one step, run `fixedUpdate(step)` and subtract the step.
3. The remainder (less than one step) waits for the next frame, so no time is lost: over a minute, exactly 3600 steps run at 60 Hz, whatever the frame rate.

**`alpha`** is that remainder as a fraction of a step. Rendering happens between two fixed steps, so the renderer draws the state interpolated between the previous and the current step by `alpha`. Without that, motion stutters whenever the frame rate and the step rate don't line up (e.g. 144 Hz display, 60 Hz simulation).

**The step cap (`maxFixedSteps`)** prevents the *spiral of death*. If one frame is slow, the next frame needs more steps, which makes it slower still, and so on. Two guards stop that:
- `FrameClock` clamps a frame's delta to 0.25 s, so a breakpoint or a stall counts as a quarter of a second.
- `FixedTimestep` runs at most `maxFixedSteps` per frame and **drops** the rest (only the fraction below one step is kept). The game runs slower than real time for that frame instead of falling ever further behind.

## How it works

- **Callbacks, not inheritance.** `GameLoopCallbacks` is a struct of `std::function`s, so an app passes lambdas and the loop needs no virtual interface (techstack.md §3).
- **The clock and the event bus are injected** (techstack.md §18). Tests use a `FrameClock` started at a fixed time and call `tick(time)`, so every step count and alpha is exact.
- **Two dispatch points** (techstack.md §22): after the pump, so a resize is handled before simulating, and after `update`, so whatever the frame raised is handled before drawing.
- **Profiling:** Tracy zones `Events`, `Fixed update`, `Update` and `Render`, and the frame mark at the end of each tick (techstack.md §3).
- **No frame pacing yet.** Player and Renderer still sleep up to 16 ms in `pumpEvents` while idle; the swapchain's vsync (M5) takes over pacing.

## Tests

`Core.FixedTimestep.*` (whole steps, carried remainder, the cap, zero delta, alpha range and no lost time over 1000 uneven frames) and `Core.GameLoopTest.*` (phase order, step count and deltas, both dispatch points, a 10 s stall, empty callbacks). `Core.GameLoopConfigTest.*` covers the settings.

See also: [FrameClock](FrameClock.md), [Events](Events.md), techstack.md §23, Glenn Fiedler, [Fix Your Timestep!](https://gafferongames.com/post/fix_your_timestep/).
