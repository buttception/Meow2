# Meow2 Wiki

Meow2 is a modular C++20 game engine: Vulkan and Direct3D 12 behind one RHI (switchable at runtime), SDL3 windowing, DirectXMath, Jolt physics, HLSL shaders via DXC, and glTF 2.0 assets. Windows first; Linux and Android planned.

This wiki is the developer documentation: what each feature does, how to use it, and how it works. Each module gets a page as it is implemented.

## Projects

| Project | What it is |
|---|---|
| `Meow2Engine` | static library with all runtime code |
| `Meow2Renderer` | showcase and test bed for rendering (Dear ImGui) |
| `Meow2Editor` | Qt 6.8 editor, drivable by AI agents over MCP |
| `Meow2Player` | standalone runtime for exported games |

## Quick start (Windows)

From a *Developer PowerShell for VS 2022*:

```
git clone --recurse-submodules https://github.com/buttception/Meow2.git
cd Meow2
cmake --workflow --preset windows-msvc-debug     # configure + build + test
```

See [Building](Building.md) for all presets and [Testing](Testing.md) for running and writing tests.

## Pages

| Module | Page | Status |
|---|---|---|
| Build system | [Building](Building.md) | ✅ M0 |
| Testing | [Testing](Testing.md) | ✅ M0 |
| Platform & config header | [Platform](Platform.md) | ✅ M0 |
| Asserts & debugger detection | [Asserts](Asserts.md) | ✅ M1 |
| Code style & warnings | [CodeStyle](CodeStyle.md) | ✅ M0 |
| CI (GitHub Actions) | [CI](CI.md) | ✅ M0 |
| Architecture (DI, globals, threading) | [Architecture](Architecture.md) | ✅ M0 |
| Working with AI agents (Claude Code, worktrees) | [AgentWorkflow](AgentWorkflow.md) | ✅ M0 |

## Roadmap

M0 (foundation) is done (`v0.1.0`). Next up is **M1, core runtime**: asserts, logging, crash dumps, config, memory (arenas, tracking by tag), events, job system (enkiTS), Tracy profiling, sanitizers and release zips. After that, **M2, platform**: SDL3 window, game loop and input. The full roadmap is in [milestones.md](../../milestones.md).

## API reference

The generated Doxygen reference for the public engine API is at **https://buttception.github.io/Meow2/**. It's rebuilt on every merge to `main`, and the wiki explains concepts and usage, while the reference lists every class, function and macro.

See also: [techstack.md](../../techstack.md) (stack and conventions), [AGENTS.md](../../AGENTS.md) (how agents work on this repo).
