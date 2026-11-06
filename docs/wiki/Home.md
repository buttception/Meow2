# Meow2 Wiki

Meow2 is a modular C++20 game engine: Vulkan and Direct3D 12 behind one RHI (switchable at runtime), SDL3 windowing, DirectXMath, Jolt physics, HLSL shaders via DXC, and glTF 2.0 assets. Windows first; Linux and Android planned.

This wiki is the developer documentation: what each feature does, how to use it, and how it works. Each module gets a page as it is implemented.

## Projects

| Project | What it is |
|---|---|
| `Meow2Engine` | static library with all runtime code |
| `Meow2Renderer` | showcase and test bed for rendering (Dear ImGui) |
| `Meow2Editor` | Qt editor (Qt ≥ 6.8), drivable by AI agents over MCP |
| `Meow2Player` | standalone runtime for exported games |

## Quick start (Windows)

From a *Developer PowerShell for VS 2022*:

```
git clone --recurse-submodules https://github.com/buttception/Meow2.git
cd Meow2
set CMAKE_PREFIX_PATH=C:\Qt\6.12.0\msvc2022_64  # Qt for the editor (optional; see Building → Qt)
cmake --workflow --preset windows-msvc-debug     # configure + build + test
```

You need Visual Studio 2022 (C++ workload), CMake ≥ 3.25 and Git; for the editor also Qt ≥ 6.8 with the **MSVC 2022 64-bit** kit ([Building → Qt](Building.md#qt)). Without Qt, everything but the editor builds.

See [Building](Building.md) for all presets and [Testing](Testing.md) for running and writing tests.

## Pages

| Module | Page | Status |
|---|---|---|
| Build system | [Building](Building.md) | ✅ M0 |
| Testing | [Testing](Testing.md) | ✅ M0 |
| Platform & config header | [Platform](Platform.md) | ✅ M0 |
| Asserts & debugger detection | [Asserts](Asserts.md) | ✅ M1 |
| Config files (TOML, `--set` overrides) | [Config](Config.md) | ✅ M1 |
| Frame clock (frame time, clamped delta) | [FrameClock](FrameClock.md) | ✅ M1 |
| Game loop (fixed timestep, update, render) | [GameLoop](GameLoop.md) | ✅ M2 |
| Input (keys, mouse, gamepads, events, SDL sources) | [Input](Input.md) | ✅ M2 |
| File system (virtual paths, mounts) | [FileSystem](FileSystem.md) | ✅ M2 |
| Window (SDL3, settings in the config) | [Window](Window.md) | ✅ M2 |
| Editor (Qt main window, viewport) | [Editor](Editor.md) | 🟨 M9 (main window only) |
| Error handling (`Result<T>`) | [ErrorHandling](ErrorHandling.md) | ✅ M1 |
| Events (`EventBus`) | [Events](Events.md) | ✅ M1 |
| IDs (128-bit, kind tag, lock-free generator) | [Ids](Ids.md) | ✅ M2 |
| Math (DirectXMath types, load/store, constants) | [Math](Math.md) | 🟨 M3 |
| Scene (entities and components on EnTT, command buffers) | [Scene](Scene.md) | ✅ M2 |
| Jobs (`JobSystem`, enkiTS) | [Jobs](Jobs.md) | ✅ M1 |
| Profiling (Tracy) | [Profiling](Profiling.md) | ✅ M1 |
| Releases (zip, GitHub Releases) | [Releases](Releases.md) | ✅ M1 |
| Crash dumps (minidumps) | [CrashDumps](CrashDumps.md) | ✅ M1 (Windows) |
| Logging (spdlog) | [Logging](Logging.md) | ✅ M1 |
| Memory (arenas, tracking, leak check, memory tags) | [Memory](Memory.md) | ✅ M1 |
| Code style & warnings | [CodeStyle](CodeStyle.md) | ✅ M0 |
| CI (GitHub Actions) | [CI](CI.md) | ✅ M0 |
| Architecture (DI, globals, threading) | [Architecture](Architecture.md) | ✅ M0 |
| Working with AI agents (Claude Code, worktrees) | [AgentWorkflow](AgentWorkflow.md) | ✅ M0 |

## Roadmap

M0 (foundation), **M1 (core runtime, `v0.2.x`)**, **M2 (platform & scene foundation, `v0.3.0`)** and **M3 (math, `v0.4.0`)** are done: logging, crash dumps, memory tracking, config, events, jobs, Tracy, sanitizers, release zips, the game loop, window and input events, IDs, the scene with command buffers, and the virtual file system, and the math: transforms and their hierarchy, camera and reversed-Z projections, bounding volumes and culling, rays and picking, random numbers, colours, easing and splines, and vertex packing ([Math](Math.md)). The Qt editor window (M9) is in too. Next up are **M4, shaders and assets** (HLSL through DXC, cooked as assets with permutations) and **M5, the RHI** (Vulkan and D3D12). The full roadmap is in [milestones.md](../../milestones.md).

## API reference

The generated Doxygen reference for the public engine API is at **https://buttception.github.io/Meow2/**. It's rebuilt on every merge to `main`, and the wiki explains concepts and usage, while the reference lists every class, function and macro.

See also: [techstack.md](../../techstack.md) (stack and conventions), [AGENTS.md](../../AGENTS.md) (how agents work on this repo).
