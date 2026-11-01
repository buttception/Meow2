# Window

Meow2Renderer and Meow2Player open one OS window through **SDL3**. Its size, position, maximized state and fullscreen mode are kept in the app's [config file](Config.md), so the window reopens where you left it.

- Headers:
  - `#include <Meow2/Platform/Platform.h>`
  - `#include <Meow2/Platform/Window/Window.h>`
  - `#include <Meow2/Platform/Window/WindowConfig.h>`
- Namespace: `Meow2::Platform`.
- Library: [SDL3](https://github.com/libsdl-org/SDL) release-3.4.18 (submodule `external/SDL`, static). It's used only in `src/Platform/Sdl/` and `src/Platform/Window/Sdl/`.
- Status: ✅ M2 (#56). Window events are **polled** until the EventBus exists (M1). The Qt editor creates its own windows (techstack.md §9).

## Settings

In `Meow2<App>.toml` (see [Config](Config.md)):

```toml
[window]
width = 1600          # size when windowed, in desktop units (160..16384)
height = 900
x = 200               # left/top edge; centred when missing or off every display
y = 120
maximized = false
mode = "windowed"     # or "fullscreen" (borderless, desktop resolution)

[platform]
videoDriver = ""      # empty = the OS default; "offscreen" = no visible window (tests, CI)
```

- **At exit** the app writes the window's state back, but only the keys that changed.
- **Maximized or fullscreen:** the saved size is still the *normal* size, so restoring the window goes back to it.
- **Monitor unplugged:** a saved position that's no longer on any display falls back to centred.
- **Bad values:** a size out of range or an unknown mode logs a warning and uses the default. The window always opens.

For one run: `Meow2Player.exe --set window.mode=fullscreen` or `--set window.width=800`.

## Using it in code

```cpp
using namespace Meow2;

Result<Platform::Platform> platform = Platform::Platform::create({}, engineLog);
const Platform::WindowDesc desc = Platform::windowDescFromConfig(config, {.title = "Meow2Player"}, engineLog);
Result<Platform::Window> window = Platform::Window::create(*platform, engineLog, desc);

while (!platform->quitRequested() && !window->closeRequested())
{
    platform->pumpEvents(std::chrono::milliseconds{16}); // sleeps while idle, at most 16 ms
    if (window->wasResized())
    {
        // resize the swapchain to window->pixelWidth() x window->pixelHeight()
    }
}
Platform::storeWindowState(config, *window);
```

| API | What it does |
|---|---|
| `Platform::create(desc, log)` | Starts SDL's video subsystem. One per process; `AlreadyExists` for a second one, `Unsupported` if the video driver can't start (e.g. no display on Linux). |
| `pumpEvents(wait)` | Handles all pending OS events and updates every window's state. Call it once per frame, before reading window state. |
| `quitRequested()` | The OS asked the app to quit: last window closed, Ctrl+C in the console, logoff. |
| `Window::create(platform, log, desc)` | Opens a window. `WindowDesc` has title, size, optional position, maximized, mode, resizable, hidden. |
| `closeRequested()` | Close button or Alt+F4. The window stays open until destroyed, so the app can save first. |
| `wasResized()` | The size changed during the last `pumpEvents()`; true for one pump. |
| `bounds()` / `restoredBounds()` | Current position and size / the last *plain windowed* ones (what's saved). |
| `pixelWidth()` / `pixelHeight()` | Drawable size in pixels: the swapchain size. On a 150 % display it's 1.5× the bounds. |
| `maximized()`, `minimized()`, `focused()`, `mode()` | Current flags. |
| `setSize`, `setPosition`, `setMode`, `maximize`, `restore`, `show`, `setTitle` | Change the window. The state updates at the next `pumpEvents()`. |
| `nativeHandle()` | `HWND`, or X11 display + window, or Wayland display + surface, for the RHI's swapchain (M5). Query it when creating the swapchain; don't keep it. |

`Platform` and `Window` are main-thread only. Destroy every `Window` before its `Platform`; declaring them in that order in `main` does it.

## How it works

- **Pimpl over SDL.** The public headers have only engine types. The SDL code lives in `src/Platform/Sdl/PlatformSdl.cpp` and `src/Platform/Window/Sdl/WindowSdl.cpp`, so another backend could replace those two files (techstack.md §3, build-time selection).
- **Event routing.**
  - Each `Window` registers its SDL window ID with the `Platform`.
  - `pumpEvents()` gives each window its own events: today only the close request is acted on directly.
  - After the queue is empty, every window **re-reads its state from SDL**: position, size, pixel size, flags. Reading the final state once, instead of replaying each intermediate event, can't get out of sync.
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

## Limits (for now)

- **Exclusive fullscreen** (changing the display mode) isn't supported; `fullscreen` is borderless at the desktop resolution.
- **One window per app.**
- **Window events are polled.** They move to the EventBus (`WindowResized`, …) when it exists.
- **No input yet.** Keyboard, mouse and gamepads come with `InputSystem` (M2).

See also: techstack.md §9, [Config](Config.md), [Memory](Memory.md), API reference `Meow2::Platform`.
