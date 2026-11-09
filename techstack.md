# Meow2 — Tech Stack

> Source of truth for *what* Meow2 is built with and *how it is laid out*.
> Process rules (branches, issues, PRs, permissions) live in [AGENTS.md](AGENTS.md).
> This file holds decisions and rules. What has been built, and how it works, is in [docs/wiki/](docs/wiki/Home.md), one page per module.
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
| `Meow2Editor` | exe | Qt (≥ 6.8, tested with 6.12) editor: scene editing, asset import, game export. **Fully drivable through an MCP server** (§20), so AI agents can build in it | `Meow2Engine`, Qt6 |
| `Meow2Player` | exe | Standalone game runtime the editor bundles on export. Uses the built-in UI. No ImGui, no Qt. | `Meow2Engine` |
| `Meow2ShaderCompiler` | exe | Build tool: cooks `shaders/` (§12). Not shipped | `Meow2Engine` |
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
| `Core` | files directly in `Core/` (Platform, Version, Assert, Debugger), `Memory`, `Log`, `Crash`, `Config`, `Time`, `Events`, `Jobs`, `Profiling`, `Id` | `Win32`, `Linux`, `Android`; library wrappers such as `Spdlog`, `Enkits`, `Tracy` |
| `Platform` | `Window`, `Input`, `FileSystem` | `Sdl`, `Win32`, `Linux`, `Android` |
| `Scene` | files directly in `Scene/` (planned, §25) | — |
| `Math` | files directly in `Math/` | — |
| `Graphics` | `Rhi`, `Shader`, `Renderer` | `Vulkan`, `D3D12` (Rhi), `Dxc` (Shader) |
| `Assets` | files directly in `Assets/` (asset core), `Shader`, `Texture`, `Gltf` | — |
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
| `MEOW2_BUILD_EDITOR` | ON (desktop only) | Builds the editor if Qt ≥ 6.8 is found; skipped with a warning otherwise (an error in CI). See [Building → Qt](docs/wiki/Building.md#qt) |
| `MEOW2_BUILD_RENDERER` | ON | |
| `MEOW2_BUILD_PLAYER` | ON | |
| `MEOW2_BUILD_TESTS` | ON | |
| `MEOW2_GPU_TESTS` | OFF | Tests needing a GPU (labels `gpu`, `visual`) |
| `MEOW2_PHYSICS_BACKEND` | `Jolt` | `Jolt` now; `PhysX`, `Box3D` reserved |
| `MEOW2_SIMD` | `AVX2` | x64 instruction set for the whole build: `AVX2` (+ FMA3, F16C) or `SSE4.2`; every app checks the CPU at startup (§10) |
| `MEOW2_SHADER_TOOLS` | ON | Downloads the pinned DXC (`MEOW2_DXC_DIR` uses an unpacked release instead); off, shaders can't be compiled, only loaded |

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
| Config | `Config` | TOML via **toml++** | defaults (in code) → `Meow2<App>.toml` next to the exe (`--config`) → `--set key=value`; write-back on change |
| Time | `FrameClock` | `std::chrono::steady_clock` | §23 |
| Events | `EventBus` | in-house | §22 |
| Jobs | `JobSystem` | **enkiTS** | §19 |
| Profiling | `MEOW2_PROFILE_*` macros | **Tracy** | Debug/RelWithDebInfo only; compiled out of Release |
| Id | `Uuid`, `EntityId`, `AssetId`, …, `UuidGenerator` | in-house, 128-bit snowflake-style UUIDv8 with a kind tag | §24 (M2) |
| Scene | `Scene` (entities, components) | **EnTT** | §25 (M2) |
| Platform / Window | `Platform`, `Window` | **SDL3** (§9) | One window; the editor's windows are Qt's |
| Input | `InputSystem` + input sources | SDL3 sources (engine), Qt source (editor) | §9 |
| FileSystem | `FileSystem` / VFS | native folders (`std::filesystem`); packages later; Android `AAssetManager` later | virtual paths `<mount>/…`, folder mounts, atomic writes |
| Math | aliases + inline functions (no virtuals) | DirectXMath | §10 |
| RHI | `IRhiDevice`, `IRhiCommandList`, `IRhiSwapchain`, resources by handle | Vulkan, D3D12 | §11 |
| Shader | `ShaderCompiler` | DXC (HLSL → DXIL / SPIR-V) | Interface leaves room for a custom shader language (§12) |
| Renderer | `Renderer`, render graph | on RHI only | |
| Assets | `AssetManager`, `importGltf`, `decodeImage` | asset core and shader assets (M4, §13a); glTF **2.0** importer, `.gltf` and `.glb`, through **cgltf**; PNG/JPEG through **stb_image**; textures cooked to BC7/BC5 `.m2texture` through **DirectXTex** | §13, §13a |
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
- Third-party types never appear in public engine headers (except DirectXMath in `Meow2/Math`; tl::expected through `Meow2/Core/Result.h`, because `Result<T>` is part of the API itself; and EnTT in `Meow2/Scene`, because its registry and views are the Scene API, §25).
- The one other place with virtual calls is memory: allocators implement `std::pmr::memory_resource`, whose interface the C++ standard defines as virtual (§6).

---

## 4. Third-party dependencies (git submodules)

All libraries are git submodules under `external/`, checked out at a **release tag** and frozen there.
Upgrading one is its own `chore/` PR. **Adding any submodule requires owner permission.**

| Library | Use | License | How |
|---|---|---|---|
| SDL3 release-3.4.18 | window, input, gamepad | zlib | submodule; static, audio/camera/GPU/render subsystems off; only included under `src/Platform/**/Sdl/`. Linux needs X11/Wayland dev packages to build it ([Building](docs/wiki/Building.md#linux-packages)) |
| DirectXMath may2026 | math | MIT | submodule; header-only, own INTERFACE target (`DirectXMath::DirectXMath`, linked PUBLIC: the `Meow2/Math` public-header exception); a `sal.h` shim (`Meow2Engine/shims/`) on its include path off Windows |
| spdlog v1.17.0 | logging (bundled fmt; static library, no default logger, wchar file names on Windows) | MIT | submodule |
| JoltPhysics | physics | MIT | submodule |
| Dear ImGui v1.93.0-docking | Meow2Renderer UI | MIT | submodule (owner request, 2026-11-07); built as `meow2_imgui` only with the Renderer, never in the engine or Player; memory under `MemTag::ImGui`; its own backends aren't used: input comes from engine events and it draws through the RHI (`shaders/ImGui.hlsl`). `IMGUI_DISABLE_OBSOLETE_FUNCTIONS` |
| Vulkan-Headers, volk vulkan-sdk-1.4.363.0 | Vulkan headers + loader-less function loading | Apache-2.0 / MIT | submodule; volk static, own target, X11 and Wayland types declared by volk (no X headers needed) |
| VulkanMemoryAllocator v3.5.0 | VK memory | MIT | submodule; implementation in its own library, outside the engine's warnings |
| D3D12MemoryAllocator v3.2.1 | D3D12 memory | MIT | submodule; implementation in its own library, outside the engine's warnings |
| DirectX-Headers v1.620.1 | D3D12 headers, the same version as the Agility SDK | MIT | submodule; header-only target, ahead of the Windows SDK's own `d3d12.h`; only included under `src/Graphics/Rhi/D3D12/` |
| cgltf v1.15 | glTF 2.0 import (`.gltf`, `.glb`) | MIT | submodule; one header, its implementation in its own library (`meow2_cgltf`) outside the engine's warnings; files read through the VFS and memory under `MemTag::Assets` through its callbacks. Chosen over fastgltf (owner decision 2026-11-07): neither decodes Draco, and cgltf is one header with no dependencies. Only included under `src/Assets/Gltf/` |
| DirectXTex may2026 | BC7 and BC5 texture compression for the texture cooker | MIT | submodule (owner-approved 2026-11-08); built from its sources as its own library (`meow2_directxtex`) outside the engine's warnings, without its CMake project (which adds install rules); on Linux it uses DirectX-Headers' WSL stubs and our `sal.h` shim; UBSan's alignment check is off for it (a harmless misaligned read in its BC7 encoder). Only included by `src/Assets/Texture/DirectXTex/`, because its header pulls in the D3D11 and Windows headers on Windows |
| GoogleTest v1.17.0 | unit tests (gmock off) | BSD-3 | submodule |
| tl::expected v1.3.1 | `Result<T>` until C++23 `std::expected` (only the `std::expected`-compatible API is used) | CC0 | submodule; header-only, own INTERFACE target (its CMakeLists pulls in CTest/Catch2). **Public-header exception:** included by `Meow2/Core/Result.h` |
| EnTT v3.16.0 | ECS for `Scene` (§25) | MIT | submodule; header-only, own INTERFACE target (`EnTT::EnTT`, linked PUBLIC). **Public-header exception:** `Meow2/Scene/Scene.h` exposes the registry and views. The registry allocates through `Memory::ResourceAllocator` (EnTT doesn't build with `std::pmr::polymorphic_allocator`) |
| toml++ v3.4.0 | config files | MIT | submodule; header-only, own INTERFACE target, `TOML_EXCEPTIONS=0`; only included by `src/Core/Config/Config.cpp` and `src/Assets/` |
| enkiTS v1.12 | job system | zlib | submodule; static, C interface/examples off; only included by `src/Core/Jobs/Enkits/` |
| Tracy v0.14.1 (client only) | CPU/GPU/memory profiler | BSD-3 | submodule; static, on demand, localhost only, no broadcast, its crash handler off; `TRACY_ENABLE` per configuration (Debug/RWDI). The **viewer is not in the repo**: download it from Tracy's GitHub releases, same version as the submodule |
| **Not submodules** | | | |
| stb_image v2.30 | PNG and JPEG decoding | MIT or public domain | one header, `external/stb/stb_image.h`, copied unchanged (owner-approved) like RenderDoc's; its implementation in its own library (`meow2_stb_image`), memory through the engine's hooks. Only included under `src/Assets/Texture/`. BC7 and other GPU formats come from the texture cooker later, not from it |
| Barlow Semi Condensed (Regular, Medium) | UI font of Meow2Renderer and Meow2Editor | SIL Open Font License 1.1 | two TrueType files and the licence in `assets/fonts/BarlowSemiCondensed/`, from Google Fonts' repository (owner-approved 2026-11-08); copied next to each exe and into the release zip. Consolas, the monospace font, is Microsoft's and not shipped: it's loaded from the system on Windows ([Theme](docs/wiki/Theme.md)) |
| RenderDoc in-app API v1.47 | frame capture from inside the app (§11) | MIT | one header, `external/renderdoc/renderdoc_app.h`, copied unchanged (owner-approved): RenderDoc's whole repository would be a large submodule for one file. Only included by `src/Graphics/Rhi/RenderDocCapture.cpp` |
| Qt ≥ 6.8 (tested 6.12.0, MSVC 2022 64-bit kit) | Meow2Editor (Widgets) | LGPLv3 (link dynamically) | Qt online installer / Maintenance Tool on Windows, aqtinstall 3.3.0 in a plain step in Linux CI (only GitHub-owned actions are allowed); found through `CMAKE_PREFIX_PATH`; `windeployqt` copies the DLLs next to the exe after each build |
| DirectX 12 Agility SDK 1.620.1 | the D3D12 runtime (`D3D12Core.dll`) shipped next to each app: SM 6.6 and enhanced barriers on Windows 10 too, the same runtime on every PC (owner decision 2026-11-07) | Microsoft (redistributable: `D3D12Core.dll`, `d3d12SDKLayers.dll`) | not a submodule: `cmake/Meow2Agility.cmake` downloads the pinned NuGet package at configure time and checks its SHA-256, like DXC. `meow2_use_agility(<exe>)` adds the exports Windows looks for and copies the runtime into `D3D12/` next to the exe. The release zip ships `D3D12Core.dll` and its licence |
| Vulkan SDK | validation layers, tools | — | LunarG installer (dev machines only) |
| DXC v1.9.2609 | shader compiler (§12) | NCSA/MIT (`dxcompiler`); Microsoft licence (`dxil`) | not a submodule: `cmake/Meow2Dxc.cmake` downloads the pinned release at configure time into the build folder and checks its SHA-256. Development only for now: not in the release zip, because the release's licence refers to a distributables list it doesn't contain. `Meow2Player` never ships it |
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
- **`TrackingAllocator`** (Debug/RelWithDebInfo): wraps an upstream resource. Each block gets a small header (magic, size, tag), and counters are kept per `MemTag` (current, peak, allocation count). A `ScopedLeakCheck` in each app logs the leaks by tag at the end of `main`. In Release, `MallocResource` is used directly with no headers.
- **`MemTag`**: one per subsystem (`General`, `Logging`, `Config`, `Jobs`, `Events`, `Platform`, `Renderer`, `Rhi`, `Physics`, `Assets`, `Editor`, `ImGui`, `Profiler`, `Scene`, …). The stats feed the memory window in Meow2Renderer and Tracy's memory view.
- **Libraries with an allocator hook** (SDL `SDL_SetMemoryFunctions`, Jolt `JPH::Allocate/Free`, ImGui `SetAllocatorFunctions`, enkiTS custom allocator, VMA/D3D12MA CPU callbacks) call `globalTracker().allocateTagged()` directly with their own tag (added with each library). **SDL: implemented (M2).** Most hooks are C functions without a user-data pointer, so they reach the process-wide tracker (§18).
- **Everything else** (spdlog, toml++, Tracy, `std::make_unique`, …) goes through a **global `operator new`/`delete` override** (Debug/RelWithDebInfo). **Not in Meow2Editor:** on Windows every DLL binds `new`/`delete` itself, and Qt's DLLs delete objects the exe allocated (parent widgets delete their children), so a replaced `operator new` would hand tracked blocks to the C runtime's `free`. The editor uses the normal heap; its library hooks are still tracked, tagged with the thread-local current tag (`MemTagScope` sets it for a scope, default `General`). No double counting: hooked libraries never go through `new`.
  - It replaces every variant: plain, array, aligned (`std::align_val_t`), `nothrow`, and sized delete.
  - **The tracker behind it lives for the whole process** (`globalTracker()`: `constinit`, never destroyed), not in the composition root. Memory allocated during `main` can be freed after `main` returns (static destructors), so the tracker must exist from before the first to after the last allocation. (The first plan, a tracker owned by `main` plus a `MallocResource` fallback outside it, would have left those late `delete`s with no tracker to go to.)
  - It's defined in a source file compiled into **each executable** (`meow2_track_global_new(<target>)`), never in the static library, where the linker could silently drop it. `MEOW2_MEMORY_TRACKING=0` turns it off (sanitizer builds).
- **`BlockArena`** (`std::pmr::memory_resource`, not thread-safe; one per thread where needed):
  - **Size-class block lists:** requests ≤ 8 KiB go into 64 KiB blocks, ≤ 128 KiB into 1 MiB blocks, larger ones (or alignments above 4 KiB) get a dedicated block of exactly their size plus a small header. So the unused tail of a block is at most ~12.5 % of it. Unused bytes are accepted; blocks are never combined.
  - **Allocate** bumps a pointer in the current block of the size class. If the request doesn't fit, it moves to the next block in that list (reusing an existing one) or allocates a new block from upstream and links it.
  - **`deallocate` is a no-op**; memory comes back only on `reset()`. Containers that grow (`vector::push_back`) leave their old buffers behind until reset, so `reserve()` up front where the size is known.
  - **`reset()`** rewinds every list to its first block and **keeps the blocks**, so after warm-up an arena makes no upstream calls at all. Dedicated blocks are kept too; a later large request reuses the smallest free one it fits in.
  - **`trim(keepBytes)`** gives unused blocks (no allocation since the last reset) back to upstream until at most `keepBytes` stay reserved, e.g. a recent high-water mark of `usedBytes()`. So a one-off spike (e.g. a level load) doesn't hold memory forever.
  - **No defragmentation needed:** nothing is freed individually, so no holes form inside an arena, and the upstream heap sees few, same-sized, long-lived blocks. Defragmentation is a topic for long-lived GPU heaps (VMA/D3D12MA), not for arenas.
  - Every arena has a `MemTag`; it allocates its blocks inside a `MemTagScope` of that tag, so a `TrackingAllocator` upstream counts them under it.
- **Debug fill patterns** (Debug/RelWithDebInfo, `Meow2/Core/Memory/DebugFill.h`): allocators fill memory they hand out with `0xCD`, freed or reset memory with `0xDD`, and guard bytes with `0xFD` (the Microsoft debug-CRT values). On by default, switchable per allocator at runtime (profiling), compiled out of Release (`MEOW2_MEMORY_DEBUG_FILL`). `BlockArena` and `TrackingAllocator` do it, so every tracked `new` gets it too. See [docs/wiki/Memory.md](docs/wiki/Memory.md).
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
  - **Console (stdout, colour):** Debug and RelWithDebInfo only. Release executables are built with the Windows GUI subsystem, so there's no console window.
  - **In-app console:** thread-safe ring buffer (`ConsoleRingSink`) designed in M1, shown as an ImGui window in Meow2Renderer (M6) and a Qt dock in Meow2Editor (M9). Qt widgets are updated only on the GUI thread (queued connection).
  - Android `logcat` sink later.
- **Log folder:** `logs/` **next to the executable** by default. A `--log-dir <path>` command-line option (or `log.dir` in the config) overrides it. When the executable is launched from the Visual Studio solution, CMake sets the debugger's command arguments (`VS_DEBUGGER_COMMAND_ARGUMENTS`) to `--log-dir <repo>/logs`, so runs from the IDE log into the repo's `logs/` folder (git-ignored). Run directly, the same exe logs next to itself.
- Loggers are thread-safe (`_mt` sinks). The crash handler flushes all loggers.

## 8. Crash dumps (Windows)

- `CrashHandler::install()` hooks `SetUnhandledExceptionFilter`, `_set_purecall_handler`, `_set_invalid_parameter_handler`, `std::set_terminate`, and `SIGABRT`.
- `MiniDumpWriteDump` runs on a pre-created watchdog thread. Output: `<log folder>/Meow2<App>_<timestamp>.dmp`, next to that run's log.
- **All configurations, Release included**: Release is what players run, so its dumps matter most. Release zips ship the matching PDBs (§21).
- Not written when a debugger is attached (the debugger gets the exception instead).
- Tested by a `windows`-labelled test that crashes a child process on purpose and checks the `.dmp` exists.

  See [docs/wiki/CrashDumps.md](docs/wiki/CrashDumps.md).
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

- DirectXMath. `XMFLOAT*` for storage, `XMVECTOR`/`XMMATRIX` for computation (`FXMVECTOR` parameter rules). **AVX2 (+ FMA3, F16C) on x64** (owner decision 2026-11-05: wider, faster batch math; excludes only old or very low-end CPUs), selectable down to SSE4.2 with a CMake option, with a startup check that explains a missing AVX2 instead of crashing; NEON on arm64.
- **Left-handed: +X right, +Y up, +Z forward.**
- Depth range **0..1**, **reversed infinite Z**: near plane maps to 1, infinity maps to 0. Depth buffer `D32_FLOAT`, clear to 0, compare `GREATER_EQUAL`. A custom `perspectiveReversedInfiniteLH(fovY, aspect, nearZ)` (not available in DirectXMath).
- Vulkan: negative-height viewport flips Y so both backends share the same matrices.
- glTF is right-handed (+Y up, +Z forward, +X left): the importer converts on load (negate X, fix winding).
- **Large worlds** (decided 2026-11-05): `float` positions have about 1 mm of precision 10 km from the origin and about 8 mm at 100 km, enough for most games. Two cheap measures are taken now so bigger worlds stay possible without a rewrite:
  - **camera-relative rendering** from the first renderer (M5): positions are made relative to the camera on the CPU, so the GPU only ever sees small numbers;
  - **one `WorldPosition` type** for positions in the world (a `Vec3` today), so switching it to doubles, or adding a floating origin, changes one type rather than every system.

  Doubles or a floating origin themselves (and Jolt's double-precision build) come with the terrain/streaming work, if a game needs worlds larger than a few kilometres.
- **Noise** (Perlin/simplex) isn't planned for M3: most of it runs on the GPU in shaders; a CPU version comes with its first CPU user (terrain heights for physics, camera shake, procedural placement).

## 11. RHI

- Explicit model (queues, command lists, fences, descriptor heaps/sets, explicit barriers). Direct3D 12 + Vulkan 1.3 (dynamic rendering, synchronization2).
- Order: Vulkan first, then D3D12 behind the same interface.
- **Runtime hot swap** (Vulkan ↔ D3D12, Windows only):
  1. A switch is requested; it runs at the next frame boundary.
  2. Wait for the GPU to go idle, then destroy every GPU object, the swapchain and the device.
  3. Create the new device and swapchain on the **same window**.
  4. Recreate all GPU resources from their **CPU-side descriptions**. The registry is `Renderer::RenderDevice`'s list of device users: each owner of GPU objects releases them and makes them again from what it kept, because data derived from handles (a mesh record's heap indices) can only be rebuilt by its owner. Shaders are already compiled for both (DXIL + SPIR-V).
  5. A switch that fails (no device, or an owner can't restore) goes back to the old backend, so the app keeps running.
  - Rule this implies: nothing above the RHI may cache backend-native handles or pointers. Only engine handles, which are re-bound after the swap.
  - It costs a hitch of seconds, not a seamless switch. That is fine for comparing backends in Meow2Renderer.
- **Bindless and GPU-driven** (owner decision 2026-11-06): one global descriptor heap and one fixed binding layout (Vulkan: set 0, binding 0 the sampler heap, binding 1 the mutable resource heap, passed to DXC explicitly); shaders get indices through push constants and index `ResourceDescriptorHeap`. On Vulkan DXC emulates that with descriptor indexing and `VK_EXT_mutable_descriptor_type`; on D3D12 it needs SM 6.6. Drawing is indirect with a count buffer, vertices are pulled from buffers, culling runs in compute. Older GPUs and Android need a check before M12.
- **Pipeline cache** and up-front pipeline creation: the driver compiles DXIL/SPIR-V to machine code for the exact GPU when a pipeline is created, so that result is cached on disk, once per GPU and driver version.
- Resources are referenced by handles owned by the device (32-bit index + generation). **GPU resources don't get UUIDs** (§24): they exist only while the program runs, are recreated on every hot swap and are looked up every frame, so a 128-bit key would cost hashing for nothing. A resource may record the `AssetId` it was loaded from, for debugging and Tracy names.
- **D3D12** (M5): one root signature (the push constants as root constants at b0, heaps indexed directly), **enhanced barriers** (the same stages, accesses and layouts as Vulkan's synchronization2, so `ResourceState` maps one to one), and buffer ↔ texture copies with tightly packed rows. A GPU without them is refused with a message, as on Vulkan. Shaders that write a buffer use a second heap slot (`bindlessStorageIndex`), because D3D12 keeps read and write views apart.
- Debug: validation layers / D3D12 debug layer, object names, validation messages routed to the `RHI` logger.
- **Debug tools** (owner decision 2026-11-07): RenderDoc is the baseline on both backends, with in-app capture and pass markers. PIX is for D3D12 (timing, and ray tracing later). NVIDIA Nsight Graphics is for deep GPU profiling and ray tracing. Markers go through one RHI call, so all three show the same pass names.

## 12. Shaders

- Authored in **HLSL**. Compiled with **DXC** → DXIL (D3D12) and SPIR-V (Vulkan).
- `ShaderCompiler` takes source + entry + stage + defines and returns bytecode. DXC is one implementation, so a **future custom shader language** can either compile to HLSL (and reuse DXC) or produce SPIR-V/DXIL directly.
- **Pipeline:** `Pbr.hlsl` + `Pbr.hlsl.meta` (ID, entry points, option axes, defines) → cooked by `Meow2ShaderCompiler` → `<AssetId>.m2shader` (both bytecodes per variant) → loaded as a shader asset; a variant is picked by option values.
- **No reflection library:** bindless has one fixed layout, so structs are shared between C++ and HLSL and checked by size.
- **DXC doesn't optimise for a GPU:** DXIL and SPIR-V are portable; the driver compiles them for the exact GPU when a pipeline is created, on the player's machine (§11). So `Meow2Player` ships only cooked shaders, no DXC. Variants by GPU capability are ordinary option axes. DXC on a player's machine only makes sense for mods.
- Offline compilation via a CMake step. Runtime hot reload where DXC is available (Renderer, Editor).

## 13. Rendering verification (glTF sample scenes)

- Format: **glTF 2.0 only**, both `.gltf` (+ external `.bin`/images) and `.glb` (binary). glTF 1.0 is not supported.
- Test scenes: Khronos **glTF-Sample-Assets**, pinned to a commit.
- **Fetched on demand, not cloned up front.** `cmake -P cmake/FetchGltfSample.cmake <ModelName> [...]` (implemented in M6; visual tests and Meow2Renderer's scene browser will call it too):
  - on first use, does a blobless, sparse clone of the pinned commit into `assets/gltf-sample-assets/` (git-ignored);
  - then adds only `Models/<ModelName>/` to the sparse checkout, so only that model is downloaded.
- Meow2Renderer has a scene browser listing the sample models, marking which are downloaded, and can open any local `.gltf` / `.glb`.
- **Visual regression tests**: render a scene headlessly from a fixed camera, save PNG, and compare with an owner-approved golden image (per backend) using a tolerance (per-pixel threshold + % of pixels, or FLIP). CTest label `visual`. A new golden is only accepted after the owner has looked at it.

## 13a. Assets (M4)

- **Source → cooked object:** a source file, a `.meta` sidecar next to it holding its `AssetId` (§24) and import settings, and a cooked file named by the ID. Renaming a source keeps its ID.
- **`AssetManager`** (concrete, injected, thread-safe): typed handles, loads through the VFS (§3), memory under `MemTag::Assets`. No virtuals: asset types are templates or a `std::variant`.
- Shader assets come first (M4); meshes, textures and materials use the same core (M6).

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
  - Memory tracking (§6): the global `operator new`/`delete` replacement and the C allocator hooks of libraries use one process-wide `TrackingAllocator` (`globalTracker()`, `constinit`, never destroyed, because `delete` can run after `main` returns); the current `MemTag` is `thread_local`.

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
- RHI: command lists are recorded on any thread (one list per job, one command pool per thread per frame in flight) and submitted from one thread, in an order the caller gives; resource creation on the device is thread-safe.
- **Render graph** (owner decision 2026-11-07): compiled on the main thread, which fixes pass order and barriers; passes then record in parallel on jobs, one command list per pass or per group of small passes. One pass is never split across lists: Vulkan and D3D12 render passes don't span lists without extra machinery, and GPU-driven passes are few draws.
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
- **Builds between releases** are identified by the git commit: the version string gets SemVer build metadata, `0.1.0+g1a2b3c4` (`+g` + short hash, `.dirty` if the tree had local changes), so logs and crash dumps say exactly what was built.
- **Releases (from v0.2.0):** every MINOR (and PATCH) tag gets a GitHub Release with a zip that **runs out of the box** on a Windows PC:
  - `Meow2-<version>-windows-x64.zip`, containing the Release build of the executables, their **PDBs** (so `.dmp` files from that version can be read), the MSVC runtime DLLs (app-local, via CMake's `InstallRequiredSystemLibraries`), and any third-party DLLs.
  - Built with CMake install rules + CPack (`ZIP` generator).
  - **Release notes = that version's section of [`CHANGELOG.md`](CHANGELOG.md)** ([Keep a Changelog](https://keepachangelog.com/en/1.1.0/) format). Every PR adds its lines under `[Unreleased]`; the milestone-closing PR renames that section to the new version.
  - A `release.yml` workflow triggered by a `v*` tag: the self-hosted Windows runner builds, tests and packages, and uploads the zip as an artifact. A separate job on a GitHub-hosted runner, the only one with `contents: write`, creates the Release and attaches the zip, so no write token ever reaches the owner's PC (§ CI safety in AGENTS.md §5).
  - The editor is in the zip too, with Qt deployed next to it (Qt's deploy script).

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

## 24. IDs (M2)

Things that are **saved and referenced across runs** (entities, assets, scenes, prefabs) get a 128-bit ID that says what kind of thing it names. Runtime-only objects use handles instead: `entt::entity` for entities while a scene runs (§25), RHI handles for GPU resources (§11).

**Layout: a snowflake-style [RFC 9562](https://www.rfc-editor.org/rfc/rfc9562) UUIDv8** (v8 is the RFC's layout for custom fields, so these are valid UUIDs that any tool accepts):

```
 bit 127                                                                                    bit 0
| unix time, ms (48) | version = 8 (4) | sequence (12) | variant = 0b10 (2) | kind (8) | node (54) |
```

| Field | Bits | Why |
|---|---|---|
| Time | 48 | Milliseconds since 1970 (lasts until the year 10889). IDs sort by creation time, so saved files list things in a stable order and git diffs stay small |
| Version, variant | 4 + 2 | Fixed values that make it a standard UUID |
| Sequence | 12 | 4096 IDs per millisecond per generator; on overflow the generator borrows the next millisecond, so IDs never go backwards |
| Kind | 8 | `IdKind`: `Invalid` (0), `Entity`, `Asset`, `Scene`, `Prefab`, … (up to 255). `id.kind()` says what an unknown ID refers to |
| Node | 54 | Random, chosen when the generator is created (`std::random_device`). Replaces Snowflake's worker ID, so separate machines, editor instances and AI agents need no coordination |

- **Types:** `Meow2::Uuid` (two `std::uint64_t`, comparable, hashable) and typed wrappers `EntityId`, `AssetId`, … that only accept their kind, so an asset ID can't be passed where an entity ID is expected. The all-zero ID is invalid.
- **Text form:** the standard `xxxxxxxx-xxxx-8xxx-xxxx-xxxxxxxxxxxx` (lowercase hex) in TOML, scene files and logs; parsing returns `Result<Uuid>` and checks version, variant and kind.
- **Assets** get a generated ID stored in a `.meta` sidecar next to the source file, so renaming or moving the file keeps its ID. (A hash of the path would change on every rename and break references.)
- **Not secret:** IDs contain their creation time and are guessable; never use them as tokens (the MCP token is separate random data, §20).
- **Threading:** one `UuidGenerator`, created by the composition root and injected, is thread-safe without a lock: time and sequence live in one `std::atomic<std::uint64_t>`, and `next()` advances it with a compare-and-swap (`max(now, last + 1)`). That handles millions of IDs per second, far more than entity creation needs. If profiling ever shows contention, each job thread gets its own generator with the thread index in the node bits.
- **Options considered:** UUIDv4 (random: no order, no room for a kind), UUIDv7 (ordered, but no kind field), 64-bit Snowflake (needs a unique worker ID per machine, handed out by someone, and few spare bits), a hash of the name or path (changes on rename), a runtime counter (not stable across runs).

## 25. Scene & ECS (M2)

The engine's entities and components use **[EnTT](https://github.com/skypjack/EnTT)** (MIT, header-only), v3.16.0. Its types are part of the public `Meow2/Scene` API (owner decision): its views and registry *are* the API, so wrapping them would mean rewriting them.

- **Two identities per entity:** `entt::entity` (32 bits: index + version) is the fast runtime handle used by systems; `EntityId` (§24) is the saved identity used by scene files, prefabs, undo, the editor and MCP. `Scene` owns the `entt::registry`, every entity carries an `IdComponent`, and `Scene` keeps an `EntityId → entt::entity` map.
- **Components are plain data structs** (§16), like events (§22). Behaviour lives in systems that iterate views.
- **Memory:** EnTT's storages take an allocator. The registry uses `Memory::ResourceAllocator` (a plain allocator over a `memory_resource`; `std::pmr::polymorphic_allocator` doesn't compile with EnTT because of its uses-allocator `construct`) over a `TaggedResource`, so everything is counted under `MemTag::Scene` (§6).
- **Transforms** (M3): `LocalTransform`, `WorldTransform` (a matrix, exact under non-uniform scale), `PreviousWorldTransform` and `Parent`, kept up to date by `TransformHierarchy` (see below).
- **Threading** (the registry isn't thread-safe):
  1. **Structural changes** (create or destroy entities, add or remove components) happen only on the main thread. Jobs record them in **per-thread command buffers** (allocated from the frame arena), which the main thread applies at fixed points in the frame (§23). EnTT has no command buffers, so the engine provides them.
  2. **Every component pool is created at startup** (`registry.storage<T>()` for each registered component type). EnTT otherwise creates a pool on first use, and a first use on a worker thread would be a race.
  3. **Parallel iteration** is safe when jobs touch disjoint entities, or disjoint component types, while nothing changes structure: a view is split into ranges with `JobSystem::parallelFor`.
  4. **Groups** reorder their pools when components are added or removed, so they count as structural changes too.
  5. **Signals** (`on_construct`, `on_destroy`) run on the thread making the change, so in practice the main thread; anything other subsystems should hear about goes on the `EventBus`.
  6. **Later:** systems declare which components they read and write, `entt::organizer` builds a dependency graph from that, and the graph runs as enkiTS tasks with dependencies, with no locks inside systems.
- **Why EnTT:** sparse-set storage (fast add/remove, cache-friendly iteration), widely used, MIT; `entt::dispatcher` was already noted as the same design as our `EventBus` (§22). The bus stays ours.
- **No double-buffered ECS** (decided 2026-11-03): systems run in parallel on one copy of the data (parallel views, read/write-scheduled systems, command buffers). Overlapping rendering with the next frame's simulation is done by the extract step (§23, M5): only render data is copied into a frame packet. A full double buffer would copy every component every frame and add a frame of gameplay lag; only `Transform` keeps a previous copy, for `render(alpha)` interpolation.

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
- 2026-10-29 — §3/§4: `Result<T>` implemented over tl::expected v1.3.1 (std::expected-compatible subset only); tl::expected is the second public-header exception (after DirectXMath).
- 2026-10-29 — §8: crash handler implemented on Windows (watchdog thread, hooks, dialogs suppressed, crash-probe tests); Linux/Android no-op for now.
- 2026-10-29 — §6: `BlockArena` and `MemTag` implemented; trim semantics (unused blocks only, down to `keepBytes`), alignments above 4 KiB get a dedicated block, dedicated blocks reused best-fit; debug fill patterns (`0xCD`/`0xDD`/`0xFD`).
- 2026-10-31 — §6: `TrackingAllocator`, `MallocResource` and `MemTagScope` implemented (guard bytes, lock-free per-tag counters, `allocateTagged` for library hooks); `BlockArena` blocks counted under the arena's tag.
- 2026-10-31 — §6/§18: global `operator new`/`delete` tracking implemented; the tracker behind it is process-wide (`constinit`, never destroyed) instead of owned by the composition root, because `delete` can run after `main` returns; `ScopedLeakCheck` logs leaks per tag at app exit; library hooks land with each library.
- 2026-11-01 — §3/§4: Config implemented over toml++ v3.4.0 (`Meow2<App>.toml` next to the exe, `--config`, `--set key=value` overrides, write-back only on change, broken files never overwritten).
- 2026-11-01 — §4/§9: SDL3 release-3.4.18 window implemented (Platform + Window, settings in the config, polled events until the EventBus, SDL memory hook under `MemTag::Platform`, offscreen driver for tests); Linux CI installs SDL's X11/Wayland build packages.
- 2026-11-01 — Qt: ≥ 6.8, tested with 6.12.0 (MSVC 2022 64-bit kit), instead of 6.8 LTS; editor main window + viewport implemented (§9); the editor isn't covered by the global `new`/`delete` tracking (§6); `MEOW2_BUILD_EDITOR` skips the editor without Qt (error in CI).
- 2026-11-01 — §5/§7: Release executables are GUI-subsystem programs (`meow2_gui_subsystem_in_release`, standard `main` kept via `/ENTRY:mainCRTStartup`).
- 2026-11-01 — §23: `FrameClock` implemented (steady_clock, clamped delta, frame count, game-time total, explicit time points for tests).
- 2026-11-01 — §4: Linux CI installs Qt with aqtinstall in a plain step; the third-party install-qt-action was refused by the repository's Actions settings and kept CI from starting (#64).
- 2026-11-01 — §22: `EventBus` implemented (typed events, publish/enqueue/dispatchQueued, RAII subscriptions, double-buffered arena queue).
- 2026-11-01 — §21: build metadata implemented (git commit read at build time, `getBuildMetadata()`).
- 2026-11-01 — §4/§19: `JobSystem` implemented over enkiTS v1.12 (parallel-for, jobs, main-thread tasks, thread index, allocator hook under `MemTag::Jobs`).
- 2026-11-01 — §3/§4: Tracy v0.14.1 client implemented (macros without Tracy in public headers, on demand + localhost only, job thread names, memory as per-tag plots instead of per-allocation events because allocations outlive Tracy's lifetime).
- 2026-11-01 — §19: sanitizer presets and CI jobs implemented (ASan+UBSan, TSan; engine + Player/Renderer, editor excluded by owner decision).
- 2026-11-01 — §5/§21: PDBs for Release builds; release zip (CPack) with PDBs, MSVC runtime and Qt, checked by running every app from the unpacked zip; release.yml publishes GitHub Releases with the CHANGELOG section as notes.
- 2026-11-02 — New §24 IDs (128-bit snowflake-style UUIDv8 with a kind tag, lock-free generator; GPU resources keep RHI handles, §11) and §25 Scene & ECS (EnTT planned, ⚠ OPEN until the submodule is approved; threading rules). Planned for M2.
- 2026-11-02 — §9: window events implemented on the `EventBus` (coalesced per pump, queued, `Platform` takes the bus).
- 2026-11-02 — §23: game loop implemented (`GameLoop`, `FixedTimestep`, `loop.*` config keys); all three apps use it.
- 2026-11-03 — §9: `InputSystem` implemented (raw input queue, frame edges + fixed-step latch, mouse, gamepads, bus events); the SDL sources follow.
- 2026-11-03 — §9: SDL input sources implemented (keyboard/mouse/text and gamepads → `InputSystem`; Platform routes non-window events to them).
- 2026-11-03 — §24: IDs implemented (`Uuid`, `TypedId`, lock-free `UuidGenerator`).
- 2026-11-03 — §3/§4/§25: EnTT v3.16.0 added (owner-approved) and is the third public-header exception (`Meow2/Scene`); `Scene` part 1 implemented; `MemTag::Scene`; the registry uses `Memory::ResourceAllocator` because EnTT doesn't build with `std::pmr::polymorphic_allocator`.
- 2026-11-03 — §25: command buffers, transient entities and a signal-synced `entt::dense_map` ID map implemented; decision recorded: no double-buffered ECS (render overlap via the extract step).
- 2026-11-03 — §3: virtual file system implemented (folder mounts, path rules, atomic writes, thread-safe reads).
- 2026-11-05 — §10: AVX2 (+ FMA3, F16C) as the x64 SIMD baseline (owner decision); large worlds (camera-relative rendering from M5, a single `WorldPosition` type; doubles or a floating origin deferred to terrain/streaming) and noise (GPU first, CPU with its first user).
- 2026-11-05 — §4/§10: DirectXMath may2026 added (owner-approved) with a `sal.h` shim for GCC/Clang; `Meow2/Math` types implemented.
- 2026-11-05 — §2/§10: `MEOW2_SIMD` (AVX2 default) and the startup CPU check implemented; batch helpers added.
- 2026-11-05 — §10: scalar helpers implemented.
- 2026-11-05 — §10: deterministic random numbers (PCG32) implemented; GCC/Clang build with `-ffp-contract=off` so float results match MSVC.
- 2026-11-05 — §10: colours implemented.
- 2026-11-05 — §10: quaternion helpers implemented (Euler order roll → pitch → yaw).
- 2026-11-05 — §10: `Transform` and the `WorldPosition` type implemented.
- 2026-11-05 — §10: easing curves and splines (with constant-speed paths) implemented.
- 2026-11-05 — §10: reversed-Z projections and the Vulkan viewport flip implemented.
- 2026-11-05 — §10: camera maths and camera-relative rendering helpers implemented.
- 2026-11-05 — §10: bounding volumes, the camera frustum and culling implemented.
- 2026-11-05 — §10: rays and picking implemented.
- 2026-11-05 — §10: vertex packing and quantisation implemented.
- 2026-11-05 — §25: transform hierarchy implemented (parallel by depth level, dirty flags, previous world transform for interpolation).
- 2026-11-06 — §3/§4/§11/§12: new §13a Assets; M4 plan: DXC v1.9.2609 pinned and downloaded, no SPIRV-Reflect, bindless and GPU-driven rendering (owner decision), cooked shaders only in `Meow2Player`, pipeline cache in M5.
- 2026-11-06 — §12: `ShaderCompiler` implemented.
- 2026-11-06 — §13a: asset core implemented.
- 2026-11-06 — §12: shader settings and permutations implemented.
- 2026-11-06 — §12: cooker and `.m2shader` implemented.
- 2026-11-06 — §2/§12: shader build step implemented.
- 2026-11-06 — §12: bindless shader contract implemented.
- 2026-11-06 — §12: shader hot reload implemented.
- 2026-11-07 — Implementation notes moved to `docs/wiki/`; this file keeps decisions and rules. §11: debug tools (RenderDoc baseline, PIX and Nsight).
- 2026-11-07 — §4: Vulkan-Headers, volk and VMA added (owner-approved).
- 2026-11-07 — §11: heap bindings fixed and passed to DXC explicitly, because its default depends on what a shader declares.
- 2026-11-07 — §19: command lists per job, parallel render graph recording (owner decision).
- 2026-11-07 — §4: RenderDoc's in-app API header added (owner-approved).
- 2026-11-07 — §4/§11: D3D12 backend: DirectX-Headers and D3D12MA added, the Agility SDK shipped next to each app (owner decision); enhanced barriers and tightly packed copies required.
- 2026-11-07 — §11: hot swap implemented: `RenderDevice` device users are the resource registry; a failed switch goes back.
- 2026-11-07 — §3/§4/§13: cgltf v1.15 and stb_image v2.30 added (owner-approved); the glTF loader is no longer open; the sample fetch script implemented; `Assets/Texture` module.
- 2026-11-07 — §4: Dear ImGui v1.93.0-docking added (owner request) for Meow2Renderer; drawn through the RHI, fed by engine input.
- 2026-11-08 — §4: Barlow Semi Condensed bundled as the UI font (owner-approved); Consolas used from the system on Windows.
- 2026-11-09 — §3/§4: DirectXTex may2026 added (owner-approved) for BC7 and BC5 texture cooking; BC formats need `textureCompressionBC` on Vulkan.
