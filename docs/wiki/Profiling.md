# Profiling

Debug and RelWithDebInfo builds contain the client of the [Tracy](https://github.com/wolfpld/tracy) profiler. Connect the Tracy viewer to a running app and you see, live:
- a timeline of every thread, with zones for engine work and the jobs;
- frame times;
- memory per tag.

Release builds contain none of it.

- Header: `#include <Meow2/Core/Profiling/Profile.h>`.
- Library: Tracy v0.14.1, client only (submodule `external/tracy`), used only in `src/Core/Profiling/Tracy/`.
- Status: ✅ M1 (#72).

## Looking at a running app

1. Download the **Tracy viewer** (`tracy-profiler.exe`) for **v0.14.1** from https://github.com/wolfpld/tracy/releases. The viewer and client versions must match. The viewer isn't in the repository (techstack.md §4).
2. Start a Debug or RelWithDebInfo build of Meow2Player, Meow2Renderer or Meow2Editor.
3. In the viewer, click **Connect** (address `127.0.0.1`).

What you'll see:
- **Threads:** `Main` and `Job thread 1…N`.
- **Zones:** `Platform::pumpEvents`, `EventBus::dispatchQueued`, `Config::load`, and one `Job` zone per piece of every `parallelFor`.
- **Frames:** one per loop iteration (`MEOW2_PROFILE_FRAME_MARK()`).
- **Plots:** `Memory/General`, `Memory/Jobs`, `Memory/Platform`, …: each tag's current bytes, once per frame.

## Adding zones

```cpp
#include <Meow2/Core/Profiling/Profile.h>

void Physics::step()
{
    MEOW2_PROFILE_ZONE();                       // named after the function, until the end of the scope
    {
        MEOW2_PROFILE_ZONE_NAMED("Broadphase"); // a string literal
        broadphase();
    }
}
```

| Macro | What it records |
|---|---|
| `MEOW2_PROFILE_ZONE()` / `MEOW2_PROFILE_ZONE_NAMED("name")` | Time from here to the end of the scope, on the current thread. |
| `MEOW2_PROFILE_FRAME_MARK()` | The end of a frame. Call once per frame, in the app's loop. |
| `MEOW2_PROFILE_THREAD_NAME("name")` | The current thread's name. |
| `MEOW2_PROFILE_MEMORY(tracker)` | Each memory tag's current bytes, as plots. Call once per frame. |

All of them compile to nothing when profiling is off: in Release, and with `-DMEOW2_PROFILING=OFF` (the sanitizer builds). `Meow2::Profiling::c_profilingEnabled` says which.

## Privacy and cost

- **Localhost only:** the client listens on `127.0.0.1:8086` and never announces itself on the network (`TRACY_ONLY_LOCALHOST`, `TRACY_NO_BROADCAST`).
- **On demand:** nothing is recorded until a viewer connects (`TRACY_ON_DEMAND`). An unprofiled Debug run costs a listening thread and a cheap check per zone.
- **Our crash handler stays in charge:** Tracy's own crash handler is off (`TRACY_NO_CRASH_HANDLER`), so crash dumps work as before ([CrashDumps](CrashDumps.md)).

## How it works

- **No Tracy header in public code.** The macros create a `static const Meow2::Profiling::SourceLocation` per zone, our own struct with the same layout as Tracy's C `___tracy_source_location_data`. A compile-time check (`std::is_layout_compatible`) keeps the two identical. `ScopedZone` calls Tracy's C API from `ProfileTracy.cpp` and keeps Tracy's zone context as 16 opaque bytes.
- **Per configuration:** `TRACY_ENABLE` is added to the Tracy library with a generator expression (Debug and RelWithDebInfo, if `MEOW2_PROFILING` is on), and so is `MEOW2_PROFILING=1|0` for the engine. In the Visual Studio solution each configuration gets the right one.
- **Job threads** are named by enkiTS's thread-start callback ([Jobs](Jobs.md)).
- **Memory is plotted, not traced per allocation.** Tracy can record every allocation, but ours start before Tracy does (static initialisation) and end after it shuts down (static destructors), and in on-demand mode it would see frees of blocks allocated before the viewer connected. Plotting each tag's current bytes once per frame shows the same picture per subsystem, safely. Per-allocation events (with call stacks) can be added later for one tag at a time if a leak needs hunting.

## Not yet

- GPU zones (Vulkan and D3D12, M5).
- Locks and messages.
- Per-allocation memory events.

See also: techstack.md §3/§4, [Jobs](Jobs.md), [Memory](Memory.md).
