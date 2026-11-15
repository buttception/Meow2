# Editor

Meow2Editor is a **Qt** application: a main window with an **editor viewport** in the middle and a **game viewport** dock, both drawn by the engine's renderer, plus a log console dock, a settings window, menus and a toolbar. **Every editor action is a command** with a JSON parameter schema, run through one registry with one undo history (techstack.md §20), so the MCP server (M7 PR 6) can drive everything the UI can. Like the Player and Renderer, it remembers its window size, position, maximized state and fullscreen mode in its [config file](Config.md).

- Code:
  - `Meow2Editor/src/`: `EditorMainWindow`, `ViewportWidget`, `WindowPlacement`, `EditorTheme`, `main.cpp`;
  - `Meow2Editor/src/Commands/`: `EditorCommandRegistry`, `UndoStack`, `CommandSchema`, `EditorSettings`, `EditorCommands` (the built-in commands);
  - `Meow2Editor/src/Panels/`: `ConsoleDock`, `SettingsDialog`;
  - built as the `Meow2EditorCore` library plus the exe, so the tests use the same code.
- Requirements: Qt ≥ 6.8 (Widgets), tested with 6.12.0. Installing Qt and pointing CMake at it is on the [Building → Qt](Building.md#qt) page.
- Status: 🟨 M7. Main window and window settings (#58); commands and undo, console, settings window, toolbar (#210); the two viewports, camera rigs and Qt input (#214); the project folder, opening and saving scenes, the Assets dock (#222); the Hierarchy and Inspector docks and the edit commands (#224).
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

- **File:** New scene, Open scene…, Open recent, Save, Save as…, Open project folder…, Import model… ([Project and scenes](#project-and-scenes)), Settings… (Ctrl+,). **Edit:** Undo (Ctrl+Z), Redo (Ctrl+Y). **Window:** Console, Game view, Assets, Hierarchy, Inspector. **Edit** also has Create entity, Duplicate (Ctrl+D) and Delete (Del).
- **Toolbar:** Save, Undo, Redo, Settings.
- Each action is bound to a command name. `EditorMainWindow::tick()`, every frame, enables an action exactly when its command exists and is enabled. File actions that may ask first (unsaved changes, a file name) ask, then run their command. Undo and Redo name their step: "Undo Set position".

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
| `editor.gameRenderScale` | 100 | 25–100 (%) | applies at once; also the Game dock's drop-down |
| `editor.console.followNewest` | true | | restart |
| `jobs.threadCount` | 0 (one per hardware thread) | 0–256 | restart |
| `loop.fixedStepHz` | 60 | 1–1000 | restart |
| `loop.maxFixedSteps` | 8 | 1–64 | restart |

## Project and scenes

The editor works on a **project folder**: the scenes, models and their `.meta` files of one game. The open scene is a `.m2scene` file in it ([Scene files](SceneFiles.md)), the same format Meow2Renderer opens and saves.

- **The project folder** is `editor.project` in `Meow2Editor.toml` (default: `project/` next to the exe, made if missing). It's mounted read-write and its `.meta` files are scanned, so scenes find their models' meshes. File → Open project folder… (`project.open`) switches to another; a folder that doesn't exist is created.
- **At start** the editor reopens the most recent scene, or starts a new one: a main camera and an `Environment` (sun and exposure, which light both viewports).
- **File menu:** New scene (Ctrl+N), Open scene… (Ctrl+O), Open recent, Save (Ctrl+S; asks for a file for an untitled scene), Save as… (Ctrl+Shift+S), Open project folder…, Import model…. The file dialogs open in the project; a file outside it is refused.
- **Unsaved changes:** the undo history's clean mark says whether the scene changed since it was opened, started or saved. The title shows the scene's file and a `*` while it has unsaved changes. New, Open, a double-click in the Assets dock, Open project folder and closing the window ask **Save / Discard / Cancel** first. Opening or starting a scene clears the undo history (its steps were about the old scene).
- **Recent scenes** (`editor.recentScenes`): the last 8 opened or saved, newest first; File → Open recent. They're paths in the project, so switching project clears them.
- **The Assets dock** (Window → Assets; left side at first): the project's scene files and models in their folders. Double-click a scene to open it, a model to bring it into the scene.
- **Importing a model** (`asset.importModel`): a `.gltf` or `.glb` of the project comes in under a root entity named after the file, with its nodes, transforms and meshes. Its meshes get stable IDs in its `.meta` the first time, so the scene saves and reopens with them. Undo removes what the import made; redo imports again.
- **A model outside the project** (File → Import model… on a file elsewhere, such as the Khronos samples in `assets/gltf-sample-assets`) is first copied into `Models/<model name>/` with the rest of its folder (its `.bin` and textures), then imported; a second import reuses the copy. Only the window does this, for a file you picked: commands never read outside the project, so an agent works with what's in it.

### Project and scene commands

`SceneDocument` (`Meow2Editor/src/Project/`) registers them, so menus, the Assets dock and (with PR 6) MCP all use the same code:

| Command | Kind | Parameters | What it does |
|---|---|---|---|
| `project.open` | action | `path` (full folder path), `discard` | Opens (or creates) a project folder; starts a new scene in it |
| `scene.new` | action | `discard` | A new, untitled scene: a main camera and an Environment |
| `scene.open` | action | `path`, `discard` | Opens a `.m2scene` of the project, with the models it uses |
| `scene.save` | action | — | Saves to the scene's file; disabled for an untitled scene |
| `scene.saveAs` | action | `path` | Saves to a `.m2scene` of the project, which becomes the scene's file |
| `scene.info` | query | — | The scene's file, unsaved changes, the project folder, recent scenes, entity count |
| `asset.list` | query | — | The project's scenes and models |
| `asset.importModel` | action | `path` | Brings a `.gltf`/`.glb` of the project into the scene (undoable) |

- **Paths are relative to the project** (`Levels/Main.m2scene`, any case of the extension). Absolute paths, drive letters and `..` are refused, so a command can't read or write outside the project (techstack §20).
- **Nothing is dropped silently:** `scene.new`, `scene.open` and `project.open` fail while there are unsaved changes unless `discard` is true. The window asks the user and then passes it; an agent has to say so.
- **Opening is all or nothing:** a file that can't be read leaves the current scene, its GPU meshes and its history as they were. Only once the file has been read is the GPU scene emptied (`EditorRenderer::resetScene()`, keeping every viewport's view) and refilled with the scene's models.

## Hierarchy and Inspector

- **Hierarchy dock** (left): the scene's entities as a tree, children under their parent. Click to select (Ctrl/Shift for several), F2 or a double-click to rename, drag an entity onto another to hang it under it (onto empty space: to the root), Create for a new entity (under the selected one). The tree follows the scene: a change made by a command, undo or an agent shows at the next frame.
- **Inspector dock** (right, under the game view): the selected entity's name and components, an editor per field made from the component registry ([Scene files → Components describe their fields once](SceneFiles.md#components-describe-their-fields-once)):

  | Field type | Editor |
  |---|---|
  | bool | check box |
  | int, float | spin box |
  | vec3, color | 3 or 4 spin boxes |
  | quat (rotations) | Euler angles in degrees: X pitch, Y yaw, Z roll |
  | enum | drop-down |
  | string, asset, entity | text (an asset picker comes later) |

  An edit counts when it's finished (Enter, or leaving the box), so typing a number records one step, not one per key. Each component has Remove; Add component lists those the entity doesn't have. Components kept from a scene file but unknown here are named at the bottom.
- **Rotations:** stored as quaternions, shown as Euler angles. Several angle sets give the same rotation, so angles worked out from the quaternion could jump to another set; the inspector keeps the angles you typed while they still give the stored rotation, and shows those (techstack §10).
- **Edit menu:** Create entity, Duplicate (Ctrl+D), Delete (Del), on the selection.
- **Undo** brings back exactly what was there: a deleted entity returns with the same ID, components and children, so steps further back still find it.

### Edit commands

`SceneEditor` (`Meow2Editor/src/Editing/`) registers them; the docks, menus and (with PR 6) MCP all use them. Entities are named by their `EntityId` text.

| Command | Kind | Parameters | What it does |
|---|---|---|---|
| `entity.create` | action | `name`, `parent` | A new entity with a transform, at the root or under `parent`; returns its `id` |
| `entity.delete` | action | `id` | Deletes it and everything under it |
| `entity.duplicate` | action | `id` | Copies it and everything under it with new IDs, under the same parent; returns the copy's `id` |
| `entity.rename` | action | `id`, `name` | Renames it |
| `entity.setParent` | action | `id`, `parent` ("" for the root) | Moves it; its local transform is kept. Making an entity its own ancestor is refused |
| `component.add` | action | `id`, `component` | Adds a component with defaults (`Parent` goes through `entity.setParent`) |
| `component.remove` | action | `id`, `component` | Removes it (a transform with a parent or children stays) |
| `component.setField` | action | `id`, `component`, `field`, `value` | Sets one field: a number, true/false, text, or an array of 3 or 4 numbers |
| `selection.set` | action, not recorded | `ids` | Selects entities |
| `selection.get` | query | — | The selected IDs |
| `scene.hierarchy` | query | — | The entities as a tree of `{id, name, children}` |
| `entity.get` | query | `id` | Name, parent, every component's field values, unknown components |
| `component.list` | query | — | Every component's fields with their types (and an enum's values): the schema for the inspector and agents |

- **Snapshots:** deleting records each entity of the subtree as it was (`EntitySnapshot`: ID, name, every registered component's fields, kept unknown components); undo makes them again, parents first, and attaches them to the transform hierarchy. Duplicating uses the same snapshot with new IDs, and points references inside the copy (the children's parents) at the copy.
- **The world's revision** (`EditorWorld::revision()`) goes up with every change; the docks rebuild only when it moved, so they cost nothing while nothing changes.

## Viewports

The **editor viewport** (the window's centre) shows the scene through the editor's own camera; the **game viewport** (the Game dock, on the right at first) shows it through the scene's main `Camera` entity, as the game will. Each is a native child window with its own swapchain and its own `GpuScene` view, so the two can look at the same scene from different places in the same frame ([Rendering → Several views per frame](Rendering.md#several-views-per-frame)).

- **Moving the editor camera** (while the editor viewport has focus; a click gives it focus):

  | Input | Orbit mode (default) | Fly mode |
  |---|---|---|
  | Left drag | turn around the point in front | — |
  | Middle drag | pan | — |
  | Wheel | zoom | change speed |
  | Right drag | fly while held (look around, WASD/QE move, Shift faster); let go to orbit again | look around |
  | WASD / QE | (while flying) | move, down/up |
  | F | frame the scene (`viewport.focus`) | frame the scene |

  The rigs are the engine's ([Math → Camera rigs](Math.md#camera-rigs)), shared with Meow2Renderer. Switching between them never jumps: the fly camera starts where the orbit was, and the orbit picks up around the point ahead of where the flight ended.
- **Render scale** (the Game dock's drop-down: 100, 75, 50, 25 %; `editor.gameRenderScale`): the game view's scene is drawn at that share of the viewport's size and scaled up by the tone-map pass, which samples the smaller image with linear filtering. At 50 % that's a quarter of the pixels to shade. The editor viewport always draws at full size.
- **A viewport that can't be seen draws nothing:** a hidden dock, a tab behind another, or a zero size. `ViewportController` checks each widget every frame (`isVisible()` and a non-empty visible region) before the renderer culls or draws anything for it.
- **The dock layout** (where the Game and Console docks are, their sizes, which are hidden) is saved in `editor.layout` at exit and restored at the next start.
- **The scene** is the open scene file's, or a new one ([Project and scenes](#project-and-scenes)); its `Environment` sets the viewports' sun and exposure. (`EditorWorld::buildDefaultScene()`, a ground slab and coloured cubes, is kept for tests.) Each frame the world's transforms are updated and handed to `GpuScene::setInstances()`, which uploads only what moved.
- **Without a GPU** (the offscreen platform in tests and CI, or a GPU that can't start), the editor runs without its renderer: the viewports stay empty, and every command still works.

### Input: QtInputSource

The engine's `InputSystem` is fed by `QtInputSource`, an event filter on both viewport widgets ([Input → The Qt source](Input.md#the-qt-source)):

- keys, text, mouse buttons, motion and the wheel reach the `InputSystem` **only while that viewport has focus**, so typing in the console's filter never moves the camera;
- keys are the **physical** key, from Qt's native scan code (Windows scan code set 1, X11's evdev codes), so WASD stays WASD on an AZERTY layout; where there's no native code (the offscreen platform), the Qt key is the fallback;
- losing focus releases every key and button still held, so nothing stays stuck down;
- events aren't consumed: Qt's shortcuts (Ctrl+Z, F) still fire, and editor actions stay commands, never input (techstack.md §9).

### Viewport commands

| Command | Kind | Parameters | What it does |
|---|---|---|---|
| `viewport.focus` | action | — | Frames the whole scene in the editor viewport (F). |
| `viewport.setCameraMode` | action | `mode` (`orbit`, `fly`) | The editor camera's mode (View menu). |
| `viewport.setRenderScale` | action | `percent` (25–100) | The game view's render scale; saved as `editor.gameRenderScale`. |
| `viewport.info` | query | — | Each viewport's size, visibility, whether it drew, its scene size; the camera mode and position; the render scale. |

None is recorded in the undo history: they change how the scene is *viewed*, not the scene. `view.showPanel` also takes `game` now.

## How it works

- **Startup order.** `main` builds the engine infrastructure first: logging, crash handler, leak check, config. Then it creates the `QApplication`, then the main window, which is placed with `showWithDesc(window, windowDescFromConfig(config, …), log)`.
- **Then the commands.** After the `QApplication`: `EditorSettings` over the config, the `UndoStack` (limit from `editor.undoLimit`), the `EditorCommandRegistry`, `registerEditorCommands()`, and then the main window, which gets them through `EditorMainWindowDeps`.
- **Qt owns the event loop.** `QApplication::exec()` runs until the window is closed. A `QTimer` (16 ms) is the frame tick: it runs the engine's game loop `tick()` (techstack.md §23), then the window's `tick()` (console lines, action states). `app.quitAfterFrames` ends smoke runs.
- **The viewports draw from the window's tick.** `ViewportController::tick()` begins the input frame, moves the editor camera (only while the editor viewport has focus), updates the world's transforms and GPU instances, checks each viewport's size and visibility, takes the game camera from the scene's main `Camera`, and calls `EditorRenderer::renderFrame()`, which draws every visible viewport in one render graph: per viewport a `GpuScene` view and a tone-map pass into its swapchain.
- **`EditorRenderer` owns the GPU side:** the device, the pipeline library, the sky and one `GpuScene` (deferred, with shadows) shared by both viewports. It's made only on a real platform with `editor.render` on (`rhi.backend` and `rhi.validation` as in Renderer); otherwise the editor runs without it.
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
- `Editor.QtInputSourceTest.*`: keys, mouse and wheel reach the `InputSystem` only from the focused viewport, and losing focus releases them; `Editor.QtInputSourceKeys.MapsNativeScanCodes`: the Windows and evdev tables.
- `Editor.EditorCameraTest.*`: right drag flies and letting go orbits without a jump; Fly mode; focus frames the bounds and stops the spin.
- `Editor.ViewportCommandsTest.*`: `viewport.setRenderScale` saves the setting and updates the drop-down, out-of-range values are refused, `viewport.setCameraMode` and `viewport.info`; the test scene has a main camera.
- `Editor.MainWindowTest.LayoutRoundTrips`: the dock layout saved in `editor.layout` is restored by the next window.
- `EditorGpu.EditorRendererTest.TwoViewportsAndRenderScale` (label `gpu`): an editor and a game viewport drawn offscreen in one frame, both filled; at 50 % the game scene is half size and still fills its viewport; a hidden viewport isn't drawn.
- `EditorGpu.EditorRendererWindowTest.WindowViewportResizes`: a window viewport resized between frames (#216). `EditorGpu.EditorRendererTest.OpensASavedScene`: a scene with a model, saved and opened again, is drawn from an emptied GPU scene with only its model, in both viewports.
- `Editor.SceneDocumentTest.*`: save and open round trip; unsaved changes need `discard`, saving marks clean, opening clears the history; paths outside the project refused; recent scenes (order, no repeats, at most 8, reopened at start); importing a model with stable mesh IDs, undo and redo.
- `Editor.MainWindowTest.UnsavedChangesAsk`, `AssetsDockOpensScenes`: Save / Discard / Cancel before a new or opened scene, the `*` in the title; a double-click in the Assets dock opens a scene.
- `Editor.SceneEditorTest.*`: create, delete and undo keep IDs, components and parents; duplicate copies the subtree with new IDs; rename and re-parent (cycles refused); add, remove and set fields with undo, wrong shapes refused; the queries. `Editor.SceneDocumentTest.ModelsOutsideTheProjectAreCopiedIn`.
- `Editor.HierarchyDockTest.FollowsTheScene`, `Editor.InspectorDockTest.EditsIssueCommands`: the tree nests, selects, renames and re-parents through commands and follows changes; finished edits run `component.setField` once; Euler angles stay as typed.
- `Smoke.Editor.Runs` and `Smoke.Editor.OpensWindow`: the app runs 3 frames offscreen and exits 0.

## Not yet

- GPU picking in the viewport, the selection outline and gizmos (M7 PR 4, second half); an asset picker for mesh fields; editing several entities at once.
- The MCP server (M7 PR 6).
- Prefabs: a model as one asset whose instances keep their edits when it's re-imported; textures cooked into the project at import.
- Renaming, moving and deleting assets in the Assets dock (each needs its `.meta` kept with it).
- Switching the graphics backend in the editor (the viewports would need to be a `RenderDevice` user); the Renderer's debug tools (M7 PR 5).
- Mouse capture while flying (the cursor stays visible and can leave the viewport).
- Routing Qt's own messages (`qWarning`, …) to the engine log.
- Qt and its plugins in the release zip.

See also: techstack.md §9 and §20, [Window](Window.md), [Building → Qt](Building.md#qt).
