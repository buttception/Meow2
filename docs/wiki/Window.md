# Window

Meow2Renderer and Meow2Player open one OS window through **SDL3**. Its size, position, maximized state and fullscreen mode are kept in the app's [config file](Config.md), so the window reopens where you left it.

- Headers:
  - `#include <Meow2/Platform/Platform.h>`
  - `#include <Meow2/Platform/Window/Window.h>`
  - `#include <Meow2/Platform/Window/WindowConfig.h>`
  - `#include <Meow2/Platform/Window/WindowEvents.h>` (events on the [EventBus](Events.md))
- Namespace: `Meow2::Platform`.
- Library: [SDL3](https://github.com/libsdl-org/SDL) release-3.4.18 (submodule `external/SDL`, static). It's used only in `src/Platform/Sdl/` and `src/Platform/Window/Sdl/`.
- Status: ✅ M2 (#56; events on the EventBus #82). The Qt editor creates its own windows (techstack.md §9).

## Settings

In `Meow2<App>.toml` (see [Config](Config.md)):

```toml
[window]
width = 1600          # size when windowed, in logical units (160..16384); 0 = 80 % of the display
height = 900
x = 200               # left/top edge; centred when missing or off every display
y = 120
maximized = false
mode = "windowed"     # or "fullscreen" (borderless, desktop resolution)
focusable = true      # false: shown without taking focus (tests, agents); read only, never saved

[platform]
videoDriver = ""      # empty = the OS default; "offscreen" = no visible window (tests, CI)
```

- **Logical units** (owner decision 2026-11-10): sizes are what the window would be at 100 % display scaling, so a window looks the same size on a 125 % laptop and a 200 % monitor, as every desktop app does, and a saved size means the same after moving to another screen. Positions are in the desktop's own coordinates. The drawable area always has the display's full pixel resolution (`pixelWidth()`), so nothing is drawn blurry.
  - SDL measures windows in pixels on Windows and X11, so the size is multiplied by the display's scale (1.5 at 150 %) when the window is made, and divided by it when saved (`Window::unitsPerLogical()`). On macOS and Wayland the system already scales window sizes, so the factor is 1. Qt (the editor) works in logical units itself.
- **Default size:** 80 % of the usable area of the display the window opens on (`c_defaultScreenShare`), centred: it fits any screen and scaling. The size it gets is saved at exit, so delete `width`/`height` (or the file) to get the default again.
- **Too big for the display** (a size saved on a bigger screen, or a large size at a high scale): shrunk to the display's usable area, less room for the title bar and frame, keeping its shape; the log says so.
- **At exit** the app writes the window's state back, but only the keys that changed.
- **Maximized or fullscreen:** the saved size is still the *normal* size, so restoring the window goes back to it.
- **Monitor unplugged:** a saved position that's no longer on any display falls back to centred.
- **Bad values:** a size out of range or an unknown mode logs a warning and uses the default. The window always opens.

For one run: `Meow2Player.exe --set window.mode=fullscreen` or `--set window.width=800`.

## Using it in code

```cpp
using namespace Meow2;

Events::EventBus events{Memory::globalTracker()};
Result<Platform::Platform> platform = Platform::Platform::create({}, engineLog, events);
const Platform::WindowDesc desc = Platform::windowDescFromConfig(config, {.title = "Meow2Player"}, engineLog);
Result<Platform::Window> window = Platform::Window::create(*platform, engineLog, desc);

bool running = true;
const Events::Subscription onClose =
    events.subscribe<Platform::WindowCloseRequested>([&](const Platform::WindowCloseRequested&) { running = false; });
const Events::Subscription onResize = events.subscribe<Platform::WindowResized>(
    [&](const Platform::WindowResized& e) { /* resize the swapchain to e.pixelWidth x e.pixelHeight */ });

while (running)
{
    platform->pumpEvents(std::chrono::milliseconds{16}); // sleeps while idle, at most 16 ms; enqueues window events
    events.dispatchQueued();                             // the handlers above run here
}
Platform::storeWindowState(config, *window);
```

| API | What it does |
|---|---|
| `Platform::create(desc, log, events)` | Starts SDL's video subsystem; window events go to `events`. One per process; `AlreadyExists` for a second one, `Unsupported` if the video driver can't start (e.g. no display on Linux). |
| `pumpEvents(wait)` | Handles all pending OS events, updates every window's state and **enqueues** an event for each change. Call it once per frame, then `events.dispatchQueued()`. |
| `quitRequested()` | The OS asked the app to quit: last window closed, Ctrl+C in the console, logoff. |
| `Window::create(platform, log, desc)` | Opens a window. `WindowDesc` has title, size, optional position, maximized, mode, resizable, hidden. |
| `id()` | The `WindowId` that window events carry. |
| `displayScale()` | Pixels per desktop unit on the window's display: 1.0 at 100 %, 1.5 at 150 %. |
| `closeRequested()` | Close button or Alt+F4. The window stays open until destroyed, so the app can save first. |
| `wasResized()` | The size changed during the last `pumpEvents()`; true for one pump. |
| `bounds()` / `restoredBounds()` | Current position and size / the last *plain windowed* ones (what's saved). |
| `pixelWidth()` / `pixelHeight()` | Drawable size in pixels: the swapchain size. On a 150 % display it's 1.5× the bounds. |
| `maximized()`, `minimized()`, `focused()`, `mode()` | Current flags. |
| `setSize`, `setPosition`, `setMode`, `maximize`, `restore`, `show`, `setTitle` | Change the window. The state updates at the next `pumpEvents()`. |
| `nativeHandle()` | `HWND`, or X11 display + window, or Wayland display + surface, for the RHI's swapchain (M5). Query it when creating the swapchain; don't keep it. |

`Platform` and `Window` are main-thread only. Destroy every `Window` before its `Platform`; declaring them in that order in `main` does it.

## Window events

`pumpEvents()` puts these on the `EventBus` (`WindowEvents.h`); they arrive at the next `dispatchQueued()`. Every event carries the `WindowId` (`window.id()`).

| Event | When | Fields |
|---|---|---|
| `WindowMoved` | the window moved | `x`, `y` |
| `WindowResized` | its size or pixel size changed: resize the swapchain | `width`, `height`, `pixelWidth`, `pixelHeight` |
| `WindowStateChanged` | maximized, minimized, restored, fullscreen or windowed | `maximized`, `minimized`, `mode` (the whole new state) |
| `WindowFocusChanged` | it gained or lost keyboard focus | `focused` |
| `WindowDisplayScaleChanged` | it moved to a display with another DPI setting | `scale` |
| `WindowCloseRequested` | close button or Alt+F4; sent once | — |
| `QuitRequested` | the OS asked the app to quit (Ctrl+C, logoff, last window closed); sent once | — |

- **Coalesced:** after each pump every window compares its state with the previous pump's and sends at most one event of each kind, with the final values. Dragging the frame across many OS events gives one `WindowResized` per frame.
- **Order** within one pump: per window moved, resized, state, focus, scale, close; then quit.
- **Queued, never immediate:** no handler runs inside `pumpEvents()`; they run at the frame's dispatch point (techstack.md §23).
- **Creating a window sends nothing:** its initial state is what `create()` returned.
- The getters (`bounds()`, `closeRequested()`, `wasResized()`, …) still return the current values, e.g. for the size when creating a swapchain.

The apps stop on `WindowCloseRequested` or `QuitRequested`, and log `WindowResized` at debug level.

## How it works

- **Pimpl over SDL.** The public headers have only engine types. The SDL code lives in `src/Platform/Sdl/PlatformSdl.cpp` and `src/Platform/Window/Sdl/WindowSdl.cpp`, so another backend could replace those two files (techstack.md §3, build-time selection).
- **Event routing.**
  - Each `Window` registers its SDL window ID with the `Platform`.
  - `pumpEvents()` gives each window its own events: today only the close request is acted on directly.
  - After the queue is empty, every window **re-reads its state from SDL**: position, size, pixel size, display scale, flags. Reading the final state once, instead of replaying each intermediate event, can't get out of sync.
  - It then **compares** that state with the previous pump's and enqueues one event per kind of change (see above).
- **Restored bounds.** Whenever a window is plain windowed (not maximized, minimized or fullscreen), its bounds are copied to `restoredBounds()`. That's the size to save, and the size to come back to.
- **High DPI.** Windows are created with SDL's high-pixel-density flag. Sizes in the config and the API are in desktop units, and the drawable area gets the full pixel resolution (`pixelWidth()`).
- **Idle apps sleep.** `pumpEvents(16 ms)` waits for the first event with `SDL_WaitEventTimeout`, so an app with nothing to draw doesn't spin a CPU core. The game loop (M2) will pace frames properly.
- **Memory.** In Debug and RelWithDebInfo, SDL allocates through `SDL_SetMemoryFunctions` hooks into `globalTracker()`, under `MemTag::Platform`. The apps' leak check covers SDL too. `SDL_Quit` frees everything SDL allocated; the `Platform.WindowTest.SdlMemoryIsTracked` test checks that.
- **One Platform per process.** SDL keeps process-global state, so the Platform is the single owning object for it (techstack.md §18). It refuses a second instance while one is alive.

## Testing without a desktop

SDL's **`offscreen`** video driver creates windows that are never shown and need no display server. The unit tests (`Platform.*`) and the apps' smoke tests use it:

```
Meow2Renderer.exe --set platform.videoDriver=offscreen --set app.quitAfterFrames=3
```

So the Linux CI job runs the same tests without X11 or Wayland. It still installs their development packages, because SDL's build needs them. A Windows-only test (`Platform.WindowWin32.NativeHandle`) opens a hidden real window and checks its `HWND`.

**Tests that need a real window** (the RHI GPU tests: a swapchain needs one) open it without taking focus (`WindowDesc::focusable = false`, so SDL shows it with `SWP_NOACTIVATE`) and, when the PC has more than one display, on the first one that isn't primary (`Platform::displays()`, each display's usable area and which is primary). Whoever is using the PC keeps their keyboard focus and their main screen. The same works for any app run: `--set window.focusable=false --set window.x=<x> --set window.y=<y>` (a position on the second display).

## Limits (for now)

- **Exclusive fullscreen** (changing the display mode) isn't supported; `fullscreen` is borderless at the desktop resolution.
- **One window per app.**
- **Input** (keyboard, mouse, gamepads) is the [Input](Input.md) page: the SDL input sources receive it through the same `pumpEvents()`.

See also: techstack.md §9, [Config](Config.md), [Memory](Memory.md), API reference `Meow2::Platform`.

## Implementation summary

- `Meow2::Platform::Platform` (owns SDL's init, one per process, `pumpEvents(wait)`, SDL memory hook → `MemTag::Platform`) and `Window` (pimpl, `WindowDesc`, polled state: bounds, restored bounds, pixel size, maximized/minimized/focused, fullscreen, close request; `nativeHandle()` for the RHI). `windowDescFromConfig` / `storeWindowState` keep the `window.*` keys in the app's config. `platform.videoDriver = "offscreen"` runs without a desktop (tests, CI). See [docs/wiki/Window.md](docs/wiki/Window.md). **Window events on the `EventBus` (M2):** `pumpEvents()` enqueues `WindowResized`, `WindowMoved`, `WindowStateChanged`, `WindowFocusChanged`, `WindowDisplayScaleChanged`, `WindowCloseRequested` and `QuitRequested` (`Meow2/Platform/Window/WindowEvents.h`), coalesced to one per kind per pump; `Platform` gets the bus injected.
