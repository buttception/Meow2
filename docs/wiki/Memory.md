# Memory

How Meow2 allocates memory. Everything is built on the C++ standard's allocator interface, `std::pmr::memory_resource`, so any `std::pmr` container (`std::pmr::vector`, `std::pmr::string`, `std::pmr::unordered_map`, …) can use any engine allocator. Design: [techstack.md §6](../../techstack.md).

| Piece | Status |
|---|---|
| `MemTag`, `MemTagScope`: which subsystem memory belongs to | ✅ |
| `BlockArena`: bump allocator for short-lived data | ✅ |
| `MallocResource`: `malloc`/`free` at the bottom | ✅ |
| `TrackingAllocator`: per-tag statistics, leak report, guard bytes | ✅ |
| Global `operator new`/`delete` tracking, leak report at app exit | ✅ |
| Allocator hooks of SDL, enkiTS, ImGui, Jolt, VMA/D3D12MA | ⬜ with each library |
| `FrameArena`: one arena per frame in flight × thread | ⬜ M5 |

## Memory tags

`Meow2::Memory::MemTag` (`<Meow2/Core/Memory/MemTag.h>`) names the subsystem an allocation is counted under: `General`, `Logging`, `Config`, `Jobs`, `Events`, `Platform`, `Renderer`, `Rhi`, `Physics`, `Assets`, `Editor`, `ImGui`, `Profiler`. `memTagName(tag)` returns the name as text, for statistics and reports. Every arena is created with a tag, and the tracking allocator keeps its counters (current bytes, peak, allocation counts) per tag, so a report can say "Physics: 40 MiB".

### The current tag (`MemTagScope`)

Most allocations can't pass a tag (a `std::pmr::vector` only knows its resource; later, every `new`). They're counted under the calling thread's **current tag**, which a `MemTagScope` sets for a block of code and restores at its end:

```cpp
{
    const Meow2::Memory::MemTagScope scope{Meow2::Memory::MemTag::Physics};
    std::pmr::vector<Body> bodies{&tracker};   // counted under Physics
}                                              // back to the previous tag (General outside any scope)
```

The tag is per thread (`thread_local`), so a scope on one thread never affects another. `currentMemTag()` returns it.

## MallocResource

`Meow2::Memory::MallocResource` (`<Meow2/Core/Memory/MallocResource.h>`) is the bottom of the stack: a `memory_resource` that calls `malloc`/`free` directly. It never uses `operator new`, so it keeps working underneath the global `operator new` replacement (it would otherwise call itself). Alignments above 16 bytes are handled portably by over-allocating and keeping `malloc`'s pointer just in front of the aligned address. It's thread-safe and stateless.

## TrackingAllocator

`Meow2::Memory::TrackingAllocator` (`<Meow2/Core/Memory/TrackingAllocator.h>`) wraps an upstream resource (usually a `MallocResource`) and counts every allocation per tag.

```cpp
Meow2::Memory::MallocResource mallocResource;
Meow2::Memory::TrackingAllocator tracker{mallocResource};

Meow2::Memory::BlockArena arena{tracker, Meow2::Memory::MemTag::Renderer};   // blocks counted under Renderer
void* imguiMemory = tracker.allocateTagged(size, 16, Meow2::Memory::MemTag::ImGui); // explicit tag (library hooks)

const Meow2::Memory::MemTagStats stats = tracker.stats(Meow2::Memory::MemTag::Renderer);
// stats.currentBytes, stats.peakBytes, stats.liveAllocations, stats.totalAllocations

if (tracker.hasLeaks())
{
    engineLog.warn("{}", tracker.leakReport());
}
```

- **Per allocation**, the tracker asks upstream for a bit more: a header in front (magic value, size, tag, alignment) and 16 guard bytes behind. The caller only sees its own bytes; `stats()` counts only those (no overhead).
- **Tag:** `allocate()` uses the current tag (`MemTagScope`); `allocateTagged()` takes one explicitly. Freeing reads the tag from the header, so memory can be freed on any thread, under any scope.
- **Statistics** per tag: current bytes, peak bytes, live allocations, total allocations; `totalStats()` sums all tags (its peak is the sum of the per-tag peaks, an upper bound).
- **Leak report:** `leakReport()` lists the live allocations per tag:
  ```
  Memory leaks: 3 allocations, 1546 bytes still allocated
    Physics: 2 allocations, 1536 bytes
    Assets: 1 allocations, 10 bytes
  ```
  At app exit, `ScopedLeakCheck` (below) prints the same kind of report for what leaked during the run.
- **Checks** (asserts in Debug and RelWithDebInfo):
  - freeing a pointer the tracker didn't allocate, or freeing it twice (wrong magic in front of it; the block is then leaked rather than corrupting the heap);
  - freeing with a different size than was allocated;
  - **heap overruns:** with debug fill on, the 16 guard bytes are filled with `0xFD` and checked on free. Writing even one byte past the end of an allocation fails an assert when it's freed.
- **Debug fill:** new memory `0xCD`, freed memory `0xDD`, guards `0xFD` (see below). `setDebugFill(false)` turns fills and guard checks off, e.g. while profiling.
- **Thread-safe without a lock:** the counters are `std::atomic`. The peak is updated with a compare-and-swap loop.
- Meant for Debug and RelWithDebInfo. Release uses `MallocResource` directly: no headers, no counters (techstack.md §5).

## Global `new`/`delete` tracking

In **Debug and RelWithDebInfo**, every Meow2 executable replaces the global `operator new` and `operator delete`. So every C++ heap allocation goes through one process-wide `TrackingAllocator`, `Meow2::Memory::globalTracker()` (`<Meow2/Core/Memory/GlobalTracking.h>`). That includes `std::string`, `std::vector`, `std::make_unique`, spdlog, and any library that uses `new`. Each one is counted under the calling thread's current tag:

```cpp
{
    const Meow2::Memory::MemTagScope scope{Meow2::Memory::MemTag::Physics};
    auto world = std::make_unique<PhysicsWorld>();   // counted under Physics
}
const auto physics = Meow2::Memory::globalTracker().stats(Meow2::Memory::MemTag::Physics);
```

- **Which executables:** those whose CMake calls `meow2_track_global_new(<target>)`, which are the three apps and `Meow2EngineGlobalNewTests`. The other test suites keep the normal heap.
- **Every form is replaced** (20 functions): plain and array, default and over-aligned (`alignas`), throwing and `nothrow`, and every `delete` including the sized ones. So no allocation slips past the tracker, and no tracked block ever reaches the CRT's `free`.
- **You get the tracker's checks for free:** `0xCD` in new memory, `0xDD` in freed memory, `0xFD` guard bytes that catch writes past the end of any `new`'d object, and asserts on wrong frees.
- **The tracker lives for the whole process.** It's `constinit`, ready before the first global constructor runs, and never destroyed. A `delete` that runs after `main` returns (in a static destructor) still finds it. That's why it isn't owned by `main` like other subsystems.
- **Release** replaces nothing: `new` is the CRT's, with no overhead. `MEOW2_MEMORY_TRACKING=0` turns the replacement off in any build; sanitizer builds need that, because AddressSanitizer replaces `new` itself.
- **Cost:** each allocation carries a 48-byte header and 16 guard bytes, and is filled. That's fine for Debug. While profiling RelWithDebInfo, `globalTracker().setDebugFill(false)` removes the fills.
- **Why it's compiled into each executable:** the replacement lives in `src/Core/Memory/GlobalNewDelete.cpp`, and `meow2_track_global_new()` adds it to each executable rather than to the `Meow2Engine` static library. A linker only takes objects out of a static library when something refers to them by name, and nothing refers to `operator new` that way, so the replacement could silently be dropped.

## Leak check at exit

`Meow2::Memory::ScopedLeakCheck` (`<Meow2/Core/Memory/LeakCheck.h>`) records the tracker's live allocations per tag when it's created, and compares when it's destroyed. Each app creates one right after its logging and crash handler, so it covers the app's whole run:

```
[22:30:53.961]<info> Engine: No memory leaks (9 allocations during the run)
```

or, if something wasn't freed:

```
[22:30:53.961]<warn> Engine: Memory leaks: 1 allocations, 1234 bytes not freed
  Assets: 1 allocations, 1234 bytes
```

- The smoke tests `Smoke.<App>.NoLeaks` fail if an app doesn't end with "No memory leaks" (Debug and RelWithDebInfo).
- To find a leak: the tag tells you the subsystem. Then break on allocations of that size, or run the ASan CI job (M1), which reports the exact call stack.
- **Limits:** it counts allocations, it doesn't identify them.
  - An unrelated free of a block allocated *before* the check started can hide a leak in the same tag.
  - Memory freed only after `main` returns (by a static destructor) shows as a leak.

  Engine code has no mutable statics, so neither happens there today.

## BlockArena

`Meow2::Memory::BlockArena` (`<Meow2/Core/Memory/BlockArena.h>`) is an **arena**: it gets large blocks from an upstream resource and hands out memory by moving a pointer forward through them. Freeing a single allocation does nothing; `reset()` makes all of it available again at once.

### When to use it

For data with a clear end of life that's shared by many allocations: everything built during one frame, one job, one level load, or one event dispatch. Allocation is a few instructions instead of a heap call, there's no per-allocation header, and nothing can leak or fragment, because everything goes at once.

Don't use it for long-lived objects that are freed one by one (they'd never come back until `reset()`), or from several threads at once (it isn't thread-safe; use one arena per thread).

### Usage

```cpp
#include <Meow2/Core/Memory/BlockArena.h>

Meow2::Memory::BlockArena arena{*std::pmr::new_delete_resource(), Meow2::Memory::MemTag::Renderer};

for (;;) // e.g. once per frame
{
    {
        std::pmr::vector<DrawItem> drawItems{&arena};
        drawItems.reserve(visibleCount);                   // see "Pitfalls"
        std::pmr::vector<std::pmr::string> labels{&arena}; // nested strings use the arena too
        // ... build and use this frame's data ...
    }                                                      // containers gone before the reset
    arena.reset();                                         // all memory back at once; blocks are kept
}
```

The upstream resource (here the standard `new`/`delete` one) must outlive the arena. In the engine it's the `TrackingAllocator` (Debug/RelWithDebInfo) or a `MallocResource` (Release); a tracker counts the arena's blocks under the arena's tag.

### How it works

Requests go into **size classes**, each a linked list of equal blocks:

| Request | Served from |
|---|---|
| ≤ 8 KiB (alignment ≤ 4 KiB) | 64 KiB blocks |
| ≤ 128 KiB (alignment ≤ 4 KiB) | 1 MiB blocks |
| larger, or alignment > 4 KiB | a dedicated block of exactly that size (+ a small header) |

Because a request is at most 1/8 of its block, the unused space at the end of a full block stays small (≤ ~12.5 %).

- **allocate:** align the cursor in the current block of the size class and bump it. If the request doesn't fit, move to the next block in the list (one kept from earlier), or get a new block from upstream and append it.
- **deallocate:** does nothing.
- **`reset()`:** rewinds every list to its first block and marks dedicated blocks as free. No block is returned to upstream, so after the first frame or two (warm-up), an arena makes **no upstream calls at all**. A large request after a reset reuses the smallest free dedicated block that fits.
- **`trim(keepBytes)`:** gives unused blocks back to upstream until at most `keepBytes` stay reserved: free dedicated blocks first, then blocks of each list that haven't been used since the last reset. Blocks that may hold live data are never freed. Use it after a spike (a level load) with a typical size, e.g. a recent maximum of `usedBytes()`. `reset(); trim(0);` empties the arena completely.
- **Statistics:** `reservedBytes()` (what the arena holds from upstream), `usedBytes()` (requested since the last reset), `blockCount()`, `tag()`.
- The bookkeeping lives in a **header at the start of each block** (next block, size, alignment), so the arena never allocates anything for itself.
- Upstream failures (`std::bad_alloc`) reach the caller unchanged, and the arena stays usable.

## Debug fill patterns

In **Debug and RelWithDebInfo**, engine allocators write recognisable bytes into memory, so reading memory at the wrong time shows up in the debugger instead of looking like plausible data:

| Byte | Name | Written when |
|---|---|---|
| `0xCD` | `c_allocatedFill` ("clean") | memory is handed out, before its owner writes it |
| `0xDD` | `c_freedFill` ("dead") | memory is deallocated, or its arena is reset |
| `0xFD` | `c_guardFill` ("fence") | 16 guard bytes behind every `TrackingAllocator` allocation, checked on free |

- They're the values the Microsoft debug C runtime uses for its own heap, so `0xCDCDCDCD` means "never written" and `0xDDDDDDDD` means "already freed" everywhere in a Visual Studio session.
- Read as a pointer, `0xCDCDCDCDCDCDCDCD` and `0xDDDDDDDDDDDDDDDD` are invalid addresses on x64, so following a stale pointer crashes immediately instead of corrupting something.
- `BlockArena` fills the returned bytes on `allocate`, the freed bytes on `deallocate` (that catches reads of a vector's old buffer after it grew), and, on `reset()`, everything used since the last reset (that catches containers that outlived the reset). The cost is one `memset` over the bytes actually used.
- **Turn it off for profiling:** `arena.setDebugFill(false)`. To remove the code entirely, build with `MEOW2_MEMORY_DEBUG_FILL=0`. Release never fills (`c_debugFillAvailable` is `false`).
- Fills only make stale **reads** visible. A **write** to freed memory still corrupts whoever owns it now; finding those is AddressSanitizer's job (the `linux-clang-asan` CI job, planned in M1).

### Pitfalls

- **Containers must be destroyed before `reset()`** (and before `trim()` frees their memory, and before the arena is destroyed). A container that still exists afterwards points at memory that's about to be reused, and with debug fill on, even destroying it later writes `0xDD` over someone else's data. Let containers go out of scope first. (`v.clear()` or `v = {}` is **not** enough: a `std::pmr` vector keeps its buffer, and the next `push_back` would write into memory the arena hands out again.)
- **Growing containers waste memory:** `push_back` past the capacity allocates a bigger buffer and leaves the old one behind until `reset()`. Call `reserve()` when the size is known.
- **Not thread-safe:** one arena per thread, or a lock around it. The `FrameArena` (M5) will hold one per worker thread.
- **The arena can't be copied or moved**, because containers hold a pointer to it.

### Existing engine code

Every `new` in the apps goes through the global tracker (above). Nothing in the engine uses an arena yet: the code so far (logging, crash handling, error handling) has no frame-scoped data: log messages are formatted on any thread and handed to spdlog, which copies them; the crash handler must not allocate at all while crashing. The first users will be the event bus (its queue, reset after each dispatch), the job system (per-worker scratch) and the frame arena (M5).
