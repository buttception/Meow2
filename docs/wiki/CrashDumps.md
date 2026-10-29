# Crash dumps

`#include <Meow2/Core/Crash/CrashHandler.h>`

When a Meow2 program crashes, it writes a **minidump** next to that run's log and records it in the log:

```
logs/
  Meow2Renderer_2026-10-29_22-28-59.log     ← "... <error> Engine: Crash (unhandled exception 0xC0000005): dump written to ..."
  Meow2Renderer_2026-10-29_22-28-59.dmp     ← the dump
```

This happens in **every configuration, Release included**: Release is what players run, so its dumps are the ones you'll most want.

## What a minidump is

A minidump is a snapshot of the process at the moment of the crash:
- the call stack and registers of every thread;
- the loaded modules (exe and DLLs, with their versions);
- the memory those stacks point to.

It's usually 100 KB to a few MB, not the whole process memory. With the matching **`.pdb`** files (debug symbols, which every Meow2 build keeps and release zips include), a debugger shows the crash as if it had been attached at that moment: the source line, the call stack, local variables.

## Opening a dump in Visual Studio

1. **File → Open → File…** and pick the `.dmp`.
2. On the *Minidump File Summary* page, click **Debug with Native Only**.
3. Visual Studio stops at the crash. If the stack shows `Meow2Renderer.exe!<unknown>` instead of function names, it can't find the PDBs:
   - **Set symbol paths** (on the summary page), add the folder that has the `.pdb` files (the build's `bin/<Config>/`, or the unzipped release), and reload.
   - The PDBs must come from **exactly the build that crashed**. A rebuild, even of the same commit, produces non-matching PDBs.
4. If the source isn't found, point Visual Studio at the repository checked out at that version (`git checkout v0.2.0`, …).

For crashes without an exception (`abort()`, `std::terminate`, …), the dump opens one or two frames inside Meow2's crash handler. Look further down the **Call Stack** window for your code.

## What counts as a crash

| Crash | Example | Log says |
|---|---|---|
| Unhandled structured exception | null-pointer write, stack overflow, divide by zero | `unhandled exception 0xC0000005` (access violation), `0xC00000FD` (stack overflow), … |
| Uncaught C++ exception | `throw` with no `catch` | `uncaught C++ exception` |
| `abort()` | `MEOW2_LOG_FATAL`, a failed `MEOW2_ASSERT` (no debugger), `std::abort()` | `abort()` |
| `std::terminate` | called directly, `noexcept` violated | `std::terminate (…)` |
| Pure virtual call | calling a virtual function from a base-class destructor | `pure virtual function call` |
| CRT invalid parameter | `printf(nullptr)`, bad file descriptor | `CRT invalid parameter` |

**With a debugger attached, no dump is written:** the debugger stops at the crash, which is more useful.

## Using it

The composition root creates it right after logging and keeps it alive until the end of `main` (all three apps already do this):

```cpp
Meow2::Log::Logger engineLog = logSystem.createLogger("Engine");
const Meow2::Log::ScopedAssertLogging assertLogging{engineLog};

std::filesystem::path dumpPath = logSystem.filePath();     // pair the dump with this run's log
dumpPath.replace_extension(".dmp");
const Meow2::Crash::CrashHandler crashHandler{{.dumpPath = dumpPath}, engineLog};
```

Only one may exist at a time (asserted). Its destructor restores whatever handlers were installed before.

## How it works (Windows)

```
crashing thread                                 watchdog thread (created at startup, waiting)
───────────────                                 ────────────────────────────────────────────
crash → OS/CRT calls our hook
  record exception pointers + reason
  SetEvent(crash) ───────────────────────────►  wakes up
  wait (≤ 10 s)                                  MiniDumpWriteDump(...) → .dmp
                                                 log "Crash (...): dump written to ..." + flush
                                   ◄──────────  SetEvent(done)
  end the process
```

- **Why a second thread:** after a stack overflow, the crashing thread has almost no stack left. After memory corruption, the heap may be broken. So the crashing thread only stores a few values and signals an event. All the real work (file I/O, `MiniDumpWriteDump`, formatting the log line) happens on a healthy thread that was created at startup, while creating threads still worked. Microsoft also recommends calling `MiniDumpWriteDump` from a different thread than the one that crashed.
- **The hooks:**
  - `SetUnhandledExceptionFilter` (structured exceptions);
  - `signal(SIGABRT)` (`abort`);
  - `std::set_terminate`;
  - `_set_purecall_handler`;
  - `_set_invalid_parameter_handler`.

  `abort`, `terminate`, pure calls and invalid parameters don't come with a CPU context, so the hook raises a private structured exception first (`RaiseException` inside `__try`/`__except`). The dump then has real register state and opens near the crash site.
- **No dialogs:** `_set_abort_behavior` turns off the "abort() has been called" message box and Windows Error Reporting for `abort()`. Without a debugger, Debug-CRT check failures print to stderr instead of opening a dialog. The unhandled-exception path ends the process directly. That matters for CI and for players: a crashed game shouldn't hang on a hidden dialog.
- **Bounded:** the crashing thread waits at most 10 seconds for the watchdog. If two threads crash at once, the first writes the dump and the second waits.
- **Dump contents:** `MiniDumpWithIndirectlyReferencedMemory | MiniDumpWithThreadInfo | MiniDumpWithHandleData | MiniDumpWithUnloadedModules`. That's enough to inspect locals, without dumping the whole heap.

## Linux / Android

Not implemented yet: `CrashHandler` does nothing there (`isActive()` is `false`) and logs that at `debug` level. A signal handler can't safely write a dump itself. The standard approach is an out-of-process writer (crashpad or breakpad), planned with the Platforms milestone.

## Tests

`Meow2CrashProbe` (`Meow2Engine/tests/Core/Crash/CrashProbe.cpp`) sets up logging and a CrashHandler like an app does, then crashes the way it's told. `cmake/RunCrashProbe.cmake` runs it in a fresh folder and checks:
- the process failed;
- there's exactly one `.dmp`, starting with the minidump signature `MDMP`;
- the log reports the dump.

The tests are `Core.CrashHandler.*`, labels `integration;windows`:
- `NoCrashNoDump`;
- `AccessViolationWritesDump`, `StackOverflowWritesDump`;
- `AbortWritesDump`, `TerminateWritesDump`, `CppExceptionWritesDump`, `InvalidParameterWritesDump`;
- `FatalLogWritesDump`, `FailedAssertWritesDump` (in Release, asserts are off, so this one checks there's no crash).
