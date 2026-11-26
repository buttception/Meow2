# Editor

Meow2Editor is a **Qt** application: a main window with an **editor viewport** in the middle and a **game viewport** dock, both drawn by the engine's renderer, plus a log console dock, a settings window, menus and a toolbar. **Every editor action is a command** with a JSON parameter schema, run through one registry with one undo history (techstack.md §20), so the [MCP server](Mcp.md) drives everything the UI can. Like the Player and Renderer, it remembers its window size, position, maximized state and fullscreen mode in its [config file](Config.md).

- Code:
  - `Meow2Editor/src/`: `EditorMainWindow`, `ViewportWidget`, `WindowPlacement`, `EditorTheme`, `main.cpp`;
  - `Meow2Editor/src/Commands/`: `EditorCommandRegistry`, `UndoStack`, `CommandSchema`, `EditorSettings`, `EditorCommands` (the built-in commands);
  - `Meow2Editor/src/Panels/`: `ConsoleDock`, `SettingsDialog`, `DebugDock`, `StatsDock` (and the other docks);
  - `Meow2Editor/src/Viewports/`: `ViewportController`, `EditorRenderer`, `DebugTools` (the debug tools' commands), `CaptureTools` (the frame capture's);
  - built as the `Meow2EditorCore` library plus the exe, so the tests use the same code.
- Requirements: Qt ≥ 6.8 (Widgets), tested with 6.12.0. Installing Qt and pointing CMake at it is on the [Building → Qt](Building.md#qt) page.
- Status: ✅ M7 (0.8.0). Main window and window settings (#58); commands and undo, console, settings window, toolbar (#210); the two viewports, camera rigs and Qt input (#214); the project folder, opening and saving scenes, the Assets dock (#222); the Hierarchy and Inspector docks and the edit commands (#224); picking and the selection outline (#226); gizmos, scene settings and projects (#228); the Debug and Stats docks (#230); camera icons (#231); the frame capture and its render graph (#232); the MCP server, headless mode and screenshots (#234).
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
- A position that is no longer on any screen falls back to centred. The check (`Platform::isReachable`) is the same one the SDL apps use. A position on screen but too close to the top for the title bar (a window whose client area starts at y 0) is moved down just enough (`c_titleBarRoom`, 32 logical pixels): `setGeometry()` places the client area, so the frame sits above it, and a window without a reachable title bar can't be moved.
- A headless run (`--headless`) never saves the window's placement or the dock layout: its window was never shown.
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
| `app.quit` | action | — | Closes the editor a moment later (its caller gets its answer first). Unsaved changes are lost. |
| `mcp.copySetup` | action | — | Copies the line that connects Claude Code to the MCP server (Help menu); enabled only while it runs. Registered by `main()`, not offered as a tool. |

## Menus and toolbar

- **File:** New scene, Open scene…, Open recent, Save, Save as…, Open project folder…, Import model… ([Project and scenes](#project-and-scenes)), Settings… (Ctrl+,). **Edit:** Undo (Ctrl+Z), Redo (Ctrl+Y). **Window:** Console, Game view, Assets, Hierarchy, Inspector. **Edit** also has Create entity, Duplicate (Ctrl+D) and Delete (Del). **Help:** Copy MCP setup ([MCP](Mcp.md)).
- **Toolbar:** Save, Undo, Redo, Move (W), Rotate (E), Scale (R), Local axes, Snap, Settings. **View** has the same gizmo items after the camera modes.
- **Dock tabs** go on top: docks stacked in one place show their tabs above them (`setTabPosition(…, North)`), not below as Qt does by default. A dock that shares its place with others hides its own title bar, since its tab already names it (an empty `titleBarWidget`); it gets the title back when it stands alone or floats. Drag a tab to move or undock it.
- **The mouse wheel edits a field only while it has focus** (click or Tab into it first). Over an unfocused spin box, drop-down or slider the wheel scrolls the panel instead, so scrolling down the Inspector never changes values on the way (`WheelGuard`, an application event filter: those widgets get `Qt::StrongFocus`, and a wheel event over an unfocused one goes to the scroll area around it).
- Each action is bound to a command name. `EditorMainWindow::tick()`, every frame, enables an action exactly when its command exists and is enabled. File actions that may ask first (unsaved changes, a file name) ask, then run their command. Undo and Redo name their step: "Undo Set position".

## Console

A dock at the bottom, over the engine's `ConsoleBuffer` (the last 2000 lines every logger wrote, [Logging](Logging.md)).

- **Only the level is coloured**, as in PowerShell: `<debug>` grey, `<info>` the text colour, `<warn>` yellow, `<error>` red. The time, logger and message keep the normal text colour.
- **Long lines wrap** at the dock's edge; there's no horizontal scroll bar.
- **Filters:** a checkbox per level and a text filter (message or logger, any case). **Clear** forgets what's shown; **Copy** copies the selection, or every shown line, as the log file has them; **Follow** keeps it scrolled to the newest line.
- **No cross-thread signals:** any thread may log, and `tick()` copies the new lines out of the buffer on the GUI thread once a frame.
- An agent reads the log with the `log.lines` query instead.

## Settings window

Edit → Settings…: one field per editor setting. A change runs `settings.set`, so the window and an agent go through the same checks. Settings marked * are read at startup and take effect after a restart.

| Key | Default | Range | |
|---|---|---|---|
| `editor.undoLimit` | 200 | 10–10000 | applies at once |
| `editor.gameRenderScale` | 100 | 25–100 (%) | applies at once; also the Game dock's drop-down |
| `editor.snap.translate` | 50 | 1–100000 (cm) | the move gizmo's snap step |
| `editor.snap.angle` | 15 | 1–180 (°) | the rotate gizmo's snap step |
| `editor.snap.scale` | 10 | 1–1000 (%) | the scale gizmo snaps to multiples of it |
| `editor.console.followNewest` | true | | restart |
| `jobs.threadCount` | 0 (one per hardware thread) | 0–256 | restart |
| `loop.fixedStepHz` | 60 | 1–1000 | restart |
| `loop.maxFixedSteps` | 8 | 1–64 | restart |

## Project and scenes

The editor works on a **project folder**: the scenes, models and their `.meta` files of one game. The open scene is a `.m2scene` file in it ([Scene files](SceneFiles.md)), the same format Meow2Renderer opens and saves.

- **A project** is laid out as in Unity: `Assets/` for everything imported (models under `Assets/Models/<name>/`, with their textures), `Scenes/` for scenes, and `<name>.m2project`, a small TOML file naming it. File → New project… makes one in a folder you name; File → Open project… opens another; any folder opened gets the layout added if it's missing (`project.open`). The open project is `editor.project` in `Meow2Editor.toml` (default: `project/` next to the exe) and reopens at start. It's mounted read-write and its `.meta` files are scanned, so scenes find their models' meshes.
- **At start** the editor reopens the most recent scene, or starts a new one: a main camera, with the default sun and exposure as the scene's settings ([Scene settings](#scene-settings)).
- **File menu:** New scene (Ctrl+N), Open scene… (Ctrl+O), Open recent, Save (Ctrl+S; asks for a file for an untitled scene), Save as… (Ctrl+Shift+S; starts in `Scenes/`), New project…, Open project…, Import model…, Scene settings…. The file dialogs open in the project; a file outside it is refused.
- **Unsaved changes:** the undo history's clean mark says whether the scene changed since it was opened, started or saved. The title shows the scene's file and a `*` while it has unsaved changes. New, Open, a double-click in the Assets dock, Open project folder and closing the window ask **Save / Discard / Cancel** first. Opening or starting a scene clears the undo history (its steps were about the old scene).
- **Recent scenes** (`editor.recentScenes`): the last 8 opened or saved, newest first; File → Open recent. They're paths in the project, so switching project clears them.
- **The Assets dock** (Window → Assets; left side at first): the project's scenes, models, textures and shaders in their folders, with the engine's asset icons (`assets/icons/assets/*.svg`, [Theme](Theme.md#asset-icons), built into the editor as Qt resources; Qt's own icons if Qt's Svg module is missing). Double-click a scene to open it, a model to bring it into the scene.
- **Hot reload:** a model whose files change on disk while the editor runs (its `.gltf`, its `.bin`, a texture repainted) is re-imported and uploaded again under the same mesh IDs, so every entity drawing it shows the new meshes, materials and textures (`EditorWorld::reloadChangedModels()`, twice a second, after a file has settled for 0.3 s). The entities stay as they are, edits included; a file that can't be read keeps the old model. Shaders reloaded already ([Shaders → Hot reload](Shaders.md)). The old GPU copies stay until the scene is closed.
- **Importing a model** (`asset.importModel`): a `.gltf` or `.glb` of the project comes in under a root entity named after the file, with its nodes, transforms and meshes. Its meshes get stable IDs in its `.meta` the first time, so the scene saves and reopens with them. Undo removes what the import made; redo imports again.
- **A model outside the project** (File → Import model… on a file elsewhere, such as the Khronos samples in `assets/gltf-sample-assets`) is first copied into `Assets/Models/<model name>/` with the rest of its folder (its `.bin` and textures), then imported; a second import reuses the copy. Only the window does this, for a file you picked: commands never read outside the project, so an agent works with what's in it.

### Project and scene commands

`SceneDocument` (`Meow2Editor/src/Project/`) registers them, so menus, the Assets dock and (with PR 6) MCP all use the same code:

| Command | Kind | Parameters | What it does |
|---|---|---|---|
| `project.open` | action | `path` (full folder path), `discard` | Opens a project, made if the folder doesn't exist and given `Assets/`, `Scenes/` and a project file if missing; starts a new scene in it |
| `scene.new` | action | `discard` | A new, untitled scene: a main camera 8 m back on −Z, and daylight: the sun 55° high behind that camera, exposure 2.5 (`c_newSceneEnvironment`; the engine's own defaults read as dusk) |
| `scene.open` | action | `path`, `discard` | Opens a `.m2scene` of the project, with the models it uses |
| `scene.save` | action | — | Saves to the scene's file; disabled for an untitled scene |
| `scene.saveAs` | action | `path` | Saves to a `.m2scene` of the project, which becomes the scene's file |
| `scene.info` | query | — | The scene's file, unsaved changes, the project folder, recent scenes, entity count |
| `asset.list` | query | — | The project's scenes and models |
| `asset.importModel` | action | `path` | Brings a `.gltf`/`.glb` of the project into the scene (undoable) |

- **Paths are relative to the project** (`Levels/Main.m2scene`, any case of the extension). Absolute paths, drive letters and `..` are refused, so a command can't read or write outside the project (techstack §20).
- **Nothing is dropped silently:** `scene.new`, `scene.open` and `project.open` fail while there are unsaved changes unless `discard` is true. The window asks the user and then passes it; an agent has to say so.
- **Opening is all or nothing:** a file that can't be read leaves the current scene, its GPU meshes and its history as they were. Only once the file has been read is the GPU scene emptied (`EditorRenderer::resetScene()`, keeping every viewport's view) and refilled with the scene's models.

## Scene settings

What belongs to the scene rather than to an entity: today the `Environment`, the sun's elevation and azimuth (degrees), its colour and strength, and the exposure. They light both viewports.

- **The Scene settings dock** (File → Scene settings…, Window → Scene settings; a tab beside the Inspector at first): a field per setting, made from the component registry's description of `Environment` (marked `setting`, so it's never offered as a component). An edit runs `scene.setSetting`, one undo step.
- **In the file** they're the scene's `[settings.Environment]` table ([Scene files](SceneFiles.md#scene-settings)); a file from before kept an `Environment` entity, which is folded into the setting when it's read.

| Command | Kind | Parameters | What it does |
|---|---|---|---|
| `scene.settings` | query | — | The settings and their values |
| `scene.setSetting` | action | `setting` (`Environment`), `field`, `value` | Sets one field (undoable) |

`component.list` lists the settings' fields under `settings`.

## Hierarchy and Inspector

- **Hierarchy dock** (left): the scene's entities as a tree, children under their parent. Click to select (Ctrl/Shift for several), F2 to rename, double-click to frame the entity in the editor viewport (`viewport.focus`), drag an entity onto another to hang it under it (onto empty space: to the root), Create for a new entity (under the selected one). The tree follows the scene: a change made by a command, undo or an agent shows at the next frame. Typing doesn't jump to an entity whose name starts with that letter (Qt's tree search is off), so W/E/R and F stay shortcuts.
- **Inspector dock** (right, under the game view): the selected entity's name, its ID in grey under it (selectable, to copy), and its components as cards: a slim header bar with a fold arrow, the name and a × to remove it, over the fields. Folding is remembered per component type while the editor runs. Number boxes are compact (no arrow buttons, three decimals) and vectors have coloured X Y Z letters, so the panel can be narrow; it never gets narrower than its fields plus the scroll bar, so the bar doesn't cover them (`FittingScrollArea`, also used by Scene settings). Qt's style sheets leave the text box of a spin box without arrows 1 px wide, so `CompactBox` places it itself. **Every dock has a minimum size**, so none can be squeezed until it stops working. An editor per field is made from the component registry ([Scene files → Components describe their fields once](SceneFiles.md#components-describe-their-fields-once)):

  | Field type | Editor |
  |---|---|
  | bool | check box |
  | int, float | compact number box |
  | vec3, color | 3 or 4 spin boxes |
  | quat (rotations) | Euler angles in degrees: X pitch, Y yaw, Z roll |
  | enum | drop-down |
  | string | text |
  | asset (a mesh) | a drop-down of the meshes the scene has loaded, by name ("Sponza: mesh_12": the model file, then the mesh); the ID stays in the file and the tooltip |
  | entity (`Parent`) | a drop-down of the scene's entities by name, repeated names with the start of their ID; picking a parent runs `entity.setParent` |

  An edit counts when it's finished (Enter, or leaving the box), so typing a number records one step, not one per key. Each component has Remove. **Add component** is a search field: type part of a name (any case) to see the matches, or click into it for the whole list; Enter or a pick adds it. Components kept from a scene file but unknown here are named at the bottom.
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
| `entity.setTransform` | action (`preview`: not recorded) | `id`, `position` [3], `rotation` [4, a quaternion], `scale` [3] (each optional), `preview`, `from` | Sets the local transform; undo goes back to `from` (where a drag began), or to what it was. The gizmos' command |
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

- **Moving the editor camera** (while the editor viewport has focus; a click gives it focus): a plain left click selects ([below](#selecting-in-the-viewport)), so orbiting takes Alt, as in Unity and Maya.

  | Input | Orbit mode (default) | Fly mode |
  |---|---|---|
  | Alt + left drag | turn around the point in front | — |
  | Middle drag | pan | — |
  | Wheel | zoom | change speed |
  | Right drag | fly while held (look around, WASD/QE move, Shift faster); let go to orbit again | look around |
  | WASD / QE | (while flying) | move, down/up |
  | F | frame the selection, or the scene with nothing selected (`viewport.focus`) | the same |

  Framing stands far enough back for the whole of what's framed to fit the view: a sphere of radius r fills a 60° lens at r / sin(30°) = 2 r, plus 10 %. A double-click in the Hierarchy frames that entity with everything under it (its meshes' boxes in world space, `EditorWorld::boundsOf()`).
- **The view-axis gizmo** (top-right corner of the editor viewport): the world's axes as the camera sees them, a coloured ball at each positive end (X red, Y green, Z blue) and a dim one at each negative end. Click a ball to look from that side (`viewport.viewFrom`): +X looks along −X at what the camera looks at, from as far away; ±Y look straight down or up (just short of it, at the rigs' pitch limit).

  The rigs are the engine's ([Math → Camera rigs](Math.md#camera-rigs)), shared with Meow2Renderer. Switching between them never jumps: the fly camera starts where the orbit was, and the orbit picks up around the point ahead of where the flight ended.
- **Render scale** (the Game dock's drop-down: 100, 75, 50, 25 %; `editor.gameRenderScale`): the game view's scene is drawn at that share of the viewport's size and scaled up by the tone-map pass, which samples the smaller image with linear filtering. At 50 % that's a quarter of the pixels to shade. The editor viewport always draws at full size.
- **A viewport that can't be seen draws nothing:** a hidden dock, a tab behind another, or a zero size. `ViewportController` checks each widget every frame (`isVisible()` and a non-empty visible region) before the renderer culls or draws anything for it.
- **The dock layout** (where the Game and Console docks are, their sizes, which are hidden) is saved in `editor.layout` at exit and restored at the next start.
- **The scene** is the open scene file's, or a new one ([Project and scenes](#project-and-scenes)); its `Environment` sets the viewports' sun and exposure. (`EditorWorld::buildDefaultScene()`, a ground slab and coloured cubes, is kept for tests.) Each frame the world's transforms are updated and handed to `GpuScene::setInstances()`, which uploads only what moved.
- **Without a GPU** (the offscreen platform in tests and CI, or a GPU that can't start), the editor runs without its renderer: the viewports stay empty, and every command still works.

### Selecting in the viewport

- **Click** an object in the editor viewport to select it (Alt + drag orbits instead). **Click the same spot again** to select the one behind it (through glass, or an object hidden behind another), and again to go on; it wraps round. **Ctrl + click** adds an object to the selection or removes it. A click on nothing (the sky) clears the selection. A click that moves more than 4 pixels before the button comes up is a drag, not a click.
- **Selected objects get an orange outline** in the editor viewport, also where something stands in front of them; the game view never shows it. Selecting a model's root outlines all of its parts.
- **How a click becomes a selection:**
  1. `ViewportController` sees the press and release (no Alt, little movement) and asks `EditorRenderer::requestPick()` for that pixel (Qt's logical position times the display's pixel ratio).
  2. The editor viewport's next frame draws its culled instances once more with `psPick`, which records every surface within 2 pixels of the cursor with its depth ([Rendering → Overlay passes](Rendering.md#overlay-passes-picking-and-the-selection-mask)); the records are copied to a readback buffer of that frame slot.
  3. When that slot comes round again (a frame or two later; the GPU is done with it then), `takePickResult()` hands over the hits: each instance once, what covers the cursor's pixel first, then the nearest.
  4. `EditorWorld::instanceEntity()` maps each instance slot back to its entity; `ClickSelection` drops entities gone since, lists each once, and picks the nearest, or the next one when the same spot (within 2 pixels) is clicked again with the same hits under it.
  5. The new selection goes through `selection.set`, like the Hierarchy's, so an agent sees the same command.
- **The outline:** `EditorWorld::update()` marks the instances of selected entities (and their children) `InstanceDesc::selected`. In the editor viewport (`EditorRenderer::setOutline()`), a pass draws them into a mask without a depth test, and `SelectionOutline.hlsl` draws a 2-pixel orange edge around the mask over the scene's HDR colour, before the tone map. With nothing selected, both passes are skipped.
- **Why the GPU and not rays on the CPU:** the GPU draws exactly what's on screen, alpha cutouts included, with no copy of the meshes on the CPU and no acceleration structure to keep up to date; meshlet and indirect paths pick the same. The cost is one small pass on the frame after a click, and an answer a frame or two later, which a click never notices.

### Gizmos

The last selected entity gets a gizmo in the editor viewport, always on top and the same size on screen however far away it is:

| Mode | Key | Handles | Dragging |
|---|---|---|---|
| Move | W | three arrows, and three squares between them | an arrow moves along its axis, following the point under the cursor; a square moves in its plane (the XY square along X and Y, and so on) |
| Rotate | E | three rings | turns about the axis by the angle the cursor sweeps round the centre, shown as an orange slice of the ring from where the drag began to where it is |
| Scale | R | three axes with boxes, a centre box | scales along the axis; the centre box scales all three (drag right to grow, left to shrink) |

- X is red, Y green, Z blue (a square takes the colour of the axis it lies across); the handle under the cursor (or being dragged) turns yellow. Handles are drawn 3 px thick (4 under the cursor), the arrowheads and boxes solid, the squares see-through. A gizmo handle wins over a click that would pick.
- **Local axes** (toolbar, View menu) move and rotate along the entity's own axes instead of the world's; scale always uses its own.
- **Snapping:** the toolbar's Snap, or hold Ctrl while dragging. Moves go in steps of `editor.snap.translate` cm along the axis, turns in `editor.snap.angle` degrees, and scales snap to multiples of `editor.snap.scale` %.
- W/E/R work while the editor viewport has focus and the camera isn't flying. They are separate actions on the viewport, switched off while the camera flies, because a Qt shortcut takes its key before the viewport sees it: as toolbar shortcuts they kept W and E from the flying camera. The toolbar buttons have no key and always work.
- **A drag is one undo step.** While the button is held, each change runs `entity.setTransform` with `preview: true`, which moves the entity without touching the history; on release one recorded step names the transform the drag began from (`from`), so Ctrl+Z puts it back in one go. An agent can do the same in one call.
- **How it works** (`Editing/Gizmo`, plain maths with no Qt or GPU): the gizmo works on the entity's *world* transform from when the drag began.
  - Move: the closest point on the axis line to the cursor's ray, minus where it was grabbed.
  - Rotate: where the ray meets the plane across the axis, and the signed angle from where it was grabbed.
  - Scale: how far along the axis the cursor is now, divided by where it grabbed.
  - A square: where the ray meets the square's plane, minus where it was grabbed, along each of its two axes (each snapped on its own).
  - Snapping rounds the change, not the result, so an entity at 0.3 m moves to 0.8 m, 1.3 m…
  - `ViewportController` turns the new world transform into a local one through the parent's world matrix (local = world × parent⁻¹), exact unless a parent is scaled unevenly and rotated.
  - The handles are filled triangles (`Renderer::DebugLines`, which draws triangles with alpha blending as well as lines): a thick line is a quad along it, turned to face the camera; a cone and a box are solid. They're drawn into the editor viewport's HDR colour before the tone map, their colours divided by the exposure so they stay bright in any scene. Their size comes from how many pixels a metre covers at the entity: 100 px.
  - Hit tests are in screen space: the closest handle within 8 px, the centre box within 12 px of the centre.

### Frame capture and the render graph

**Capture frame** (F9, View → Capture frame, or the button in the **Render graph** dock, a tab beside the console) keeps one frame in memory: its render graph, both viewports' passes, and a copy of every texture as each pass left it.

- **The graph:** the same node graph as Meow2Renderer's (levels top to bottom, inputs left, outputs right, wires coloured by kind: orange colour, cyan depth, green buffer, violet indirect arguments; debug passes dashed; skipped passes greyed and marked *(cached)*), drawn by `FlowView` from the engine's `Renderer::FrameFlow` and `placeFlow()`.
- **Click a pass** to list what it reads (and which pass wrote it) and writes. **Click a pin** (or a texture in that list) to see the texture as the pass left it: an output as this pass wrote it, an input as the pass before left it. R, G, B, A buttons choose the channels (one alone shows as grey) and a range sets what's black and white. Depth starts as one channel over 0 … 0.05 (reversed Z puts most of a scene near 0).
- **No live view** (owner decision): the graph is the captured frame's. For watching passes live, Meow2Renderer's Render graph window is the tool. RenderDoc (F11 in the Renderer) keeps captures on disk; this one is gone when the editor closes.
- **Textures as pictures:** Qt can't show GPU textures, so the picked copy is drawn into an RGBA8 sRGB picture of at most 512 pixels a side and copied into a readback buffer. When that frame slot comes round again (its fence has signalled), the bytes are taken, so nothing waits on the GPU and the picture arrives a frame or two after the click. Only the picked texture is read back, at preview size: a frame's full-size HDR textures would be tens of megabytes.

| Command | Kind | Parameters | What it does |
|---|---|---|---|
| `capture.frame` | action | — | Captures the next frame drawn (F9). Fails without a GPU. |
| `capture.clear` | action | — | Drops the capture and frees its copies. |
| `capture.info` | query | `brief` (optional) | Whether there's a capture, its frame number; unless `brief`, its graph (`flow`: passes with their accesses, resources with their kind, size and format) and `snapshots` (the pass and resource of each kept texture). |
| `capture.show` | action | `pass`, `resource`, `channels`, `rangeMin`, `rangeMax`, `maxSize` | Asks for the texture `pass` left of `resource` as a picture. |
| `capture.image` | query | `data` (optional) | The last picture: `serial` (rises with each new one), `snapshot`, size, settings, `pending`; with `data`, the picture as a base64 PNG. |

None is undoable. The dock only runs these and reads the queries, so an agent sees the same graph and pictures (PR 6).

- **How it works.** The capture is the engine's `Renderer::FrameCapture`, shared with Meow2Renderer. `EditorRenderer` calls its `beginFrame()` after the device's (taking a picture that came back), `afterPasses()` after every viewport's passes (capturing: a copy pass inserted after each pass that writes a texture, last pass first, so inserting doesn't move the passes still to visit; and the asked-for picture's display and readback passes), and `endFrame()` after execution. `CaptureTools` takes each picture after a frame and serves the commands.

### Icons for invisible objects

Entities nothing draws get an **icon** in the editor viewport, so they aren't invisible and can be clicked. Today that's **cameras**: a wireframe camera (a box for the body, the lens opening out at its field of view and the game view's shape, a filled triangle on top for up), light grey for the main camera and darker grey for the others. A selected camera turns orange and shows what it sees: its view volume, 10 m out. Icons keep their size on screen (48 pixels) like the gizmo, are drawn only in the editor viewport, and a click on one selects its entity (Ctrl adds or removes), before the GPU pick, which can't see them.

- **Not a component.** Icons belong to the editor: nothing is added to an entity, nothing is saved, and the game never sees them.
- **A system for any kind** (`Editing/EntityIcons`). A kind is a component type nothing draws plus an `IconProvider` returning its `IconShape`: outlines and filled triangles in icon units (1.3 units = 48 pixels), lines in metres for when it's selected, the click target's centre, and whether it's emphasised. `EntityIcons` places every entity of every kind from its world transform (scale left out, so a scaled entity doesn't stretch its icon), sizes, colours, draws and hit-tests them. An entity with several kinds gets the first kind's icon.
- **Adding one** (a light, a sound source) is one call in `registerDefaultIcons()`:

  ```cpp
  icons.add<Scene::PointLight>("Point light", [](Scene::Scene& scene, entt::entity entity, const IconContext&)
                                {
                                    IconShape shape;
                                    // edges, fills, selectedLines (its range), centre ...
                                    return std::optional{shape};
                                });
  ```

### Input: QtInputSource

The engine's `InputSystem` is fed by `QtInputSource`, an event filter on both viewport widgets ([Input → The Qt source](Input.md#the-qt-source)):

- keys, text, mouse buttons, motion and the wheel reach the `InputSystem` **only while that viewport has focus**, so typing in the console's filter never moves the camera;
- keys are the **physical** key, from Qt's native scan code (Windows scan code set 1, X11's evdev codes), so WASD stays WASD on an AZERTY layout; where there's no native code (the offscreen platform), the Qt key is the fallback;
- losing focus releases every key and button still held, so nothing stays stuck down;
- events aren't consumed: Qt's shortcuts (Ctrl+Z, F) still fire, and editor actions stay commands, never input (techstack.md §9).

### Viewport commands

| Command | Kind | Parameters | What it does |
|---|---|---|---|
| `viewport.focus` | action | `id` (optional) | Frames an entity and everything under it; without `id` the selection, or the whole scene (F, a double-click in the Hierarchy). |
| `viewport.viewFrom` | action | `side` (`+x`, `-x`, `+y`, `-y`, `+z`, `-z`) | Looks from that side at what the camera looks at (the view-axis gizmo). |
| `viewport.setCameraMode` | action | `mode` (`orbit`, `fly`) | The editor camera's mode (View menu). |
| `viewport.setRenderScale` | action | `percent` (25–100) | The game view's render scale; saved as `editor.gameRenderScale`. |
| `viewport.setGizmo` | action | `mode` (`translate`, `rotate`, `scale`), `space` (`world`, `local`), `snap` (each optional) | The gizmo's mode, axes and snapping (toolbar, W/E/R). |
| `viewport.info` | query | — | Each viewport's size, visibility, whether it drew, its scene size; the camera mode and position; the render scale; the gizmo's mode, space and snapping. |
| `viewport.screenshot` | query | `viewport` (`editor`, `game`), `maxSize` (16–4096, 1024) | A PNG of what the viewport shows, scaled down. Deferred: it answers with `viewport.screenshotResult {serial}` to poll, which says `{waiting: true}` until the picture is back; over MCP the call waits for it ([MCP](Mcp.md)). |
| `viewport.screenshotResult` | query | `serial` | The picture: `viewport`, `width`, `height`, `png` (base64). |

None is recorded in the undo history: they change how the scene is *viewed*, not the scene. `view.showPanel` also takes `game` now.

### Debug tools and stats

The Renderer's rendering debug tools, for the **editor viewport only** (the game view keeps showing the game), in the **Debug** dock (a tab beside the Inspector and Scene settings):

- **View**: a button per debug view, grouped as in Meow2Renderer (Geometry, Surface, Material, Lighting); the chosen one in red, and a line under the buttons on how to read it (overdraw's colours, the cascades'). **Position** has a dashed outline: a debug-only pass makes its texture from depth ([Renderer → Debug views](Renderer.md)).
- **Wireframe**: off, overlay (edges over the scene), only (edges on black, hidden lines removed).
- **Culling**: **Freeze** keeps culling with the camera's view of the moment, so you can fly away and see what was culled; **Show frustum** draws that frozen view volume in yellow (200 m long: the far plane is infinite).
- **Grid**: the endless ground grid (y = 0, X red, Z blue), on until you turn it off.

The **Stats** dock (a tab beside the console) shows frame time and FPS with a graph of the last 240 frames (a dashed line at 60 frames a second), the graphics API, GPU and its memory against its budget, validation errors, the scene's counts (entities, instances, meshes, materials, textures, the draw path) and memory per tag, CPU and GPU. It refreshes four times a second while it can be seen.

| Command | Kind | Parameters | What it does |
|---|---|---|---|
| `debug.setView` | action | `view` (`shaded`, `triangles`, `meshlets`, … `cascades`) | The editor viewport's view; saved as `editor.debug.view`. |
| `debug.setWireframe` | action | `mode` (`off`, `overlay`, `only`) | Saved as `editor.debug.wireframe`. |
| `debug.setCulling` | action | `freeze`, `showFrustum` (each optional) | Freezing isn't saved (it only means something while the editor runs); `showFrustum` is (`editor.debug.showFrustum`). |
| `debug.setGrid` | action | `on` | Saved as `editor.debug.grid` (on by default). |
| `debug.state` | query | — | All of the above, and `failed` if a debug pipeline couldn't be built. |
| `stats.frame` | query | — | `frameMs`, `fps`, `worstMs`, `frames` (oldest first), `entities`; with a GPU `backend`, `gpu`, `gpuMemoryUsed`, `gpuMemoryBudget`, `validationErrors`, `instances`, `meshes`, `materials`, `textures`, `drawPath`; `memory` (`tag`, `cpuBytes`, `gpuBytes`, tags in use only). |

- **Not undoable**, like the viewport commands: they change how the scene is looked at. The docks only run these commands and show what the queries say, so a change over MCP shows in them too.
- **Saved by name** in the editor's config (`editor.debug.view = "baseColor"`), not in the settings window: they're switched from the Debug dock.
- **How it works.** The tools moved from Meow2Renderer into the engine (`Renderer::RenderDebug`, `InfiniteGrid`, `DepthPositionView`), so both apps draw them with the same code. `ViewportController` hands `DebugTools`' choices to `EditorRenderer` every frame. Before the editor viewport's passes, `RenderDebug::apply()` sets the scene's debug pipelines and sky; before the game viewport's, `RenderDebug::restore()` sets them back. One `GpuScene` draws both views in one frame, so a pass must pick its pipeline when it's added, not when it records: the Blended pass did the latter, and was fixed ([Rendering](Rendering.md#overlay-passes-picking-and-the-selection-mask)). A debug view is presented without the tone curve, into the swapchain or an offscreen target alike.
- `view.showPanel` takes `debug` and `stats` too (Window → Debug, Stats).

## How it works

- **Startup order.** `main` builds the engine infrastructure first: logging, crash handler, leak check, config. Then it creates the `QApplication`, then the main window, which is placed with `showWithDesc(window, windowDescFromConfig(config, …), log)`.
- **Then the commands.** After the `QApplication`: `EditorSettings` over the config, the `UndoStack` (limit from `editor.undoLimit`), the `EditorCommandRegistry`, `registerEditorCommands()`, and then the main window, which gets them through `EditorMainWindowDeps`.
- **Qt owns the event loop.** `QApplication::exec()` runs until the window is closed. A `QTimer` is the frame tick ([Frame pacing](#frame-pacing)): it runs the engine's game loop `tick()` (techstack.md §23), then the window's `tick()` (console lines, action states). `app.quitAfterFrames` ends smoke runs.
- **The viewports draw from the window's tick.** `ViewportController::tick()` begins the input frame, moves the editor camera (only while the editor viewport has focus), updates the world's transforms and GPU instances, checks each viewport's size and visibility, takes the game camera from the scene's main `Camera`, and calls `EditorRenderer::renderFrame()`, which draws every visible viewport in one render graph: per viewport a `GpuScene` view and a tone-map pass into its swapchain.
- **`EditorRenderer` owns the GPU side:** the device, the pipeline library, the sky and one `GpuScene` (deferred, with shadows) shared by both viewports. It's made only on a real platform with `editor.render` on (`rhi.backend` and `rhi.validation` as in Renderer); otherwise the editor runs without it.
- **The viewport is a native window.** Qt normally paints all widgets of a window into one shared surface. A swapchain needs an OS window of its own (an `HWND`, an X11 window) that nothing else paints. `ViewportWidget` asks for one (`Qt::WA_NativeWindow`) and turns Qt's painting off for it (`WA_PaintOnScreen`, no paint engine).
  - `nativeHandle()` returns the same `Platform::NativeWindowHandle` as an SDL window, so the RHI won't care which kind of window it renders into.
  - Wayland and offscreen return `None` for now.
- **Placement.**
  - `setGeometry()` places the client area (without the frame), matching what the `window.*` keys mean.
  - `setWindowState()` (maximized/fullscreen) is called **before** `show()`, so Qt keeps the normal geometry to restore to.
- **Qt's DLLs next to the exe.** After every build, `windeployqt` copies the Qt DLLs and plugins (including `qoffscreen`) into `bin/`. See [Building → Qt](Building.md#qt).

### Frame pacing

- A frame that drew a viewport is followed at once (a zero-interval timer: as soon as Qt's queued events are handled), and drawing waits for the display (vsync), so the editor runs at the display's refresh rate, 144 Hz on a 144 Hz screen.
- With nothing drawn (viewports hidden or minimised) nothing waits, so a 16 ms timer paces the loop instead of spinning a core.
- The timer is a `Qt::PreciseTimer`. Qt's default coarse timers run on the Windows timer tick of about 15.6 ms, so the old 16 ms timer fired every 15 or 31 ms: uneven frames, at most 60 a second, which felt choppy.
- Gizmo drags don't rebuild the docks every frame: `entity.setTransform` previews don't bump `EditorWorld::revision()`, and the Hierarchy and Inspector catch up with the drag's final step.

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
  - a position too high for the title bar moved down (`TitleBarStaysOnScreen`);
  - hidden.
- `Editor.EditorCommandRegistryTest.*`: listing, duplicate names, unknown commands, parameter checks, execute/undo/redo, failures and queries not recorded, disabled commands.
- `Editor.UndoStackTest.*`: redo dropped by a new step, the limit, the clean mark, the revision.
- `Editor.EditorCommandsTest.*`: `settings.set` checks and saves, the undo limit applies at once, `edit.undo`/`edit.redo`/`history.get`, `log.lines` with its filters.
- `Editor.ConsoleDockTest.*`: only the level coloured (and readable on the background), level and text filters, copy.
- `Editor.SettingsDialogTest.ChangeIssuesCommand`, `Editor.MainWindowTest.*`: fields and actions run commands; actions follow the registry; panels open through `view.showPanel`.
- `Editor.ViewportTest.*`: the viewport is the native central widget; offscreen has no OS handle.
- `Editor.ViewportWin32.NativeHandle` (Windows only): with the real platform, the viewport has its own `HWND`.
- `Editor.QtInputSourceTest.*`: keys, mouse and wheel reach the `InputSystem` only from the focused viewport, and losing focus releases them; `Editor.QtInputSourceKeys.MapsNativeScanCodes`: the Windows and evdev tables.
- `Editor.EditorCameraTest.*`: right drag flies and letting go orbits without a jump; Fly mode; focus frames the bounds and stops the spin; `OrbitNeedsAlt`: a left drag orbits only with Alt held.
- `Editor.ViewportPickingTest.ClicksSelect`: the nearest hit is selected, the same spot again steps behind it and wraps round, another spot or other hits start over, Ctrl adds and removes, nothing under the cursor clears; gone and repeated entities are dropped.
- `Editor.GizmoTest.TranslatesAlongTheAxis`, `RotatesAroundTheAxis`, `ScalesAndHitTests`: a drag moves along the axis by what's under the cursor (only along it, in whole steps with snapping, along the entity's own axis in local space); turns by the swept angle (15° steps, added to an existing rotation); scales one axis or all from the centre, never to nothing; hit tests find each handle and nothing far away; the gizmo keeps its size on screen.
- `Editor.InspectorDockTest.CardsIdAndComponentSearch`: the grey ID under the name, cards that fold (kept across rebuilds), compact number boxes, Add component by search (whole names, any case). `Editor.HierarchyDockTest.KeysDontSearchAndDoubleClickFrames`: typing doesn't jump to an entity, F2 renames, a double-click frames the entity. `Editor.ConsoleDockTest.LongLinesWrap`. `Editor.InspectorDockTest.NumberBoxesShowTheirValuesAndFit`: with the theme, the values fit their boxes and the boxes stay inside the dock. `Editor.QtInputSourceTest.UnfocusedViewportIsIgnored` also checks that the first click into the viewport carries its position (it used to pick wherever the cursor last moved while the viewport had focus).
- `Editor.GizmoTest.PlanesShapesAndArc`: a square moves in its plane (snapped per axis); the shapes are solid and see-through; rotating draws the orange slice. `Editor.GizmoTest.ViewAxesPickASide`, `Editor.ViewportCommandsTest.ViewFromASide`: the corner gizmo's balls where the axes point, the camera looking from that side.
- `Editor.SceneEditorTest.EnvironmentIsASceneSetting`, `Editor.MainWindowTest.SceneSettingsDockAndAssetIcons`: settings commands with undo, not a component; the dock edits through them; the asset icons by file type, built in. `Editor.SceneDocumentTest.ProjectsHaveAssetsAndScenes`, `ChangedModelsReload`: the project layout; a model changed on disk reloads under the same mesh IDs, a broken save keeps the old one.
- `Editor.SceneEditorTest.SetTransformCommand`: only the given parts change; previews aren't recorded; the last step undoes back to `from` in one step; quaternions normalised; wrong shapes refused. `Editor.ViewportCommandsTest.GizmoCommand`: `viewport. `Editor.ViewportCommandsTest.GizmoKeysStepAsideWhileFlying`: the toolbar items take no key, and W/E/R are off while the camera flies. `Editor.InspectorDockTest.MeshAndEntityFieldsShowNames`: mesh and parent fields are drop-downs of names (repeated names told apart), picking one sets the ID, undo restores it.setGizmo` and `viewport.info`, bad values refused, the toolbar follows and runs it. `Editor.MainWindowTest.DockTabsOnTop`. `Editor.MainWindowTest.TabbedDocksHideTheirTitle`: a dock tabbed with another loses its title bar, gets it back when the other tab closes or it's split off. `Editor.WheelGuardTest.WheelEditsOnlyFocusedFields`: the wheel over an unfocused spin box or drop-down leaves its value and scrolls the panel; focused, it edits.
- `Editor.ViewportCommandsTest.*`: `viewport.setRenderScale` saves the setting and updates the drop-down, out-of-range values are refused, `viewport.setCameraMode` and `viewport.info`; the test scene has a main camera.
- `Editor.MainWindowTest.LayoutRoundTrips`: the dock layout saved in `editor.layout` is restored by the next window.
- `EditorGpu.EditorRendererTest.TwoViewportsAndRenderScale` (label `gpu`): an editor and a game viewport drawn offscreen in one frame, both filled; at 50 % the game scene is half size and still fills its viewport; a hidden viewport isn't drawn.
- `EditorGpu.EditorRendererTest.DrawsLines`: lines given to the editor viewport change its image along them, the game view not at all, and go when cleared.
- `EditorGpu.EditorRendererTest.PicksWhatIsUnderTheCursor`: a pick at the centre finds the middle cube first and the ground behind it, each once and in order; a pick in the sky finds nothing. `OutlinesTheSelection`: selecting the cube changes only orange pixels around it in the editor view, none in the game view, and clearing the selection removes them.
- `EditorGpu.EditorRendererWindowTest.WindowViewportResizes`: a window viewport resized between frames (#216). `EditorGpu.EditorRendererTest.OpensASavedScene`: a scene with a model, saved and opened again, is drawn from an emptied GPU scene with only its model, in both viewports.
- `Editor.SceneDocumentTest.*`: save and open round trip; unsaved changes need `discard`, saving marks clean, opening clears the history; paths outside the project refused; recent scenes (order, no repeats, at most 8, reopened at start); importing a model with stable mesh IDs, undo and redo.
- `Editor.MainWindowTest.UnsavedChangesAsk`, `AssetsDockOpensScenes`: Save / Discard / Cancel before a new or opened scene, the `*` in the title; a double-click in the Assets dock opens a scene.
- `Editor.SceneEditorTest.*`: create, delete and undo keep IDs, components and parents; duplicate copies the subtree with new IDs; rename and re-parent (cycles refused); add, remove and set fields with undo, wrong shapes refused; the queries. `Editor.SceneDocumentTest.ModelsOutsideTheProjectAreCopiedIn`.
- `Editor.HierarchyDockTest.FollowsTheScene`, `Editor.InspectorDockTest.EditsIssueCommands`: the tree nests, selects, renames and re-parents through commands and follows changes; finished edits run `component.setField` once; Euler angles stay as typed.
- `Editor.DebugCommandsTest.SettingsRoundTripAndSave`, `RejectsUnknownValues`: the debug commands report and save their choices (not freezing), nothing goes on the undo stack, the next window starts from them; unknown views and modes are refused. `Editor.DebugDockTest.ButtonsIssueCommands`: a button per view (dashed where a debug pass makes it), clicks run the commands, a change from elsewhere shows with its hint. `Editor.StatsDockTest.ShowsFrameAndMemory`, `Editor.StatsDockFormat.BytesForPeople`: frame times and the memory table from `stats.frame`.
- `Editor.CameraIcons.ShapeAroundTheCamera`, `SelectedShowsItsViewInOrange`, `ClickPicksTheNearest`: the camera icon keeps its size on screen, opens wider for a wider lens, shows its view volume in orange when selected, nothing behind the viewer; a click finds the nearest. `Editor.EntityIconsTest.EveryCameraGetsOneAndKindsCanBeAdded`: every camera of a scene gets one (the main one emphasised, the selected one marked), and a new kind is one `add()`; an entity of two kinds keeps the first's.
- `Editor.CaptureCommandsTest.WithoutGpu`: without a GPU, capturing and showing fail with a message, info and image say there's nothing. `Editor.CaptureFlowJson.RoundTrips`: the graph survives JSON both ways; an access to a missing resource is dropped. `Editor.RenderGraphDockTest.ShowsAFlowAndPicks` (against stand-in commands): a node per pass, a click on an input pin asks for the texture as the pass before left it, a buffer pin asks for nothing, the pass's reads and writes listed, the picture shown when it comes back. `EditorGpu.EditorRendererTest.CapturesAFrame`: a capture of both viewports has their passes and texture copies, and a picked copy comes back as a picture that isn't black.
- `EditorGpu.EditorRendererTest.DebugViewOnlyInEditorViewport`: a debug view changes the editor viewport and not one pixel of the game view; Position, Overdraw and Triangles draw with the validation layer quiet; back to shaded, the same image. `GridAndWireframeDraw`: the grid adds lines; wireframe only is edges on black, overlay edges over the scene. `FrozenCullingKeepsFrustum`: from outside, the frozen frustum's lines show, and go when culling is unfrozen.
- The MCP server, headless mode and screenshots: see [MCP → Tests](Mcp.md#tests). `Editor.CaptureCommandsTest.WithoutGpu` also checks `capture.image {after}` waiting and `viewport.screenshot` failing without a GPU.
- `Smoke.Editor.Runs` and `Smoke.Editor.OpensWindow`: the app runs 3 frames offscreen and exits 0.

## Not yet

- Gizmos on several selected entities at once, plane handles (two axes), solid handles, pivot options; picking meshes from the Assets dock by dragging; editing several entities at once in the Inspector.
- MCP resources and prompts (only tools are served), and streaming a call's progress over SSE.
- Prefabs: a model as one asset whose instances keep their edits when it's re-imported; textures cooked into the project at import.
- Renaming, moving and deleting assets in the Assets dock (each needs its `.meta` kept with it).
- Switching the graphics backend in the editor (the viewports would need to be a `RenderDevice` user).
- Mouse capture while flying (the cursor stays visible and can leave the viewport).
- Routing Qt's own messages (`qWarning`, …) to the engine log.
- Qt and its plugins in the release zip.

See also: techstack.md §9 and §20, [Window](Window.md), [Building → Qt](Building.md#qt).
