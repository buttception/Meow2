# Meow2

A modular C++20 game engine with Vulkan and Direct3D 12 behind a common RHI (switchable at runtime). Windows first; Linux and Android planned.

| Project | Type | What it is |
|---|---|---|
| **Meow2Engine** | static library | Core, memory arenas, logging, crash dumps, platform (SDL3), math (DirectXMath), RHI (Vulkan/D3D12), HLSL shaders via DXC, renderer, physics (Jolt), built-in game UI |
| **Meow2Renderer** | executable | Rendering/shader test bed and showcase (Khronos glTF 2.0 sample scenes), Dear ImGui UI |
| **Meow2Editor** | executable | Qt 6.8 editor that exports games as standalone executables |
| **Meow2Player** | executable | Standalone game runtime bundled by the editor on export |

## Status

🚧 Pre-alpha. M0 (foundation) in progress: the CMake skeleton builds an empty engine and apps. See [milestones.md](milestones.md).

## Documentation

- [techstack.md](techstack.md) — libraries, architecture, conventions
- [milestones.md](milestones.md) — roadmap and progress
- [AGENTS.md](AGENTS.md) — development workflow (issues, branches, tests, PRs, reviews)
- [docs/wiki](docs/wiki/Home.md) — feature documentation (mirrored to the GitHub wiki)

## Building

> Not available yet. The CMake skeleton arrives in milestone M0.

```
git clone --recurse-submodules https://github.com/buttception/Meow2.git
cmake --preset windows-msvc-debug          # or -relwithdebinfo / -release
cmake --build --preset windows-msvc-debug
```

Requires Visual Studio 2022, CMake ≥ 3.25, Ninja, the Vulkan SDK, and Qt 6.8 (editor only).

## Running tests

```
ctest --preset windows-msvc-debug --output-on-failure
```

## License

See [LICENSE](LICENSE).
