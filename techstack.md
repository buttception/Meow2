# Meow2 — Tech Stack

> Source of truth for *what* Meow2 is built with and *how it is laid out*.
> Process rules (branches, issues, PRs, permissions) live in [AGENTS.md](AGENTS.md).
> Items marked **⚠ OPEN** are decisions still waiting on the project owner.

---

## 1. Targets & platforms

| Item | Choice |
|---|---|
| Primary platform | Windows 10/11 x64 — Vulkan + Direct3D 12 |
| Planned platforms | Linux x64 (Ubuntu, X11/Wayland) — Vulkan; Wine/Proton (Vulkan backend runs via winevulkan, D3D12 via vkd3d-proton); Android arm64 — Vulkan |
| Language | C++20 (no C++20 modules — header/source only) |
| Compilers | MSVC 2022 (primary), Clang/GCC (Linux), Android NDK Clang |
| Build system | CMake ≥ 3.25 with `CMakePresets.json`, Ninja generator |
| Build configurations | `Debug`, `RelWithDebInfo`, `Release` |
| Code style | clang-format 19 (`.clang-format`; pinned, see [CodeStyle](docs/wiki/CodeStyle.md)), `.editorconfig` for all text files, warnings as errors via `meow2_set_warnings()` — see [docs/wiki/CodeStyle.md](docs/wiki/CodeStyle.md) |
| Tests | CTest + GoogleTest (`meow2_add_test_suite()`), see [docs/wiki/Testing.md](docs/wiki/Testing.md) |
| Dependencies | Git submodules in `external/`, pinned to release tags (see §4). Exceptions: Qt (installer), Vulkan SDK (installer), DXC (prebuilt binaries) |

**Portability rules** (so Linux/Android stay possible later):
- Platform-specific code only in `Win32`/`Linux`/`Android` backend folders under `src/` (e.g. `src/Core/Win32/`, `src/Platform/Window/Win32/`), behind an engine API (§2 "Engine folders").
- No `<windows.h>` outside Win32 files. No MSVC-only extensions in shared code.
- Paths go through the VFS (§3), never hard-coded `\` separators.
- D3D12 is Windows-only. Vulkan is the cross-platform backend and must never depend on Win32 types.

---

## 2. CMake projects

| Target | Type | Purpose | Links |
|---|---|---|---|
| `Meow2Engine` | static lib | All runtime code | `external/` libs |
| `Meow2Renderer` | exe | Lightweight app to test rendering & shaders and **showcase engine capabilities**. Dev UI: Dear ImGui (docking). Loads glTF sample scenes. | `Meow2Engine`, imgui |
| `Meow2Editor` | exe | Qt 6.8 editor: scene editing, asset import, game export. **Fully drivable through an MCP server** (§20), so AI agents can build in it | `Meow2Engine`, Qt6 |
| `Meow2Player` | exe | Standalone game runtime the editor bundles on export. Uses the built-in UI. No ImGui, no Qt. | `Meow2Engine` |
| `*Tests` | exe | One GoogleTest executable per module (e.g. `Meow2EngineCoreTests`), registered with CTest | `Meow2Engine`, `GTest::gtest_main` |

The rendering code (RHI, render graph, passes) lives in `Meow2Engine`. `Meow2Renderer` only drives and displays it.

### Directory layout

```
Meow2/
├─ CMakeLists.txt
├─ CMakePresets.json
├─ cmake/                    # Meow2Warnings.cmake, Meow2Tests.cmake, Meow2Shaders.cmake, ...
├─ external/                 # git submodules, pinned (see §4)
├─ Meow2Engine/
│  ├─ include/Meow2/<Subsystem>/[<Module>/]...         public headers
│  ├─ src/<Subsystem>/[<Module>/][<Backend>/]...       private code; backends/OS code in the last level
│  └─ tests/<Subsystem>/[<Module>/]...
├─ Meow2Renderer/
├─ Meow2Editor/
├─ Meow2Player/
├─ shaders/                  # HLSL
├─ assets/
│  └─ gltf-sample-assets/    # git-ignored; filled on demand (§13)
├─ tests/visual/goldens/     # approved reference images per scene/backend
├─ docs/wiki/
├─ techstack.md
└─ AGENTS.md
```

### Engine folders: Subsystem / Module / Backend

Engine code is grouped by **subsystem**, then **module**, then (in `src/` only) **backend or OS**. That's at most **three folder levels** below `include/Meow2/`, `src/` and `tests/`. The Visual Studio filters mirror these folders exactly (`cmake/Meow2SourceGroups.cmake`), so Solution Explorer and the disk always look the same.

| Subsystem | Modules (folder) | Backends / OS folders (in `src/`) |
|---|---|---|
| `Core` | files directly in `Core/` (Platform, Version, Assert, Debugger), `Memory`, `Log`, `Crash`, `Config`, `Time`, `Events`, `Jobs`, `Profiling` | `Win32`, `Linux`, `Android`; library wrappers such as `Spdlog`, `Enkits`, `Tracy` |
| `Platform` | `Window`, `Input`, `FileSystem` | `Sdl`, `Win32`, `Linux`, `Android` |
| `Math` | files directly in `Math/` | — |
| `Graphics` | `Rhi`, `Shader`, `Renderer` | `Vulkan`, `D3D12` (Rhi), `Dxc` (Shader) |
| `Assets` | `Gltf`, `Registry` | — |
| `Physics` | files directly in `Physics/` | `Jolt` (later `PhysX`, `Box3D`) |
| `GameUi` | files directly in `GameUi/` | — |
| `Packaging` | files directly in `Packaging/` | — |

Examples:

| File | Include / filter |
|---|---|
| `include/Meow2/Core/Assert.h` | `#include <Meow2/Core/Assert.h>`, filter `Core` |
| `src/Core/Win32/DebuggerWin32.cpp` | filter `Core\Win32` |
| `include/Meow2/Core/Memory/BlockArena.h` | `#include <Meow2/Core/Memory/BlockArena.h>`, filter `Core\Memory` |
| `include/Meow2/Graphics/Rhi/IRhiDevice.h` | filter `Graphics\Rhi` |
| `src/Graphics/Rhi/D3D12/RhiDeviceD3D12.cpp` | filter `Graphics\Rhi\D3D12` |

- A module's headers and sources share one filter (no "Header Files"/"Source Files" split).
- Namespaces follow subsystem/module where it helps (`Meow2::Memory`, `Meow2::Rhi`). They don't have to repeat every folder level.
- Don't go deeper than three levels. If a module grows, split it into two modules.
- Targets are grouped in **solution folders**: `Engine`, `Apps`, `Tests`, `External`, `Tools`.

### CMake options

| Option | Default | Effect |
|---|---|---|
| `MEOW2_RHI_VULKAN` | ON | Vulkan backend |
| `MEOW2_RHI_D3D12` | ON on Windows, forced OFF elsewhere | D3D12 backend |
| `MEOW2_BUILD_EDITOR` | ON (desktop only) | Requires Qt 6.8 |
| `MEOW2_BUILD_RENDERER` | ON | |
| `MEOW2_BUILD_PLAYER` | ON | |
| `MEOW2_BUILD_TESTS` | ON | |
| `MEOW2_GPU_TESTS` | OFF | Tests needing a GPU (labels `gpu`, `visual`) |
| `MEOW2_PHYSICS_BACKEND` | `Jolt` | `Jolt` now; `PhysX`, `Box3D` reserved |

### Presets
`windows-msvc-{debug,relwithdebinfo,release}`, `linux-clang-{debug,relwithdebinfo,release}`, later `android-arm64-*`.

```
cmake --preset windows-msvc-debug
cmake --build --preset windows-msvc-debug
ctest --preset windows-msvc-debug --output-on-failure
ctest --preset windows-msvc-debug -L unit
```

---

## 3. Engine modules (inside `Meow2Engine`)

| Module | Interface(s) | Implementation | Notes |
|---|---|---|---|
| Core | — | `Platform.h` (platform/compiler/arch/config macros), asserts, `Result<T>` (= `tl::expected`), handles (with the RHI) | Containers: `std::pmr` ones, no custom containers |
| Memory | `std::pmr::memory_resource` — the one virtual allocator interface (no separate `IAllocator`) | `TrackingAllocator`, `MallocResource`, `BlockArena`, `FrameArena` (M5) | See §6 |
| Log | `Logger`, `LogSink` | spdlog | §7 |
| Crash | `CrashHandler` | Win32 minidumps; Linux/Android later (e.g. crashpad/breakpad) | §8 |
| Config | `Config` | TOML via **toml++** | defaults → `config.toml` → command line; write-back |
| Time | `FrameClock` | `std::chrono::steady_clock` | §23 |
| Events | `EventBus` | in-house | §22 |
| Jobs | `JobSystem` | **enkiTS** | §19 |
| Profiling | `MEOW2_PROFILE_*` macros | **Tracy** | Debug/RelWithDebInfo only; compiled out of Release |
| Platform / Window | `Platform`, `Window` | **SDL3** (§9) | One window; the editor's windows are Qt's |
| Input | `InputSystem` + input sources | SDL3 sources (engine), Qt source (editor) | §9 |
| FileSystem | `FileSystem` / VFS | native files; packages later; Android `AAssetManager` later | |
| Math | aliases + inline functions (no virtuals) | DirectXMath | §10 |
| RHI | `IRhiDevice`, `IRhiCommandList`, `IRhiSwapchain`, resources by handle | Vulkan, D3D12 | §11 |
| Shader | `ShaderCompiler` | DXC (HLSL → DXIL / SPIR-V) | Interface leaves room for a custom shader language (§12) |
| Renderer | `Renderer`, render graph | on RHI only | |
| Assets | `AssetImporter`, `AssetRegistry` | glTF **2.0** importer, `.gltf` and `.glb` (library **⚠ OPEN**: fastgltf / cgltf / tinygltf) | §13 |
| Physics | `PhysicsWorld`, `RigidBody`, `Shape`, `PhysicsDebugDraw` | **Jolt** | PhysX / Box3D selectable at build time later (§14) |
| GameUI | `UiContext`, widgets | in-house, drawn via Renderer | Used by `Meow2Player` |
| Packaging | `GamePackager` | in-house | Editor export |

**Interface policy — build-time selection everywhere, except the graphics API**

| Subsystem | Selected | Mechanism |
|---|---|---|
| RHI (Vulkan / D3D12) | **at runtime**, hot-swappable | Virtual `IRhi*` interfaces, both backends compiled in, factory `createRhiDevice(RhiBackend, desc)` (§11) |
| Everything else (window, physics, logging, crash, filesystem, shader compiler, asset importers, memory) | **at build time** | One public header per subsystem declares a concrete class (e.g. `Meow2::Physics::PhysicsWorld`) with a private `Impl` (pimpl). A CMake option (`MEOW2_PHYSICS_BACKEND=Jolt`) chooses which `src/<Subsystem>/<Module>/<Backend>/` sources are compiled. No virtual calls. |

- Backend contracts are written down in the header, and enforced by shared "contract" tests that every backend must pass.
- Tests that need a fake build with a `Null`/`Fake` backend (e.g. `MEOW2_PHYSICS_BACKEND=Null`), instead of mocking through virtuals.
- Third-party types never appear in public engine headers (except DirectXMath in `Meow2/Math`).
- The one other place with virtual calls is memory: allocators implement `std::pmr::memory_resource`, whose interface the C++ standard defines as virtual (§6).

---

## 4. Third-party dependencies (git submodules)

All libraries are git submodules under `external/`, checked out at a **release tag** and frozen there.
Upgrading one is its own `chore/` PR. **Adding any submodule requires owner permission.**

| Library | Use | License | How |
|---|---|---|---|
| SDL3 | window, input, gamepad | zlib | submodule |
| DirectXMath | math | MIT | submodule (header-only; needs a `sal.h` shim off Windows) |
| spdlog v1.17.0 | logging (bundled fmt; static library, no default logger, wchar file names on Windows) | MIT | submodule |
| JoltPhysics | physics | MIT | submodule |
| Dear ImGui (docking branch) | Meow2Renderer UI | MIT | submodule |
| Vulkan-Headers, volk | Vulkan headers + loader-less function loading | Apache-2.0 / MIT | submodule |
| VulkanMemoryAllocator | VK memory | MIT | submodule |
| D3D12MemoryAllocator | D3D12 memory | MIT | submodule |
| DirectX-Headers / Agility SDK | D3D12 headers, newer features | MIT / MS | submodule / NuGet package (**⚠ OPEN**) |
| glTF loader (**⚠ OPEN**) | asset import | MIT | submodule |
| GoogleTest v1.17.0 | unit tests (gmock off) | BSD-3 | submodule |
| tl::expected | `Result<T>` until C++23 `std::expected` | CC0 | submodule (M1) |
| toml++ | config files | MIT | submodule (M1) |
| enkiTS | job system | zlib | submodule (M1) |
| Tracy (client only) | CPU/GPU/memory profiler | BSD-3 | submodule (M1). The **viewer is not in the repo**: download it from Tracy's GitHub releases, same version as the submodule |
| **Not submodules** | | | |
| Qt 6.8 LTS | Meow2Editor | LGPLv3 (link dynamically) | Qt online installer; `CMAKE_PREFIX_PATH` |
| Vulkan SDK | validation layers, tools | — | LunarG installer (dev machines only) |
| DXC | shader compiler | NCSA/MIT | prebuilt release binaries from Microsoft (or the copy in the Vulkan SDK) — not built from source |
| Doxygen ≥ 1.9.8 | API documentation (`docs` target, §16.1) | GPL-2.0 (tool only, not linked) | `winget install DimitriVanHeesch.Doxygen` (Windows), `apt install doxygen` (Linux CI). Optional locally: the `docs` target and test are skipped if it's missing; required in CI |

Clone with: `git clone --recurse-submodules`. glTF sample assets are **not** submodules — they are fetched on demand (§13).

---

## 5. Build configurations

| Config | Optimisation | Asserts | Logging | Console window | Memory tracking | Tracy | Crash dumps | Use |
|---|---|---|---|---|---|---|---|---|
| Debug | off | on | debug+ | yes | yes | yes | yes | day-to-day debugging |
| RelWithDebInfo | on (`/O2`) | on (cheap ones) | debug+ | yes | yes | yes | yes | profiling, testing, crash-dump analysis |
| Release | full (`/O2 /GL`, LTO) | off | info+ | no (GUI subsystem) | no | compiled out | yes | exported games, release zips |

PDBs are kept for every build, so crash dumps can be symbolicated. Release zips include the PDBs of that build (§21), so dumps from players can be read later.

---

## 6. Memory

**The allocator interface is `std::pmr::memory_resource`** (C++17). Every engine allocator derives from it, so any `std::pmr` container (`std::pmr::vector`, `std::pmr::string`, `std::pmr::unordered_map`, …) can allocate from it. The container type doesn't change with the allocator (`std::pmr::vector<int>` whatever backs it), and nested containers inherit the resource automatically. Hot internal code may still use a typed `std::allocator`-style adapter over the same resource if profiling shows the virtual call matters.

```
                    ┌──────────────── TrackingAllocator (thread-safe; per-MemTag counts, peak, leak report) ───┐
                    │                                        │                                                │
  library hooks (SDL, Jolt, ImGui,     global operator new/delete           BlockArena blocks
  enkiTS, VMA/D3D12MA CPU callbacks)   (spdlog, toml++, Tracy, std, …)      (tag of the arena)
  → their own MemTag                   → thread-local current tag                   │
                                         (MemTagScope s{MemTag::Logging};)          ▼
                                                                    std::pmr containers, frame data
                    └───────────────────────────── MallocResource (upstream: malloc/free) ──────────────────┘
```

- **`MallocResource`**: upstream of everything, calls `malloc`/`free` directly (never `new`, so the override can't recurse).
- **`TrackingAllocator`** (Debug/RelWithDebInfo): wraps an upstream resource. Each block gets a small header (magic, size, tag), and counters are kept per `MemTag` (current, peak, allocation count). At shutdown, after static destructors, it prints a leak report by tag. In Release, `MallocResource` is used directly with no headers.
- **`MemTag`**: one per subsystem (`General`, `Logging`, `Config`, `Jobs`, `Events`, `Platform`, `Renderer`, `Rhi`, `Physics`, `Assets`, `Editor`, `ImGui`, `Profiler`, …). The stats feed the memory window in Meow2Renderer and Tracy's memory view.
- **Libraries with an allocator hook** (SDL `SDL_SetMemoryFunctions`, Jolt `JPH::Allocate/Free`, ImGui `SetAllocatorFunctions`, enkiTS custom allocator, VMA/D3D12MA CPU callbacks) call `TrackingAllocator` directly with their own tag. Most hooks are C functions without a user-data pointer, so they reach the allocator through **one** `// GLOBAL:` pointer set and cleared by the composition root (§18).
- **Everything else** (spdlog, toml++, Tracy, `std::make_unique`, …) goes through a **global `operator new`/`delete` override** (Debug/RelWithDebInfo), tagged with the thread-local current tag (`MemTagScope` sets it for a scope, default `General`). No double counting: hooked libraries never go through `new`.
  - It replaces every variant: plain, array, aligned (`std::align_val_t`), `nothrow`, and sized delete.
  - It works before `main` and after it returns by falling back to `MallocResource`; the header magic tells `delete` which path a block came from.
  - It's defined in a source file compiled into **each executable** (via the CMake helper that creates Meow2 executables), never in the static library, where the linker could silently drop it.
- **`BlockArena`** (`std::pmr::memory_resource`, not thread-safe; one per thread where needed):
  - **Size-class block lists:** requests ≤ 8 KiB go into 64 KiB blocks, ≤ 128 KiB into 1 MiB blocks, larger ones get a dedicated block of exactly their size. So the unused tail of a block is at most ~12.5 % of it. Unused bytes are accepted; blocks are never combined.
  - **Allocate** bumps a pointer in the current block of the size class. If the request doesn't fit, it moves to the next block in that list (reusing an existing one) or allocates a new block from upstream and links it.
  - **`deallocate` is a no-op**; memory comes back only on `reset()`. Containers that grow (`vector::push_back`) leave their old buffers behind until reset, so `reserve()` up front where the size is known.
  - **`reset()`** rewinds every list to its first block and **keeps the blocks**, so after warm-up an arena makes no upstream calls at all. Dedicated large blocks are kept for reuse when a request of the same or smaller size class appears again.
  - **`trim(keepBytes)`** frees blocks beyond the recent high-water mark, so a one-off spike (e.g. a level load) doesn't hold memory forever.
  - **No defragmentation needed:** nothing is freed individually, so no holes form inside an arena, and the upstream heap sees few, same-sized, long-lived blocks. Defragmentation is a topic for long-lived GPU heaps (VMA/D3D12MA), not for arenas.
  - Every arena has a `MemTag`; its blocks are counted under that tag.
- **`FrameArena`** (M5, with the renderer): `framesInFlight × workerThreads` `BlockArena`s. `beginFrame(frameIndex)` resets that frame's arenas after the frame's GPU fence has signalled; `current()` returns the calling thread's arena (thread index from the job system). Frame-scoped data only: a container must never outlive the reset of the arena it uses.
- Pool and stack allocators are added when a concrete user appears.
- GPU memory goes through VMA / D3D12MA, not these allocators. The per-frame **upload ring buffer** (M5) is the GPU-side twin of the frame arena: CPU-writable GPU memory, reset by the same fence.

## 7. Logging

- spdlog behind `Logger`. **No global/default logger** (spdlog's global registry is not used): the composition root creates the `Logger`, and subsystems receive it (or a per-subsystem `LogChannel`) by injection (§18).
- **Five levels:** `debug`, `info`, `warn`, `error`, `fatal` (spdlog's trace is not used; `critical` is printed as `fatal`). Macros take the logger explicitly: `MEOW2_LOG_{DEBUG,INFO,WARN,ERROR,FATAL}(m_log, fmt, ...)`. `debug` is compiled out of Release (§5).
- **`fatal` ends the program:** it logs, flushes every sink, breaks into the debugger in Debug, then crashes on purpose through the crash handler, so a `.dmp` is written (§8). It's `[[noreturn]]`.
- **Format:** `[14:03:27.512]<warn> Engine: Swapchain recreated (1600x900)`, i.e. spdlog pattern `[%H:%M:%S.%e]<%l> %n: %v`, 24-hour local time with milliseconds.
- **Logger names = which binary is logging.** The engine is a static library inside each executable, so this is done with names, not detection: the composition root creates an `Engine` logger (injected into engine code) and an app logger (`Editor`, `Renderer`, `Player`). Both write to the same sinks. Sub-names such as `Engine.RHI` are possible later.
- **Sinks:**
  - **File:** one file per run, `Meow2<App>_<yyyy-mm-dd_hh-mm-ss>.log`, in the log folder, rotating by size. All configs.
  - **Console (stdout, colour):** Debug and RelWithDebInfo only. Release executables are built with the Windows GUI subsystem (`WIN32_EXECUTABLE` for Release), so there's no console window.
  - **In-app console:** thread-safe ring buffer (`ConsoleRingSink`) designed in M1, shown as an ImGui window in Meow2Renderer (M6) and a Qt dock in Meow2Editor (M9). Qt widgets are updated only on the GUI thread (queued connection).
  - Android `logcat` sink later.
- **Log folder:** `logs/` **next to the executable** by default. A `--log-dir <path>` command-line option (or `log.dir` in the config) overrides it. When the executable is launched from the Visual Studio solution, CMake sets the debugger's command arguments (`VS_DEBUGGER_COMMAND_ARGUMENTS`) to `--log-dir <repo>/logs`, so runs from the IDE log into the repo's `logs/` folder (git-ignored). Run directly, the same exe logs next to itself.
- Loggers are thread-safe (`_mt` sinks). The crash handler flushes all loggers.
- **Implemented (M1):** `Meow2/Core/Log/` — `LogSystem` (sinks, `createLogger`), `Logger` (`std::format` messages, `[[noreturn]] fatal`), `ScopedAssertLogging` (failed asserts → `error` + flush). Messages are formatted with `std::format` and passed to spdlog as plain text; `warn` and above flush immediately. See [docs/wiki/Logging.md](docs/wiki/Logging.md).

## 8. Crash dumps (Windows)

- `CrashHandler::install()` hooks `SetUnhandledExceptionFilter`, `_set_purecall_handler`, `_set_invalid_parameter_handler`, `std::set_terminate`, and `SIGABRT`.
- `MiniDumpWriteDump` runs on a pre-created watchdog thread. Output: `<log folder>/Meow2<App>_<timestamp>.dmp`, next to that run's log.
- **All configurations, Release included**: Release is what players run, so its dumps matter most. Release zips ship the matching PDBs (§21).
- Not written when a debugger is attached (the debugger gets the exception instead).
- Tested by a `windows`-labelled test that crashes a child process on purpose and checks the `.dmp` exists.
- GPU faults: D3D12 DRED, Vulkan `VK_EXT_device_fault` where available. Logged on device loss.

## 9. Windowing — SDL3

- **SDL3** behind `Window` / `Input` / `Platform` (build-time selection). Covers Windows, Linux (X11/Wayland) and Android. No GLFW, no GLEW (GLEW is an OpenGL loader; we don't use OpenGL).
- Windows are created API-neutral. The RHI creates its surface/swapchain from the native handle (`HWND`, X11/Wayland, `ANativeWindow`), so the same window survives a Vulkan ↔ D3D12 hot swap.
- **Dockable windows (future):**
  - Meow2Renderer: Dear ImGui docking branch + multi-viewports (tool windows can be dragged out into real OS windows; ImGui has an SDL3 platform backend for this).
  - Meow2Editor: Qt `QDockWidget`s. The viewport is a Qt widget, and the RHI renders into its native handle. **The editor never creates SDL windows**: Qt owns them.
- Meow2Player and Meow2Renderer: one SDL3 window, fullscreen/windowed. Window events (resize, close, focus, DPI) are published on the `EventBus` (§22).

### Input

One engine input state, read the same way in all three executables. Small adapters ("input sources") translate each backend's events into it:

```
Player / Renderer:   SDL events ───► SdlInputSource   ─┐
Editor viewport:     Qt events  ───► QtInputSource    ─┼─► InputSystem::submit(InputEvent)
All three:           SDL gamepads ─► SdlGamepadSource ─┘            │
                                                                    ├─► polling: isDown(Key::W), wasPressed(Key::Space),
                                                                    │            mouseDelta(), axis(GamepadAxis::LeftX)
                                                                    └─► events on the EventBus: KeyPressed, KeyReleased,
                                                                        MouseButtonChanged, MouseMoved, GamepadConnected, TextInput
```
- **Engine types** (`Meow2/Input`, no SDL/Qt types): `Key` (USB HID scan codes = physical key, layout-independent, the same codes SDL uses), `MouseButton`, `GamepadButton`, `GamepadAxis`, and `InputEvent` (plain data).
- **`InputSystem`** (concrete, injected): `submit()` is called by the sources; `beginFrame()` turns the queued events into this frame's state and publishes the changes as input events on the `EventBus`. Gameplay **polls**. Code that cares about a *change* (a UI reacting to a key press, rebinding, "gamepad connected") subscribes to the events.
- **Fixed timestep:** a press is latched until the next `fixedUpdate` consumes it, so it's never lost when `fixedUpdate` runs 0 times in a frame and never seen twice when it runs several times (§23).
- **Sources are translators, not virtual interfaces.** Each holds an `InputSystem&` and calls `submit()`.
  - `SdlInputSource`, `SdlGamepadSource`: engine (`src/Platform/Sdl/`). Gamepads aren't tied to a window, so `SdlGamepadSource` also works in the editor.
  - `QtInputSource`: **editor code** (Qt never enters the engine). It's an event filter on the viewport widget that forwards keyboard and mouse events **only while the viewport has focus**, mapping Qt's native scan codes to `Key`.
- **Mouse capture** (relative mode) is done by the active source: SDL relative mouse mode, or a Qt cursor hide + warp.
- **Editor rule:** editor actions (Ctrl+Z, Delete, gizmo shortcuts) are Qt shortcuts that issue editor commands (§20) and never go through `InputSystem`. `InputSystem` serves the game (play mode, future scripting) and the viewport camera.
- **Later:** an action layer ("Jump" = Space or gamepad A) on top of `InputSystem`, with bindings in the TOML config.

## 10. Math & coordinates

- DirectXMath. `XMFLOAT*` for storage, `XMVECTOR`/`XMMATRIX` for computation (`FXMVECTOR` parameter rules). SSE on x64, NEON on arm64.
- **Left-handed: +X right, +Y up, +Z forward.**
- Depth range **0..1**, **reversed infinite Z**: near plane maps to 1, infinity maps to 0. Depth buffer `D32_FLOAT`, clear to 0, compare `GREATER_EQUAL`. A custom `perspectiveReversedInfiniteLH(fovY, aspect, nearZ)` (not available in DirectXMath).
- Vulkan: negative-height viewport flips Y so both backends share the same matrices.
- glTF is right-handed (+Y up, +Z forward, +X left): the importer converts on load (negate X, fix winding).

## 11. RHI

- Explicit model (queues, command lists, fences, descriptor heaps/sets, explicit barriers). Direct3D 12 + Vulkan 1.3 (dynamic rendering, synchronization2).
- Order: Vulkan first, then D3D12 behind the same interface.
- **Runtime hot swap** (Vulkan ↔ D3D12, Windows only):
  1. A switch is requested; it runs at the next frame boundary.
  2. Wait for the GPU to go idle, then destroy every GPU object, the swapchain and the device.
  3. Create the new device and swapchain on the **same window**.
  4. Recreate all GPU resources from their **CPU-side descriptions** (the renderer keeps a resource registry). Shaders are already compiled for both (DXIL + SPIR-V).
  - Rule this implies: nothing above the RHI may cache backend-native handles or pointers. Only engine handles, which are re-bound after the swap.
  - It costs a hitch of seconds, not a seamless switch. That is fine for comparing backends in Meow2Renderer.
- Resources are referenced by handles owned by the device.
- Debug: validation layers / D3D12 debug layer, object names, validation messages routed to the `RHI` logger.

## 12. Shaders

- Authored in **HLSL**. Compiled with **DXC** → DXIL (D3D12) and SPIR-V (Vulkan).
- `ShaderCompiler` takes source + entry + stage + defines and returns bytecode + reflection. DXC is one implementation, so a **future custom shader language** can either compile to HLSL (and reuse DXC) or produce SPIR-V/DXIL directly.
- Offline compilation via a CMake step. Runtime hot reload in Meow2Renderer.

## 13. Rendering verification (glTF sample scenes)

- Format: **glTF 2.0 only**, both `.gltf` (+ external `.bin`/images) and `.glb` (binary). glTF 1.0 is not supported.
- Test scenes: Khronos **glTF-Sample-Assets**, pinned to a commit.
- **Fetched on demand, not cloned up front.** `cmake -P cmake/FetchGltfSample.cmake <ModelName>` (also called by visual tests and by Meow2Renderer when a scene is missing):
  - on first use, does a blobless, sparse clone of the pinned commit into `assets/gltf-sample-assets/` (git-ignored);
  - then adds only `Models/<ModelName>/` to the sparse checkout, so only that model is downloaded.
- Meow2Renderer has a scene browser listing the sample models, marking which are downloaded, and can open any local `.gltf` / `.glb`.
- **Visual regression tests**: render a scene headlessly from a fixed camera, save PNG, and compare with an owner-approved golden image (per backend) using a tolerance (per-pixel threshold + % of pixels, or FLIP). CTest label `visual`. A new golden is only accepted after the owner has looked at it.

## 14. Physics

- **Jolt** first. `PhysicsWorld` etc. are designed around concepts shared by Jolt, PhysX and Box3D (bodies, shapes, layers/filters, queries, character controller), so other backends can be swapped in.
- Engine-side types only in the interface (no `JPH::` types); conversion happens in `src/Physics/Jolt/`.

## 15. Game export

1. The editor cooks assets into a package (format **⚠ OPEN**).
2. It copies a prebuilt `Meow2Player` (per target platform) plus the package into the output folder.
3. Player mounts the package via the VFS and loads the start scene.

## 16. Naming conventions

| Kind | Style | Example |
|---|---|---|
| Types (class/struct/enum/alias) | PascalCase | `RhiDevice`, `TextureDesc` |
| Interfaces (runtime-virtual only: the RHI) | `I` + PascalCase | `IRhiDevice` |
| Functions / methods | camelCase | `createBuffer()` |
| Parameters | camelCase | `frameIndex` |
| Local variables | camelCase | `viewMatrix` |
| Member variables | `m_` + camelCase | `m_frameIndex` |
| Fields of plain data structs | camelCase, **no** `m_` | `transform.position` |
| Static variables (members, locals, file scope) | `s_` + camelCase | `s_instanceCount` |
| `const` / `constexpr` variables | `c_` + camelCase | `c_maxFramesInFlight` |
| Enum values | PascalCase | `RhiBackend::Vulkan` |
| Macros | `MEOW2_UPPER_SNAKE` | `MEOW2_ASSERT` |
| Namespaces | `Meow2`, `Meow2::Rhi` | |
| Files | PascalCase matching the main type | `RhiDevice.h` |

- Prefix precedence: `c_` > `s_` > `m_`. A `const` local is `c_`; a `static constexpr` member is `c_`.
- **Plain data structs** hold only data: a `struct` with public non-static data members, no member functions (defaulted comparison operators are fine) and no invariant to protect. Their fields drop `m_`: `struct Transform { XMFLOAT3 position; XMFLOAT4 orientation; XMFLOAT3 scale; };` is used as `transform.position`. `<Name>Deps` structs (§18) are plain data too. As soon as a type gets a member function, private data or an invariant, it's class-like and its members use `m_`. `static`/`const` members of a plain data struct still use `s_`/`c_`.
- Loop counters `i`, `j`, `k` are exempt.
- No leading underscores on any identifier.

### 16.1 Documentation comments (Doxygen)

All code is documented, and the public API (`include/Meow2/**`) is documented in **Doxygen** syntax, so an API reference can be generated (`docs` target, M1).

- **Style:** `///` line comments (C++ style; clang-format keeps them aligned) with `@`-commands. A comment goes **directly above** what it documents; `///<` is for trailing comments on struct fields and enum values.
- **Required on every public declaration:**
  - **File:** `@file` plus `@brief`, with what the header is for.
  - **Class/struct:** `@brief` (one sentence), then details. A **`@par Thread safety`** paragraph is mandatory (§19), and so is ownership or lifetime when it matters (who owns it, what it must outlive).
  - **Function:** `@brief`, `@param` for each parameter (with `[in]`/`[out]` when it isn't obvious), `@return`, `@pre` and `@post` for contracts that are checked by asserts, plus `@note` or `@warning` for pitfalls.
  - **Fields, enum values, constants, macros:** a `@brief` or a trailing `///<`.
  - Template parameters: `@tparam`.
- **Content:** say what it does and why, and its units and ranges (seconds, pixels, bytes, 0..1), not a restatement of the name. `@see` links to related APIs or techstack sections.
- **Private code** (`src/`): the same style for classes and non-trivial functions (Doxygen can be run with `EXTRACT_PRIVATE` for internal docs), and `//` comments for the *why* of non-obvious code.
- **Generated headers** (`Version.h.in`) and macros (`Platform.h`) are documented too.

```cpp
/// @file
/// @brief Fixed-block bump allocator for short-lived allocations.

namespace Meow2::Memory
{
/// @brief Arena that hands out memory by bumping a pointer through size-class block lists.
///
/// Individual deallocations are no-ops; all memory is reclaimed at once by reset().
/// @par Thread safety
/// Not thread-safe. Use one arena per thread (see FrameArena).
/// @see techstack.md §6
class BlockArena final : public std::pmr::memory_resource
{
public:
    /// @brief Creates an empty arena; no memory is allocated until the first request.
    /// @param upstream Resource the blocks are allocated from. Must outlive the arena.
    /// @param tag      Memory tag the blocks are counted under.
    BlockArena(std::pmr::memory_resource& upstream, MemTag tag);

    /// @brief Rewinds all block lists to their first block. Keeps the blocks for reuse.
    /// @warning Every container using this arena must be gone or unused afterwards.
    void reset();
};
} // namespace Meow2::Memory
```

- **Enforcement:** `cmake/Meow2Docs.cmake` generates the Doxyfile with `WARN_IF_UNDOCUMENTED`, `WARN_NO_PARAMDOC`, `WARN_IF_INCOMPLETE_DOC` and `WARN_AS_ERROR = FAIL_ON_WARNINGS` for the public headers. The test **`Docs.PublicApi.IsDocumented`** (label `lint`) runs it, and fails with `file(line): error: Member … is not documented`. CI installs Doxygen and fails at configure time if it's missing; locally, the target and test are skipped without Doxygen. Review also checks private code (AGENTS.md §2.6).
- **Macros chosen by `#if`** (e.g. `Platform.h`): document each one with an explicit `/// @def NAME` block. Doxygen preprocesses as Windows/MSVC/x64/Debug (`PREDEFINED`), so the active branch defines every macro exactly once.
- **Output:** `cmake --build --preset <p> --target docs` → `<build dir>/docs/html/index.html`. The main page is `docs/api/MainPage.md`.

## 17. Roadmap

See [milestones.md](milestones.md).

## 18. Architecture: dependency injection, ownership, no globals

Goal: the dependency graph is **visible in the code**: what a class needs is exactly what its constructor takes.

**Dependency injection (constructor injection)**
- A class receives everything it uses through its **constructor**: other subsystems, allocators, the logger, configuration. It never reaches out to a global, a singleton or a service locator.
- Few dependencies → plain parameters: `Window(Logger& logger, const WindowDesc& desc)`.
  More than ~3 → a `<Name>Deps` struct of references, so call sites stay readable:
  `Renderer(const RendererDeps& deps)` with `RendererDeps{ .logger = …, .device = …, .window = … }` (a plain data struct, so no `m_`, §16).
- Injected dependencies are stored as **references** (`Logger& m_logger;`): non-null, non-owning, can't be re-pointed. Use a pointer only when the dependency is genuinely optional.
- Build-time-selected backends (§3) are still injected: the *type* is chosen by CMake, the *instance* is passed in.

**Composition root**
- Each executable (`Meow2Player`, `Meow2Renderer`, `Meow2Editor`, test fixtures) has **one place**, its `main()`/`Application` class, that creates every subsystem in dependency order and wires them together. Nothing else creates subsystems.
- The composition root **owns** subsystems (by value or `std::unique_ptr`). Everyone else holds non-owning references.
- **Lifetime rule:** a dependency must outlive everything that references it. Construct in dependency order; destruction runs in reverse automatically (locals, or members of an `Application` class, which are constructed in **declaration order**, so declare members in dependency order).

**Globals**
- Not allowed: mutable global/namespace-scope variables, singletons (`Foo::get()`), mutable `static` members, mutable function-local `static`s, service locators.
- Allowed: `constexpr`/`const` constants (`c_` prefix); stateless free functions.
- **Exceptions** need a `// GLOBAL: <why>` comment and a mention in the PR. Known ones:
  - OS callbacks with no user-data parameter (e.g. `SetUnhandledExceptionFilter` in the crash handler): one `s_` pointer, set and cleared by the composition root.
  - Per-thread scratch data inside the job system/allocators (`thread_local`), behind their APIs.
  - Third-party libraries with process-global state (SDL3 init, the Vulkan loader, Tracy): wrapped by one owning object created by the composition root.
  - Memory tracking (§6): the global `operator new`/`delete` override and the C allocator hooks of libraries reach the `TrackingAllocator` through one `s_` pointer, set and cleared by the composition root; the current `MemTag` is `thread_local`.

## 19. Threading

The engine starts single-threaded but every module is written so it can run on worker threads later.

- **Main thread** owns the OS window and the event loop (SDL3 requires event handling on the main thread).
- **Job system (M1): [enkiTS](https://github.com/dougbinks/enkiTS)** behind an engine `JobSystem` class (pimpl, build-time selection, so the library can be swapped in one folder):
  - created by the composition root: worker threads = hardware threads − 1, plus the main thread, which also runs tasks while it waits;
  - task sets (parallel-for over a range) with dependencies, `wait(handle)`, priorities;
  - **pinned tasks** run on a specific thread (e.g. the main thread for SDL calls);
  - `threadIndex()` gives each worker a stable index, used to pick per-thread arenas (§6) without locks;
  - its custom-allocator callbacks go to the `TrackingAllocator` (`MemTag::Jobs`), and its profiler callbacks name the threads in Tracy;
  - Jolt's job interface is implemented on top of it (M8).

  Long-lived threads are only created by the job system or the composition root.
- **The game loop itself is not threaded** (§23): one main thread runs the phases in order, and the parallelism lives inside phases as jobs.
- **Every class states its thread safety** in its header comment, one of:
  - *Not thread-safe*: use from one thread at a time (the default for most objects);
  - *Immutable after construction*: any thread may read;
  - *Thread-safe*: internally synchronised (say how).
- Prefer, in this order: **immutable data**, **data owned by one thread** (e.g. a frame arena per worker), **message passing/queues**, and only then shared data with locks.
- Locks: `std::mutex` / `std::shared_mutex` with `std::scoped_lock`; never call into another subsystem or user callback while holding a lock; lock order documented where two locks are taken.
- Atomics: `std::atomic` with the default (sequentially consistent) ordering unless a profiler shows a need, and then with a comment explaining the weaker ordering.
- Logging is thread-safe (spdlog multi-threaded sinks); the Qt console sink marshals to the GUI thread.
- RHI: command lists are recorded on any thread (one list per thread), submitted from one thread; resource creation on the device is thread-safe.
- No mutable globals (§18) is the main threading rule: shared mutable state that isn't visible in a constructor is where data races hide.
- **Sanitizers (M1):** compiler instrumentation that checks the program while the tests run.
  - **ASan** (AddressSanitizer): out-of-bounds access, use-after-free, double free, leaks.
  - **UBSan** (UndefinedBehaviorSanitizer): signed overflow, null or misaligned access, invalid casts.
  - **TSan** (ThreadSanitizer): data races.

  The presets are `linux-clang-asan` (ASan + UBSan, from the start of M1) and `linux-clang-tsan` (added with the job system), each run as its own CI job on GitHub-hosted runners. That doesn't slow down local or Windows builds; the tests themselves run 2–15× slower in those jobs. An optional local `windows-msvc-asan` preset (MSVC `/fsanitize=address`) can be added for Windows-only code.

## 20. Editor automation: commands + MCP server

Requirement: **anything a user can do in the editor, an AI agent can do through MCP** (Model Context Protocol), so agents can build scenes and games in it.

**Command architecture (the foundation)**
- Every editor action is a **command**: a name, a parameter schema (JSON Schema), `execute()` and `undo()`. Examples: `scene.createEntity`, `entity.addComponent`, `component.setProperty`, `asset.import`, `project.export`.
- Menus, toolbar buttons, shortcuts, drag-and-drop, the inspector, undo/redo **and** MCP all go through the same `EditorCommandRegistry`. **No editor UI may change editor state directly**; it must issue a command. That's what guarantees MCP covers *everything* the user can do: a feature that exists in the UI exists as a command, so it exists as an MCP tool.
- Read-only **queries** (scene hierarchy, selection, component values, asset list, log, project settings) are registered the same way and exposed as MCP tools/resources. A **viewport screenshot** query returns an image, so an agent can *see* the result.

**MCP server**
- Built into Meow2Editor; exposes `tools/list` (generated from the registry's names, descriptions and schemas) and `tools/call` (→ `execute`). Agent changes are undoable and appear live in the UI.
- Transport: MCP *Streamable HTTP* on **127.0.0.1 only** (never other interfaces), **off by default**, enabled per session from the editor or with `--mcp`; requires a per-session token; validates the `Origin` header (DNS-rebinding protection, as the MCP spec requires).
- File access through commands is limited to the open project folder.
- Threading (§19): requests arrive on a network thread and are executed on the editor's main thread (Qt queued invocation); the HTTP thread never touches editor state.
- **Headless mode** (`Meow2Editor --headless --mcp`): no window, off-screen viewport. Lets agents and CI drive the editor on a machine without a desktop (e.g. Linux CI, an agent's cloud workspace).
- Libraries (**⚠ OPEN**, decided in M9 with owner permission): JSON (nlohmann/json, MIT) and a small HTTP server (cpp-httplib, MIT). Qt's own `QHttpServer` is avoided because it's GPL/commercial-licensed, not LGPL.

**Testing:** every command has unit tests (execute + undo round-trip). An integration test starts the headless editor, lists tools, calls commands over MCP and checks the resulting scene.

## 21. Versioning

[Semantic Versioning](https://semver.org) `MAJOR.MINOR.PATCH`. The single source is `project(Meow2 VERSION x.y.z)` in the top-level `CMakeLists.txt`, which is written into `Meow2/Core/Version.h` (see [docs/wiki/Building.md](docs/wiki/Building.md#engine-version)).

| Part | Changes when | Who |
|---|---|---|
| MAJOR | stays **0** until the public API and the game package format are promised stable (M11, export, at the earliest). Under 0.x, any MINOR may break things. | owner decides |
| MINOR | **once per milestone.** The PR that closes a milestone sets `0.<milestone+1>.0`: M0 → 0.1.0, M1 → 0.2.0, M2 → 0.3.0, … | the milestone-closing PR |
| PATCH | **only for a fix to an already-tagged release**, e.g. a bug in `v0.2.0` shipped as 0.2.1 before the next milestone is done. Normal feature PRs never touch it. | a `fix/` PR on a release branch |

- **Feature PRs don't change the version.** Every one would edit the same `VERSION` line, so any two open PRs would conflict, and a number that moves with every merge says no more than the commit hash.
- **Tags:** after the milestone-closing PR is merged, its merge commit on `main` gets an annotated tag `vX.Y.Z` (e.g. `v0.1.0`). Tags are never moved or deleted.
- **Patch releases:** branch `release/0.Y` from the tag `v0.Y.0`, fix there (or cherry-pick the fix from `main`), bump PATCH, tag `v0.Y.Z`. `main` keeps going towards the next MINOR.
- **Builds between releases** are identified by the git commit: the version string gets SemVer build metadata, `0.1.0+g1a2b3c4` (`+g` + short hash, `.dirty` if the tree had local changes), so logs and crash dumps say exactly what was built. (Planned for M1; until then the string is just `x.y.z`.)
- **Releases (from v0.2.0):** every MINOR (and PATCH) tag gets a GitHub Release with a zip that **runs out of the box** on a Windows PC:
  - `Meow2-<version>-windows-x64.zip`, containing the Release build of the executables, their **PDBs** (so `.dmp` files from that version can be read), the MSVC runtime DLLs (app-local, via CMake's `InstallRequiredSystemLibraries`), and any third-party DLLs.
  - Built with CMake install rules + CPack (`ZIP` generator).
  - **Release notes = that version's section of [`CHANGELOG.md`](CHANGELOG.md)** ([Keep a Changelog](https://keepachangelog.com/en/1.1.0/) format). Every PR adds its lines under `[Unreleased]`; the milestone-closing PR renames that section to the new version.
  - A `release.yml` workflow triggered by a `v*` tag: the self-hosted Windows runner builds, tests and packages, and uploads the zip as an artifact. A separate job on a GitHub-hosted runner, the only one with `contents: write`, creates the Release and attaches the zip, so no write token ever reaches the owner's PC (§ CI safety in AGENTS.md §5).
  - The editor joins the zip once it uses Qt (needs `windeployqt`, M9).

## 22. Events

A shared **`EventBus`** in the engine, created by the composition root and injected, used by all three executables. It's for things that *happen*: window resized, key pressed, gamepad connected, asset loaded, entity spawned. Not for high-volume per-frame data, which stays in arrays processed by jobs.

```cpp
struct WindowResized { std::uint32_t width; std::uint32_t height; };    // plain data, no base class

Subscription m_onResize = m_events.subscribe<WindowResized>(
    [this](const WindowResized& e) { m_swapchain.resize(e.width, e.height); });

m_events.enqueue(WindowResized{1600, 900});   // any thread; delivered on the main thread at the next dispatch point
m_events.publish(WindowResized{1600, 900});   // main thread only; delivered immediately (rarely needed)
```
- **Events are plain data structs.** There's no event base class and no listener interface to inherit from. The type is the channel, identified by a compile-time type ID (no RTTI).
- **RAII subscriptions:** `subscribe` returns a move-only `Subscription` that unsubscribes in its destructor, so a destroyed object can never be called back. Declare subscription members **last** in a class, so they're destroyed first, before the members their handlers use.
- **Queued by default:** `enqueue` is thread-safe and stores the event in the bus's own `BlockArena` (`MemTag::Events`, reset after each dispatch). The main thread delivers queued events at fixed points in the frame (§23): after input, and after `update`. Order is first-in-first-out per producing thread; the order between threads is unspecified.
- **Immediate:** `publish` calls handlers synchronously on the main thread, for the few cases where the publisher needs the effect before continuing.
- **Safe during dispatch:** subscribing or unsubscribing inside a handler takes effect after the current dispatch, and an event enqueued inside a handler is delivered at the next dispatch point. That prevents iterator invalidation and infinite loops.
- **Why not the observer pattern:**
  - Observer couples each listener to a subject-specific interface (a virtual `onX` per subject).
  - It leaves dangling pointers when a listener forgets to unregister.
  - It dispatches immediately from wherever the subject happens to be, which causes re-entrancy and ordering bugs.

  Signal/slot libraries (sigslot, Boost.Signals2) fix the lifetimes, but the listener still needs a reference to the emitter. `entt::dispatcher` is the same design as this bus; it's worth reconsidering if EnTT is adopted for the scene/ECS later.
- **Threading (§19):** subscribe, unsubscribe, `publish` and dispatch run on the main thread; `enqueue` runs on any thread.
- Input (§9) publishes `KeyPressed`, `KeyReleased`, `MouseButtonChanged`, `MouseMoved`, `GamepadConnected`, `TextInput`; the platform layer publishes window events.

## 23. Game loop & time

- **`FrameClock`**: `std::chrono::steady_clock`, delta time in seconds (`double` for accumulation, `float` handed to gameplay), frame counter, total time. The delta is clamped to `c_maxFrameDt` (0.25 s), so a breakpoint or a stall doesn't trigger hundreds of catch-up steps (the "spiral of death").
- **Fixed timestep with an accumulator** (see Glenn Fiedler, ["Fix Your Timestep!"](https://gafferongames.com/post/fix_your_timestep/)). One iteration of the loop is `tick()`:

```cpp
void Application::tick()
{
    const double frameDt = m_clock.tick();                    // clamped
    m_platform.pumpEvents();                                  // SDL/Qt → input sources, window events enqueued
    m_input.beginFrame();                                     // edges + input events
    m_events.dispatchQueued();

    m_accumulator += frameDt;
    int steps = 0;
    while (m_accumulator >= c_fixedDt && steps < c_maxFixedSteps)
    {
        fixedUpdate(c_fixedDt);                               // physics, gameplay simulation
        m_accumulator -= c_fixedDt;
        ++steps;
    }
    update(frameDt);                                          // camera, animation, UI: variable step
    m_events.dispatchQueued();
    extract();                                                // M5: frame packet in the frame arena
    render(m_accumulator / c_fixedDt);                        // alpha: interpolate previous → current state
    MEOW2_PROFILE_FRAME_MARK();
}
```
- **`fixedUpdate`** runs 0..n times per frame at a constant rate (`c_fixedDt` = 1/60 s by default, configurable in `config.toml`). Physics needs a constant step to be stable and deterministic.
- **`update`** runs once per frame with the real (clamped) delta.
- **`render(alpha)`** interpolates between the previous and current fixed-step state, so motion is smooth when the frame rate and the fixed rate don't line up.
- **`tick()`, not `run()`:** Player and Renderer call `while (app.isRunning()) app.tick();`. The editor calls `tick()` from a Qt timer, because Qt owns its event loop.
- **Threading:** the phases run in order on the main thread, and the parallelism is inside them (jobs, §19). There's no render thread or double-buffered simulation for now. The **extract step** (M5) copies what the renderer needs into a frame packet allocated from the frame arena, and the renderer reads only the packet. If a render thread is added later, the packet becomes the hand-off, and nothing else changes.

---

## Changelog
- 2026-10-26 — Initial version.
- 2026-10-26 — Qt 6.8 LTS; Jolt first (PhysX/Box3D-ready); planned Linux/Wine/Android; windowing options; submodules for deps; custom memory arenas; reversed infinite Z; Debug/RelWithDebInfo/Release; glTF sample scenes + visual regression; `_` = all locals; DXC + room for a custom shader language.
- 2026-10-26 — Dropped the `_` prefix for locals (plain camelCase); SDL3 chosen for windowing (dockable windows planned); build-time selection for all subsystems except the RHI, which is runtime hot-swappable (Vulkan ↔ D3D12); glTF 2.0 only (`.gltf` + `.glb`), sample assets fetched on demand; milestones are general and work follows owner requests.
- 2026-10-26 — Roadmap moved to milestones.md.
- 2026-10-26 — Test framework: GoogleTest v1.17.0 (submodule).
- 2026-10-26 — Code style: clang-format + warnings-as-errors.
- 2026-10-26 — Architecture rules (§18: dependency injection, composition root, no globals) and threading rules (§19); logger injected, no global logger.
- 2026-10-26 — §20: editor command architecture + built-in MCP server (localhost-only, token, headless mode).
- 2026-10-27 — §16: fields of plain data structs (incl. `<Name>Deps`) drop the `m_` prefix; clang-format pinned to major version 19.
- 2026-10-27 — §21 Versioning: MINOR per milestone (bumped by the milestone-closing PR, tagged `vX.Y.Z`), PATCH only for fixes to a tagged release, git hash as build metadata.
- 2026-10-27 — M1/M2 planning:
  - `std::pmr::memory_resource` replaces `IAllocator`; §6 rewritten (TrackingAllocator + MemTags, `operator new` override, library hooks, size-class `BlockArena`, FrameArena with the RHI);
  - §7 logging: 5 levels, format, logger names, log folder (`--log-dir`, repo `logs/` from the VS solution);
  - §8 dumps in all configs, PDBs in release zips;
  - §9 input design (sources → `InputSystem`, polling + events, editor rule);
  - §19 enkiTS, sanitizer details;
  - §21 GitHub Release zips;
  - new §22 Events, §23 Game loop & time;
  - new libraries tl::expected, toml++, enkiTS, Tracy;
  - milestone references renumbered.
- 2026-10-27 — §16.1: documentation comments in Doxygen syntax (`///`, mandatory on all public API, thread safety paragraph); Doxygen listed as a tool (installed on request, M1).
- 2026-10-27 — §16.1 enforcement implemented: `docs` target, `Docs.PublicApi.IsDocumented` test, `@def` for `#if`-selected macros; Doxygen ≥ 1.9.8 required in CI.
- 2026-10-27 — §21: release notes come from `CHANGELOG.md`. Asserts implemented (§3 Core).
- 2026-10-27 — §2: engine folders Subsystem/Module/Backend (subsystem table), Visual Studio filters and solution folders mirror them; `windows-vs2022` presets (solution from any prompt, regenerate-only build preset).
