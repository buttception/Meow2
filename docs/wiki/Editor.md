# Editor

Meow2Editor is a **Qt** application. Right now it is a main window with a native viewport. The RHI will render into that viewport (M5), and the docks, editor commands and the MCP server come with M7 (techstack.md §20). Like the Player and Renderer, it remembers its window size, position, maximized state and fullscreen mode in its [config file](Config.md).

- Code:
  - `Meow2Editor/src/`: `EditorMainWindow`, `ViewportWidget`, `WindowPlacement`, `main.cpp`;
  - built as the `Meow2EditorCore` library plus the exe, so the tests use the same code.
- Requirements: Qt ≥ 6.8 (Widgets), tested with 6.12.0. Installing Qt and pointing CMake at it is on the [Building → Qt](Building.md#qt) page.
- Status: 🟨 M7. Main window and window settings are done (#58).
- **Window size:** in logical units like every app ([Window](Window.md#settings)): 80 % of the screen the first time, and shrunk to fit when a saved size is too big for the screen (Qt sizes follow display scaling, so 1920 × 1080 at 150 % would be 2880 × 1620 real pixels).

## Settings

The same `window.*` keys as the other apps ([Window](Window.md#settings)), in `Meow2Editor.toml`:

```toml
[window]
width = 1600
height = 900
x = 200
y = 120
maximized = true
mode = "windowed"     # or "fullscreen"

[platform]
videoDriver = ""      # empty = the OS default; "offscreen" = no visible window (tests, CI)
```

- At exit the editor writes back the window's **normal** geometry (Qt's `normalGeometry()`), so a window closed while maximized reopens maximized and restores to its normal size.
- A position that is no longer on any screen falls back to centred. The check (`Platform::isReachable`) is the same one the SDL apps use.
- `platform.videoDriver` is passed to Qt as its `-platform` option. The key uses SDL's names, so `x11` becomes Qt's `xcb`; the others (`windows`, `wayland`, `offscreen`) are the same.

## How it works

- **Startup order.** `main` builds the engine infrastructure first: logging, crash handler, leak check, config. Then it creates the `QApplication`, then the main window, which is placed with `showWithDesc(window, windowDescFromConfig(config, …), log)`.
- **Qt owns the event loop.** `QApplication::exec()` runs until the window is closed. A `QTimer` (16 ms) is the frame tick. Today it only counts frames, and `app.quitAfterFrames` ends smoke runs. Later it will call the engine's `tick()` (techstack.md §23).
- **The viewport is a native window.** Qt normally paints all widgets of a window into one shared surface. A swapchain needs an OS window of its own (an `HWND`, an X11 window) that nothing else paints. `ViewportWidget` asks for one (`Qt::WA_NativeWindow`) and turns Qt's painting off for it (`WA_PaintOnScreen`, no paint engine).
  - `nativeHandle()` returns the same `Platform::NativeWindowHandle` as an SDL window, so the RHI won't care which kind of window it renders into.
  - Wayland and offscreen return `None` for now.
- **Placement.**
  - `setGeometry()` places the client area (without the frame), matching what the `window.*` keys mean.
  - `setWindowState()` (maximized/fullscreen) is called **before** `show()`, so Qt keeps the normal geometry to restore to.
- **Qt's DLLs next to the exe.** After every build, `windeployqt` copies the Qt DLLs and plugins (including `qoffscreen`) into `bin/`. See [Building → Qt](Building.md#qt).

## Look

The same theme as Meow2Renderer, as Fusion style, palette and style sheet, with Barlow Semi Condensed as the application font and the E-badge cat as the icon: see [Theme](Theme.md). `EditorTheme.*` holds it; `main.cpp` applies it right after creating the `QApplication`.

## No memory tracking in the editor

Player and Renderer send every `new`/`delete` through the memory tracker ([Memory](Memory.md)). **The editor doesn't.**

- **Why:** on Windows each DLL binds `operator new`/`delete` itself. Our replacement only covers code compiled into our exe, while Qt's DLLs use the C runtime's versions. Qt objects are routinely created by us and deleted by Qt; a parent widget deletes its children inside `Qt6Widgets.dll`. Such a `delete` would hand a tracked block, with its header in front, to the C runtime's `free()` and corrupt the heap.
- **What's still tracked:** the editor's leak check covers allocations made through library hooks (SDL, Jolt, … `globalTracker().allocateTagged()`). Today it reports `No memory leaks (0 allocations during the run)`.

## Tests

The `Editor.*` tests run on Qt's **offscreen** platform, so they need no desktop. Each test creates its own `QApplication` (`tests/QtTestApplication.h`).

- `Editor.WindowPlacementTest.*`:
  - applying a desc;
  - normal geometry kept while maximized;
  - fullscreen;
  - off-screen position centred;
  - hidden.
- `Editor.ViewportTest.*`: the viewport is the native central widget; offscreen has no OS handle.
- `Editor.ViewportWin32.NativeHandle` (Windows only): with the real platform, the viewport has its own `HWND`.
- `Smoke.Editor.Runs` and `Smoke.Editor.OpensWindow`: the app runs 3 frames offscreen and exits 0.

## Not yet

- Dock layout: scene hierarchy, inspector, log console.
- Editor commands and the MCP server.
- `QtInputSource`.
- Rendering into the viewport.
- Routing Qt's own messages (`qWarning`, …) to the engine log.
- Qt and its plugins in the release zip.

See also: techstack.md §9 and §20, [Window](Window.md), [Building → Qt](Building.md#qt).
