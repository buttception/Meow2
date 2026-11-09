# Meow2 — Milestones

A map of where the engine is heading, not a schedule. Work happens **as the owner asks for it**, in groups of related features (1 group = 1 issue, 1 branch, 1 PR, AGENTS.md §2) at a time.
If a requested feature needs something from an earlier milestone that doesn't exist yet, the agent says so before starting.

Status: ⬜ not started · 🟨 in progress · ✅ done

Issues are labelled `milestone:M<n>` on GitHub.

---

## M0 — Foundation ✅
**Goal:** a repo that builds an empty engine, editor, renderer and player in all three configs and runs tests from the CLI.
**Depends on:** —

| Feature | Status | Issue / PR |
|---|---|---|
| `.gitattributes` (line endings) | ✅ | #1 / PR #3 |
| Planning docs (techstack, AGENTS, milestones, README, wiki home) | ✅ | #2 / PR #4 |
| CMake skeleton: `Meow2Engine` (static), `Meow2Renderer`, `Meow2Editor`, `Meow2Player`, tests | ✅ | #5 / PR #9 |
| `CMakePresets.json`: Debug / RelWithDebInfo / Release | ✅ | #6 / PR #10 (landed via PR #21) |
| Test framework + CTest integration | ✅ | #7 / PR #11 (landed via PR #21) |
| `.clang-format`, warnings-as-errors | ✅ | #8 / PR #12 (landed via PR #21) |
| CI: GitHub Actions (hosted Linux + self-hosted Windows runner) | ✅ | #13 / PR #17 |
| Architecture rules: dependency injection, no globals, threading | ✅ | #14 / PR #18 |
| Platform/config header (`Meow2/Core/Platform.h`) | ✅ | #15 / PR #19 |
| `.editorconfig` + text-file lint test | ✅ | #16 / PR #20 |
| Claude Code setup: `CLAUDE.md`, worktree workflow, Environments rewrite | ✅ | #22 / PR #23 |
| CI: RelWithDebInfo jobs (Windows MSVC + Linux Clang) | ✅ | #24 / PR #26 |
| Naming: plain data struct fields without `m_`; clang-format pinned to 19 | ✅ | #25 / PR #26 |
| README: build from a standard prompt (Visual Studio solution) | ✅ | #27 / PR #28 |
| Versioning rule (MINOR per milestone, tags, PATCH for release fixes) | ✅ | #29 / PR #30 |

**Released as `v0.1.0`.** Each milestone ends with a version bump in its closing PR (techstack.md §21).

**Done when:** `cmake --preset … && cmake --build … && ctest …` succeeds on Windows for all three configs, and CI runs it on every PR.

## M1 — Core runtime ✅
**Goal:** everything the engine needs before it opens a window: see what it's doing (logs, profiler), survive and diagnose crashes, manage memory with full attribution, run work on all cores, and ship a release zip.
**Depends on:** M0

Rough order: asserts → logging → crash dumps → config → clock → memory → events → jobs → profiler; sanitizers and packaging alongside.

| Feature | Notes (techstack.md) | Status | Issue / PR |
|---|---|---|---|
| Asserts: `MEOW2_ASSERT` (off in Release), `MEOW2_VERIFY` (always evaluates), debug break, failure handler | §3 | ✅ | #35 / PR #36 |
| `CHANGELOG.md` (Keep a Changelog), used as release notes | §21 | ✅ | #35 / PR #36 |
| API reference published to GitHub Pages (`docs.yml`) | §16.1 | ✅ | #37 / PR #38 |
| Visual Studio workflow: `windows-vs2022` presets (incl. regenerate-only), filters by Subsystem/Module/Backend, solution folders | §2 | ✅ | #37 / PR #38 |
| `Result<T>` = `tl::expected` (submodule; swap to `std::expected` with C++23) | §3, §4 | ✅ | #44 / PR #45 |
| Logging: spdlog, levels debug/info/warn/error/fatal, `[hh:mm:ss.ms]<level> Name: msg`, `Engine` + per-app loggers, file sink (log folder) + console sink (Debug/RWDI), `fatal` flushes and crashes | §7 | ✅ | #39 / PR #41 |
| Release exes use the Windows GUI subsystem (no console window) | §5, §7 | ✅ | #60 / PR #61 |
| Crash dumps: minidump on crash in **all configs**, log flush, skipped under a debugger, child-process crash test | §8 | ✅ (Windows) | #46 / PR #47 |
| Config: TOML via toml++, defaults → `config.toml` → command line, write-back | §3, §4 | ✅ | #54 / PR #55 |
| `FrameClock`: high-resolution time, delta time, frame counter | §23 | ✅ | #62 / PR #63 |
| Memory: `TrackingAllocator` (thread-safe, `MemTag`s, per-tag stats, leak report), `MallocResource` | §6 | ✅ | #50 / PR #51 |
| Memory: `BlockArena` (size-class block lists, reset keeps blocks, trim) as a `std::pmr::memory_resource`, `MemTag` | §6 | ✅ | #48 / PR #49 |
| Memory: global `operator new`/`delete` override (Debug/RWDI) + allocator hooks for libraries that have them, thread-local tag scopes | §6, §18 | ✅ (hooks land with each library) | #52 / PR #53 |
| Events: typed `EventBus` (immediate + queued, RAII subscriptions) | §22 | ✅ | #66 / PR #67 |
| Job system: enkiTS behind `JobSystem` (pinned main-thread tasks, custom allocator, per-thread index) | §19 | ✅ | #70 / PR #71 |
| Profiler: Tracy client (Debug/RWDI, localhost, on demand), zones in engine code, memory events from `TrackingAllocator`, enkiTS thread names | §3, §4 | ✅ (memory as per-tag plots) | #72 / PR #73 |
| Sanitizer presets + CI jobs: `linux-clang-asan` (ASan + UBSan) from the start, `linux-clang-tsan` with the job system | §19 | ✅ | #74 / PR #75 |
| Build metadata: git commit in the version string (`0.1.0+g1a2b3c4`) | §21 | ✅ | #68 / PR #69 |
| API docs: Doxyfile + `docs` CMake target (skipped if Doxygen isn't installed), existing headers converted to Doxygen comments, CI job that fails on undocumented public API | §16.1 | ✅ | #33 / PR #34 |
| Release packaging: CMake install rules + CPack ZIP (exes, PDBs, MSVC runtime DLLs), `release.yml` builds it on a `v*` tag and publishes a GitHub Release | §21 | ✅ | #76 / PR #77 |
| Git hooks: regenerate the Visual Studio solution after pull/rebase/branch switch when CMake files change | §2 | ✅ | #40 / PR #41 |
| Closing PR bumps the version to **0.2.0** (tag `v0.2.0`, first release zip) | §21 | ✅ | #78 / PR #79 |

**Done when:** an app logs to file and console, writes a `.dmp` on a forced crash in every config, reports leaks by tag at exit, runs jobs on all cores visible in Tracy, and the ASan/UBSan/TSan CI jobs are green.

**Released as `v0.2.1`.** `v0.2.0` was tagged on the merge of PR #79 (with the owner's go-ahead), but its release run stopped on a workflow bug after building the zip (#84), so the first GitHub Release is the patch `v0.2.1` from `release/0.2` (same engine). Crash dumps are Windows-only for now (Linux with M12). "Jobs visible in Tracy" needs the Tracy viewer, which the owner checks once by connecting it to a Debug build.

## M2 — Platform & scene foundation ✅
**Goal:** a window on screen, a real game loop and input, shared by Renderer and Player (the editor feeds the same input system from Qt in M9), plus the IDs and the entity/component store everything after it builds on.
**Depends on:** M1

Rough order: window events → game loop → input → IDs → scene/ECS → file system.

| Feature | Notes (techstack.md) | Status | Issue / PR |
|---|---|---|---|
| SDL3 window: one window, API-neutral, native handle; size/position/mode in the config | §9 | ✅ | #56 / PR #57 |
| Window events (resize, move, state, focus, DPI, close, quit) on the `EventBus`; polled getters stay for current values | §9, §22 | ✅ | #82 / PR #83 |
| Game loop: `tick()` with fixed timestep (`fixedUpdate` 0..n times), `update`, `render(alpha)`, clamped frame time; the editor ticks from a Qt timer | §23 | ✅ | #87 / PR #88 |
| `InputSystem`: engine `Key` (HID scan codes), mouse, gamepad; polling (`isDown`, `wasPressed`, …) + input events on the `EventBus`; presses latched for `fixedUpdate` | §9 | ✅ | #89 / PR #90 |
| `SdlInputSource` (keyboard/mouse/text) and `SdlGamepadSource` (Player and Renderer; the editor gets `QtInputSource` in M9) | §9 | ✅ | #91 / PR #92 |
| IDs: 128-bit `Uuid` (snowflake-style UUIDv8: time, sequence, kind, random node), `EntityId`/`AssetId` wrappers, lock-free `UuidGenerator`, text form | §24 | ✅ | #93 / PR #94 |
| Scene/ECS: EnTT in `Scene`, `EntityId` ↔ `entt::entity`, pools created at startup, `MemTag::Scene` (part 1); command buffers for jobs, transient entities (part 2) | §25 | ✅ | #95 / PR #96, #97 / PR #98 |
| Virtual file system (`FileSystem`): virtual paths, folder mounts (read-only / read-write), atomic writes, thread-safe | §3 | ✅ | #99 / PR #100 |
| Closing PR bumps the version to **0.3.0** | §21 | ✅ | #101 / PR #102 |

**Done when:** Renderer and Player run a fixed-timestep loop driven by SDL input and window events from the `EventBus`, and a `Scene` creates, finds by `EntityId` and destroys entities, with structural changes from jobs applied through command buffers (TSan job green). Transforms wait for M3's math.

**Released as `v0.3.0`** (tag on the merge of PR #102, with the owner's go-ahead).

## M3 — Math ✅
**Goal:** one coordinate convention, used consistently: math types and helpers, transforms and the transform hierarchy in the scene, a camera with reversed-Z projections, the bounding volumes and rays that culling (M5/M7) and editor picking (M9) need, and the general-purpose tools around them: SIMD batches, deterministic random numbers, colours, easing and splines, vertex packing.
**Depends on:** M0 (math), M2 (the transform hierarchy lives in the `Scene`)

Rough order: DirectXMath + types → SIMD baseline → scalar helpers, random numbers, colours → quaternions → transforms → easing and splines → projections and camera → bounds and culling → rays → packing → transform hierarchy.

| Feature | Notes (techstack.md) | Status | Issue / PR |
|---|---|---|---|
| DirectXMath submodule (`may2026`, owner-approved) + `sal.h` shim for Linux; `Meow2/Math` types: `Vec2/3/4`, `Quat`, `Mat4` for storage vs `XMVECTOR`/`XMMATRIX` for computation, load/store, constants, `nearlyEqual`, `std::formatter` and GoogleTest printers | §10 | ✅ | #103 / PR #104 |
| SIMD: **AVX2 baseline on x64** (owner decision 2026-11-05; with FMA3 and F16C: `/arch:AVX2`, `-mavx2 -mfma -mf16c`, DirectXMath's `_XM_AVX2_INTRINSICS_`), a CMake option (`MEOW2_SIMD=AVX2|SSE4.2`) so it can be lowered; a CPU check at startup that shows a clear message on a CPU without AVX2 instead of crashing on an illegal instruction; NEON on arm64; alignment rules; batch helpers over arrays (transform N points, N spheres vs a frustum) for the systems that process thousands of items | §10, §19 | ✅ | #105 / PR #106 |
| Scalar helpers: radians ↔ degrees, `lerp`, `clamp`, `saturate`, `wrapAngle`, `smoothstep` | §10 | ✅ | #107 / PR #108 |
| Random numbers: a small, fast, seedable generator (PCG32 or xoshiro), one per system or thread, never global; our own ranges and distributions (floats, ints, unit vectors, points in a sphere), because `std::` distributions give different results on MSVC and libstdc++; the same sequence on every platform for replays | §10, §18 | ✅ | #109 / PR #110 |
| Colours: linear `Color` (float RGBA) and `Color8` (8-bit sRGB), exact sRGB ↔ linear conversion, HSV, premultiplied alpha, named constants | §10 | ✅ | #111 / PR #112 |
| Quaternions: axis-angle, Euler angles with a documented order (yaw Y, pitch X, roll Z), look rotation, `slerp`, normalise, angle between | §10 | ✅ | #113 / PR #114 |
| `Transform` (TRS: position, rotation, scale): to/from matrix, compose, inverse, transform a point/direction, interpolate (for `render(alpha)`) | §10, §23 | ✅ | #115 / PR #116 |
| Easing and splines: the standard easing curves (quad, cubic, sine, expo, back, elastic, bounce; in/out/in-out); Catmull-Rom, cubic Bézier and Hermite splines for points and rotations; arc-length parameterisation (constant speed along a path) | §10 | ✅ | #117 / PR #118 |
| Projections: reversed infinite-Z perspective and reversed-Z orthographic (left-handed, depth 0..1); the Vulkan Y-flip convention checked by tests | §10, §11 | ✅ | #119 / PR #120 |
| Camera: view matrix from a transform, field of view/aspect/near, screen ↔ world (project/unproject), its frustum; camera-relative positions for rendering (§10 large worlds) | §10 | ✅ | #121 / PR #122 |
| Bounding volumes and culling: AABB, sphere, oriented box, frustum (DirectXCollision); contains/intersects; frustum vs AABB/sphere | §10, §13 | ✅ | #123 / PR #124 |
| Rays and picking: a ray from a screen point; ray vs plane, sphere, AABB, triangle (editor picking, M9) | §10 | ✅ | #125 / PR #126 |
| Vertex packing and quantisation: half floats, unorm/snorm 8/16, octahedral normals (2 × 16 bit), tangent frames as a packed quaternion, positions quantised to 16 bit inside a mesh's bounds; round-trip error bounds tested | §10, §11 | ✅ | #127 / PR #128 |
| Transform hierarchy in the `Scene`: `LocalTransform`, `WorldTransform`, `Parent`; world transforms updated by depth level in parallel jobs with the SIMD batch helpers; dirty flags; the previous transform kept for `render(alpha)` | §23, §25 | ✅ | #129 / PR #130 |
| Closing PR bumps the version to **0.4.0** | §21 | ✅ | #131 / PR #132 |

**Done when:** the projection matrices match hand-checked values in tests, a parented hierarchy of 100k entities updates its world transforms in parallel (TSan job green), a ray from a screen point picks the right AABB, the same seed gives the same random sequence on Windows and Linux CI, and packed vertices round-trip within their documented error.

**Released as `v0.4.0`** (tag on the merge of PR #132, with the owner's go-ahead). Stacked PRs #108 → #130 implemented the rows; each one says what it covers.

**Not in M3:** noise (Perlin/simplex): most noise is generated on the GPU in shaders; a CPU version comes with the first CPU user (terrain heights for physics, camera shake, procedural placement). Large-world precision: see techstack §10; only the cheap parts (camera-relative rendering, one `WorldPosition` type) are planned now.

## M4 — Shaders and assets ✅
**Goal:** one HLSL source compiled to DXIL and SPIR-V, managed as assets (source → cooked object) with permutations and macros set in one place, built by CMake, reloadable while an app runs, and ready for a bindless, GPU-driven renderer.
**Depends on:** M2 (file system, IDs, events), M1 (jobs, config)

Rough order: DXC → compiler → asset core → shader assets and permutations → cooker → build step → bindless contract → hot reload.

| Feature | Notes (techstack.md) | Status | Issue / PR |
|---|---|---|---|
| DXC dependency: pinned release v1.9.2609, downloaded at configure time with a SHA-256 check, same version on Linux and Windows and in CI; development only for now (not in the release zip, see techstack §4) | §4, §12 | ✅ | #135 / PR #136 |
| `ShaderCompiler`: HLSL → DXIL and SPIR-V (SM 6.6, Vulkan 1.3), debug and optimised, defines, includes through the VFS, errors with file and line as `Result` | §12 | ✅ | #137 / PR #138 |
| Asset core: `AssetManager`, `.meta` sidecars with stable `AssetId`s, typed handles, loading through the VFS, thread-safe, `MemTag::Assets` | §13a, §24 | ✅ | #139 / PR #140 |
| Shader assets and permutations: entry points, option axes and defines in the `.meta`, `ShaderVariantKey`, unknown options rejected, a warning on too many variants | §12 | ✅ | #141 / PR #142 |
| Cooker and `.m2shader`: bytecode per variant, entry point and target, versioned file, cache key from source, includes, `.meta`, flags and compiler version; `ShaderAsset` loads it with no compiler | §12 | ✅ | #143 / PR #144 |
| Build step: `Meow2ShaderCompiler` tool, `meow2_add_shaders()` in CMake, `shaders/` folder; only changed shaders recook | §12 | ✅ | #145 / PR #146 |
| Bindless contract: `Common.hlsli`, heap access through `ResourceDescriptorHeap`, push-constant layout, structs shared between C++ and HLSL with size checks | §11, §12 | ✅ | #147 / PR #148 |
| Hot reload: watch sources and `.meta` files, recompile on a job, `ShaderReloaded` on the `EventBus`; a failed compile keeps the old shader | §12, §22 | ✅ | #149 / PR #150 |
| Closing PR bumps the version to **0.5.0** | §21 | ✅ | #151 / PR #152 |

**Done when:** one shader with permutations cooks to DXIL and SPIR-V on Windows and to SPIR-V on Linux CI; the SPIR-V passes `spirv-val`; the same bindless HLSL compiles for both targets; editing an included file recooks only what depends on it, and a second cook does nothing; Player loads a `.m2shader` with no DXC present; a broken edit during hot reload logs file and line and keeps the old shader.

**Released as `v0.5.0`** (tag on the merge of PR #152, with the owner's go-ahead). DXC isn't in the release zip yet: `dxil.dll`'s licence refers to a distributables list the release doesn't contain.

**Not in M4:** GPU pipelines or running a shader (M5); glTF and textures as assets (M7, on the same asset core); the material system; a custom shader language; SPIRV-Reflect (bindless has one fixed binding layout, so reflection isn't needed; add it if a user appears).

## M5 — RHI ✅
**Goal:** draw through one interface on Vulkan and D3D12, bindless and GPU-driven from the start, and switch between them while running.
**Depends on:** M2, M3, M4

Planned as six PRs, each a group of related rows (AGENTS.md §2). New submodules and tools need the owner's approval first.

| PR | Feature | Notes (techstack.md) | Status | Issue / PR |
|---|---|---|---|---|
| 1 Vulkan bring-up | Submodules: Vulkan-Headers, volk, VulkanMemoryAllocator | §4 | ✅ | #155 / PR #156 |
| 1 | `IRhiDevice`, `IRhiCommandList`, `IRhiSwapchain`, `createRhiDevice(backend, desc)`, typed handles (index + generation) | §11 | ✅ | #155 / PR #156 |
| 1 | Device and queues, validation messages to the `RHI` logger, object names | §11 | ✅ | #155 / PR #156 |
| 1 | Capability check at startup: required features, a clear message on an unsupported GPU, a written minimum spec | §11 | ✅ | #155 / PR #156 |
| 1 | Swapchains: more than one per device (ImGui viewports, the Qt viewport), resize, minimise; clear screen in Renderer | §9, §11 | ✅ | #155 / PR #156 |
| 2 Resources and first triangle | Buffers, textures, samplers through VMA under `MemTag::Rhi` | §6 | ✅ | #157 / PR #158 |
| 2 | Deferred destruction: resources and bindless indices are freed only after the GPU has finished with them | §11 | ✅ | #157 / PR #158 |
| 2 | Barriers (synchronization2), dynamic rendering | §11 | ✅ | #157 / PR #158 |
| 2 | Bindless heap: one layout, index allocator, push constants, on the M4 `Common.hlsli` contract | §11, §12 | ✅ | #157 / PR #158 |
| 2 | Graphics and compute pipelines from `.m2shader`, pipeline cache on disk, up-front creation | §11 | ✅ | #157 / PR #158 |
| 2 | Pipelines rebuilt on `ShaderReloaded` | §12, §22 | ✅ | #157 / PR #158 |
| 2 | Triangle through bindless: the first time an M4 shader really runs | §12 | ✅ | #157 / PR #158 |
| 3 Frame infrastructure | `FrameArena` (reset by fence), upload ring, CPU readback | §6 | ✅ | #159 / PR #160 |
| 3 | Frame packet / extract step in the game loop | §23 | ✅ | #159 / PR #160 |
| 3 | Headless rendering: draw to a texture with no window and read it back (M7 golden images, CI) | §13 | ✅ | #159 / PR #160 |
| 3 | Debug tools: pass markers, RenderDoc in-app capture; Tracy GPU zones; GPU memory stats per tag | §3, §11 | ✅ | #159 / PR #160 |
| 3 | Command lists per job thread, submitted in a given order; lookups that don't block each other | §11, §19 | ✅ | #159 / PR #160 |
| 3 | RenderDoc loaded by the app; F11 opens the capture in its window | §11 | ✅ | #161 / PR #162 |
| 4 GPU-driven path | Render graph, small at first: passes and automatic barriers, compiled on the main thread and recorded in parallel, one job per pass or group of small passes (owner decision 2026-11-07); transient memory aliasing later | §3, §11, §19 | ✅ | #163 / PR #164 |
| 4 | Vertex pulling with the `Packing.h` formats | §10 | ✅ | #163 / PR #164 |
| 4 | Indirect draws with a count buffer, compute frustum culling | §11 | ✅ | #163 / PR #164 |
| 4 | Many-instance test scene, reversed-Z depth check | §10 | ✅ | #163 / PR #164 |
| 5 D3D12 parity | Submodules: D3D12MemoryAllocator, DirectX-Headers; Agility SDK | §4 | ✅ | #165 / PR #166 |
| 5 | Everything above on D3D12, the same shared tests on both backends; PIX markers | §11 | ✅ | #165 / PR #166 |
| 5 | Device loss reports: DRED on D3D12, `VK_EXT_device_fault` on Vulkan | §8 | ✅ | #165 / PR #166 |
| 5 | Look into capturing from the app with PIX (D3D12) and Nsight Graphics (NVIDIA), as with RenderDoc; each needs its own SDK or library approved first | §11 | ✅ | #165 / PR #166 |
| 6 Hot swap and close | Resource registry, recreate from CPU descriptions, swap on the same window | §11 | ✅ | #167 / PR #168 |
| 6 | Closing PR bumps the version to **0.6.0** | §21 | ✅ | #167 / PR #168 |

**Done when:** the same scene renders on both backends with validation clean; 100k instances are culled in compute and drawn indirectly; a second run creates pipelines from the cache; a Vulkan ↔ D3D12 swap works while running; readback returns the right pixels; a RenderDoc capture of a bindless frame opens and shows named passes.

**Released as `v0.6.0`** once PR #168 is merged and tagged (with the owner's go-ahead).

**Not in M5:** mesh shaders and meshlets, ray tracing, HZB occlusion culling, HDR output, async compute (all M13).

## M6 — glTF scenes and Meow2Renderer 🟨
**Goal:** open the Khronos glTF 2.0 sample scenes in Meow2Renderer through the same `Scene` the editor and player use, draw them with metallic-roughness PBR on both backends, catch regressions with golden images, and give Renderer its ImGui shell. M6 and the old M7 are merged (owner decision 2026-11-07); it stops at PBR, and the rest goes to M13 (Advanced rendering).
**Depends on:** M5

Planned as five PRs; PR 4's shell came first, at the owner's request. glTF is an import source, never the scene format: the engine's own `.m2scene` holds scenes (PR 2).

| PR | Feature | Notes (techstack.md) | Status | Issue / PR |
|---|---|---|---|---|
| 1 glTF import and first light | cgltf v1.15 (submodule) and `stb_image.h` (copied in), owner-approved | §4 | ✅ | #169 / PR #170 |
| 1 | Importer: `.gltf`/`.glb` through the VFS, data URIs, right- to left-handed, node tree, materials, PNG/JPEG images | §10, §13 | ✅ | #169 / PR #170 |
| 1 | `MeshRenderer` and the bridge to the GPU scene: UVs, textures and materials in `GpuScene` (restored after a backend switch), instances gathered from the `Scene` | §25 | ✅ | #169 / PR #170 |
| 1 | On-demand sample fetch (`cmake/FetchGltfSample.cmake`), `renderer.scene` | §13 | ✅ | #169 / PR #170 |
| 2 Scene files | `.m2scene`: one text file per scene, entities sorted by `EntityId`, deterministic saving so git merges edits to different entities | §25 | ⬜ | |
| 2 | Versions: a format version and one per component, up and down migrations, unknown components kept as they were, renames through aliases | §25 | ⬜ | |
| 2 | Each app registers the components it knows and skips the rest (Renderer ignores scripts) | §25 | ⬜ | |
| 2 | glTF import writes assets with `.meta` sidecars (stable `AssetId`s) and a prefab, so re-importing keeps edits | §13a | ⬜ | |
| 3 PBR | Metallic-roughness PBR, normal maps with tangents, occlusion, emissive, vertex colours | §11 | ✅ | #174 / PR #177 |
| 3 | `.m2texture`: cooked textures with a format field and mips, BC7 and BC5 through DirectXTex (owner-approved), a texture cache; glTF samplers; texture transforms | §13a | ✅ | #174 / PR #177, #183 / PR #185 |
| 3 | Double-sided materials and mirrored (negative-scale) nodes; alpha blending, sorted | §11 | ✅ | #174 / PR #177 |
| 3 | Image-based lighting from an environment map: a physically based sky (Hillaire, owner choice) lights the scene | §11 | ✅ | #180 / PR #181 |
| 4 Renderer app | Dear ImGui docking shell (submodule, owner request), frame and memory stats per tag; scaled for the display, 1080p default window | §2, §7 | ✅ | #172 / PR #173, #175 / PR #176 |
| 4 | Backend switch button, capture button (the same as F11) | §11 | ✅ | #172 / PR #173 |
| 4 | Scene browser: the sample list, which are downloaded, fetch on click, open any `.gltf`/`.glb` (`.m2scene` with PR 2) | §13 | ✅ | #172 / PR #173 |
| 4 | Theme (owner request): pure black with a red accent in Renderer and Editor, Barlow Semi Condensed and Consolas, app icons (cat; E and R badges), asset icons | §2 | ✅ | #178 / PR #179 |
| 4 | Log console (ring-buffer sink, in Consolas), settings window for debug tools saved to the TOML config, saved window layout | §7, §11 | ⬜ | |
| 5 Verification and close | Golden-image tests per backend (label `visual`); new goldens only with the owner's sign-off | §13 | ⬜ | |
| 5 | The 25 suggested scenes: test scenes first, then the showcase ones (DamagedHelmet the first goal) | §13 | ⬜ | |
| 5 | **meshoptimizer** (submodule, needs approval): vertex cache and overdraw optimisation at import | §4 | ⬜ | |
| 5 | Closing PR bumps the version to **0.7.0** | §21 | ⬜ | |

**Suggested scenes:** first light (Box, BoxTextured, Duck, Avocado); coordinates (OrientationTest, NegativeScaleTest, TextureCoordinateTest); PBR (MetalRoughSpheres, MetalRoughSpheresNoTextures, EnvironmentTest); normal maps (NormalTangentTest, NormalTangentMirrorTest); textures and colour (TextureSettingsTest, TextureTransformTest, VertexColorTest); transparency (AlphaBlendModeTest); showcase (DamagedHelmet, FlightHelmet, WaterBottle, BoomBox, Lantern, Corset, SciFiHelmet); stress (Sponza, ABeautifulGame).

**Done when:** the suggested scenes open in Renderer on both backends with validation clean and match their golden images; a scene saved as `.m2scene` loads in Renderer with unknown components skipped; an old-version scene file upgrades.

**Not in M6** (M13): animation and skinning, morph targets, Draco and meshopt compression, Basis Universal and BC7 textures, `KHR_materials_*` extensions, meshlets.

## M7 — merged into M6
glTF and rendering verification are part of M6 now (owner decision 2026-11-07). The number isn't reused.

## M8 — Physics ⬜
**Goal:** rigid bodies via Jolt, swappable at build time later.
**Depends on:** M1, M3

- `PhysicsWorld`, bodies, shapes, layers, queries (runs in `fixedUpdate`)
- Jolt backend (allocator hooks → `MemTag::Physics`, jobs on enkiTS)
- Debug draw through the renderer

## M9 — Editor 🟨
**Goal:** a Qt (≥ 6.8) editor with a live viewport, fully drivable by AI agents over MCP.
**Depends on:** M5, M1

- Command system: every editor action is an undoable command in `EditorCommandRegistry` (techstack §20)
- ✅ Qt main window with a native viewport, size/position/mode in the config — #58 / PR #59
- Dock layout (UI only issues commands; editor shortcuts are Qt shortcuts)
- Viewport widget (swapchain on the Qt native window), driven by `tick()` from a Qt timer
- `QtInputSource`: viewport keyboard/mouse → `InputSystem` while the viewport has focus (play mode / viewport camera)
- Log console dock (ring-buffer sink)
- Scene hierarchy, inspector, undo/redo
- GPU picking: on a click, a scissored pass around the cursor appends `{entity, depth}` per pixel; sorted on the CPU, repeated clicks step down through transparent layers; alpha cutoff in the pick shader; gizmos win; `registry.valid()` checked after the delayed readback
- Capture button and debug-tool settings: an editor command like the rest, with nothing to undo, so MCP can capture a frame too; saved to the TOML config
- **MCP server**: commands/queries as MCP tools, viewport screenshot, localhost + token, `--mcp`
- Headless mode (`--headless`) + MCP integration tests in CI

## M10 — Game UI ⬜
**Goal:** the built-in UI used by exported games.
**Depends on:** M5

- Layout, text rendering, basic widgets, input focus

## M11 — Export ⬜
**Goal:** the editor produces a standalone game.
**Depends on:** M9, M10

- Package format + cooker
- `Meow2Player` mounts the package and runs the start scene
- Editor export command

## M12 — Platforms ⬜
**Goal:** Linux and Android.
**Depends on:** M5 (Vulkan)

- Linux (X11/Wayland) build + CI
- Android (NDK, GameActivity, APK packaging)

## M13 — Advanced rendering ⬜
**Goal:** what comes after PBR, once the editor is up (owner decision 2026-11-07).
**Depends on:** M6, M9

- Animation: skinning, morph targets, glTF animation
- Compression: Draco and meshopt meshes, Basis Universal and BC7 textures
- `KHR_materials_*` extensions (clearcoat, transmission, sheen, …)
- **Meshlets**, as an optional path for dense static opaque meshes, not the only one: clusters from meshoptimizer, culled per cluster, drawn with mesh shaders where the GPU has them. The indirect path from M5 stays as the baseline for skinned, transparent and small meshes and for GPUs without mesh shaders (Android, older desktop)
- HZB occlusion culling, HDR output, async compute, ray tracing (PIX and Nsight for the ray-tracing pipeline)

---

## Changelog
- 2026-10-26 — Initial milestones (moved out of techstack.md §17).
- 2026-10-27 — M1 rescoped as "Core runtime" (old M1 Core & Memory + old M2 Logging & Crash, plus config, clock, events, Tracy, release packaging); old M3 Platform becomes M2 (with game loop and input design); later milestones renumbered (old M4 → M3 … old M13 → M12). RHI (M5) gains the frame arena, upload ring buffer and frame packet; handles move there.
- 2026-11-02 — M2 renamed "Platform & scene foundation" and turned into a feature table; adds 128-bit IDs (techstack §24) and the scene/ECS on EnTT (§25, submodule pending approval), plus a "done when".
- 2026-11-05 — M2 done (0.3.0); M3 (Math) expanded into a feature table with a "done when": types and helpers, SIMD baseline and batches, random numbers, colours, quaternions, transforms, easing and splines, projections, camera, bounds and culling, rays, vertex packing, transform hierarchy; DirectXMath submodule and the SIMD baseline pending owner decisions.
- 2026-11-05 — M3 done (0.4.0): every row implemented as a stack of PRs (#108–#132).
- 2026-11-06 — M4 is now "Shaders and assets", a feature table: DXC pinned, `ShaderCompiler`, asset core, shader assets and permutations, cooker, build step, bindless contract, hot reload. M5 gains bindless, GPU-driven drawing, readback and pipeline caching; M7's GPU scene is the main path; M9 gains GPU picking.
- 2026-11-06 — M4 done (0.5.0): every row implemented as a stack of PRs (#134–#152).
- 2026-11-07 — M5 is now a feature table in six PRs, adding deferred destruction, a small render graph, headless rendering, a capability check, multiple swapchains, pipeline rebuilds on shader reload and debug tools. M7 gains meshoptimizer and meshlets as an optional path.
- 2026-11-07 — M5 PR 3 gains command lists per job thread, and PR 4's render graph records passes in parallel (owner decision).
- 2026-11-07 — Owner request: capture buttons and a debug-tool settings window in Renderer (M6) and Editor (M9), saved to the TOML config; PIX and Nsight in-app capture to look into with D3D12 (M5 PR 5).
- 2026-11-07 — M5 PR 5 done: D3D12 with the Agility SDK (owner decision), DRED and `VK_EXT_device_fault`.
- 2026-11-07 — M5 done (0.6.0): hot swap between Vulkan and D3D12.
- 2026-11-07 — Owner request: Meow2Renderer's ImGui shell, scene picker and stats (M6 PR 4) ahead of scene files.
- 2026-11-07 — M6 rewritten as a feature table: M7 merged in, stopping at PBR (owner decision); scene files, PBR, the ImGui shell and golden images planned as PRs 2–5; new M13 Advanced rendering takes animation, compression, material extensions and meshlets.
