#pragma once

/// @file
/// @brief JobSystem: runs work on all cores. Parallel-for over a range, single jobs, and tasks for the main thread.
///
/// @code
/// Meow2::Jobs::JobSystem jobs{{}, engineLog};                    // composition root: one per process
///
/// Meow2::Jobs::Job job = jobs.parallelFor(particles.size(), 256,
///     [&](std::uint32_t begin, std::uint32_t end, std::uint32_t threadIndex)
///     {
///         for (std::uint32_t i = begin; i < end; ++i) { simulate(particles[i], scratch[threadIndex]); }
///     });
/// jobs.wait(job);                                               // the main thread helps until it's done
/// @endcode
/// @see techstack.md §19, docs/wiki/Jobs.md

#include <Meow2/Core/Log/Logger.h>

#include <cstdint>
#include <functional>
#include <limits>
#include <memory>

namespace Meow2::Jobs
{
class JobSystem;

/// @brief What threadIndex() returns on a thread the JobSystem doesn't know (one it didn't create, other than the
///        thread that created it).
inline constexpr std::uint32_t c_noThreadIndex = std::numeric_limits<std::uint32_t>::max();

/// @brief How the JobSystem is set up. Plain data.
struct JobSystemDesc
{
    /// @brief Threads that run jobs, **including** the thread that creates the JobSystem (index 0).
    ///
    /// 0 (the default) means one per hardware thread: hardware threads − 1 workers plus the creating thread. 1 means
    /// no workers: every job runs on the creating thread while it waits. Config key `jobs.threadCount`.
    std::uint32_t threadCount = 0;
};

/// @brief A started piece of work. Move-only; waits for the work when destroyed.
/// @par Thread safety
/// Not thread-safe: one owner. Its work may run on any job thread.
class Job
{
public:
    /// @brief An empty job: already done, nothing to wait for.
    Job();

    /// @brief Waits until the work is done (see JobSystem::wait()), then frees it.
    ~Job();

    /// @brief Takes over the work of `other`, which becomes empty.
    /// @param other Job to move from.
    Job(Job&& other) noexcept;

    /// @brief Waits for this job's work, then takes over `other`'s, which becomes empty.
    /// @param other Job to move from.
    /// @return `*this`.
    Job& operator=(Job&& other) noexcept;

    Job(const Job&) = delete;            ///< Not copyable: one owner per piece of work.
    Job& operator=(const Job&) = delete; ///< Not copyable: one owner per piece of work.

    /// @brief Whether the work has finished (or the job is empty). Doesn't wait.
    /// @return `true` when done.
    [[nodiscard]] bool done() const;

    /// @brief Backend state, defined in `src/Core/Jobs/Enkits/`.
    struct Task;

private:
    friend class JobSystem;
    Job(JobSystem& system, std::unique_ptr<Task> task);

    JobSystem* m_system = nullptr;
    std::unique_ptr<Task> m_task;
};

/// @brief Runs jobs on a fixed set of worker threads plus the thread that created it (techstack.md §19).
///
/// - parallelFor() splits a range into pieces (at least `minRange` items each) that run on any job thread.
/// - run() runs one function on any job thread.
/// - runOnMainThread() runs a function on the creating thread (index 0), e.g. for SDL calls, the next time that thread
///   calls runPinnedTasks() or waits for a job.
/// - wait() makes the calling thread run jobs itself until the job is done, so waiting never wastes a core. The
///   creating thread also runs its main-thread tasks while it waits.
/// - threadIndex() gives each job thread a stable index in 0..threadCount()-1, so per-thread data (arenas, scratch
///   buffers) can be indexed without locks.
///
/// Jobs may start and wait for other jobs. Job functions must not throw.
/// In Debug and RelWithDebInfo the scheduler's own memory goes to Memory::globalTracker() under MemTag::Jobs.
/// @par Thread safety
/// parallelFor(), run(), runOnMainThread(), wait() and threadIndex() may be called from any job thread (the creating
/// thread and the workers). runPinnedTasks() and the destructor only from the creating thread.
/// @par Lifetime
/// Created and owned by the composition root; must outlive every Job. The Logger must outlive it.
class JobSystem
{
public:
    /// @brief The work of parallelFor(): a part of the range `[begin, end)`, run on job thread `threadIndex`.
    using RangeFunction = std::function<void(std::uint32_t begin, std::uint32_t end, std::uint32_t threadIndex)>;

    /// @brief Starts the worker threads. The calling thread becomes job thread 0.
    /// @param desc How many threads.
    /// @param log  Receives the thread count. Must outlive the JobSystem.
    JobSystem(const JobSystemDesc& desc, Log::Logger& log);

    /// @brief Waits for all remaining jobs, then stops the workers.
    /// @pre Called on the creating thread (asserted).
    ~JobSystem();

    JobSystem(const JobSystem&) = delete;            ///< Not copyable: owns threads.
    JobSystem& operator=(const JobSystem&) = delete; ///< Not copyable: owns threads.
    JobSystem(JobSystem&&) = delete;                 ///< Not movable: jobs point at it.
    JobSystem& operator=(JobSystem&&) = delete;      ///< Not movable: jobs point at it.

    /// @brief Starts `function` on the range `[0, count)`, split into pieces of at least `minRange` items.
    /// @param count    Number of items; 0 gives an already-finished job.
    /// @param minRange Smallest piece worth a job, e.g. 64–1024 items for cheap per-item work; at least 1.
    /// @param function Called for each piece, on any job thread, possibly at the same time on several threads.
    /// @return The job.
    [[nodiscard]] Job parallelFor(std::uint32_t count, std::uint32_t minRange, RangeFunction function);

    /// @brief Starts `function` once, on any job thread.
    /// @param function The work.
    /// @return The job.
    [[nodiscard]] Job run(std::function<void()> function);

    /// @brief Starts `function` on the creating thread (index 0), the next time it runs main-thread tasks.
    ///
    /// The creating thread runs them in runPinnedTasks() and while it waits for any job.
    /// @param function The work.
    /// @return The job.
    /// @warning A worker that waits for this job blocks until the main thread gets to it: never wait for it from a job
    ///          the main thread is itself waiting for.
    [[nodiscard]] Job runOnMainThread(std::function<void()> function);

    /// @brief Runs jobs on the calling thread until `job` is done.
    /// @param job The job to wait for. An empty job returns at once.
    /// @pre Called on a job thread: threadIndex() != c_noThreadIndex (asserted).
    void wait(const Job& job);

    /// @brief Runs the main-thread tasks that are waiting (see runOnMainThread()).
    /// @pre Called on the creating thread (asserted).
    void runPinnedTasks();

    /// @brief How many threads run jobs, the creating thread included.
    /// @return The thread count, ≥ 1.
    [[nodiscard]] std::uint32_t threadCount() const;

    /// @brief The calling thread's job thread index.
    /// @return 0 on the creating thread, 1..threadCount()-1 on the workers, c_noThreadIndex on any other thread.
    [[nodiscard]] std::uint32_t threadIndex() const;

    /// @brief Backend state, defined in `src/Core/Jobs/Enkits/`.
    struct Impl;

private:
    friend class Job;
    std::unique_ptr<Impl> m_impl;
};
} // namespace Meow2::Jobs
