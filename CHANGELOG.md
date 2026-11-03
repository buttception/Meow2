# Changelog

All notable changes to Meow2, per released version. The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and versions follow the rules in [techstack.md §21](techstack.md) (MINOR = milestone).

- **Every PR** adds one or more lines under **[Unreleased]**, in the matching group (`Added`, `Changed`, `Deprecated`, `Removed`, `Fixed`, `Security`), ending with the PR number.
- **The milestone-closing PR** renames `[Unreleased]` to `[0.Y.0] — <date>`, adds a new empty `[Unreleased]` above it, and bumps `VERSION`.
- **The GitHub Release** for a tag uses that version's section as its description (AGENTS.md §2.7).

## [Unreleased]

### Added
- Scene: jobs can now create and destroy entities and add or remove components through per-thread command buffers, applied on the main thread at one point in the frame. New transient entities (no saved ID, for bullets and effects), and the ID lookup stays correct however an entity is destroyed (#98).
- Scene: entities and components on EnTT v3.16.0 (new dependency). Every entity gets a saved `EntityId` next to its runtime handle; scenes can be loaded with their saved IDs, component types are registered up front, and all scene memory shows up under the new `Scene` memory tag (#96).
- IDs: 128-bit `Uuid`s for things that are saved (entities, assets, scenes, prefabs). They sort by creation time, carry what kind of thing they name (`EntityId`, `AssetId`, … can't be mixed up), read and write as standard UUID text, and come from a lock-free `UuidGenerator` that needs no coordination between machines (#94).
- Input sources: Player and Renderer now feed the `InputSystem` from SDL: keyboard (physical keys), mouse (motion, buttons, wheel with natural scrolling handled), typed text, and gamepads (plugged in or out at any time; sticks -1..1, triggers 0..1). Gamepad support is optional; without it the apps run as before (#92).
- Input: `InputSystem` keeps keyboard (by physical key), mouse and gamepad state. Gameplay polls it (`isDown`, `wasPressed`, `mouseDelta`, `axis`, …), with presses latched for the fixed-rate `fixedUpdate` so a tap is never lost or doubled; changes go on the `EventBus` (`KeyPressed`, `MouseMoved`, `GamepadConnected`, …). Player and Renderer run it each frame; the SDL sources that feed it come next (#90).
- Game loop: every app now runs its frames through `GameLoop`: events, then `fixedUpdate` at a constant rate (60 Hz by default, `loop.fixedStepHz`), `update` with the frame time and `render` with an interpolation factor. Long frames run at most `loop.maxFixedSteps` simulation steps, so a stall can't snowball. The editor ticks it from its Qt timer (#88).
- Window events: the windowing layer puts `WindowResized`, `WindowMoved`, `WindowStateChanged`, `WindowFocusChanged`, `WindowDisplayScaleChanged`, `WindowCloseRequested` and `QuitRequested` on the `EventBus` (one per kind of change per frame, with the final values). Player and Renderer stop on the close and quit events. `Platform::create` now takes the `EventBus`; `Window::id()` and `displayScale()` are new (#83).

### Changed
- Roadmap: M2 is now "Platform & scene foundation", with a feature table that adds 128-bit IDs (time-sorted, with a kind tag such as entity or asset) and an entity/component store on EnTT; techstack.md §24 and §25 describe both, including their threading rules (#81).

## [0.2.1] — 2026-11-02

### Fixed
- Releases: the release workflow stopped after building the zip ("in was unexpected at this time"): a parenthesis in an error message ended a cmd block early. `v0.2.0` therefore has no GitHub Release; `0.2.1` is the first one, with the same engine as 0.2.0 (#85, #86).

## [0.2.0] — 2026-11-02

### Added
- Releases: each version tag builds `Meow2-<version>-windows-x64.zip` (the apps, their PDBs, the MSVC runtime and Qt; runs after unpacking anywhere), checks it by running every app from the unpacked zip, and publishes it as a GitHub Release with that version's CHANGELOG section as notes. Release builds now produce PDBs (#77).
- Sanitizers: two CI jobs build with Clang's AddressSanitizer + UndefinedBehaviorSanitizer and ThreadSanitizer and run every engine and app test under them (presets `linux-clang-asan`, `linux-clang-tsan`) (#75).
- Profiling: Debug and RelWithDebInfo builds include the Tracy client (v0.14.1): connect the Tracy viewer to a running app for a timeline of all threads (`Main`, `Job thread N`), engine zones, frames and memory per tag. It listens on localhost only, records nothing until a viewer connects, and is compiled out of Release (#73).
- Jobs: `JobSystem` (enkiTS v1.12) runs work on all cores: `parallelFor` over a range, single jobs, tasks for the main thread, and a stable per-thread index; waiting threads run jobs themselves. Each app creates one (`jobs.threadCount` in its config) (#71).
- The version string names the commit a build comes from, e.g. `0.1.0+g1a2b3c4` (`.dirty` for uncommitted changes), in every log and at app start; `getBuildMetadata()` returns just that part (#69).
- Events: `EventBus` delivers typed events immediately (`publish`) or queued from any thread (`enqueue` + `dispatchQueued`), with `Subscription`s that unsubscribe when destroyed; subscribing, unsubscribing and enqueueing from inside a handler are safe (#67).
- Doxygen API reference: `docs` CMake target, `Docs.PublicApi.IsDocumented` test that fails on undocumented public API, existing headers converted to Doxygen comments (#34).
- Asserts: `MEOW2_ASSERT`, `MEOW2_ASSERT_SLOW`, `MEOW2_VERIFY` with a replaceable failure handler (`setAssertHandler`), and `isDebuggerAttached()` (#36).
- This changelog (#36).
- README: step-by-step instructions for generating the API reference (#36).
- The API reference is published to GitHub Pages on every merge to `main`: https://buttception.github.io/Meow2/ (#38).
- `windows-vs2022` presets: generate the Visual Studio solution, build and test from any command prompt; `windows-vs2022-regenerate` refreshes the `.sln`/`.vcxproj` files after CMake changes without compiling (#38).
- Logging: `LogSystem` / `Logger` over spdlog v1.17.0 with five levels (`debug` compiled out of Release, `fatal` flushes and aborts), `[hh:mm:ss.ms]<level> Name: message` lines, a log file per run next to the exe (the repo's `logs/` when started from Visual Studio, `--log-dir` to override), console output in Debug/RelWithDebInfo, and failed asserts written to the log; `executablePath()` (#41).
- Git hooks (`.githooks/`, enabled by CMake): after `git pull`, a rebase or a branch switch that changes CMake files, the Visual Studio solution is regenerated and Visual Studio offers to reload (#41).
- Error handling: `Meow2::Result<T>` (tl::expected v1.3.1) with `Error` (code + message), `ErrorCode`, `fail()` and `std::format` support; misuse such as `*result` on an error fails a Meow2 assert (#45).
- `FrameClock`: measures each frame's time with a monotonic clock, clamps it to 0.25 s so a breakpoint or a stall doesn't turn into one huge step, and counts frames; the apps log "Ran N frames in S s" at exit (#63).
- Release builds of Meow2Player, Meow2Renderer and Meow2Editor no longer open a console window next to their own window (Windows GUI subsystem); Debug and RelWithDebInfo keep the console with the coloured log (#61).
- Editor: Meow2Editor is now a Qt application (Qt ≥ 6.8, tested with 6.12.0): a main window with a native viewport for the renderer, whose size, position, maximized state and fullscreen mode are saved in `Meow2Editor.toml` like the other apps'. Without Qt, configure skips the editor with a warning; README and the Building page explain how to install Qt's MSVC kit and point CMake at it. The editor doesn't use the global `new`/`delete` tracking (#59).
- Window: Meow2Renderer and Meow2Player open an SDL3 window (release-3.4.18) and run until it's closed. Its size, position, maximized state and fullscreen mode are saved in the app's config file and restored at the next start (`--set window.mode=fullscreen` for one run); a position on an unplugged monitor falls back to centred. SDL's memory is counted under `MemTag::Platform` (#57).
- Config: each app keeps its settings in `Meow2<App>.toml` next to its exe (`--config <path>` for another file; runs from Visual Studio use the repo's git-ignored `config/` folder) and accepts `--set key=value` overrides for one run. Changed settings are written back safely, and a file with a syntax error is reported and never overwritten (toml++ v3.4.0) (#55).
- Memory: in Debug and RelWithDebInfo every `new`/`delete` in the apps goes through a process-wide `TrackingAllocator` (counted per `MemTag`, debug fills, guard bytes), and each app logs "No memory leaks" or the leaks per tag when it exits (`ScopedLeakCheck`) (#53).
- Memory: `TrackingAllocator` counts every allocation per `MemTag` (current bytes, peak, live and total allocations), lists leaks per tag, and in Debug/RelWithDebInfo catches heap overruns with guard bytes and wrong frees with asserts; `MallocResource` (`malloc`/`free`, any alignment) sits underneath; `MemTagScope` sets the tag for a block of code; arena blocks are counted under their arena's tag (#51).
- Memory: `BlockArena`, a bump allocator for short-lived data that any `std::pmr` container can use (size-class blocks, `reset()` keeps the blocks so a warm arena makes no heap calls, `trim()`), and `MemTag` for attributing memory to subsystems. In Debug and RelWithDebInfo it fills new memory with `0xCD` and freed or reset memory with `0xDD`, so stale reads stand out (`setDebugFill(false)` for profiling) (#49).
- Crash dumps (Windows): every app writes a minidump next to its log when it crashes (access violation, stack overflow, `abort()`/`MEOW2_LOG_FATAL`/failed asserts, `std::terminate`, uncaught exceptions, pure calls, CRT invalid parameters), in all configurations, without error dialogs; skipped under a debugger (#47).
- Visual Studio filters mirror the engine folders (Subsystem\Module\Backend), and projects are grouped into solution folders (Engine, Apps, Tests, External, Tools) (#38).

### Changed
- M1 rescoped as "Core runtime" and M2 as "Platform"; design decisions for memory, logging, input, events, game loop and releases recorded in techstack.md (#32).
- CI: Linux runs Clang Debug and GCC Debug only (Clang RelWithDebInfo/Release dropped), with cached apt packages (#36).
- Clean-state check before a PR uses a temporary git worktree instead of a clone (AGENTS.md §2.4) (#36).

### Fixed
- The text-file lint crashed CMake (stack overflow in a regex) on files larger than a few tens of KB (#32).
- The API docs workflow failed on `main` because it didn't check out submodules (spdlog is needed to configure); its build now also runs on pull requests, so this is caught before merging (#43).
- CI didn't start at all from #59 on (`startup_failure`): the Linux Qt install used a third-party action, which the repository's Actions settings don't allow. Qt is now installed with aqtinstall in a plain, cached step (#65).

## [0.1.0] — 2026-10-27

M0: Foundation. A repository that builds an empty engine and apps in Debug, RelWithDebInfo and Release, runs tests from the command line, and checks every PR in CI.

### Added
- `.gitattributes` normalising line endings (#3).
- Planning docs: techstack.md, AGENTS.md, milestones.md, README, wiki home (#4).
- CMake skeleton: `Meow2Engine` static library, `Meow2Renderer`, `Meow2Editor`, `Meow2Player`, generated `Meow2/Core/Version.h`, smoke tests (#9).
- `CMakePresets.json`: `windows-msvc-*` and `linux-clang-*` for Debug, RelWithDebInfo and Release, with LTO in Release (#10, landed via #21).
- GoogleTest v1.17.0 submodule and `meow2_add_test_suite()` (#11, landed via #21).
- `.clang-format`, the `format` target and `Format.Check` test, warnings as errors via `meow2_set_warnings()` (#12, landed via #21).
- CI: GitHub Actions with hosted Linux jobs and a self-hosted Windows runner (#17).
- Architecture rules: dependency injection, composition root, no mutable globals, threading (#18).
- `Meow2/Core/Platform.h`: platform, compiler, architecture and build-configuration macros (#19).
- `.editorconfig` and the `Lint.EditorConfig.TextFilesFollowRules` test (#20).
- Claude Code setup: `CLAUDE.md`, one git worktree per session (#23).
- RelWithDebInfo CI jobs; clang-format pinned to version 19; plain data structs without `m_` (#26).
- Building from a standard command prompt with the Visual Studio generator (#28).
- Versioning rule: MINOR per milestone, tags, PATCH for release fixes (#30).

[Unreleased]: https://github.com/buttception/Meow2/compare/v0.1.0...HEAD
[0.1.0]: https://github.com/buttception/Meow2/releases/tag/v0.1.0
