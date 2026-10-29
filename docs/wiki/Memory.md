# Memory

How Meow2 allocates memory. Everything is built on the C++ standard's allocator interface, `std::pmr::memory_resource`, so any `std::pmr` container (`std::pmr::vector`, `std::pmr::string`, `std::pmr::unordered_map`, …) can use any engine allocator. Design: [techstack.md §6](../../techstack.md).

| Piece | Status |
|---|---|
| `MemTag`: which subsystem memory belongs to | ✅ |
| `BlockArena`: bump allocator for short-lived data | ✅ |
| `TrackingAllocator` + `MallocResource`: per-tag statistics, leak report | ⬜ next |
| Global `operator new`/`delete` override, library allocator hooks | ⬜ |
| `FrameArena`: one arena per frame in flight × thread | ⬜ M5 |

## Memory tags

`Meow2::Memory::MemTag` (`<Meow2/Core/Memory/MemTag.h>`) names the subsystem an allocation is counted under: `General`, `Logging`, `Config`, `Jobs`, `Events`, `Platform`, `Renderer`, `Rhi`, `Physics`, `Assets`, `Editor`, `ImGui`, `Profiler`. `memTagName(tag)` returns the name as text, for statistics and reports. Every arena is created with a tag. Once the tracking allocator exists, its counters (current bytes, peak, allocation count) are kept per tag, so a report can say "Physics: 40 MiB".

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

The upstream resource (here the standard `new`/`delete` one) must outlive the arena. The engine passes its own resource there once `MallocResource`/`TrackingAllocator` exist.

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

### Pitfalls

- **Nothing may touch arena memory after `reset()`** (or after `trim()` freed it, or the arena is destroyed). A container that still exists afterwards points at memory that's about to be reused. Let containers go out of scope first. (`v.clear()` or `v = {}` is **not** enough: a `std::pmr` vector keeps its buffer, and the next `push_back` would write into memory the arena hands out again.)
- **Growing containers waste memory:** `push_back` past the capacity allocates a bigger buffer and leaves the old one behind until `reset()`. Call `reserve()` when the size is known.
- **Not thread-safe:** one arena per thread, or a lock around it. The `FrameArena` (M5) will hold one per worker thread.
- **The arena can't be copied or moved**, because containers hold a pointer to it.

### Existing engine code

Nothing in the engine uses an arena yet. The code so far (logging, crash handling, error handling) has no frame-scoped data: log messages are formatted on any thread and handed to spdlog, which copies them; the crash handler must not allocate at all while crashing. The first users will be the event bus (its queue, reset after each dispatch), the job system (per-worker scratch) and the frame arena (M5).
