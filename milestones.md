# Meow2 — Milestones

A map of where the engine is heading, not a schedule. Work happens **as the owner asks for it**, one small feature (= 1 issue, 1 branch, 1 PR) at a time.
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

## M3 — Math 🟨
**Goal:** one coordinate convention, used consistently: math types and helpers, transforms and the transform hierarchy in the scene, a camera with reversed-Z projections, the bounding volumes and rays that culling (M5/M7) and editor picking (M9) need, and the general-purpose tools around them: SIMD batches, deterministic random numbers, colours, easing and splines, vertex packing.
**Depends on:** M0 (math), M2 (the transform hierarchy lives in the `Scene`)

Rough order: DirectXMath + types → SIMD baseline → scalar helpers, random numbers, colours → quaternions → transforms → easing and splines → projections and camera → bounds and culling → rays → packing → transform hierarchy.

| Feature | Notes (techstack.md) | Status | Issue / PR |
|---|---|---|---|
| DirectXMath submodule (`may2026`, owner-approved) + `sal.h` shim for Linux; `Meow2/Math` types: `Vec2/3/4`, `Quat`, `Mat4` for storage vs `XMVECTOR`/`XMMATRIX` for computation, load/store, constants, `nearlyEqual`, `std::formatter` and GoogleTest printers | §10 | ✅ | #103 / PR #104 |
| SIMD: **AVX2 baseline on x64** (owner decision 2026-11-05; with FMA3 and F16C: `/arch:AVX2`, `-mavx2 -mfma -mf16c`, DirectXMath's `_XM_AVX2_INTRINSICS_`), a CMake option (`MEOW2_SIMD=AVX2|SSE4.2`) so it can be lowered; a CPU check at startup that shows a clear message on a CPU without AVX2 instead of crashing on an illegal instruction; NEON on arm64; alignment rules; batch helpers over arrays (transform N points, N spheres vs a frustum) for the systems that process thousands of items | §10, §19 | ⬜ | |
| Scalar helpers: radians ↔ degrees, `lerp`, `clamp`, `saturate`, `wrapAngle`, `smoothstep` | §10 | ⬜ | |
| Random numbers: a small, fast, seedable generator (PCG32 or xoshiro), one per system or thread, never global; our own ranges and distributions (floats, ints, unit vectors, points in a sphere), because `std::` distributions give different results on MSVC and libstdc++; the same sequence on every platform for replays | §10, §18 | ⬜ | |
| Colours: linear `Color` (float RGBA) and `Color8` (8-bit sRGB), exact sRGB ↔ linear conversion, HSV, premultiplied alpha, named constants | §10 | ⬜ | |
| Quaternions: axis-angle, Euler angles with a documented order (yaw Y, pitch X, roll Z), look rotation, `slerp`, normalise, angle between | §10 | ⬜ | |
| `Transform` (TRS: position, rotation, scale): to/from matrix, compose, inverse, transform a point/direction, interpolate (for `render(alpha)`) | §10, §23 | ⬜ | |
| Easing and splines: the standard easing curves (quad, cubic, sine, expo, back, elastic, bounce; in/out/in-out); Catmull-Rom, cubic Bézier and Hermite splines for points and rotations; arc-length parameterisation (constant speed along a path) | §10 | ⬜ | |
| Projections: reversed infinite-Z perspective and reversed-Z orthographic (left-handed, depth 0..1); the Vulkan Y-flip convention checked by tests | §10, §11 | ⬜ | |
| Camera: view matrix from a transform, field of view/aspect/near, screen ↔ world (project/unproject), its frustum; camera-relative positions for rendering (§10 large worlds) | §10 | ⬜ | |
| Bounding volumes and culling: AABB, sphere, oriented box, frustum (DirectXCollision); contains/intersects; frustum vs AABB/sphere | §10, §13 | ⬜ | |
| Rays and picking: a ray from a screen point; ray vs plane, sphere, AABB, triangle (editor picking, M9) | §10 | ⬜ | |
| Vertex packing and quantisation: half floats, unorm/snorm 8/16, octahedral normals (2 × 16 bit), tangent frames as a packed quaternion, positions quantised to 16 bit inside a mesh's bounds; round-trip error bounds tested | §10, §11 | ⬜ | |
| Transform hierarchy in the `Scene`: `LocalTransform`, `WorldTransform`, `Parent`; world transforms updated by depth level in parallel jobs with the SIMD batch helpers; dirty flags; the previous transform kept for `render(alpha)` | §23, §25 | ⬜ | |
| Closing PR bumps the version to **0.4.0** | §21 | ⬜ | |

**Done when:** the projection matrices match hand-checked values in tests, a parented hierarchy of 100k entities updates its world transforms in parallel (TSan job green), a ray from a screen point picks the right AABB, the same seed gives the same random sequence on Windows and Linux CI, and packed vertices round-trip within their documented error.

**Not in M3:** noise (Perlin/simplex): most noise is generated on the GPU in shaders; a CPU version comes with the first CPU user (terrain heights for physics, camera shake, procedural placement). Large-world precision: see techstack §10; only the cheap parts (camera-relative rendering, one `WorldPosition` type) are planned now.

## M4 — Shaders ⬜
**Goal:** HLSL compiled for both APIs.
**Depends on:** M0

- `ShaderCompiler` over DXC (DXIL + SPIR-V)
- CMake shader build step
- Reflection → binding layouts
- Hot reload (used by Meow2Renderer)

## M5 — RHI ⬜
**Goal:** draw with Vulkan and D3D12 through one interface, and switch between them while running.
**Depends on:** M2, M3, M4

- RHI interface design (device, queues, command lists, resources by handle, barriers)
- Typed handles (index + generation)
- **Frame arena:** one `BlockArena` per frame in flight × worker thread, reset when that frame's GPU fence signals (§6)
- **Upload ring buffer** per frame in flight (CPU-writable GPU memory, same fence-based reset) for GPU scene updates
- **Frame packet / extract step:** `update` copies what the renderer needs into a frame-arena packet; the renderer only reads the packet (§23)
- Vulkan backend: device → swapchain → clear screen → triangle → buffers/textures/pipelines
- D3D12 backend: parity with Vulkan
- Runtime hot swap Vulkan ↔ D3D12
- GPU profiling zones (Tracy, Vulkan + D3D12)

## M6 — Meow2Renderer app ⬜
**Goal:** a showcase / test bed for rendering.
**Depends on:** M5

- ImGui docking shell (+ multi-viewport later)
- Log console window (ring-buffer sink), frame stats, memory stats (per tag)
- Backend switch button

## M7 — glTF & rendering verification ⬜
**Goal:** load the Khronos glTF 2.0 sample scenes and catch rendering regressions automatically.
**Depends on:** M5, M6

- `.gltf` / `.glb` importer, right-handed → left-handed conversion
- GPU scene: persistent instance/material buffers, delta uploads through the upload ring
- PBR materials (metallic-roughness), textures, normal maps
- On-demand sample-asset fetch, scene browser
- Visual regression tests (golden images per backend)
- Sample scenes, one by one as requested

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

---

## Changelog
- 2026-10-26 — Initial milestones (moved out of techstack.md §17).
- 2026-10-27 — M1 rescoped as "Core runtime" (old M1 Core & Memory + old M2 Logging & Crash, plus config, clock, events, Tracy, release packaging); old M3 Platform becomes M2 (with game loop and input design); later milestones renumbered (old M4 → M3 … old M13 → M12). RHI (M5) gains the frame arena, upload ring buffer and frame packet; handles move there.
- 2026-11-02 — M2 renamed "Platform & scene foundation" and turned into a feature table; adds 128-bit IDs (techstack §24) and the scene/ECS on EnTT (§25, submodule pending approval), plus a "done when".
- 2026-11-05 — M2 done (0.3.0); M3 (Math) expanded into a feature table with a "done when": types and helpers, SIMD baseline and batches, random numbers, colours, quaternions, transforms, easing and splines, projections, camera, bounds and culling, rays, vertex packing, transform hierarchy; DirectXMath submodule and the SIMD baseline pending owner decisions.
