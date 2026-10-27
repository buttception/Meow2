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

**Done when:** `cmake --preset … && cmake --build … && ctest …` succeeds on Windows for all three configs, and CI runs it on every PR.

## M1 — Core & Memory ⬜
**Goal:** the low-level building blocks everything else uses.
**Depends on:** M0

- Asserts (`MEOW2_ASSERT`, `MEOW2_VERIFY`), `Result<T>`, typed handles
- `IAllocator` + `std::pmr` adapter
- Linear arena, frame arena (per frame in flight), pool allocator, stack allocator
- Tracking allocator with memory tags and a leak report
- Job system: worker thread pool (`std::jthread`), jobs with dependencies, wait handles
- Sanitizer presets: `linux-clang-asan` (Address + UndefinedBehavior), `linux-clang-tsan` (Thread), run in CI

## M2 — Logging & Crash ⬜
**Goal:** see what the engine is doing, and get a `.dmp` when it dies.
**Depends on:** M0 (M1 nice to have)

- `Logger` over spdlog, categories, compile-time level stripping
- File sink (rotating), console ring-buffer sink
- `CrashHandler`: minidump on unhandled exception / terminate / abort, log flush

## M3 — Platform ⬜
**Goal:** a window on screen and input events.
**Depends on:** M0 (M2 recommended)

- SDL3 window (API-neutral, exposes the native handle)
- Event loop, keyboard/mouse input, gamepad
- Virtual file system (`FileSystem`)

## M4 — Math ⬜
**Goal:** one coordinate convention, used consistently.
**Depends on:** M0

- DirectXMath aliases and helpers (storage vs compute types)
- Transforms (TRS), quaternions
- Reversed infinite-Z perspective (left-handed, depth 0..1), orthographic
- Camera

## M5 — Shaders ⬜
**Goal:** HLSL compiled for both APIs.
**Depends on:** M0

- `ShaderCompiler` over DXC (DXIL + SPIR-V)
- CMake shader build step
- Reflection → binding layouts
- Hot reload (used by Meow2Renderer)

## M6 — RHI ⬜
**Goal:** draw with Vulkan and D3D12 through one interface, and switch between them while running.
**Depends on:** M3, M4, M5

- RHI interface design (device, queues, command lists, resources by handle, barriers)
- Vulkan backend: device → swapchain → clear screen → triangle → buffers/textures/pipelines
- D3D12 backend: parity with Vulkan
- Runtime hot swap Vulkan ↔ D3D12

## M7 — Meow2Renderer app ⬜
**Goal:** a showcase / test bed for rendering.
**Depends on:** M6 (M2 for the console)

- ImGui docking shell (+ multi-viewport later)
- Log console window, frame stats, memory stats
- Backend switch button

## M8 — glTF & rendering verification ⬜
**Goal:** load the Khronos glTF 2.0 sample scenes and catch rendering regressions automatically.
**Depends on:** M6, M7

- `.gltf` / `.glb` importer, right-handed → left-handed conversion
- PBR materials (metallic-roughness), textures, normal maps
- On-demand sample-asset fetch, scene browser
- Visual regression tests (golden images per backend)
- Sample scenes, one by one as requested

## M9 — Physics ⬜
**Goal:** rigid bodies via Jolt, swappable at build time later.
**Depends on:** M1, M4

- `PhysicsWorld`, bodies, shapes, layers, queries
- Jolt backend
- Debug draw through the renderer

## M10 — Editor ⬜
**Goal:** a Qt 6.8 editor with a live viewport, fully drivable by AI agents over MCP.
**Depends on:** M6, M2

- Command system: every editor action is an undoable command in `EditorCommandRegistry` (techstack §20)
- Qt shell, dock layout (UI only issues commands)
- Viewport widget (swapchain on the Qt native window)
- Log console dock
- Scene hierarchy, inspector, undo/redo
- **MCP server**: commands/queries as MCP tools, viewport screenshot, localhost + token, `--mcp`
- Headless mode (`--headless`) + MCP integration tests in CI

## M11 — Game UI ⬜
**Goal:** the built-in UI used by exported games.
**Depends on:** M6

- Layout, text rendering, basic widgets, input focus

## M12 — Export ⬜
**Goal:** the editor produces a standalone game.
**Depends on:** M10, M11

- Package format + cooker
- `Meow2Player` mounts the package and runs the start scene
- Editor export command

## M13 — Platforms ⬜
**Goal:** Linux and Android.
**Depends on:** M6 (Vulkan)

- Linux (X11/Wayland) build + CI
- Android (NDK, GameActivity, APK packaging)

---

## Changelog
- 2026-10-26 — Initial milestones (moved out of techstack.md §17).
