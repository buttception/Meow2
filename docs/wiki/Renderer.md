# Meow2Renderer

The rendering test bed and showcase: opens a glTF scene (or a grid of 100,000 cubes), draws it through the GPU-driven path on Vulkan or D3D12, and puts a Dear ImGui shell on top to pick scenes and read stats.

- **Status:** ✅ M6. ImGui shell, scene picker and stats (#172 / PR #173); log console, debug-tool settings, saved window layout, and orbit, fly and glTF cameras with animations (#188); rendering debug views, the live render graph and frame capture (#190); the meshlet path, its setting, statistics and debug view (#192); millimetre-sized models framed properly, and its first view shared with the golden-image tests (#194).

## Using it

```bash
build\windows-msvc-debug\bin\Meow2Renderer.exe
```

| Key | Does |
|---|---|
| F1 | Shows or hides the UI |
| F10 | Switches between Vulkan and D3D12 ([Rhi](Rhi.md#switching-backends-while-running)) |
| F9 | Captures a frame in the engine's own frame capture, in memory (the Frame capture window) |
| F11 | Captures a frame in RenderDoc (`rhi.renderDoc = true`, or launched from RenderDoc) |

**Camera** (Camera window, or `renderer.camera`): keys and the mouse move it unless the UI has them (a window under the cursor, a text field).

| Mode | Controls |
|---|---|
| Orbit | Left-drag turns around the scene's centre, middle-drag pans, the wheel zooms; SPIN turns it slowly by itself |
| Fly (noclip) | W/A/S/D moves, Q/E goes down/up, right-drag looks around, Shift is 4× faster, the wheel sets the speed (also a slider). Starts where the camera was |
| Scene camera | Looks through one of the glTF file's cameras (listed by name), perspective or orthographic |

### Windows

- **Stats:** frame time and FPS with a graph of the last 240 frames, the backend and GPU, the swapchain size, GPU memory against the OS budget, validation errors (shown only when there are some), the open scene's entities, instances, meshes, materials and textures and how long it took to open. **Memory** lists CPU memory (Debug and RelWithDebInfo) and GPU memory per `MemTag`. Buttons do the same as F10 and F11.
- **Scenes:** the 25 suggested Khronos samples in their groups, then every other model you've downloaded. Grey ones aren't downloaded yet:
  - **.GLTF** and **.GLB** beside a downloaded one open that version (each is greyed out when the model has no such version); a double click opens the default one (`.gltf` first);
  - **Fetch** runs `cmake -P cmake/FetchGltfSample.cmake <Name>` on a job thread and only downloads (progress in the console); the buttons appear once it's done;
  - **Cube grid** goes back to the cubes;
  - **Any file** opens a `.gltf` or `.glb` by path.

  The new scene is opened before the old one goes, so a file that fails to open leaves the current scene on screen, with the error in the window.
- **Console:** every line the app logs, coloured by level (debug grey, info white, warnings red, errors bright red), in Consolas. Toggles per level, a text filter (message or logger name, any case), CLEAR, COPY (the shown lines, as the log file has them) and FOLLOW (keeps the newest line in view unless you scroll up). Up to 2000 lines; older ones are dropped, and the window says how many.
- **Settings:** the debug tools and start-up options below, as checkboxes and the start backend. A change is saved to `Meow2Renderer.toml` at once; the window marks what applies on restart (validation, vsync, RenderDoc, the sky, the backend) or with the next scene (texture compression).
- **Camera:** the mode, the file's cameras, and an animation player: pick one of the file's animations to play it on the scene (PLAY/PAUSE, LOOP, a time slider). A glTF camera moved by the animation follows it; animated meshes move too. "(none)" puts the scene back at rest.

Window positions, sizes and docking are kept in the config (`renderer.uiLayout`, ImGui's own text) and restored at the next start.

- **Debug:** what the scene shows instead of its shading (below): one button per view, grouped, the active one red; the wireframe (off, over the scene, or only the wireframe with hidden lines removed); and FREEZE culling (keeps culling with the frustum from that moment: fly out and see what was culled), which also draws the frozen camera's view volume in yellow lines through the scene (SHOW FRUSTUM turns them off).
- **Render graph:** this frame's passes as a node graph from left to right. Each pass is a node: its inputs are pins on the left, its outputs pins on the right, each labelled with the resource. A pass goes one column after the latest pass whose output it uses, so independent work (the sky's tables, the culling) stands side by side, and within a column passes are ordered so wires cross less. Wires run from the output that wrote a resource to each input that uses it, coloured by kind: orange a colour texture, cyan depth, green a buffer, violet indirect arguments (the legend is at the top). A pass that draws over a resource another wrote (the sky over the opaque colour) has it as an input and an output. A hollow pin connects to nothing in the frame: imported as it was, or written and not read. Right- or middle-drag pans.
  - **Click a pass's title** to watch its outputs live in the **Pass** window (a tab per texture it writes, with channel toggles and a value range; depth shows its raw reversed-Z value between the range's ends). Click it again to stop.
  - **Click a pin** to see its resource: a texture opens in the Pass window on its tab (the pass that wrote it); for a buffer, which can't be shown as an image, the window lists its kind and every pass that writes or reads it, in order, with the state it uses it in.
- **Frame capture** (F9, or CAPTURE FRAME): one frame kept in memory, every pass and every texture as each pass left it. The same node graph: click a texture pin to see it as that pass left it, or a pass to list its inputs (with the pass each came from) and outputs, and click one to see it full size with the same controls. Nothing is saved to disk: RenderDoc (F11) is the tool for keeping captures.

Every window but the console is a tab of one panel on the left, the console is along the bottom, and the middle stays clear for the scene; drag tabs anywhere, and the layout is kept.

**Debug views** (the Debug window's VIEW): triangles, meshlets, instances and materials in their own colours, overdraw as a heat map (layers per pixel: blue 1, green 2, yellow 4, red 8, white 16+), depth, the mesh's and the shading normal, tangents, UVs, mip level (red 0 … magenta 5+), base colour, metallic, roughness, occlusion, emissive, alpha, and lighting only (a white surface), diffuse only and specular only.

### Settings

| Key | Default | Meaning |
|---|---|---|
| `renderer.ui` | true | The ImGui shell |
| `renderer.sky` | true | Hillaire's sky behind the scene and image-based lighting from it; false for the flat ambient light ([Rendering](Rendering.md#sky-and-image-based-lighting)) |
| `renderer.sunElevation`, `renderer.sunDirection` | 35, 30 | The starting sun, in degrees: above the horizon, and around +Y from +Z towards +X. The Stats window's SUN sliders change it, with intensity and exposure |
| `renderer.meshlets` | true | Draw through mesh shaders, culling each meshlet, where the GPU has them; false for the indirect path. Applies at once (the Settings window's MESHLETS). The Stats window says which path draws and, for meshlets, how many meshlets and triangles are left after culling ([Rendering](Rendering.md#the-meshlet-path)) |
| `renderer.grid` | true | The infinite grid on the ground (y = 0), drawn over the scene before the tone curve. Applies at once (the Settings window's GRID) |
| `renderer.compressTextures` | true (false in Debug) | Cook glTF textures to BC7 and BC5; cooked textures are kept in `cache/textures` next to the exe, so only a model's first load compresses ([Rendering](Rendering.md#cooked-textures-bc7-bc5-and-the-cache)) |
| `rhi.validation`, `rhi.vsync`, `rhi.renderDoc`, `rhi.renderDocOpenWindow`, `rhi.backend` | on except Release, true, false, true, vulkan | The debug tools, also in the Settings window ([Rhi](Rhi.md)) |
| `renderer.camera` | orbit | How each scene starts: `orbit`, `fly` or `scene` (a glTF camera) |
| `renderer.sceneCamera`, `renderer.animation` | 0, -1 | Which of the file's cameras `scene` uses, and which animation plays (-1 none): fixed viewpoints for checks and captures |
| `renderer.uiLayout` | empty | ImGui's window layout, written by the app; delete it to get the default tabs back |
| `renderer.debugView`, `renderer.wireframe` | -1, off | A debug view to start in (a `MEOW2_DEBUG_VIEW_*` number, -1 shaded) and the wireframe (`off`, `overlay`, `only`): for checks without the mouse |
| `renderer.watchPass`, `renderer.captureAtFrame` | empty, 0 | A pass to watch from the start (its name), and a frame to capture once: for checks without the mouse |
| `renderer.scene` | empty | Scene to open at start: a sample name or a `.gltf`/`.glb` path; empty for the cube grid |
| `renderer.sampleAssets` | `<repo>/assets/gltf-sample-assets` | Where the samples are |
| `renderer.instances` | 100000 | Cubes in the cube grid |
| `renderer.switchScene`, `renderer.switchSceneAtFrame` | empty, 0 | Opens that sample once at that frame, like picking it in the Scenes window; for checks with no mouse |

Settings given with `--set` are saved to `Meow2Renderer.toml` like the rest ([Config](Config.md)).

## How it works

### Code layout

`Meow2Renderer/src/` is split so tests can use everything but `main()` (`Meow2RendererCore`, like the editor's core library):

| File | What |
|---|---|
| `main.cpp` | The composition root and the frame |
| `SceneView.*` | One open scene: its `Scene`, transform hierarchy, `GpuScene` and camera target. Opening another makes a new view |
| `ImGuiLayer.*` | Dear ImGui on the engine: input, textures, drawing |
| `RendererUi.*` | The Stats and Scenes windows, the sample list, frame times |
| `ConsoleWindow.*` | The Console window over the engine's `Log::ConsoleBuffer` ([Logging](Logging.md#in-app-console)) |
| `SettingsWindow.*` | The Settings window, and the table of settings with their defaults: `main.cpp` reads the same table, so the app and the window never disagree |
| `CameraControls.*` | Orbit and fly cameras, glTF camera poses and lenses, the Camera window |
| `RenderDebug.*` | The Debug window: debug views, wireframe, frozen culling, the debug present pass |
| `FrameInspector.*` | The Render graph, Pass and Frame capture windows: the node graph (`layoutFlow`, `passPins`, `placeFlow`, `resourceKind`: plain functions, tested without ImGui), the live pass view, captures |
| `InfiniteGrid.*` | The ground grid: a compute pass into a layer, composited over the scene |
| `DebugLines.*` | Lines in world space over the scene (the frozen frustum), and `frustumLines()` |

Dear ImGui (v1.93.0-docking, a submodule) is built as `meow2_imgui` only with the Renderer: the engine and Player never see it. Its memory is counted under `MemTag::ImGui`.

### Look

Black panels with red accents, Barlow Semi Condensed for labels and Consolas for figures, the R-badge cat as its icon: see [Theme](Theme.md). `UiTheme.*` holds the ImGui side.

### ImGui on the engine

ImGui's own backends aren't used. They'd bring SDL into the app and a separate Vulkan and D3D12 path; instead:

- **Input** comes from the engine's `InputSystem` events (`KeyPressed`, `MouseMoved`, `TextEntered`, …), which are already the same on every platform. `toImGuiKey()` maps the engine's HID key positions to ImGui keys.
- **Drawing** goes through the bindless RHI like everything else. Each ImGui draw command becomes a `UiDraw` record (`shaders/Meow2Shared.hlsli`) in a buffer: where the vertices and indices are, the vertex offset, the texture's heap index, and the scale and offset to clip space. `shaders/ImGui.hlsl` reads ImGui's 20-byte vertices and the indices from buffers, so there's no vertex layout. A draw passes its record index and first index in the push constants (D3D12's `SV_VertexID` has no first vertex, Vulkan's does, so neither is used).
- **Coordinates and scale:** ImGui's display size is the window's size in *window coordinates*, the units SDL gives mouse positions in, so clicks land where they're drawn. On Windows those are pixels whatever the display scale; on a Retina Mac a window coordinate is two pixels, which goes to ImGui as its framebuffer scale. The display scale (1.25 at 125 %) only makes the UI bigger: the style is rebuilt with `ScaleAllSizes` and `FontScaleDpi`, and the windows' default positions and sizes are scaled too.
- **Colours:** ImGui's colours are sRGB, and the swapchain's sRGB view encodes what's written, so the shader decodes vertex colours and textures are sRGB. Without that the UI would look washed out.
- **The pass** is added to the render graph after the scene's. It draws on top of the swapchain image (no clear), with alpha blending and one scissor per command. A swapchain barrier first orders the scene's colour writes, made in another command list, before the UI's.
- **Textures:** ImGui 1.92+ asks the renderer to create, update and destroy textures; its font atlas grows as it meets new glyphs. Uploads are copied by an "ImGui textures" pass at the start of the frame. An ImGui texture ID is a slot in `ImGuiLayer`, never a GPU handle, so after F10 the layer only marks every texture destroyed and ImGui asks for them again.
- **Per-frame buffers** for vertices, indices and records are upload memory, one set per frame in flight, growing by half again when a frame needs more.

### Scenes

`SceneView::openModel()` imports the file ([Gltf](Gltf.md)), creates a `GpuScene` sized for its primitives, uploads it, makes the entities and frames the camera with its bounds. The `PipelineLibrary` gives every scene the same pipelines (culling, three draw lists, blended): adding a request it already has returns the existing pipeline.

### Frame and debugging

The scene is drawn in linear light into a 16-bit floating-point texture, `Scene colour`, as **Opaque**, **Sky** and **Blended** passes; a **Tone map** pass writes the swapchain, and the UI draws on top. So every step's output is a texture of the graph, which the inspector can show ([Rendering](Rendering.md#the-frame)).

- **Debug views** are a separate variant of the scene's shader (`DEBUG_VIEW = 1` in `GpuDriven.hlsl.meta`), picked with `GpuScene::makeDrawPipelines()` and `setPipelineOverride()`; the frame constants say which view. The game's shader (`DEBUG_VIEW = 0`) has none of it, and Meow2Player never builds the variant. A **debug present** pass (`ToneMap.hlsl`'s `DEBUG_OUTPUT = 1`) shows the colours without the tone curve, or turns the overdraw count into the heat map. Overdraw adds 1/16 a layer with additive pipelines and no depth test.
- **The Meshlets view** colours each meshlet (from the mesh shader, a flat per-vertex value: every vertex of a meshlet has the same one), grey where the indirect path drew. **The Triangles view** always draws through the indirect path, which gives each triangle its own vertices ([Rendering](Rendering.md#the-meshlet-path) says why).
- **The wireframe** is the scene's culled lists drawn again with line-fill pipelines (`DEBUG_VIEW = 2`) by `GpuScene::addOverlayPass()`, depth-tested so hidden edges stay hidden. "Only" draws the surfaces black first (they still write depth).
- **Watching a pass:** before the graph compiles, `FrameInspector` finds the pass by name (indices change between frames), and `RenderGraph::insertPassAfter()` puts a small pass right after it that draws each texture it wrote into a displayable RGBA8 sRGB image (`DebugTexture.hlsl`'s `psDisplay`: the value range, the channels). The graph adds the barriers: the texture is read, then made a target again for the next pass that writes it. ImGui shows the image through `ImGuiLayer::engineTexture()`, an ID resolved to a heap index only when the UI is drawn, and the UI pass declares the image as a read.
- **A capture** copies, after every pass that writes a texture, that texture as it is (`psCopy` into RGBA16F, or R32F for depth), so the range and channels can still change afterwards. A backend switch ends the capture: its textures belonged to the old device.
- **The infinite grid** is two passes ([`shaders/DebugGrid.hlsl`](../../shaders/DebugGrid.hlsl)). **Grid** is a compute shader: for each pixel it turns the pixel back into a ray (the inverse view-projection), meets the plane y = 0, and skips the pixel when the scene's depth there is nearer than the plane's (reversed Z: larger is nearer). How wide a line is follows the pixel's footprint on the plane, measured from where the next pixels' rays land (compute shaders have no derivatives), so lines stay one pixel wide at any distance. Cells are powers of ten that follow the camera's height, and the next size fades in as it rises; lines thinner than a few pixels fade out, which stops moiré far away; the grid fades with distance and towards the horizon. It writes a straight-alpha RGBA16F layer the size of the scene. **Grid composite** blends that layer over `Scene colour` with an alpha-blending pipeline. Two steps, not one compute pass writing the scene colour: reading and writing a storage texture with no declared format needs an optional Vulkan feature, and the layer is a texture of the graph the inspector can show.
- **Debug lines** (`DebugLines`) are a line-list pipeline (`PrimitiveTopology::Lines`, [Rhi](Rhi.md#pipeline-states-for-debugging)) with no depth target, so they show through the scene; each frame's lines go into an upload buffer per frame slot, after the view-projection matrix ([`shaders/DebugLines.hlsl`](../../shaders/DebugLines.hlsl)). `frustumLines()` builds a camera's twelve edges: the projection has no far plane (reversed infinite Z), so the far rectangle is put at a chosen distance (a point at distance d has depth near / d), here four times the scene's radius. The camera is kept when culling freezes (`RenderDebug::frozenCamera()`).
- **The node graph** is laid out by plain functions: `layoutFlow()` puts each pass one column after the passes it depends on and orders each column by the average row of its inputs' writers (the barycentre heuristic, one sweep left to right), and `placeFlow()` sizes each node for its title and labels and stacks a column's nodes. Wires have no labels: each end is a labelled pin, so nothing overlaps however many wires run between two passes. A pin's kind (`resourceKind()`) is the texture's format, or, for a buffer, whether any pass reads it as indirect arguments.

### Cameras

A stopgap until the editor's camera rigs (M7). Each new scene fits the cameras to it: the orbit circles its bounding sphere from +Z (glTF models face +Z, so the first view is the front), and the fly speed is half its radius per second. The near plane follows the scene's size (1 % of the radius, between 1 mm and 10 cm), so small models aren't clipped; depth is reversed and infinite, so there's no far plane.

The maths is in plain functions over plain structs (`updateOrbit`, `updateFly`, …) fed a `CameraInput` built from the `InputSystem`, so tests drive them without a window. Yaw and pitch are kept, not a quaternion: the camera never rolls, and pitch stops just short of straight up or down, where yaw would have no meaning.

**glTF cameras** look along their node's −Z with +X right. The importer mirrors X ([Gltf](Gltf.md#right-handed-to-left-handed)), so in the engine the node's −Z is still the view direction and its −X is right: `sceneCameraTransform` adds a half turn about the node's Y, which makes it an engine camera (+Z forward, +X right). The aspect ratio is the window's, not the file's; `zfar` is ignored for perspective cameras (infinite depth).

**Animations** are posed on the `Scene`: each frame an animation is chosen, every node's `LocalTransform` is set from `Assets::animatedTransform` (its rest transform where nothing animates it), the hierarchy updates, and the instances are gathered again. That's a few microseconds for sample scenes; a real animation system (blending, skinning, only the animated nodes) is M12's.

## Tests

`RendererApp.*` (`Meow2Renderer/tests/`): key mapping, the sample list (on a fake samples folder), model-name checks for the fetch command, frame times; the console's filter and its copy of the buffer (`ConsoleWindow.*`); settings written and saved, and what needs a restart (`SettingsWindow.*`); the layout's round trip through ImGui and the TOML file (`ImGuiLayout.*`); orbit, fly and glTF cameras (`CameraControls.*`); the flow: where a pass's input comes from, the columns and the order in them, each pass's pins, nodes that don't overlap, resource kinds (`FrameInspector.*`); the frustum's lines and the grid's constants (`DebugOverlays.*`). With `MEOW2_GPU_TESTS`, `Visual.Backends/DebugOverlayTest.*` draws on each backend: the grid below the horizon and nothing above, the grid hidden behind geometry, the frozen frustum's lines where they should be. The ImGui pass itself is checked by hand and by the RHI tests of its pieces: `RhiGpu*.RhiStatesTest.*` (scissor, alpha blending).
