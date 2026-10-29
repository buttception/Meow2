# Changelog

All notable changes to Meow2, per released version. The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and versions follow the rules in [techstack.md §21](techstack.md) (MINOR = milestone).

- **Every PR** adds one or more lines under **[Unreleased]**, in the matching group (`Added`, `Changed`, `Deprecated`, `Removed`, `Fixed`, `Security`), ending with the PR number.
- **The milestone-closing PR** renames `[Unreleased]` to `[0.Y.0] — <date>`, adds a new empty `[Unreleased]` above it, and bumps `VERSION`.
- **The GitHub Release** for a tag uses that version's section as its description (AGENTS.md §2.7).

## [Unreleased]

### Added
- Doxygen API reference: `docs` CMake target, `Docs.PublicApi.IsDocumented` test that fails on undocumented public API, existing headers converted to Doxygen comments (#34).
- Asserts: `MEOW2_ASSERT`, `MEOW2_ASSERT_SLOW`, `MEOW2_VERIFY` with a replaceable failure handler (`setAssertHandler`), and `isDebuggerAttached()` (#36).
- This changelog (#36).
- README: step-by-step instructions for generating the API reference (#36).
- The API reference is published to GitHub Pages on every merge to `main`: https://buttception.github.io/Meow2/ (#38).
- `windows-vs2022` presets: generate the Visual Studio solution, build and test from any command prompt; `windows-vs2022-regenerate` refreshes the `.sln`/`.vcxproj` files after CMake changes without compiling (#38).
- Logging: `LogSystem` / `Logger` over spdlog v1.17.0 with five levels (`debug` compiled out of Release, `fatal` flushes and aborts), `[hh:mm:ss.ms]<level> Name: message` lines, a log file per run next to the exe (the repo's `logs/` when started from Visual Studio, `--log-dir` to override), console output in Debug/RelWithDebInfo, and failed asserts written to the log; `executablePath()` (#41).
- Git hooks (`.githooks/`, enabled by CMake): after `git pull`, a rebase or a branch switch that changes CMake files, the Visual Studio solution is regenerated and Visual Studio offers to reload (#41).
- Error handling: `Meow2::Result<T>` (tl::expected v1.3.1) with `Error` (code + message), `ErrorCode`, `fail()` and `std::format` support; misuse such as `*result` on an error fails a Meow2 assert (#45).
- Memory: `BlockArena`, a bump allocator for short-lived data that any `std::pmr` container can use (size-class blocks, `reset()` keeps the blocks so a warm arena makes no heap calls, `trim()`), and `MemTag` for attributing memory to subsystems (#49).
- Crash dumps (Windows): every app writes a minidump next to its log when it crashes (access violation, stack overflow, `abort()`/`MEOW2_LOG_FATAL`/failed asserts, `std::terminate`, uncaught exceptions, pure calls, CRT invalid parameters), in all configurations, without error dialogs; skipped under a debugger (#47).
- Visual Studio filters mirror the engine folders (Subsystem\Module\Backend), and projects are grouped into solution folders (Engine, Apps, Tests, External, Tools) (#38).

### Changed
- M1 rescoped as "Core runtime" and M2 as "Platform"; design decisions for memory, logging, input, events, game loop and releases recorded in techstack.md (#32).
- CI: Linux runs Clang Debug and GCC Debug only (Clang RelWithDebInfo/Release dropped), with cached apt packages (#36).
- Clean-state check before a PR uses a temporary git worktree instead of a clone (AGENTS.md §2.4) (#36).

### Fixed
- The text-file lint crashed CMake (stack overflow in a regex) on files larger than a few tens of KB (#32).
- The API docs workflow failed on `main` because it didn't check out submodules (spdlog is needed to configure); its build now also runs on pull requests, so this is caught before merging (#43).

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
