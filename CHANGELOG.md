# Changelog

All notable changes to Meow2, per released version. The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and versions follow the rules in [techstack.md §21](techstack.md) (MINOR = milestone).

- **Every PR** adds one or more lines under **[Unreleased]**, in the matching group (`Added`, `Changed`, `Deprecated`, `Removed`, `Fixed`, `Security`), ending with the PR number.
- **The milestone-closing PR** renames `[Unreleased]` to `[0.Y.0] — <date>`, adds a new empty `[Unreleased]` above it, and bumps `VERSION`.
- **The GitHub Release** for a tag uses that version's section as its description (AGENTS.md §2.7).

## [Unreleased]

### Changed
- The roadmap: M7, the Editor, is planned in six steps (commands and undo, an editor and a game viewport with a faster GPU scene, scene files and an asset browser, gizmos and picking, the Renderer's debug tools in Qt, MCP for AI agents), and M12 gains inferred static or movable objects (#201).

## [0.7.0] — 2026-11-14

### Added
- Shadows from the sun: cascaded shadow maps (four cascades, 2048 × 2048 each) with soft edges, in both forward and deferred shading. Objects that never move are drawn into a cached shadow map only when something changes (the sun turns, the camera travels far, a static object moves); moving objects, such as what a glTF animation drives, are drawn every frame. Meow2Renderer has a SHADOWS setting, a Cascades debug view, and counts cache redraws in its Stats window; the golden images now include shadows (#203).
- RHI: depth-only passes and pipelines, with no colour target (#203).
- Deferred shading: opaque surfaces are drawn into a G-buffer (albedo, normal, material, emissive) and lit once per pixel by a Lighting pass that rebuilds each pixel's position from depth. It's the default; DEFERRED in Meow2Renderer's Settings switches back to forward shading, and both draw the same image (the golden images check both) (#199).
- Meow2Renderer: a Position debug view, the positions deferred lighting rebuilds from depth as 1 m colour bands, made by a debug-only pass that runs only while it's shown; its button has a dotted outline (#199).
- Meow2Renderer's Render graph marks debugging passes and the textures only they make with dashed lines, so what belongs to the game's frame stands apart (#199).
- Meow2Renderer screenshots: PrintScreen (or `renderer.screenshotAtFrame`) writes the window's own pixels to a PNG, read back from the GPU (#199).
- RHI: up to four colour targets in one pass, and reading back a swapchain image, on Vulkan and D3D12 (#199).
- Meow2Renderer draws an infinite grid on the ground (y = 0), worked out in a compute pass: the X axis red, the Z axis blue, cells in powers of ten that follow the camera's height, hidden behind geometry and faded far away. GRID in the Settings window turns it off (#197).
- Meow2Renderer: freezing culling also draws the frozen camera's view volume in yellow lines through the scene, so flying out shows what it sees; SHOW FRUSTUM hides it (#197).
- RHI: line-list pipelines (`PrimitiveTopology::Lines`) on Vulkan and D3D12, for debug lines (#197).
- Golden-image tests: the 25 suggested Khronos samples are drawn as Meow2Renderer first shows them, on Vulkan and Direct3D 12, and compared with approved images, so a change that alters how a scene looks fails a test. A failure saves what was drawn and a difference image; `MEOW2_UPDATE_GOLDENS=1` writes new candidates for review (#195).
- PNG writing (stb_image_write), for golden images and later screenshots (#195).
- Meshlets: on GPUs with mesh shaders, scenes are drawn in small clusters of up to 124 triangles, and clusters outside the view or facing away are skipped before their vertices are shaded; other GPUs draw as before, and both give the same image. Meow2Renderer's Settings window turns it on and off (MESHLETS), its Stats window shows how many meshlets and triangles were drawn, and its Debug window has a Meshlets view (#193).
- glTF meshes are reordered for the GPU when a model loads (meshoptimizer): repeated vertices merged, triangles ordered for the vertex cache and to reduce overdraw (#193).
- RHI: mesh pipelines (task and mesh shaders) with direct and indirect dispatches, on Vulkan (`VK_EXT_mesh_shader`) and D3D12; optional, reported per GPU (#193).
- Meow2Renderer can show what each step of the frame does: a Render graph window with the frame's passes as a flow (click one to watch the textures it writes, live, in a small window), F9 to capture a whole frame in memory and browse every texture as each pass left it, and a Debug window with 19 views (triangles, overdraw heat map, depth, normals, UVs, mip levels, each material channel, lighting only, …), wireframe on top or alone, and frozen culling (#191).
- Meow2Renderer: a Console window with every log line (filters by level and text, copy, follow), a Settings window for the debug tools and start-up options that saves to the config at once, and window positions kept between runs (#189).
- Meow2Renderer: orbit and fly (noclip) cameras, the glTF file's own cameras, and its animations played on the scene, so animated cameras and objects move; `renderer.camera`, `renderer.sceneCamera` and `renderer.animation` pick them at start (#189).
- glTF textures are compressed to BC7 (colour, data) and BC5 (normal maps) with DirectXTex: a quarter of the GPU memory. Cooked textures are saved as `.m2texture` files in a cache, so only a model's first load compresses; `renderer.compressTextures` turns it off. Texture transforms (`KHR_texture_transform`: offset, rotation, scale) are drawn (#185).
- A physically based sky (Sébastien Hillaire's method): blue by day, red and orange at sunset, the sun's disc, the ground below the horizon. The scene is lit by it: smooth metals reflect the sky, rough ones a blur of it, and everything gets soft light from all around; sunlight reddens as the sun sets. Meow2Renderer's Stats window has sliders for the sun (#181).
- A new look for Meow2Renderer and Meow2Editor: pure black with one red accent, square panels with red corner brackets, uppercase headings, Barlow Semi Condensed for the UI and Consolas for numbers and logs. Each app has a red cat icon; the editor's carries an E badge, the renderer's an R. Icons for asset types are ready for the editor's asset browser (#179).
- glTF materials drawn with metallic-roughness PBR on Vulkan and D3D12: normal maps (tangents from the file or generated), metallic, roughness, occlusion, emissive and vertex colours, lit by a sun and a sky/ground ambient with a neutral tone curve. Textures get mipmaps and the file's sampler settings; double-sided materials, mirrored nodes and alpha-blended materials (sorted back to front) draw correctly (#177).
- Meow2Renderer has a Dear ImGui shell: a Scenes window to open any Khronos sample (fetching it on the spot) or any `.gltf`/`.glb`, and a Stats window with frame times, the GPU, memory per tag and the open scene's size, plus buttons to switch backend and capture a frame. F1 hides it (#173).
- RHI: scissor rectangles, alpha blending, and a barrier between passes that draw to the same swapchain (#173).
- glTF 2.0 import: `.gltf` and `.glb` files open in Meow2Renderer (`--set renderer.scene=Duck`, or a path) with their node tree, meshes and base colour textures, converted to the engine's left-handed space, on Vulkan and D3D12. `cmake -P cmake/FetchGltfSample.cmake <Model>` downloads Khronos sample models on demand (#170).

### Changed
- Meow2Renderer's windows are translucent: the scene shows through them, and text stays readable (#199).
- Meow2Renderer's Render graph window is a node graph: each pass a node with its inputs and outputs as labelled pins, wires between pins coloured by kind (colour texture, depth, buffer, indirect arguments), ordered so wires cross less. Labels no longer overlap, also with the wireframe overlay on. Clicking a texture's pin shows it in the Pass window; clicking a buffer's lists every pass that writes and reads it. Right-drag pans. The Frame capture window uses the same graph (#197).
- Meow2Renderer's Debug window shows every view as a button, grouped, with the active one highlighted, instead of a drop-down (#197).
- The scene is drawn in high dynamic range and tone mapped in its own pass, before the UI; opaque surfaces, the sky and transparent ones are separate passes (#191).
- Window sizes follow display scaling in every app (the size at 100 %), and a new window takes 80 % of the screen instead of 1920 × 1080; a window too big for its screen is shrunk to fit (#191).
- Meow2Renderer: Fetch only downloads a sample; .GLTF and .GLB buttons beside each downloaded one open that version. Every window but the console is a tab of one panel on the left (#191).
- Vulkan GPUs need wireframe fill (`fillModeNonSolid`), which every desktop GPU has (#191).
- Roadmap: M7 is now the Editor (with scene files, moved out of M6), M8 Physics, M9 Scripting (new), M12 Advanced rendering and M13 Platforms; meshlets move into M6 (#189).
- Meow2Renderer's UI text is smaller, and its figures in Consolas no longer look larger than the labels (#181).
- Meow2Renderer's camera starts in front of a glTF model (on +Z, which glTF models face) instead of behind it, and the default sun lights the front (#177).
- Roadmap: M6 is now "glTF scenes and Meow2Renderer", with the old M7 merged in and stopping at PBR; a new milestone (now M12) holds advanced rendering (#170).
- Vulkan GPUs need BC texture compression (`textureCompressionBC`), which every desktop GPU has (#185).
- Vulkan GPUs need `shaderDemoteToHelperInvocation`, for `discard` in shaders (#170).
- Every app opens at 1920 x 1080 (1080p) by default instead of 1280 x 720; a size saved in the app's config still wins (#176).

### Fixed
- `ctest -L` found nothing for a test suite's second label (`visual`, `windows`): only the first label was kept (#195).
- Rotated texture transforms (`KHR_texture_transform`) turned the texture the wrong way; Khronos' TextureTransformTest now passes (#195).
- Meow2Renderer framed models only a few millimetres across from too far away (MetalRoughSpheresNoTextures) (#195).
- The console window showed some characters wrongly on Windows (`→` as `ΓåÆ`); it now uses UTF-8 (#191).
- Meow2Renderer: the fly camera's keys stopped working after clicking any window (#191).
- Meow2Editor opened bigger than the screen at 125 % or 150 % display scaling (#191).
- Direct3D 12: two pipelines that differed only in fill mode, depth test or blending could load each other from the pipeline cache (#191).
- Builds no longer fail when a server hiccups while configure downloads DXC or the Agility SDK: downloads are retried, and can be kept in a cache folder (`MEOW2_DOWNLOAD_CACHE`), which the Windows CI runner uses (#184).
- Specular highlights and the order of transparent objects in Meow2Renderer used the scene origin instead of the camera's position (#181).
- Meow2Renderer: the mouse lines up with the ImGui buttons on a scaled display (125 %, 150 %), and the UI is drawn at the display's scale (#176).

## [0.6.0] — 2026-11-07

### Added
- Switch between Vulkan and Direct3D 12 while the app runs, on the same window: F10 in Meow2Renderer. Everything on the GPU is made again from what the engine kept on the CPU, and a switch that fails goes back to the backend that was running (#168).
- Direct3D 12 backend: everything Vulkan does, from the bindless heap to GPU-driven drawing, passing the same tests. It ships Microsoft's DirectX 12 Agility SDK runtime in a `D3D12` folder, so it works on Windows 10 as well as 11. Meow2Renderer picks the backend with `--set rhi.backend=d3d12` (#166).
- A lost GPU device (a crash or driver reset) is logged with what the driver knows: DRED on D3D12, `VK_EXT_device_fault` on Vulkan where the driver has it (#166).
- Render graph: automatic barriers and passes recorded in parallel on jobs. GPU-driven drawing: compute frustum culling, indirect draws with a count buffer and vertex pulling. Meow2Renderer draws about 100,000 cubes this way (#164).
- `rhi.renderDoc`: Meow2Renderer loads RenderDoc itself, and F11 captures a frame and opens it in the RenderDoc window (#162).
- Frame infrastructure: `FrameArena`, `UploadRing`, `ReadbackQueue`, `OffscreenTarget`, a `FramePacket` with an `extract` phase in the game loop, command lists recorded on several jobs at once, pass markers, Tracy GPU zones, GPU memory stats per tag, and frame capture from inside the app when launched from RenderDoc (F11 in Meow2Renderer) (#160).
- RHI: buffers, textures, samplers, a bindless heap, explicit barriers, graphics and compute pipelines with a cache on disk, and `PipelineLibrary`, which rebuilds pipelines when a shader is reloaded. Meow2Renderer draws a triangle whose vertices come through the heap (#158).
- RHI: `IRhiDevice`, `IRhiSwapchain` and `IRhiCommandList`, typed handles, and a Vulkan 1.3 backend that picks a GPU, reports what an unsuitable one is missing, and drives several swapchains. Meow2Renderer now clears its window through it (#156).

### Changed
- Roadmap: M5 (the RHI) is done (version 0.6.0) (#168).
- `PipelineLibrary` and `GpuScene` take a `RenderDevice` instead of an `IRhiDevice` (#168).
- Docs: `techstack.md` keeps decisions and rules, with implementation detail in the wiki; M5 is planned as a feature table; agents group related features into larger PRs (#154).

## [0.5.0] — 2026-11-06

### Added
- Shaders: hot reload recompiles a shader on a job when its source, `.meta` or includes change while an app runs; a broken edit keeps the old shader and reports file and line (#150).
- Shaders: the bindless contract every shader follows, with one binding layout for the whole engine and structs compiled by both C++ and HLSL so they can't drift apart (#148).
- Shaders: a normal build cooks everything in `shaders/` with the new `Meow2ShaderCompiler` tool, and a shader that doesn't compile fails the build with file and line (#146).
- Shaders: cooking turns a source and its `.meta` into a `.m2shader` holding every variant, skipping shaders whose source, includes or settings haven't changed; games load these with no compiler (#144).
- Shaders: a shader's entry points, macros and permutation options are declared in its `.meta` file; code picks a variant by name, and a misspelt option is an error (#142).
- Assets: an asset manager with stable IDs in `.meta` files next to each source, so moving a file breaks nothing, one shared loaded copy of each asset, and reloading that reaches every user (#140).
- Shaders: `ShaderCompiler` turns HLSL into DXIL and SPIR-V through DXC, with macros, includes read through the file system, and errors that name file and line. DXC is loaded at runtime, so apps without it still start (#138).
- Shaders: the build downloads a pinned DXC (v1.9.2609) and checks its hash, so every machine compiles shaders with the same compiler (#136).

### Changed
- Roadmap: M4 (shaders and assets) is done (version 0.5.0) (#152).
- Roadmap: M4 is now "Shaders and assets", a feature table with an asset core, shader permutations and a bindless, GPU-driven direction for the renderer; editor picking moves to the GPU (#134).

## [0.4.0] — 2026-11-06

### Added
- Scene: parented transforms. Entities get a local transform relative to their parent and a world transform, updated once per step on all cores (by depth level), recomputing only what moved; the previous step is kept so rendering can interpolate smoothly between fixed steps (#130).
- Math: vertex packing for smaller, faster meshes: half floats, 8/16-bit normalised integers that decode exactly like the GPU, normals in 4 bytes, the whole tangent frame in 8, and positions in 6, each with a tested error bound (#128).
- Math: rays for picking and line of sight: a ray from a screen pixel through the camera, and hit tests against planes, spheres, boxes, rotated boxes and triangles, plus picking the closest of many boxes (the editor will use it to select objects with the mouse) (#126).
- Math: bounding volumes (boxes, spheres, rotated boxes) with containment and overlap tests, the camera frustum (also for the engine's far-plane-free projection), and frustum culling, including a fast batch cull of thousands of spheres (#124).
- Math: camera maths: a lens (perspective or orthographic), view and projection matrices from a transform, world ↔ screen conversion, and camera-relative rendering, which keeps scenes far from the origin as precise as scenes near it (#122).
- Math: reversed-Z projection matrices (perspective with no far plane, perspective with one, orthographic), depth back to distance, and the viewport flip that lets Vulkan and D3D12 share the same matrices (#120).
- Math: easing curves (quad, cubic, sine, expo, back, elastic, bounce, … in, out and in-out) for tweens and UI, and splines (Catmull-Rom, Bézier, Hermite, and smooth rotation through keys); `SplinePath` follows a path through waypoints at constant speed, for camera rails and moving platforms (#118).
- Math: `Transform` (position, rotation, scale) with conversion to and from matrices, moving points between local and parent space, combining for hierarchies, inverting and interpolating for smooth rendering between fixed steps; world positions have their own `WorldPosition` type so larger worlds can change it in one place (#116).
- Math: rotation helpers: rotations from an axis and angle, from Euler angles (a documented order, round-trips, gimbal lock handled), towards a look direction or between two directions; combining with an explicit order, inverting, slerp along the shortest path, and comparing rotations (#114).
- Math: colours: linear float `Color` for maths and lighting, 8-bit sRGB `Color8` for pickers, UI and images, with exact conversion between them (no more muddy blends), hex colours, HSV, premultiplied alpha and named colours (#112).
- Math: a seedable random number generator (PCG32) whose numbers are identical on every platform and compiler, for replays and procedural content: unbiased integer ranges, floats, chances, random directions and points in a sphere, with a separate stream per thread or system (#110).
- Math: scalar helpers for angles (degrees ↔ radians, wrapping to −π..π), interpolation (`lerp` exact at both ends, its inverse, `remap` between ranges), `clamp`/`saturate` and `smoothstep` (#108).
- SIMD: everything is now built for AVX2 (with FMA3 and F16C) on x64, so the maths runs on DirectXMath's fastest paths (`MEOW2_SIMD=SSE4.2` builds for older CPUs). On a CPU without AVX2 the apps show a clear message at startup instead of crashing. Batch helpers transform thousands of points or directions at once and compute bounding boxes (#106).
- Math: DirectXMath (`may2026`, new dependency) and the engine's math types: `Vec2/3/4`, `Quat` and `Mat4`/`Mat3x4` for storing values (zero or identity by default; a rotation is its own type), `load`/`store` to SIMD registers, the left-handed axis constants, `nearlyEqual` with an absolute and relative tolerance, and readable `std::format` output for logs (#104).

### Changed
- Roadmap: M3 (Math) is done (version 0.4.0) (#132).
- Build: GCC and Clang no longer fuse multiplies and adds on their own (`-ffp-contract=off`), so floating-point results match MSVC bit for bit (#110).

## [0.3.0] — 2026-11-05

### Added
- File system: engine code reads and writes files by virtual path (`assets/textures/cat.png`), with each part (`assets`, `save`, …) mounted to a folder by the app; packages and Android assets will plug in the same way. Paths can't escape their mount, writes are atomic (a crash never leaves a half-written file), non-ASCII names work, and any thread can use it (#100).
- Scene: jobs can now create and destroy entities and add or remove components through per-thread command buffers, applied on the main thread at one point in the frame. New transient entities (no saved ID, for bullets and effects), and the ID lookup stays correct however an entity is destroyed (#98).
- Scene: entities and components on EnTT v3.16.0 (new dependency). Every entity gets a saved `EntityId` next to its runtime handle; scenes can be loaded with their saved IDs, component types are registered up front, and all scene memory shows up under the new `Scene` memory tag (#96).
- IDs: 128-bit `Uuid`s for things that are saved (entities, assets, scenes, prefabs). They sort by creation time, carry what kind of thing they name (`EntityId`, `AssetId`, … can't be mixed up), read and write as standard UUID text, and come from a lock-free `UuidGenerator` that needs no coordination between machines (#94).
- Input sources: Player and Renderer now feed the `InputSystem` from SDL: keyboard (physical keys), mouse (motion, buttons, wheel with natural scrolling handled), typed text, and gamepads (plugged in or out at any time; sticks -1..1, triggers 0..1). Gamepad support is optional; without it the apps run as before (#92).
- Input: `InputSystem` keeps keyboard (by physical key), mouse and gamepad state. Gameplay polls it (`isDown`, `wasPressed`, `mouseDelta`, `axis`, …), with presses latched for the fixed-rate `fixedUpdate` so a tap is never lost or doubled; changes go on the `EventBus` (`KeyPressed`, `MouseMoved`, `GamepadConnected`, …). Player and Renderer run it each frame; the SDL sources that feed it come next (#90).
- Game loop: every app now runs its frames through `GameLoop`: events, then `fixedUpdate` at a constant rate (60 Hz by default, `loop.fixedStepHz`), `update` with the frame time and `render` with an interpolation factor. Long frames run at most `loop.maxFixedSteps` simulation steps, so a stall can't snowball. The editor ticks it from its Qt timer (#88).
- Window events: the windowing layer puts `WindowResized`, `WindowMoved`, `WindowStateChanged`, `WindowFocusChanged`, `WindowDisplayScaleChanged`, `WindowCloseRequested` and `QuitRequested` on the `EventBus` (one per kind of change per frame, with the final values). Player and Renderer stop on the close and quit events. `Platform::create` now takes the `EventBus`; `Window::id()` and `displayScale()` are new (#83).

### Changed
- Roadmap: M2 is done (version 0.3.0); M3 (Math) is now a feature table: DirectXMath types, SIMD batches, deterministic random numbers, colours, quaternions, transforms, easing curves and splines, reversed-Z projections, camera, bounding volumes and culling, rays and picking, vertex packing, and the transform hierarchy in the scene (#102).
- Roadmap: M2 is now "Platform & scene foundation", with a feature table that adds 128-bit IDs (time-sorted, with a kind tag such as entity or asset) and an entity/component store on EnTT; techstack.md §24 and §25 describe both, including their threading rules (#81).

## [0.2.1] — 2026-11-02

### Fixed
- Releases: the release workflow stopped after building the zip ("in was unexpected at this time"): a parenthesis in an error message ended a cmd block early. `v0.2.0` therefore has no GitHub Release; `0.2.1` is the first one, with the same engine as 0.2.0 (#85, #86).

## [0.2.0] — 2026-11-02

### Added
- Releases: each version tag builds `Meow2-<version>-windows-x64.zip` (the apps, their PDBs, the MSVC runtime and Qt; runs after unpacking anywhere), checks it by running every app from the unpacked zip, and publishes it as a GitHub Release with that version's CHANGELOG section as notes. Release builds now produce PDBs (#77).
- Sanitizers: two CI jobs build with Clang's AddressSanitizer + UndefinedBehaviorSanitizer and ThreadSanitizer and run every engine and app test under them (presets `linux-clang-asan`, `linux-clang-tsan`) (#75).
- Profiling: Debug and RelWithDebInfo builds include the Tracy client (v0.14.1): connect the Tracy viewer to a running app for a timeline of all threads (`Main`, `Job thread N`), engine zones, frames and memory per tag. It listens on localhost only, records nothing until a viewer connects, and is compiled out of Release (#73).
- Jobs: `JobSystem` (enkiTS v1.12) runs work on all cores: `parallelFor` over a range, single jobs, tasks for the main thread, and a stable per-thread index; waiting threads run jobs themselves. Each app creates one (`jobs.threadCount` in its config) (#71).
- The version string names the commit a build comes from, e.g. `0.1.0+g1a2b3c4` (`.dirty` for uncommitted changes), in every log and at app start; `getBuildMetadata()` returns just that part (#69).
- Events: `EventBus` delivers typed events immediately (`publish`) or queued from any thread (`enqueue` + `dispatchQueued`), with `Subscription`s that unsubscribe when destroyed; subscribing, unsubscribing and enqueueing from inside a handler are safe (#67).
- Doxygen API reference: `docs` CMake target, `Docs.PublicApi.IsDocumented` test that fails on undocumented public API, existing headers converted to Doxygen comments (#34).
- Asserts: `MEOW2_ASSERT`, `MEOW2_ASSERT_SLOW`, `MEOW2_VERIFY` with a replaceable failure handler (`setAssertHandler`), and `isDebuggerAttached()` (#36).
- This changelog (#36).
- README: step-by-step instructions for generating the API reference (#36).
- The API reference is published to GitHub Pages on every merge to `main`: https://buttception.github.io/Meow2/ (#38).
- `windows-vs2022` presets: generate the Visual Studio solution, build and test from any command prompt; `windows-vs2022-regenerate` refreshes the `.sln`/`.vcxproj` files after CMake changes without compiling (#38).
- Logging: `LogSystem` / `Logger` over spdlog v1.17.0 with five levels (`debug` compiled out of Release, `fatal` flushes and aborts), `[hh:mm:ss.ms]<level> Name: message` lines, a log file per run next to the exe (the repo's `logs/` when started from Visual Studio, `--log-dir` to override), console output in Debug/RelWithDebInfo, and failed asserts written to the log; `executablePath()` (#41).
- Git hooks (`.githooks/`, enabled by CMake): after `git pull`, a rebase or a branch switch that changes CMake files, the Visual Studio solution is regenerated and Visual Studio offers to reload (#41).
- Error handling: `Meow2::Result<T>` (tl::expected v1.3.1) with `Error` (code + message), `ErrorCode`, `fail()` and `std::format` support; misuse such as `*result` on an error fails a Meow2 assert (#45).
- `FrameClock`: measures each frame's time with a monotonic clock, clamps it to 0.25 s so a breakpoint or a stall doesn't turn into one huge step, and counts frames; the apps log "Ran N frames in S s" at exit (#63).
- Release builds of Meow2Player, Meow2Renderer and Meow2Editor no longer open a console window next to their own window (Windows GUI subsystem); Debug and RelWithDebInfo keep the console with the coloured log (#61).
- Editor: Meow2Editor is now a Qt application (Qt ≥ 6.8, tested with 6.12.0): a main window with a native viewport for the renderer, whose size, position, maximized state and fullscreen mode are saved in `Meow2Editor.toml` like the other apps'. Without Qt, configure skips the editor with a warning; README and the Building page explain how to install Qt's MSVC kit and point CMake at it. The editor doesn't use the global `new`/`delete` tracking (#59).
- Window: Meow2Renderer and Meow2Player open an SDL3 window (release-3.4.18) and run until it's closed. Its size, position, maximized state and fullscreen mode are saved in the app's config file and restored at the next start (`--set window.mode=fullscreen` for one run); a position on an unplugged monitor falls back to centred. SDL's memory is counted under `MemTag::Platform` (#57).
- Config: each app keeps its settings in `Meow2<App>.toml` next to its exe (`--config <path>` for another file; runs from Visual Studio use the repo's git-ignored `config/` folder) and accepts `--set key=value` overrides for one run. Changed settings are written back safely, and a file with a syntax error is reported and never overwritten (toml++ v3.4.0) (#55).
- Memory: in Debug and RelWithDebInfo every `new`/`delete` in the apps goes through a process-wide `TrackingAllocator` (counted per `MemTag`, debug fills, guard bytes), and each app logs "No memory leaks" or the leaks per tag when it exits (`ScopedLeakCheck`) (#53).
- Memory: `TrackingAllocator` counts every allocation per `MemTag` (current bytes, peak, live and total allocations), lists leaks per tag, and in Debug/RelWithDebInfo catches heap overruns with guard bytes and wrong frees with asserts; `MallocResource` (`malloc`/`free`, any alignment) sits underneath; `MemTagScope` sets the tag for a block of code; arena blocks are counted under their arena's tag (#51).
- Memory: `BlockArena`, a bump allocator for short-lived data that any `std::pmr` container can use (size-class blocks, `reset()` keeps the blocks so a warm arena makes no heap calls, `trim()`), and `MemTag` for attributing memory to subsystems. In Debug and RelWithDebInfo it fills new memory with `0xCD` and freed or reset memory with `0xDD`, so stale reads stand out (`setDebugFill(false)` for profiling) (#49).
- Crash dumps (Windows): every app writes a minidump next to its log when it crashes (access violation, stack overflow, `abort()`/`MEOW2_LOG_FATAL`/failed asserts, `std::terminate`, uncaught exceptions, pure calls, CRT invalid parameters), in all configurations, without error dialogs; skipped under a debugger (#47).
- Visual Studio filters mirror the engine folders (Subsystem\Module\Backend), and projects are grouped into solution folders (Engine, Apps, Tests, External, Tools) (#38).

### Changed
- M1 rescoped as "Core runtime" and M2 as "Platform"; design decisions for memory, logging, input, events, game loop and releases recorded in techstack.md (#32).
- CI: Linux runs Clang Debug and GCC Debug only (Clang RelWithDebInfo/Release dropped), with cached apt packages (#36).
- Clean-state check before a PR uses a temporary git worktree instead of a clone (AGENTS.md §2.4) (#36).

### Fixed
- The text-file lint crashed CMake (stack overflow in a regex) on files larger than a few tens of KB (#32).
- The API docs workflow failed on `main` because it didn't check out submodules (spdlog is needed to configure); its build now also runs on pull requests, so this is caught before merging (#43).
- CI didn't start at all from #59 on (`startup_failure`): the Linux Qt install used a third-party action, which the repository's Actions settings don't allow. Qt is now installed with aqtinstall in a plain, cached step (#65).

## [0.1.0] — 2026-10-27

M0: Foundation. A repository that builds an empty engine and apps in Debug, RelWithDebInfo and Release, runs tests from the command line, and checks every PR in CI.

### Added
- `.gitattributes` normalising line endings (#3).
- Planning docs: techstack.md, AGENTS.md, milestones.md, README, wiki home (#4).
- CMake skeleton: `Meow2Engine` static library, `Meow2Renderer`, `Meow2Editor`, `Meow2Player`, generated `Meow2/Core/Version.h`, smoke tests (#9).
- `CMakePresets.json`: `windows-msvc-*` and `linux-clang-*` for Debug, RelWithDebInfo and Release, with LTO in Release (#10, landed via #21).
- GoogleTest v1.17.0 submodule and `meow2_add_test_suite()` (#11, landed via #21).
- `.clang-format`, the `format` target and `Format.Check` test, warnings as errors via `meow2_set_warnings()` (#12, landed via #21).
- CI: GitHub Actions with hosted Linux jobs and a self-hosted Windows runner (#17).
- Architecture rules: dependency injection, composition root, no mutable globals, threading (#18).
- `Meow2/Core/Platform.h`: platform, compiler, architecture and build-configuration macros (#19).
- `.editorconfig` and the `Lint.EditorConfig.TextFilesFollowRules` test (#20).
- Claude Code setup: `CLAUDE.md`, one git worktree per session (#23).
- RelWithDebInfo CI jobs; clang-format pinned to version 19; plain data structs without `m_` (#26).
- Building from a standard command prompt with the Visual Studio generator (#28).
- Versioning rule: MINOR per milestone, tags, PATCH for release fixes (#30).

[Unreleased]: https://github.com/buttception/Meow2/compare/v0.1.0...HEAD
[0.1.0]: https://github.com/buttception/Meow2/releases/tag/v0.1.0
