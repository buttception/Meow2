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

Requirements (Windows): Visual Studio 2022 (C++ workload), CMake ≥ 3.25, Ninja (both ship with Visual Studio). Later milestones add the Vulkan SDK and Qt 6.8 (editor only).

```
git clone --recurse-submodules https://github.com/buttception/Meow2.git
cd Meow2
cmake --workflow --preset windows-msvc-debug     # configure + build + test in one go
```

On Windows, run this from a **Developer PowerShell / Command Prompt for VS 2022** (or open the folder in Visual Studio, which picks up the presets automatically).

| Preset | Use |
|---|---|
| `windows-msvc-debug` / `linux-clang-debug` | day-to-day development |
| `windows-msvc-relwithdebinfo` / `linux-clang-relwithdebinfo` | optimised + debug info: profiling, crash dumps |
| `windows-msvc-release` / `linux-clang-release` | shipping builds (LTO on) |

Step by step: `cmake --preset <p>`, `cmake --build --preset <p>`. Output: `build/<preset>/bin/`.

## Running tests

```
ctest --preset windows-msvc-debug              # all tests
ctest --preset windows-msvc-debug -L unit      # by label (unit, smoke, ...)
```

More detail: [Building](docs/wiki/Building.md), [Testing](docs/wiki/Testing.md).

Forgot `--recurse-submodules`? Run `git submodule update --init --recursive`.

## License

See [LICENSE](LICENSE).
