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
cmake --preset windows-vs2022                   # generate the solution
cmake --build --preset windows-vs2022-debug     # build
ctest --preset windows-vs2022-debug             # run the tests
```

- The first command generates the solution, **`build/vs2022/Meow2.sln`**. Open it in Visual Studio if you want the IDE; set a project (e.g. `Meow2Renderer`) as startup project to run or debug it.
  - In Solution Explorer, projects are grouped into **Apps, Engine, Tests, External, Tools**, and each project's **filters mirror the folders** on disk (`Core`, `Core\Win32`, later `Graphics\Rhi\D3D12`, …; see [techstack.md §2](techstack.md)).
- The second builds everything. Executables land in **`build/vs2022/bin/Debug/`** (`Meow2Renderer.exe`, `Meow2Editor.exe`, `Meow2Player.exe`, the test executables).
- The third runs all tests.
- Other configurations: `windows-vs2022-relwithdebinfo` and `windows-vs2022-release`. One solution holds all three.
- Without presets the same is `cmake -S . -B build/vs2022 -G "Visual Studio 17 2022" -A x64`, `cmake --build build/vs2022 --config Debug`, `ctest --test-dir build/vs2022 -C Debug`.

**Updating the solution without building.** After you change a `CMakeLists.txt` (e.g. add a source file), pull changes, or switch branches, refresh `Meow2.sln` and the `.vcxproj` files without compiling anything:
```
cmake --build --preset windows-vs2022-regenerate   # re-runs CMake only if CMake files changed; compiles nothing
cmake --preset windows-vs2022                       # always re-runs CMake (also picks up new options/tools)
```
Visual Studio then asks to **reload** the changed projects. (A normal build also regenerates automatically first, through the `ZERO_CHECK` project; these commands do it without the build.) Source files are listed explicitly in `CMakeLists.txt`, so a new `.cpp` on disk appears only after you add it there.

**After `git pull` this happens automatically:** configuring once points git at the repo's hooks (`.githooks/`). After a pull, rebase or branch switch that changes CMake files, they run the regenerate step, and Visual Studio offers to reload. Details and how to turn it off: [Building](docs/wiki/Building.md#automatic-regeneration-after-git-pull-git-hooks).

**Logs and crash dumps:** every app writes `logs/Meow2<App>_<date>_<time>.log` next to its `.exe`. Started from Visual Studio, logs go to the repository's `logs/` folder instead. If a program crashes, a `.dmp` with the same name appears next to its log (open it in Visual Studio; see [Crash dumps](docs/wiki/CrashDumps.md)). See [Logging](docs/wiki/Logging.md).

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
ctest --preset windows-vs2022-debug                    # Visual Studio solution, any prompt
ctest --preset windows-msvc-debug                      # presets, developer prompt
ctest --preset windows-msvc-debug -L unit              # by label (unit, smoke, lint, ...)
```

More detail: [Building](docs/wiki/Building.md), [Testing](docs/wiki/Testing.md), [CI](docs/wiki/CI.md).

Forgot `--recurse-submodules`? Run `git submodule update --init --recursive`.

## API documentation

**Read it online: https://buttception.github.io/Meow2/** (the API reference for `main`, rebuilt on every merge).

To generate it yourself, for example to preview your own changes:

The engine's public API (`Meow2Engine/include/Meow2/`) is documented with Doxygen comments, and you can generate a browsable HTML reference from it:

1. Install [Doxygen](https://www.doxygen.nl) ≥ 1.9.8 once, then open a **new** command prompt so it's on `PATH`:
   ```
   winget install DimitriVanHeesch.Doxygen
   ```
2. Configure as in [Building](#building), or re-run the configure step if you configured before installing Doxygen. It should print `-- Doxygen <version>: …`.
3. Build the `docs` target:
   ```
   cmake --build --preset windows-vs2022-debug --target docs      # Visual Studio solution
   cmake --build --preset windows-msvc-debug --target docs         # or: presets (developer prompt)
   ```
4. Open **`build/vs2022/docs/html/index.html`** (or `build/<preset>/docs/html/index.html`) in a browser.

Without Doxygen everything else still builds; only the `docs` target and the `Docs.PublicApi.IsDocumented` test are skipped. The comment rules are in [CodeStyle](docs/wiki/CodeStyle.md#documentation-comments-doxygen).

## Profiling

Meow2 is instrumented for the [Tracy](https://github.com/wolfpld/tracy) profiler (from M1): CPU zones, worker threads, locks, memory by tag, and later GPU timings for Vulkan and D3D12.
- Instrumentation is compiled into **Debug** and **RelWithDebInfo** builds and removed from Release. It listens on localhost only, and only when a viewer connects.
- The Tracy **viewer is not included in this repo.** Download `tracy-profiler` from [Tracy's releases](https://github.com/wolfpld/tracy/releases), using the **same version** as the `external/tracy` submodule (see [techstack.md](techstack.md) §4), start the app, and click *Connect*.

## License

See [LICENSE](LICENSE).
