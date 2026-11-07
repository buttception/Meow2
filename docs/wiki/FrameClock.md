# FrameClock

`FrameClock` measures how long each frame took. It's the first step of each [game loop](GameLoop.md) tick (techstack.md §23): its delta goes to `update()` and the fixed-timestep accumulator.

- Header: `#include <Meow2/Core/Time/FrameClock.h>`, namespace `Meow2::Time`.
- Status: ✅ M1 (#62).

## Using it

```cpp
Meow2::Time::FrameClock clock;                      // starts now

while (running)
{
    const double frameDt = clock.tick();             // seconds since the previous tick, at most 0.25
    accumulator += frameDt;                          // double: sums stay exact over long runs
    update(clock.deltaSecondsFloat());               // float: what gameplay code uses
}
MEOW2_LOG_INFO(log, "{} frames in {:.2f} s", clock.frameCount(), clock.totalSeconds());
```

| Function | Returns |
|---|---|
| `tick()` | Ends the frame. Returns its delta in seconds, clamped to `0..maxFrameDeltaSeconds()`. Call it once per frame, at the start. |
| `deltaSeconds()` / `deltaSecondsFloat()` | The last delta, as `double` or `float`. |
| `unclampedDeltaSeconds()`, `wasClamped()` | The real time before clamping, and whether it was cut. For frame-time statistics and "hitch" warnings. |
| `frameCount()` | How many times `tick()` was called. |
| `totalSeconds()` | The sum of the clamped deltas: game time, which doesn't jump after a pause. |

- The limit is a constructor parameter. The default is `FrameClock::c_maxFrameDeltaSeconds` = 0.25 s.
- `FrameClock` isn't thread-safe. The main thread owns it and passes the values on.

## Why the delta is clamped

When the program stops for a while, the next frame's delta is the whole pause: a breakpoint, dragging the window (Windows blocks the message loop while you drag), or loading a level.
- **Variable-step code** (`position += velocity * dt`) then teleports objects.
- **A fixed-timestep loop** tries to catch up with hundreds of physics steps at once. Each of those frames takes even longer, so it falls further behind: the "spiral of death".

Clamping to 0.25 s turns any pause into at most a quarter-second step. The game simply runs slower for that one frame. The real value stays available (`unclampedDeltaSeconds()`) so statistics aren't lied to.

## How it works

- **The time source is `std::chrono::steady_clock`,** which only moves forward. `system_clock` would jump when the user or NTP changes the time, giving negative or huge deltas.
- **Each `tick()`** takes the time since the previous tick, clamps it, adds it to the total and counts the frame.
- **A time earlier than the previous one** counts as no time passing. That can't happen with `steady_clock`, but it can with a caller that passes time points.
- **`tick(TimePoint now)`** takes the time explicitly, so the tests check exact values (16 ms, a 3 s pause, …) without sleeping. Replays and frame-stepping tools can use it the same way.
- **Seconds as `double`:** a `float` sum of 1/60 s steps loses milliseconds within hours. Gameplay gets a `float` delta, because a single frame's delta fits easily (techstack.md §23).

## In the apps

- **Meow2Player and Meow2Renderer** tick the clock once per loop iteration.
- **Meow2Editor** ticks it in its 16 ms Qt timer.
- **All three log** `Ran <n> frames in <s> s` at exit, and `app.quitAfterFrames` counts frames through the clock.

## Not yet

- The fixed-timestep accumulator, `fixedUpdate`/`update`/`render(alpha)` (the game loop, M2).
- Time scale and pause.
- Frame pacing or frame-rate limiting.
- Tracy frame marks.

See also: techstack.md §23, [Window](Window.md), [Editor](Editor.md).

## Implementation summary

- `Meow2::Time::FrameClock` (`Meow2/Core/Time/FrameClock.h`): `tick()` / `tick(TimePoint)` (explicit time for tests), clamped `deltaSeconds()` + `deltaSecondsFloat()`, `unclampedDeltaSeconds()`, `wasClamped()`, `frameCount()`, `totalSeconds()` (sum of clamped deltas); limit `c_maxFrameDeltaSeconds` = 0.25 s. The apps count their frames with it.
