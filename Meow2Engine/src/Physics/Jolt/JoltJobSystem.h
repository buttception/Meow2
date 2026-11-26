#pragma once

/// @file
/// @brief JoltJobSystem: Jolt's job interface on top of the engine's JobSystem (enkiTS), so physics uses the same
///        threads as everything else (techstack.md §19) instead of a thread pool of its own.

#include <Meow2/Core/Jobs/JobSystem.h>

// Jolt.h comes before every other Jolt header (it sets up Jolt's configuration), so it's kept out of the sorted
// block below.
// clang-format off
#include <Jolt/Jolt.h>
// clang-format on

#include <Jolt/Core/FixedSizeFreeList.h>
#include <Jolt/Core/JobSystemWithBarrier.h>

#include <mutex>
#include <vector>

namespace Meow2::Physics
{
/// @brief Runs Jolt's jobs on the engine's JobSystem.
///
/// Jolt asks a job system for three things: make a job (CreateJob, from a fixed pool here, as Jolt's own pool does),
/// run it when its dependencies are done (QueueJob), and wait for a group of jobs (barriers, which
/// JPH::JobSystemWithBarrier provides: a barrier's waiter also runs the barrier's ready jobs itself, so waiting never
/// idles a core).
///
/// QueueJob hands each job to JobSystem::run(). That returns a handle that waits for the job when destroyed, and Jolt
/// queues jobs and forgets them, so the handles are kept until the end of the step (endStep()), by when every job has
/// finished: the waits there return at once. A job may run twice (on our thread and in a barrier's wait); Jolt's
/// Job::Execute() makes the second call do nothing.
/// @par Thread safety
/// Jolt calls it from the stepping thread and from job threads; the kept handles are behind a mutex (held only to
/// add or take them, never while calling into the JobSystem). endStep() only on the stepping thread, after Update().
/// @par Lifetime
/// The JobSystem must outlive it.
class JoltJobSystem final : public JPH::JobSystemWithBarrier
{
public:
    /// @brief A job system with room for Jolt's most jobs and barriers per step.
    /// @param jobs The engine's job system.
    explicit JoltJobSystem(Jobs::JobSystem& jobs);

    /// @brief Waits for every job still held.
    ~JoltJobSystem() override;

    JoltJobSystem(const JoltJobSystem&) = delete;            ///< Not copyable: Jolt holds pointers to it.
    JoltJobSystem& operator=(const JoltJobSystem&) = delete; ///< Not copyable.

    /// @brief How many threads run jobs: the JobSystem's.
    /// @return The thread count.
    [[nodiscard]] int GetMaxConcurrency() const override;

    /// @brief Makes a job, and queues it at once if it has no dependencies.
    /// @param name         For profilers.
    /// @param color        For profilers.
    /// @param function     The work.
    /// @param dependencies Jobs that must finish first.
    /// @return A handle that keeps the job alive.
    JobHandle CreateJob(const char* name, JPH::ColorArg color, const JobFunction& function,
                        JPH::uint32 dependencies = 0) override;

    /// @brief Makes several jobs alike, and queues them at once if they have no dependencies.
    /// @param name         For profilers.
    /// @param color        For profilers.
    /// @param function     The work of each.
    /// @param[out] jobs    Receives `count` handles.
    /// @param count        How many.
    /// @param dependencies Jobs that must finish first.
    void CreateJobs(const char* name, JPH::ColorArg color, const JobFunction& function, JobHandle* jobs,
                    JPH::uint32 count, JPH::uint32 dependencies = 0) override;

    /// @brief Waits for the jobs started during the step. Call after PhysicsSystem::Update() returned.
    void endStep();

protected:
    /// @brief Starts a job whose dependencies are done on the engine's JobSystem.
    /// @param job The job.
    void QueueJob(Job* job) override;

    /// @brief Starts several jobs.
    /// @param jobs  The jobs.
    /// @param count How many.
    void QueueJobs(Job** jobs, JPH::uint count) override;

    /// @brief Returns a job's memory to the pool (Jolt calls it when the job's last reference goes).
    /// @param job The job.
    void FreeJob(Job* job) override;

private:
    /// @brief A job from the pool, waiting for a free slot if all are taken (they free as jobs finish).
    Job* allocateJob(const char* name, JPH::ColorArg color, const JobFunction& function, JPH::uint32 dependencies);

    Jobs::JobSystem& m_jobs;
    JPH::FixedSizeFreeList<Job> m_pool; ///< Jolt's jobs live here, as in its own thread pool.
    std::mutex m_runningMutex;          ///< Guards m_running.
    std::vector<Jobs::Job> m_running;   ///< Handles of the jobs started this step, waited for in endStep().
};
} // namespace Meow2::Physics
