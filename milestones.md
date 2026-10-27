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

## M1 — Core runtime ⬜
**Goal:** everything the engine needs before it opens a window: see what it's doing (logs, profiler), survive and diagnose crashes, manage memory with full attribution, run work on all cores, and ship a release zip.
**Depends on:** M0

Rough order: asserts → logging → crash dumps → config → clock → memory → events → jobs → profiler; sanitizers and packaging alongside.

| Feature | Notes (techstack.md) | Status | Issue / PR |
|---|---|---|---|
| Asserts: `MEOW2_ASSERT` (off in Release), `MEOW2_VERIFY` (always evaluates), debug break, failure handler | §3 | ✅ | #35 / PR #36 |
| `CHANGELOG.md` (Keep a Changelog), used as release notes | §21 | ✅ | #35 / PR #36 |
| API reference published to GitHub Pages (`docs.yml`) | §16.1 | ✅ | #37 / PR #38 |
| Visual Studio workflow: `windows-vs2022` presets (incl. regenerate-only), filters by Subsystem/Module/Backend, solution folders | §2 | ✅ | #37 / PR #38 |
| `Result<T>` = `tl::expected` (submodule; swap to `std::expected` with C++23) | §3, §4 | ⬜ | |
| Logging: spdlog, levels debug/info/warn/error/fatal, `[hh:mm:ss.ms]<level> Name: msg`, `Engine` + per-app loggers, file sink (log folder) + console sink (Debug/RWDI), `fatal` flushes and crashes | §7 | ✅ | #39 / PR #41 |
| Release exes use the Windows GUI subsystem (no console window) | §5, §7 | ⬜ | |
| Crash dumps: minidump on crash in **all configs**, log flush, skipped under a debugger, child-process crash test | §8 | ⬜ | |
| Config: TOML via toml++, defaults → `config.toml` → command line, write-back | §3, §4 | ⬜ | |
| `FrameClock`: high-resolution time, delta time, frame counter | §23 | ⬜ | |
| Memory: `TrackingAllocator` (thread-safe, `MemTag`s, per-tag stats, leak report), `MallocResource` | §6 | ⬜ | |
| Memory: `BlockArena` (size-class block lists, reset keeps blocks, trim) as a `std::pmr::memory_resource` | §6 | ⬜ | |
| Memory: global `operator new`/`delete` override (Debug/RWDI) + allocator hooks for libraries that have them, thread-local tag scopes | §6, §18 | ⬜ | |
| Events: typed `EventBus` (immediate + queued, RAII subscriptions) | §22 | ⬜ | |
| Job system: enkiTS behind `JobSystem` (pinned main-thread tasks, custom allocator, per-thread index) | §19 | ⬜ | |
| Profiler: Tracy client (Debug/RWDI, localhost, on demand), zones in engine code, memory events from `TrackingAllocator`, enkiTS thread names | §3, §4 | ⬜ | |
| Sanitizer presets + CI jobs: `linux-clang-asan` (ASan + UBSan) from the start, `linux-clang-tsan` with the job system | §19 | ⬜ | |
| Build metadata: git commit in the version string (`0.1.0+g1a2b3c4`) | §21 | ⬜ | |
| API docs: Doxyfile + `docs` CMake target (skipped if Doxygen isn't installed), existing headers converted to Doxygen comments, CI job that fails on undocumented public API | §16.1 | ✅ | #33 / PR #34 |
| Release packaging: CMake install rules + CPack ZIP (exes, PDBs, MSVC runtime DLLs), `release.yml` builds it on a `v*` tag and publishes a GitHub Release | §21 | ⬜ | |
| Git hooks: regenerate the Visual Studio solution after pull/rebase/branch switch when CMake files change | §2 | ✅ | #40 / PR #41 |
| Closing PR bumps the version to **0.2.0** (tag `v0.2.0`, first release zip) | §21 | ⬜ | |

**Done when:** an app logs to file and console, writes a `.dmp` on a forced crash in every config, reports leaks by tag at exit, runs jobs on all cores visible in Tracy, and the ASan/UBSan/TSan CI jobs are green.

## M2 — Platform ⬜
**Goal:** a window on screen, a real game loop and input, shared by Renderer and Player (the editor feeds the same input system from Qt in M9).
**Depends on:** M1

- SDL3 window (one window, API-neutral, exposes the native handle; window events → `EventBus`)
- Game loop: `tick()` with fixed timestep (`fixedUpdate` 0..n times), `update`, `render(alpha)`; clamped frame time (§23)
- `InputSystem`: engine `Key` (HID scan codes), mouse, gamepad; polling (`isDown`, `wasPressed`, …) + input events on the `EventBus`; presses latched for `fixedUpdate` (§9)
- `SdlInputSource` (keyboard/mouse) and `SdlGamepadSource` (all three apps)
- Virtual file system (`FileSystem`)
- Closing PR bumps the version to **0.3.0**

## M3 — Math ⬜
**Goal:** one coordinate convention, used consistently.
**Depends on:** M0

- DirectXMath aliases and helpers (storage vs compute types)
- Transforms (TRS), quaternions
- Reversed infinite-Z perspective (left-handed, depth 0..1), orthographic
- Camera

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

## M9 — Editor ⬜
**Goal:** a Qt 6.8 editor with a live viewport, fully drivable by AI agents over MCP.
**Depends on:** M5, M1

- Command system: every editor action is an undoable command in `EditorCommandRegistry` (techstack §20)
- Qt shell, dock layout (UI only issues commands; editor shortcuts are Qt shortcuts)
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
