# Config

Settings that last between runs, such as the window size, live in a small **TOML** file per app. You can override any of them for one run on the command line. When the app changes one, it's written back at exit.

- Header: `#include <Meow2/Core/Config/Config.h>`, namespace `Meow2::Config`
- Library: [toml++](https://github.com/marzer/tomlplusplus) v3.4.0 (submodule `external/tomlplusplus`, header-only), used only in `src/Core/Config/Config.cpp`
- Status: ✅ M1 (#54). The window settings come with the SDL window (M2).

## The file

Each app has its own file, **`Meow2<App>.toml` next to its `.exe`**, e.g. `build/windows-msvc-debug/bin/Meow2Renderer.toml`.

- **`--config <path>`** (or `--config=<path>`) uses another file.
- **Started from the Visual Studio solution**, the apps use `<repo>/config/Meow2<App>.toml`. That folder is git-ignored, so the settings survive rebuilds and clean build folders.
- **First run:** there's no file yet. The app runs with its built-in defaults and creates the file the first time it saves a change.

The format is plain [TOML](https://toml.io):

```toml
# Meow2Renderer.toml
[window]
width = 1600
height = 900
mode = "windowed"

[renderer]
scale = 1.0
```

A setting is read by its **dotted key**: `window.width` is `width` in the `[window]` table. Keys are bare TOML keys (letters, digits, `_`, `-`).

## Overriding settings for one run

```
Meow2Renderer.exe --set window.width=800 --set window.mode=fullscreen
```

- `--set key=value` (or `--set=key=value`) can be repeated. It wins over the file, **for this run only**: overrides are never saved.
- **Values:**
  - a TOML value is used as one: `800`, `1.5`, `true`, `"quoted text"`;
  - anything else is taken as text, so `fullscreen` and `My Game` work without quotes.
- An invalid argument (no `=`, or a bad key) is logged as a warning and skipped. The valid ones still apply.

## Where a value comes from

| Priority | Source | Saved? |
|---|---|---|
| 1 | `--set key=value` on the command line | never |
| 2 | the file, plus values the app changed with `set*()` | yes, by `save()` |
| 3 | the default the code passes to `get*()` | no |

**Defaults live in the code.** The file holds only what's been changed, so a new version of the app can change a default and it takes effect wherever the user hasn't set that value.

## Using it in code

The composition root opens the config once and hands out *values*, not the Config. Subsystems get their settings as plain structs (techstack.md §18):

```cpp
Meow2::Config::Config config = Meow2::Config::openAppConfig("Renderer", argc, argv, engineLog);

const WindowDesc windowDesc{
    .width = static_cast<std::uint32_t>(config.getInt("window.width", 1280)),
    .title = config.getString("window.title", "Meow2Renderer"),
};
// ... run ...
config.setInt("window.width", window.width());               // remember for next time
if (const Meow2::Result<void> saved = config.save(); !saved) // writes only if a value changed
{
    MEOW2_LOG_WARN(appLog, "Settings not saved: {}", saved.error());
}
```

| Function | What it does |
|---|---|
| `openAppConfig(app, argc, argv, log)` | Finds the file (`--config` or the default), loads it, applies `--set`, and logs the path. Always returns a usable Config. |
| `getBool` / `getInt` / `getFloat` / `getString(key, default)` | The value, or `default` if it's missing. A value of the wrong type (`width = "wide"`) logs a warning and returns the default too. `getFloat` also accepts integers (`scale = 2`). |
| `contains(key)` | Whether the key has a value. |
| `setBool` / `setInt` / `setFloat` / `setString(key, value)` | Changes the file layer. Missing tables are created. An override of the same key is dropped, so the new value is what `get*()` returns. |
| `save()` | Writes the file if something changed; does nothing otherwise. |
| `Config::load(path, log)` / `Config::empty(path, log)` | Lower level: what `openAppConfig` uses. |

`Config` isn't thread-safe. Read it on the main thread at startup and pass the values on.

## How it works

- **Two TOML tables:** the file layer (what `save()` writes) and the overrides. `get*()` looks in the overrides first, then the file. Only `set*()` touches the file layer.
- **Writes only on change.** `set*()` compares with the current value, and `save()` writes only if something actually changed. That matters because toml++ rewrites the file from its parsed data, which drops **comments and formatting**. A hand-written file stays exactly as it is until the app changes one of its values.
- **Safe writes.** `save()` writes `<file>.tmp` and then renames it over the old file. A crash or full disk while writing leaves the old file intact rather than half a file. Missing folders are created.
- **A broken file isn't overwritten.** If the file has a syntax error, `openAppConfig` logs it with the line and column (`error: Meow2Renderer.toml(3:10): …`), runs with defaults, and **disables saving** (`save()` returns `ErrorCode::Unsupported`). A typo doesn't cost you your settings: fix the file and restart.
- **Errors as values.** toml++ is compiled with `TOML_EXCEPTIONS=0`, so a parse error comes back as data and is turned into a `Result<Config>` with `ErrorCode::ParseError` ([ErrorHandling](ErrorHandling.md)).
- **Memory:** toml++'s allocations happen inside `MemTagScope{MemTag::Config}`, so they're counted under `Config` in the memory stats ([Memory](Memory.md)).
- **toml++ stays private.** The public header only has engine types (pimpl), and only `Config.cpp` includes toml++. It could be replaced without touching any caller.

## Limits (for now)

- Readable types: booleans, integers, floats and strings. Arrays and dates in the file are kept when saving, but can't be read through `Config` yet.
- `log.dir` can't be set in the config file yet. Logging starts before the config is read, so that the config's own errors can be logged. Use `--log-dir`.
- Saving loses comments in the file (see above).

See also: techstack.md §3, [Logging](Logging.md) (`--log-dir`), API reference `Meow2::Config::Config`.
