# Building Meow2

## Project structure

| Target | Kind | Folder | Links |
|---|---|---|---|
| `Meow2Engine` (alias `Meow2::Engine`) | static library | `Meow2Engine/` | — |
| `Meow2Renderer` | executable | `Meow2Renderer/` | `Meow2::Engine` |
| `Meow2Editor` | executable | `Meow2Editor/` | `Meow2::Engine` |
| `Meow2Player` | executable | `Meow2Player/` | `Meow2::Engine` |

Inside `Meow2Engine/`:
- `include/Meow2/<Module>/` — public headers, included as `#include <Meow2/Core/Version.h>`
- `src/<Module>/` — private sources (and templates for generated headers)
- generated headers (e.g. `Meow2/Core/Version.h`) are written to the build folder at configure time

Build output goes to `<build dir>/bin` (executables) and `<build dir>/lib` (libraries).

## Options

| Option | Default | Effect |
|---|---|---|
| `MEOW2_BUILD_EDITOR` | ON | Build `Meow2Editor` |
| `MEOW2_BUILD_PLAYER` | ON | Build `Meow2Player` |
| `MEOW2_BUILD_RENDERER` | ON | Build `Meow2Renderer` |

The engine library is always built.

## Presets

`CMakePresets.json` defines one preset per platform × configuration. Each preset builds into its own folder, `build/<preset>/`.

| Platform | Debug | RelWithDebInfo | Release |
|---|---|---|---|
| Windows (MSVC x64, Ninja) | `windows-msvc-debug` | `windows-msvc-relwithdebinfo` | `windows-msvc-release` |
| Linux (Clang, Ninja) | `linux-clang-debug` | `linux-clang-relwithdebinfo` | `linux-clang-release` |

| Config | What it's for |
|---|---|
| Debug | No optimisation, full debug info. Day-to-day debugging. |
| RelWithDebInfo | Optimised, with debug info. Profiling, and analysing crash dumps from optimised code. |
| Release | Fully optimised, with link-time optimisation (`CMAKE_INTERPROCEDURAL_OPTIMIZATION`). Exported games. |

```
cmake --workflow --preset windows-msvc-debug   # configure + build + test
# or step by step
cmake --preset windows-msvc-debug
cmake --build --preset windows-msvc-debug
ctest --preset windows-msvc-debug
```

- Windows presets only appear on Windows, and Linux presets only on Linux. (Workflow presets are listed everywhere, because CMake doesn't support conditions on them; running one for the other OS fails with "disabled configure preset".)
- MSVC needs the Visual Studio environment: use a *Developer PowerShell for VS 2022*, or open the folder in Visual Studio 2022 (it reads the presets and sets that environment up for you).
- Personal tweaks (other compilers, extra cache variables) go in `CMakeUserPresets.json`, which is git-ignored and can `inherit` from the shared presets.

## Building by hand (without presets)

```
cmake -S . -B build/debug -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build/debug
ctest --test-dir build/debug --output-on-failure
```

Building inside the source folder (`cmake -S . -B .`) is refused on purpose.

## Smoke tests

Each executable has a CTest smoke test (`Smoke.<App>.Runs`, label `smoke`). It runs the program and passes only if it exits with 0 **and** prints its name and the engine version. Add one for a new executable with:

```cmake
meow2_add_smoke_test(Smoke.MyApp.Runs MyApp "MyApp.*${PROJECT_VERSION}")
```

## Engine version

`project(Meow2 VERSION x.y.z)` in the top-level `CMakeLists.txt` is the single source of the version. It is written into the generated `Meow2/Core/Version.h`:
- `Meow2::c_versionMajor/Minor/Patch`, `Meow2::c_versionString` — the version the *caller was compiled against*
- `Meow2::getVersionString()` — the version of the *library actually linked*
