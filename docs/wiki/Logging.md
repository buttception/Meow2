# Logging

`#include <Meow2/Core/Log/LogSystem.h>`, built on [spdlog](https://github.com/gabime/spdlog) v1.17.0. No spdlog header appears in the engine's public headers.

## What it looks like

```
[14:03:27.512]<info> Renderer: Meow2Renderer started: engine 0.1.0, Windows / MSVC / Debug
[14:03:27.530]<warn> Engine: Swapchain recreated (1600x900)
```
`[hh:mm:ss.ms]` is 24-hour local time with milliseconds. Then come the level, the logger name (which binary or subsystem logged it), and the message.

| Level | Use | Release |
|---|---|---|
| `debug` | details for developers | **compiled out** (`MEOW2_LOG_DEBUG`) |
| `info` | normal events | ✅ |
| `warn` | unexpected but handled | ✅, flushed to disk at once |
| `error` | an operation failed, the program continues | ✅, flushed at once |
| `fatal` | can't continue: logs, flushes, then **ends the program** | ✅ |

## Where logs go

- **File:** one per run, `Meow2<App>_<yyyy-mm-dd_hh-mm-ss>.log`, rotating at 10 MiB (5 old files kept). Every configuration writes it.
- **Folder:** `logs/` **next to the executable** by default. `--log-dir <path>` (or `--log-dir=<path>`) overrides it.
  - **Started from the Visual Studio solution** (F5), CMake makes the debugger pass `--log-dir <repo>/logs`, so IDE runs log into the repository's git-ignored `logs/` folder.
  - **Started directly** (double-click, command line), the same exe logs next to itself.
- **Console:** Debug and RelWithDebInfo also print to standard output, with the level coloured. Release doesn't, and Release builds of the apps are **Windows GUI-subsystem** programs, so starting them opens no console window at all. Their output still reaches a pipe or file the caller redirects (that's how the smoke tests read it). See [Building → Release builds without a console](Building.md#release-builds-without-a-console).
- If the file can't be created (no permission, bad path), logging continues on the console only, with a warning. Logging never stops a program from starting.

## Using it

The composition root (each app's `main`) creates the LogSystem and the loggers, then injects the loggers (techstack.md §18):

```cpp
#include <Meow2/Core/Log/AssertLogging.h>
#include <Meow2/Core/Log/LogSystem.h>

int main(int argc, char** argv)
{
    using namespace Meow2::Log;
    const auto logDir = logDirectoryFromArguments(argc, argv).value_or(defaultLogDirectory());
    LogSystem logSystem{{.appName = "Renderer", .directory = logDir}};
    Logger engineLog = logSystem.createLogger("Engine");     // engine code gets this one
    Logger appLog = logSystem.createLogger("Renderer");      // app code gets this one
    const ScopedAssertLogging assertLogging{engineLog};       // failed asserts go into the log too

    Renderer renderer{{.logger = engineLog, /* ... */}};
}
```

In a class:

```cpp
class Window
{
public:
    explicit Window(Meow2::Log::Logger& log) : m_log(log) {}

    void resize(int width, int height)
    {
        MEOW2_LOG_INFO(m_log, "Resized to {}x{}", width, height);   // std::format syntax, checked at compile time
        MEOW2_LOG_DEBUG(m_log, "DPI scale {}", dpiScale());         // dpiScale() isn't even called in Release
    }

private:
    Meow2::Log::Logger& m_log;
};
```

- **Always use the macros** for `debug`: `MEOW2_LOG_DEBUG` removes the call *and its arguments* from Release. Calling `log.debug(...)` directly still logs.
- Messages use `std::format` placeholders (`{}`, `{:.2f}`, `{:>8}`). A wrong number of arguments is a **compile error**.
- **There's no global logger.** Code that needs to log takes a `Logger&` in its constructor.
- `MEOW2_LOG_FATAL(log, ...)` never returns. It writes the line, flushes every sink, breaks into the debugger if one is attached, then aborts. The crash handler, later in M1, adds a `.dmp` next to the log.

## In-app console

An app that shows its log on screen (Meow2Renderer's Console window, later the editor's console dock) gives its `LogSystem` a `ConsoleBuffer`:

```cpp
Meow2::Log::ConsoleBuffer consoleLines{2000};   // declared first: every logger writes into it
Meow2::Log::LogSystem logSystem{{.appName = "Renderer", .directory = logDirectory, .consoleBuffer = &consoleLines}};
...
std::vector<Meow2::Log::LogLine> lines;
const std::uint64_t missed = consoleLines.linesSince(next, lines);   // the UI, once a frame
```

- It's a **ring** of the last N lines. Each line has a sequence number; a reader keeps the next one it hasn't seen and asks for what came since, and is told how many it missed if it fell more than N lines behind.
- **All its memory is taken in the constructor**: one fixed slot per line (about 1 KB: time, level, a 31-byte logger name, a 1000-byte message). `add()` copies into a slot and never allocates, so logging stays cheap on every thread. It also keeps the leak check honest: the buffer has to exist before the check (which needs a logger), so lines it allocated during the run would otherwise be reported as leaks at exit.
- A longer message is cut on a UTF-8 character boundary and ends in `…`; the log file keeps it whole.
- The sink that fills it (`ConsoleRingSink`, in `LogSystem.cpp`) formats only the time; level, logger name and text stay separate fields, so a console can colour and filter them.

## Asserts in the log

`ScopedAssertLogging` (from `Meow2/Core/Log/AssertLogging.h`) installs an assert handler that writes the failure to the log as an `error` and flushes it, before the usual stderr output and break/abort (see [Asserts](Asserts.md)):
```
[14:05:11.204]<error> Engine: Assertion failed: index < m_count (index out of range) at F:/Meow2/.../Foo.cpp(42) in getItem
```
Its destructor restores the previous handler.

## How it works

- `LogSystem` creates the **sinks** once: a `rotating_file_sink_mt`, plus a `stdout_color_sink_mt` when the console is on. Every `Logger` it creates is an `spdlog::logger` over the **same** sinks, so all channels end up in one file, in order.
- The loggers are **not** registered in spdlog's global registry, and spdlog's default logger is disabled (`SPDLOG_DISABLE_DEFAULT_LOGGER`). Loggers are owned and injected like any other dependency.
- The level names come from a custom pattern flag (`%*`). spdlog's own names would be `warning`/`critical`.
- Messages are formatted with `std::format` in the engine, then handed to spdlog as plain text, so `{}` inside a message is never re-interpreted.
- On Windows, file names are passed as `wchar_t` (`SPDLOG_WCHAR_FILENAMES`), so log folders with non-ASCII characters work.
- `warn` and above trigger an immediate flush (`flush_on`), so the last lines before a crash are on disk.

## Thread safety

`Logger` and `LogSystem` are thread-safe: any thread may log at any time (spdlog's `_mt` sinks lock internally). The order between threads is the order the sinks receive the lines.

## Tests

`Meow2EngineCoreTests`:
- `Core.Logger.*`: line format, level names, minimum level, shared sinks, braces in messages, debug compiled out of Release, and `fatal` (death test, then the file is checked).
- `Core.LogSystem.*`: folder creation and file name, falling back to the console when the folder is unusable, `--log-dir` parsing, the default folder.
- `Core.AssertLogging.*`: a failed assert ends up in the log (death test), and the previous handler is restored.
- `Core.ConsoleBuffer.*`: the newest lines kept in order, reading only new lines and counting missed ones, many writers with a reader at once (TSan), long messages cut on a character boundary; `Core.LogSystem.WritesToTheConsoleBuffer`.

## Not yet

- The editor's console dock (M7) on the same `ConsoleBuffer`.
- The runtime level from `config.toml` comes with the Config feature.
- Release builds are GUI-subsystem executables (no console window) since #61.

## Implementation summary

- `Meow2/Core/Log/` — `LogSystem` (sinks, `createLogger`), `Logger` (`std::format` messages, `[[noreturn]] fatal`), `ScopedAssertLogging` (failed asserts → `error` + flush). Messages are formatted with `std::format` and passed to spdlog as plain text; `warn` and above flush immediately.
