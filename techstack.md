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
- Platform-specific code only in `src/Platform/<Win32|Linux|Android>/`, behind interfaces.
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
│  ├─ include/Meow2/<Module>/...
│  ├─ src/<Module>/...
│  └─ tests/<Module>/...
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
| Core | — | `Platform.h` (platform/compiler/arch/config macros), asserts, `Result<T>`, handles, containers | No third-party deps |
| Memory | `IAllocator` — the one virtual allocator interface, used where allocators are passed around (+ `std::pmr::memory_resource` adapter) | linear/arena, frame (N-buffered), pool, stack, tracking/debug | See §6 |
| Log | `Logger`, `LogSink` | spdlog | §7 |
| Crash | `CrashHandler` | Win32 minidumps; Linux/Android later (e.g. crashpad/breakpad) | §8 |
| Platform / Window / Input | `Platform`, `Window`, `Input` | **SDL3** (§9) | Can also wrap an existing native window (Qt viewport) |
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
| Everything else (window, physics, logging, crash, filesystem, shader compiler, asset importers, memory) | **at build time** | One public header per subsystem declares a concrete class (e.g. `Meow2::Physics::PhysicsWorld`) with a private `Impl` (pimpl). A CMake option (`MEOW2_PHYSICS_BACKEND=Jolt`) chooses which `src/<Module>/<Backend>/` sources are compiled. No virtual calls. |

- Backend contracts are written down in the header, and enforced by shared "contract" tests that every backend must pass.
- Tests that need a fake build with a `Null`/`Fake` backend (e.g. `MEOW2_PHYSICS_BACKEND=Null`), instead of mocking through virtuals.
- Third-party types never appear in public engine headers (except DirectXMath in `Meow2/Math`).

---

## 4. Third-party dependencies (git submodules)

All libraries are git submodules under `external/`, checked out at a **release tag** and frozen there.
Upgrading one is its own `chore/` PR. **Adding any submodule requires owner permission.**

| Library | Use | License | How |
|---|---|---|---|
| SDL3 | window, input, gamepad | zlib | submodule |
| DirectXMath | math | MIT | submodule (header-only; needs a `sal.h` shim off Windows) |
| spdlog | logging (bundled fmt) | MIT | submodule |
| JoltPhysics | physics | MIT | submodule |
| Dear ImGui (docking branch) | Meow2Renderer UI | MIT | submodule |
| Vulkan-Headers, volk | Vulkan headers + loader-less function loading | Apache-2.0 / MIT | submodule |
| VulkanMemoryAllocator | VK memory | MIT | submodule |
| D3D12MemoryAllocator | D3D12 memory | MIT | submodule |
| DirectX-Headers / Agility SDK | D3D12 headers, newer features | MIT / MS | submodule / NuGet package (**⚠ OPEN**) |
| glTF loader (**⚠ OPEN**) | asset import | MIT | submodule |
| GoogleTest v1.17.0 | unit tests (gmock off) | BSD-3 | submodule |
| **Not submodules** | | | |
| Qt 6.8 LTS | Meow2Editor | LGPLv3 (link dynamically) | Qt online installer; `CMAKE_PREFIX_PATH` |
| Vulkan SDK | validation layers, tools | — | LunarG installer (dev machines only) |
| DXC | shader compiler | NCSA/MIT | prebuilt release binaries from Microsoft (or the copy in the Vulkan SDK) — not built from source |

Clone with: `git clone --recurse-submodules`. glTF sample assets are **not** submodules — they are fetched on demand (§13).

---

## 5. Build configurations

| Config | Optimisation | Asserts | Logging | Debug info | Use |
|---|---|---|---|---|---|
| Debug | off | on | trace+ | full | day-to-day debugging |
| RelWithDebInfo | on (`/O2`) | on (cheap ones) | debug+ | full PDB | profiling, testing, crash-dump analysis |
| Release | full (`/O2 /GL`, LTO) | off | info+ | PDB kept **but not shipped** | exported games |

PDBs are kept for every build, so crash dumps can be symbolicated.

---

## 6. Memory

- `IAllocator` with `allocate(size, alignment)` / `deallocate(ptr, size)`, plus an adapter so any allocator can be used as a `std::pmr::memory_resource` (works with `std::pmr::vector` etc.).
- Allocators:
  - **LinearArena** — bump pointer, reset all at once.
  - **FrameArena** — one linear arena per frame-in-flight, reset when that frame's GPU fence signals.
  - **PoolAllocator** — fixed-size blocks.
  - **StackAllocator** — LIFO with markers.
  - **TrackingAllocator** — wraps another allocator: counts, tags, leak report at shutdown (Debug/RelWithDebInfo).
- Memory tags per subsystem (`MemTag::Renderer`, `MemTag::Physics`, ...) feed stats in Meow2Renderer.
- GPU memory goes through VMA / D3D12MA, not these allocators.

## 7. Logging

- spdlog behind `Logger`. **No global/default logger** (spdlog's global registry is not used): the composition root creates the `Logger`, and subsystems receive it (or a per-subsystem `LogChannel`) by injection (§18).
- Macros take the logger explicitly: `MEOW2_LOG_{TRACE,DEBUG,INFO,WARN,ERROR,CRITICAL}(m_log, fmt, ...)`; levels compiled out per config (§5).
- Sinks:
  - **File** — rotating file sink to a configurable path.
  - **In-app console** — thread-safe ring buffer (`ConsoleRingSink`), shown as an ImGui window in Meow2Renderer and a Qt dock in Meow2Editor. Qt widgets are updated only on the GUI thread (queued connection).
  - stdout color sink in Debug; Android `logcat` sink later.
- The crash handler flushes all loggers.

## 8. Crash dumps (Windows)

- `CrashHandler::install()` hooks `SetUnhandledExceptionFilter`, `_set_purecall_handler`, `_set_invalid_parameter_handler`, `std::set_terminate`, and `SIGABRT`.
- `MiniDumpWriteDump` runs on a pre-created watchdog thread. Output: `<dump dir>/Meow2_<app>_<timestamp>.dmp`.
- GPU faults: D3D12 DRED, Vulkan `VK_EXT_device_fault` where available. Logged on device loss.

## 9. Windowing — SDL3

- **SDL3** behind `Window` / `Input` / `Platform` (build-time selection). Covers Windows, Linux (X11/Wayland) and Android. No GLFW, no GLEW (GLEW is an OpenGL loader; we don't use OpenGL).
- Windows are created API-neutral. The RHI creates its surface/swapchain from the native handle (`HWND`, X11/Wayland, `ANativeWindow`), so the same window survives a Vulkan ↔ D3D12 hot swap.
- **Dockable windows (future):**
  - Meow2Renderer: Dear ImGui docking branch + multi-viewports (tool windows can be dragged out into real OS windows; ImGui has an SDL3 platform backend for this).
  - Meow2Editor: Qt `QDockWidget`s. The viewport is a Qt widget; SDL3 can wrap its existing native window for input if needed, or the RHI uses the handle directly.
- Meow2Player: one SDL3 window, fullscreen/windowed.

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
| Interfaces (runtime-virtual only: RHI, `IAllocator`) | `I` + PascalCase | `IRhiDevice` |
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
  - Third-party libraries with process-global state (SDL3 init, the Vulkan loader): wrapped by one owning object created by the composition root.

## 19. Threading

The engine starts single-threaded but every module is written so it can run on worker threads later.

- **Main thread** owns the OS window and the event loop (SDL3 requires event handling on the main thread).
- **Planned (M1):** a job system: a fixed pool of worker threads (`std::jthread` + `std::stop_token`), jobs with dependencies, `waitFor(handle)`. Long-lived threads are only created by the job system or the composition root.
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
- **Sanitizers (M1):** presets with AddressSanitizer/UndefinedBehaviorSanitizer and ThreadSanitizer run the tests on Linux to catch memory errors and data races.

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
- Libraries (**⚠ OPEN**, decided in M10 with owner permission): JSON (nlohmann/json, MIT) and a small HTTP server (cpp-httplib, MIT). Qt's own `QHttpServer` is avoided because it's GPL/commercial-licensed, not LGPL.

**Testing:** every command has unit tests (execute + undo round-trip). An integration test starts the headless editor, lists tools, calls commands over MCP and checks the resulting scene.

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
