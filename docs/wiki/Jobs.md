# Jobs

The `JobSystem` runs work on all cores. Big loops (particles, culling, physics later) are split into pieces that worker threads run in parallel. The main thread helps whenever it waits, so waiting never wastes a core.

- Header: `#include <Meow2/Core/Jobs/JobSystem.h>`, namespace `Meow2::Jobs`.
- Library: [enkiTS](https://github.com/dougbinks/enkiTS) v1.12 (submodule `external/enkiTS`, static), used only in `src/Core/Jobs/Enkits/`.
- Status: ✅ M1 (#70). Each app creates one at start and logs `Jobs: N threads`.

## Using it

```cpp
Meow2::Jobs::JobSystem jobs{{}, engineLog};       // composition root; 0 threads = one per hardware thread

// Parallel-for: the range is split into pieces of at least minRange items.
Meow2::Jobs::Job update = jobs.parallelFor(count, 256,
    [&](std::uint32_t begin, std::uint32_t end, std::uint32_t threadIndex)
    {
        for (std::uint32_t i = begin; i < end; ++i) { simulate(items[i], scratch[threadIndex]); }
    });

Meow2::Jobs::Job load = jobs.run([&] { decodeTexture(file); });          // one function, any thread
Meow2::Jobs::Job ui = jobs.runOnMainThread([&] { window.setTitle(t); }); // on the main thread (e.g. SDL calls)

jobs.wait(update);     // the waiting thread runs jobs itself until `update` is done
jobs.runPinnedTasks(); // main thread: run main-thread tasks now (waiting also runs them)
```

| API | What it does |
|---|---|
| `parallelFor(count, minRange, f)` | Calls `f(begin, end, threadIndex)` for pieces of `[0, count)`, possibly on several threads at once. |
| `run(f)` | Calls `f()` once on any job thread. |
| `runOnMainThread(f)` | Calls `f()` on the creating thread (index 0), when it next calls `runPinnedTasks()` or waits. |
| `wait(job)` | Runs jobs on the calling thread until `job` is done. |
| `Job` | Move-only; `done()` checks without waiting. **Destroying an unfinished job waits for it.** |
| `threadIndex()` | 0 on the main thread, 1..`threadCount()`-1 on workers, `c_noThreadIndex` elsewhere. |

- **Config:** `jobs.threadCount` in the app's [config](Config.md). 0 (the default) means one thread per hardware thread; 1 means everything runs on the main thread while it waits, which is useful for debugging a race.

## Rules

- **Jobs may start and wait for other jobs.** Waiting inside a job runs other work instead of blocking a thread.
- **Job functions must not throw.**
- **Index per-thread data with `threadIndex`,** for example a scratch buffer or an arena per thread. It's stable for the thread's lifetime, and no two threads share one, so no lock is needed.
- **Don't let a worker wait for a main-thread task while the main thread waits for that worker:** neither would ever run it.
- **The JobSystem is created and destroyed on the main thread.** It must outlive every `Job`.

## How it works

- **enkiTS** keeps a queue per thread. Idle workers take pieces from other threads' queues ("work stealing"), so uneven work spreads by itself.
- **`parallelFor`** creates an enkiTS task set. enkiTS splits the range into pieces of at least `minRange` items, and more pieces than threads, so stealing has something to take. That's why `f` gets ranges, not single items.
- **A `Job` owns the task object.** enkiTS keeps a pointer to the task until it completes, which is why destroying an unfinished `Job` waits.
- **Main-thread tasks** are enkiTS "pinned tasks" for thread 0. enkiTS runs them on thread 0 whenever that thread looks for work: in `RunPinnedTasks()` and while it waits.
- **Memory:** in Debug and RelWithDebInfo, enkiTS's own allocations go to `globalTracker()` under `MemTag::Jobs` (its custom allocator hook). The leak check covers them, in the editor too.
- **Profiling:** enkiTS's thread-start callback names each worker `Job thread N` in Tracy, and every piece of a `parallelFor` is a `Job` zone ([Profiling](Profiling.md)).

## Not yet

- Jolt's job interface on top of this (M8).
- Dependencies between jobs and priorities, when a user needs them. enkiTS supports both.

See also: techstack.md §19, [Memory](Memory.md).
