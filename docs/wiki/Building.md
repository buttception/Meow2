# Building Meow2

## Project structure

| Target | Kind | Folder | Links |
|---|---|---|---|
| `Meow2Engine` (alias `Meow2::Engine`) | static library | `Meow2Engine/` | — |
| `Meow2Renderer` | executable | `Meow2Renderer/` | `Meow2::Engine` |
| `Meow2Editor` | executable | `Meow2Editor/` | `Meow2::Engine` |
| `Meow2Player` | executable | `Meow2Player/` | `Meow2::Engine` |

Inside `Meow2Engine/`:
- `include/Meow2/<Subsystem>/[<Module>/]` — public headers, included as `#include <Meow2/Core/Version.h>`
- `src/<Subsystem>/[<Module>/][<Backend>/]` — private sources (and templates for generated headers); OS- and library-specific code in the last level (`src/Core/Win32/`)
- At most three folder levels; the subsystem list is in [techstack.md §2](../../techstack.md).
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
- MSVC needs the Visual Studio environment: use a *Developer PowerShell for VS 2022*, or open the folder in Visual Studio 2022 (it reads the presets and sets that environment up for you). To build from a standard prompt instead, see the next section.

## Visual Studio solution (any command prompt)

The presets use the **Ninja** generator. Ninja just runs whatever `cl.exe` is on `PATH`, which is why it needs a developer prompt (`vcvars64.bat` puts the compiler, linker and Windows SDK paths into the environment). CMake's **Visual Studio generator** works differently: it writes a `.sln`/`.vcxproj` and lets MSBuild find the toolchain through the Visual Studio installation. So it works from a plain cmd or PowerShell window:

```
cmake -S . -B build/vs2022 -G "Visual Studio 17 2022" -A x64
cmake --build build/vs2022 --config Debug
ctest --test-dir build/vs2022 -C Debug --output-on-failure
```

| | Ninja presets | Visual Studio solution |
|---|---|---|
| Needs a developer prompt | yes | no |
| Configurations | one per build folder (`build/<preset>/`) | all in one (`--config` / `-C` picks one) |
| Output | `build/<preset>/bin/` | `build/vs2022/bin/<Config>/` |
| IDE | open the folder in VS | open `build/vs2022/Meow2.sln` |
| Used by CI | yes | no |

- `-A x64` matters: without it the generator may pick the 32-bit platform.
- Multi-config generators need `--config` when building and `-C` when testing; without `-C`, `ctest` can't find the test executables.
- `Format.Check` also runs here: CMake looks for clang-format inside the Visual Studio installation the generator chose (`CMAKE_GENERATOR_INSTANCE`).

### Presets for the solution

`CMakePresets.json` has the same thing as presets, so you don't have to remember the flags:

| Command | Does |
|---|---|
| `cmake --preset windows-vs2022` | generate/refresh `build/vs2022/Meow2.sln` (always re-runs CMake) |
| `cmake --build --preset windows-vs2022-regenerate` | re-run CMake **only if** a CMake file changed, and update the `.sln`/`.vcxproj`; **compiles nothing** |
| `cmake --build --preset windows-vs2022-debug` (`-relwithdebinfo`, `-release`) | build one configuration |
| `ctest --preset windows-vs2022-debug` (…) | run the tests of that configuration |

**How regeneration works:** CMake records every file it read while configuring (`CMakeLists.txt`, `cmake/*.cmake`, `Version.h.in`, …). The Visual Studio generator adds a small project, **`ZERO_CHECK`**, that compares those files' timestamps with the last run and re-runs CMake if any changed. Every normal build runs `ZERO_CHECK` first. The `regenerate` preset builds **only** `ZERO_CHECK`, so it refreshes the solution without compiling. CMake only rewrites a `.vcxproj` whose content actually changes, so Visual Studio asks to reload just those projects.

Source files are listed explicitly in `CMakeLists.txt` (no globbing), so creating a `.cpp` on disk changes nothing until it's added to a `target_sources()` list. That edit is what triggers the regeneration.

### Filters and solution folders

`cmake/Meow2SourceGroups.cmake` makes Solution Explorer mirror the disk:
- **Filters:** each file's filter is its folder below `include/Meow2/`, `src/` or `tests/`, so `Core\Assert.h` and `Core\Assert.cpp` sit together under `Core`, and `src/Core/Win32/DebuggerWin32.cpp` is under `Core\Win32`. There are at most three levels (Subsystem\Module\Backend); anything deeper is shown at the third level. Generated headers (e.g. `Version.h` in the build folder) go to their module's filter too.
- **Solution folders:** `Engine` (Meow2Engine), `Apps` (Renderer, Editor, Player), `Tests` (test executables, warning probe), `External` (googletest), `Tools` (`format`, `format-check`, `docs`). CMake's own `ALL_BUILD`, `ZERO_CHECK` and `RUN_TESTS` are in `CMakePredefinedTargets`.
- New targets call `meow2_source_groups(<target>)` at the end of their `CMakeLists.txt` and set `FOLDER` (AGENTS.md §3).
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

### How the version changes

| Version | Released when | Tag |
|---|---|---|
| 0.1.0 | M0 (foundation) done | `v0.1.0` |
| 0.2.0 | M1 (core runtime) done | `v0.2.0` |
| 0.N+1.0 | milestone N done | `v0.N+1.0` |
| 0.Y.Z (Z > 0) | a fix to the already-released 0.Y | `v0.Y.Z` |

- Only the PR that closes a milestone changes `VERSION`. Normal feature PRs never touch it, so they never conflict on that line.
- While M1 is being worked on, `main` still says 0.1.0. To tell such builds apart, the version string will carry the git commit as build metadata, e.g. `0.1.0+g1a2b3c4` (planned for M1).
- `git describe --tags` shows where a checkout is relative to the last release, e.g. `v0.1.0-12-g1a2b3c4` = 12 commits after 0.1.0.
- Full rules: [techstack.md §21](../../techstack.md) and [AGENTS.md §2.7](../../AGENTS.md).
