# Meow2

[![CI](https://github.com/buttception/Meow2/actions/workflows/ci.yml/badge.svg)](https://github.com/buttception/Meow2/actions/workflows/ci.yml)

A modular C++20 game engine with Vulkan and Direct3D 12 behind a common RHI (switchable at runtime). Windows first; Linux and Android planned.

| Project | Type | What it is |
|---|---|---|
| **Meow2Engine** | static library | Core, memory arenas, logging, crash dumps, platform (SDL3), math (DirectXMath), RHI (Vulkan/D3D12), HLSL shaders via DXC, renderer, physics (Jolt), built-in game UI |
| **Meow2Renderer** | executable | Rendering/shader test bed and showcase (Khronos glTF 2.0 sample scenes), Dear ImGui UI |
| **Meow2Editor** | executable | Qt 6.8 editor that exports games as standalone executables |
| **Meow2Player** | executable | Standalone game runtime bundled by the editor on export |

## Status

🚧 Pre-alpha. M0 (foundation) is done: the CMake skeleton builds an empty engine and apps in all three configs, with tests and CI. Next: M1 (core runtime: logging, crash dumps, memory, jobs, profiling). See [milestones.md](milestones.md).

## Documentation

- [techstack.md](techstack.md) — libraries, architecture, conventions
- [milestones.md](milestones.md) — roadmap and progress
- [AGENTS.md](AGENTS.md) — development workflow (issues, branches, tests, PRs, reviews)
- [docs/wiki](docs/wiki/Home.md) — feature documentation (mirrored to the GitHub wiki)

## Building

Requirements (Windows): Visual Studio 2022 with the *Desktop development with C++* workload, CMake ≥ 3.25 on `PATH`, and Git. Later milestones add the Vulkan SDK and Qt 6.8 (editor only).

### Quick start: any command prompt (Visual Studio solution)

Works from a **standard** cmd or PowerShell window, with no developer prompt and no `vcvars`. CMake's Visual Studio generator finds the compiler itself.

```
git clone --recurse-submodules https://github.com/buttception/Meow2.git
cd Meow2
cmake -S . -B build/vs2022 -G "Visual Studio 17 2022" -A x64
cmake --build build/vs2022 --config Debug
ctest --test-dir build/vs2022 -C Debug --output-on-failure
```

- The first command generates the solution, **`build/vs2022/Meow2.sln`**. Open it in Visual Studio if you want the IDE; set a project (e.g. `Meow2Renderer`) as startup project to run or debug it.
- The second builds everything. Executables land in **`build/vs2022/bin/Debug/`** (`Meow2Renderer.exe`, `Meow2Editor.exe`, `Meow2Player.exe`, the test executables).
- The third runs all tests.
- For another configuration, use `--config Release` / `-C Release` (or `RelWithDebInfo`). One solution holds all three, so there's no need to regenerate.

### Presets (Ninja; developer prompt)

The presets are what CI uses. They build with Ninja (faster incremental builds, one folder per configuration), but Ninja needs the compiler on `PATH`, so run them from a **Developer PowerShell / Command Prompt for VS 2022** (or open the folder in Visual Studio, which picks up the presets automatically):

```
cmake --workflow --preset windows-msvc-debug     # configure + build + test in one go
```

| Preset | Use |
|---|---|
| `windows-msvc-debug` / `linux-clang-debug` | day-to-day development |
| `windows-msvc-relwithdebinfo` / `linux-clang-relwithdebinfo` | optimised + debug info: profiling, crash dumps |
| `windows-msvc-release` / `linux-clang-release` | shipping builds (LTO on) |

Step by step: `cmake --preset <p>`, `cmake --build --preset <p>`. Output: `build/<preset>/bin/`.

## Running tests

```
ctest --test-dir build/vs2022 -C Debug                 # Visual Studio solution, any prompt
ctest --preset windows-msvc-debug                      # presets, developer prompt
ctest --preset windows-msvc-debug -L unit              # by label (unit, smoke, lint, ...)
```

More detail: [Building](docs/wiki/Building.md), [Testing](docs/wiki/Testing.md), [CI](docs/wiki/CI.md).

Forgot `--recurse-submodules`? Run `git submodule update --init --recursive`.

## API documentation

The public API is documented with Doxygen comments. With [Doxygen](https://www.doxygen.nl) installed (`winget install DimitriVanHeesch.Doxygen`), generate the HTML reference with:

```
cmake --build build/vs2022 --config Debug --target docs
```
Then open `build/vs2022/docs/html/index.html`. See [CodeStyle](docs/wiki/CodeStyle.md#documentation-comments-doxygen) for the comment rules.

## Profiling

Meow2 is instrumented for the [Tracy](https://github.com/wolfpld/tracy) profiler (from M1): CPU zones, worker threads, locks, memory by tag, and later GPU timings for Vulkan and D3D12.
- Instrumentation is compiled into **Debug** and **RelWithDebInfo** builds and removed from Release. It listens on localhost only, and only when a viewer connects.
- The Tracy **viewer is not included in this repo.** Download `tracy-profiler` from [Tracy's releases](https://github.com/wolfpld/tracy/releases), using the **same version** as the `external/tracy` submodule (see [techstack.md](techstack.md) §4), start the app, and click *Connect*.

## License

See [LICENSE](LICENSE).
