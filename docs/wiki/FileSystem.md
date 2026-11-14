# File system

Engine code reads and writes files by **virtual path**, like `assets/textures/cat.png`. The first part names a **mount**, and the app decides what each mount is: a folder on disk today, a game package once games are exported (M12), Android's built-in assets later (M14). The same game code then runs from the editor's project folder, an exported game and a phone.

- Header: `#include <Meow2/Platform/FileSystem/FileSystem.h>`. Namespace `Meow2::FileSystem`.
- Status: ✅ M2 (#99). Folder mounts; packages and Android assets come with M12/M14.

## Using it

```cpp
Meow2::FileSystem::FileSystem files{engineLog};                                // composition root, then injected
files.mountDirectory("assets", projectFolder / "assets");                       // read-only (default)
files.mountDirectory("save", savesFolder, Meow2::FileSystem::MountAccess::ReadWrite);

Meow2::Result<std::string> shader = files.readText("assets/shaders/basic.hlsl");
Meow2::Result<std::vector<std::byte>> image = files.readFile("assets/textures/cat.png");
files.writeText("save/slot1/progress.toml", text);                             // creates folders, atomic
for (const Meow2::FileSystem::DirectoryEntry& entry : *files.list("assets/levels")) { /* ... */ }
```

| API | What it does |
|---|---|
| `mountDirectory(name, folder, access)` | makes a folder available as `name/…`; `NotFound` if it doesn't exist, `AlreadyExists` if the name is taken |
| `unmount(name)`, `isMounted(name)` | |
| `readFile(path)` / `readText(path)` | the whole file as bytes / text (unchanged: no newline or encoding conversion) |
| `writeFile(path, bytes)` / `writeText(path, text)` | only on `ReadWrite` mounts (`AccessDenied` otherwise); creates folders; **atomic** |
| `exists(path)`, `fileSize(path)` | |
| `list(folder)` | entries sorted by name, each with `isDirectory`; the mount name alone lists its root |
| `nativePath(path)` | the real path, for the editor and external tools |

Everything returns a `Result`: `InvalidArgument` for a bad path, `NotFound` for an unknown mount or a missing file, `AccessDenied`/`IoError` for OS failures.

## Path rules

- `<mount>/<path inside it>`, with `/` as the separator on every OS, in **UTF-8** (non-ASCII names work, on Windows too).
- **Exact case:** `Cat.png` and `cat.png` are different files on Linux and Android, so write the real name. Windows would forgive it; the other platforms won't.
- Rejected: an empty path, a leading `/`, `\`, `:` (drive letters, Windows file streams), `a//b`, a trailing `/`, `.` and `..`. **No path can leave its mount**, so data from outside (a mod, a level file, an MCP command) can't read `../../Windows`.
- Mount names: letters, digits, `_` and `-`.

## Atomic writes

`writeFile()` writes `name.tmp` first, then renames it over `name`. If the game crashes or the disk fills up halfway, the old file is still there, intact, instead of a half-written save. It's the same technique `Config` uses for settings files.

## Threading

Any thread can read, write and query at the same time; asset loading runs in jobs. The mount table is guarded by a `std::shared_mutex`:
- a call looks up its mount under a **shared** lock, which many readers can hold at once;
- it releases the lock **before** any file I/O, so a slow disk never blocks mounting;
- `mountDirectory`/`unmount` take the lock exclusively.

Two threads writing the *same* file at once is still a race on the file itself (the last rename wins). `Platform.FileSystemTest.ConcurrentReads` runs 4 threads × 200 reads, and the TSan CI job checks it for races.

## How it works

- **Mounts are a `std::variant`**, today just `DirectoryMount{root}`. Packages and Android assets will be more alternatives, handled with `std::visit`. There are no virtual calls, matching techstack.md §3: the kinds are known at compile time, but several can be mounted at once.
- Virtual paths become native paths through `char8_t` (`std::u8string`). Without that, the Windows standard library would read a `char` path in the ANSI code page and mangle non-ASCII names.
- OS errors (`std::error_code`) map to engine error codes: missing → `NotFound`, permission → `AccessDenied`, everything else → `IoError`.

## Not yet

- **Package mounts** (M12) and **Android assets** (M14).
- **Overlay mounts** (several sources under one name, later ones winning, for mods and patches).
- **File watching** for hot reload (shaders in Meow2Renderer, M4).
- **Async loading:** jobs call the file system directly for now; streaming comes with the asset system.
- The apps don't mount anything yet; the first user is the asset system.

See also: techstack.md §1 (portability rules), §3, §15, [Config](Config.md).

## Implementation summary

- virtual paths `<mount>/…` (no escaping a mount), folder mounts read-only/read-write, atomic writes, thread-safe (shared lock, no lock during I/O), mounts as a `std::variant`; see [docs/wiki/FileSystem.md](docs/wiki/FileSystem.md)
