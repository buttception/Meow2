# Editor

Meow2Editor is a **Qt** application: a main window with a native viewport, a log console dock, a settings window, menus and a toolbar. **Every editor action is a command** with a JSON parameter schema, run through one registry with one undo history (techstack.md §20), so the MCP server (M7 PR 6) can drive everything the UI can. The RHI renders into the viewport from M7 PR 2. Like the Player and Renderer, it remembers its window size, position, maximized state and fullscreen mode in its [config file](Config.md).

- Code:
  - `Meow2Editor/src/`: `EditorMainWindow`, `ViewportWidget`, `WindowPlacement`, `EditorTheme`, `main.cpp`;
  - `Meow2Editor/src/Commands/`: `EditorCommandRegistry`, `UndoStack`, `CommandSchema`, `EditorSettings`, `EditorCommands` (the built-in commands);
  - `Meow2Editor/src/Panels/`: `ConsoleDock`, `SettingsDialog`;
  - built as the `Meow2EditorCore` library plus the exe, so the tests use the same code.
- Requirements: Qt ≥ 6.8 (Widgets), tested with 6.12.0. Installing Qt and pointing CMake at it is on the [Building → Qt](Building.md#qt) page.
- Status: 🟨 M7. Main window and window settings (#58); commands and undo, console, settings window, toolbar (#210).
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

## Commands

Everything the editor does goes through **`EditorCommandRegistry::execute(name, parameters)`**: menus, toolbar buttons, shortcuts, the settings window, and later MCP. No widget changes editor state itself. That's what makes "anything a user can do, an agent can do" true by construction: a feature that has a button has a command, so it has an MCP tool.

```cpp
commands.add({.name = u"scene.rename"_s,
              .description = u"Renames an entity."_s,
              .parameters = objectSchema({{.name = u"entity"_s, .schema = propertySchema(u"string"_s, u"Its EntityId."_s), .required = true},
                                          {.name = u"name"_s, .schema = propertySchema(u"string"_s, u"The new name."_s), .required = true}})},
             [&](const QJsonObject& p) -> Result<CommandOutcome>
             {
                 // ...change it, then say how to take it back:
                 return CommandOutcome{.undoStep = UndoStep{.label = u"Rename"_s, .undo = ..., .redo = ...}};
             });

commands.execute(u"scene.rename", {{u"entity"_s, id}, {u"name"_s, u"Lamp"_s}});
```

- **A command** is a `CommandInfo` (name, description, kind, JSON Schema of its parameters, an optional `isEnabled` check) and a handler (a `std::function`, no virtuals).
  - **Actions** change something. If it's undoable, the handler returns an `UndoStep`: a label and two closures, `undo` and `redo`, that capture the old and new values by value.
  - **Queries** only read. They're never recorded.
- **`execute()`**:
  1. finds the command (`NotFound` if there's none);
  2. asks `isEnabled()` (`Unsupported` if not now: Undo with nothing to undo);
  3. checks the parameters against the schema (`InvalidArgument`, naming the parameter; the handler doesn't run);
  4. runs the handler, and on success records its undo step;
  5. logs the action and its parameters at debug level, and any failure as a warning.
- **Schemas** are JSON Schema, the format MCP tool definitions use, so they go to MCP clients unchanged. `CommandSchema.h` checks the subset commands need: an object with `properties` and `required`; per property `type`, `enum`, `minimum`/`maximum`. A parameter that isn't in `properties` is refused, so a typo isn't silently ignored. `objectSchema()` and `propertySchema()` build them.
- **The history** (`UndoStack`): one list of steps and a position. Undo moves the position back, redo forward, and a new step drops the redo steps. It keeps `editor.undoLimit` steps (200 by default), dropping the oldest. A **clean mark** remembers the position at the last save, so `isClean()` tells whether there are unsaved changes, even across undo and redo.

### Built-in commands

| Command | Kind | Parameters | What it does |
|---|---|---|---|
| `edit.undo`, `edit.redo` | action | — | Undo or redo one step; enabled only when there is one. Not recorded themselves. |
| `settings.set` | action | `key`, `value` | Changes a setting and saves `Meow2Editor.toml` at once. Not recorded: preferences aren't part of the document. |
| `view.showPanel` | action | `panel` (`console`, `settings`), `visible` | Shows or hides a panel. Registered by the main window. |
| `settings.list` | query | — | Every setting with its value, default, range and whether it needs a restart. |
| `history.get` | query | — | The undo and redo labels (next first) and whether the history is clean. |
| `log.lines` | query | `since`, `levels`, `text`, `limit` | Recent log lines, oldest first, and `next`, the `since` for the next call. |

## Menus and toolbar

- **File:** Save (`scene.save`, Ctrl+S), Settings… (Ctrl+,). **Edit:** Undo (Ctrl+Z), Redo (Ctrl+Y). **Window:** Console.
- **Toolbar:** Save, Undo, Redo, Settings.
- Each action is bound to a command name. `EditorMainWindow::tick()`, every frame, enables an action exactly when its command exists and is enabled. So Save stays disabled until `scene.save` exists (M7 PR 3), and lights up by itself then. Undo and Redo name their step: "Undo Set position".

## Console

A dock at the bottom, over the engine's `ConsoleBuffer` (the last 2000 lines every logger wrote, [Logging](Logging.md)).

- **Only the level is coloured**, as in PowerShell: `<debug>` grey, `<info>` the text colour, `<warn>` yellow, `<error>` red. The time, logger and message keep the normal text colour.
- **Filters:** a checkbox per level and a text filter (message or logger, any case). **Clear** forgets what's shown; **Copy** copies the selection, or every shown line, as the log file has them; **Follow** keeps it scrolled to the newest line.
- **No cross-thread signals:** any thread may log, and `tick()` copies the new lines out of the buffer on the GUI thread once a frame.
- An agent reads the log with the `log.lines` query instead.

## Settings window

Edit → Settings…: one field per editor setting. A change runs `settings.set`, so the window and an agent go through the same checks. Settings marked * are read at startup and take effect after a restart.

| Key | Default | Range | |
|---|---|---|---|
| `editor.undoLimit` | 200 | 10–10000 | applies at once |
| `editor.console.followNewest` | true | | restart |
| `jobs.threadCount` | 0 (one per hardware thread) | 0–256 | restart |
| `loop.fixedStepHz` | 60 | 1–1000 | restart |
| `loop.maxFixedSteps` | 8 | 1–64 | restart |

## How it works

- **Startup order.** `main` builds the engine infrastructure first: logging, crash handler, leak check, config. Then it creates the `QApplication`, then the main window, which is placed with `showWithDesc(window, windowDescFromConfig(config, …), log)`.
- **Then the commands.** After the `QApplication`: `EditorSettings` over the config, the `UndoStack` (limit from `editor.undoLimit`), the `EditorCommandRegistry`, `registerEditorCommands()`, and then the main window, which gets them through `EditorMainWindowDeps`.
- **Qt owns the event loop.** `QApplication::exec()` runs until the window is closed. A `QTimer` (16 ms) is the frame tick: it runs the engine's game loop `tick()` (techstack.md §23), then the window's `tick()` (console lines, action states). `app.quitAfterFrames` ends smoke runs.
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

The `Editor.*` tests run on Qt's **offscreen** platform, so they need no desktop. Each test creates its own `QApplication` (`tests/QtTestApplication.h`); `tests/EditorTestRig.h` adds a settings file in a fresh folder, the history, the registry with the built-in commands, and a console buffer.

- `Editor.WindowPlacementTest.*`:
  - applying a desc;
  - normal geometry kept while maximized;
  - fullscreen;
  - off-screen position centred;
  - hidden.
- `Editor.EditorCommandRegistryTest.*`: listing, duplicate names, unknown commands, parameter checks, execute/undo/redo, failures and queries not recorded, disabled commands.
- `Editor.UndoStackTest.*`: redo dropped by a new step, the limit, the clean mark, the revision.
- `Editor.EditorCommandsTest.*`: `settings.set` checks and saves, the undo limit applies at once, `edit.undo`/`edit.redo`/`history.get`, `log.lines` with its filters.
- `Editor.ConsoleDockTest.*`: only the level coloured (and readable on the background), level and text filters, copy.
- `Editor.SettingsDialogTest.ChangeIssuesCommand`, `Editor.MainWindowTest.*`: fields and actions run commands; actions follow the registry; panels open through `view.showPanel`.
- `Editor.ViewportTest.*`: the viewport is the native central widget; offscreen has no OS handle.
- `Editor.ViewportWin32.NativeHandle` (Windows only): with the real platform, the viewport has its own `HWND`.
- `Smoke.Editor.Runs` and `Smoke.Editor.OpensWindow`: the app runs 3 frames offscreen and exits 0.

## Not yet

- Dock layout saved between runs, scene hierarchy, inspector (M7 PR 2–4).
- The MCP server (M7 PR 6).
- Saving scenes, which brings `scene.save` and the clean mark into use (M7 PR 3).
- `QtInputSource`.
- Rendering into the viewport.
- Routing Qt's own messages (`qWarning`, …) to the engine log.
- Qt and its plugins in the release zip.

See also: techstack.md §9 and §20, [Window](Window.md), [Building → Qt](Building.md#qt).
