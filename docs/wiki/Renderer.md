# Meow2Renderer

The rendering test bed and showcase: opens a glTF scene (or a grid of 100,000 cubes), draws it through the GPU-driven path on Vulkan or D3D12, and puts a Dear ImGui shell on top to pick scenes and read stats.

- **Status:** 🟨 M6. ImGui shell, scene picker and stats (#172 / PR #173). Log console, debug-tool settings window and saved window layout come later in M6.

## Using it

```bash
build\windows-msvc-debug\bin\Meow2Renderer.exe
```

| Key | Does |
|---|---|
| F1 | Shows or hides the UI |
| F10 | Switches between Vulkan and D3D12 ([Rhi](Rhi.md#switching-backends-while-running)) |
| F11 | Captures a frame in RenderDoc (`rhi.renderDoc = true`, or launched from RenderDoc) |

### Windows

- **Stats:** frame time and FPS with a graph of the last 240 frames, the backend and GPU, the swapchain size, GPU memory against the OS budget, validation errors (shown only when there are some), the open scene's entities, instances, meshes, materials and textures and how long it took to open. **Memory** lists CPU memory (Debug and RelWithDebInfo) and GPU memory per `MemTag`. Buttons do the same as F10 and F11.
- **Scenes:** the 25 suggested Khronos samples in their groups, then every other model you've downloaded. Grey ones aren't downloaded yet:
  - **Open** (or a double click) opens a downloaded one;
  - **Fetch and open** runs `cmake -P cmake/FetchGltfSample.cmake <Name>` on a job thread, then opens it (progress in the console);
  - **Cube grid** goes back to the cubes;
  - **Any file** opens a `.gltf` or `.glb` by path.

  The new scene is opened before the old one goes, so a file that fails to open leaves the current scene on screen, with the error in the window.

### Settings

| Key | Default | Meaning |
|---|---|---|
| `renderer.ui` | true | The ImGui shell |
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

Dear ImGui (v1.93.0-docking, a submodule) is built as `meow2_imgui` only with the Renderer: the engine and Player never see it. Its memory is counted under `MemTag::ImGui`.

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

`SceneView::openModel()` imports the file ([Gltf](Gltf.md)), creates a `GpuScene` sized for its primitives, uploads it, makes the entities and frames the camera with its bounds. The `PipelineLibrary` gives every scene the same two pipelines: adding a request it already has returns the existing pipeline.

The camera circles the scene's bounding sphere; its near plane follows the scene's size (1 % of the radius, between 1 mm and 10 cm), so small models aren't clipped.

## Tests

`RendererApp.*` (`Meow2Renderer/tests/`): key mapping, the sample list (on a fake samples folder), model-name checks for the fetch command, frame times. The ImGui pass itself is checked by hand and by the RHI tests of its pieces: `RhiGpu*.RhiStatesTest.*` (scissor, alpha blending).
