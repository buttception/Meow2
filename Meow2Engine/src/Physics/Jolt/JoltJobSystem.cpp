/// @file
/// @brief JoltJobSystem (JoltJobSystem.h).

#include "Physics/Jolt/JoltJobSystem.h"

#include <Jolt/Physics/PhysicsSettings.h>

#include <thread>
#include <utility>

namespace Meow2::Physics
{
JoltJobSystem::JoltJobSystem(Jobs::JobSystem& jobs)
    : JPH::JobSystemWithBarrier(JPH::cMaxPhysicsBarriers)
    , m_jobs(jobs)
{
    m_pool.Init(JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsJobs);
    m_running.reserve(JPH::cMaxPhysicsJobs);
}

JoltJobSystem::~JoltJobSystem()
{
    endStep();
}

int JoltJobSystem::GetMaxConcurrency() const
{
    return static_cast<int>(m_jobs.threadCount());
}

JoltJobSystem::Job* JoltJobSystem::allocateJob(const char* name, JPH::ColorArg color, const JobFunction& function,
                                               JPH::uint32 dependencies)
{
    for (;;)
    {
        const JPH::uint32 index = m_pool.ConstructObject(name, color, this, function, dependencies);
        if (index != JPH::FixedSizeFreeList<Job>::cInvalidObjectIndex)
        {
            return &m_pool.Get(index);
        }
        // All cMaxPhysicsJobs are in use: they free as running jobs finish. Jolt never makes that many in one step.
        std::this_thread::yield();
    }
}

JoltJobSystem::JobHandle JoltJobSystem::CreateJob(const char* name, JPH::ColorArg color, const JobFunction& function,
                                                  JPH::uint32 dependencies)
{
    Job* job = allocateJob(name, color, function, dependencies);
    JobHandle handle(job); // a reference first: once queued, the job may finish and release its own at once
    if (dependencies == 0)
    {
        QueueJob(job);
    }
    return handle;
}

void JoltJobSystem::CreateJobs(const char* name, JPH::ColorArg color, const JobFunction& function, JobHandle* jobs,
                               JPH::uint32 count, JPH::uint32 dependencies)
{
    for (JPH::uint32 i = 0; i < count; ++i)
    {
        jobs[i] = JobHandle(allocateJob(name, color, function, dependencies));
    }
    if (dependencies == 0)
    {
        // A handle is exactly one Job pointer (as Jolt's own thread pool relies on).
        static_assert(sizeof(JobHandle) == sizeof(Job*));
        QueueJobs(reinterpret_cast<Job**>(jobs), count);
    }
}

void JoltJobSystem::QueueJob(Job* job)
{
    job->AddRef(); // ours until it has run: Jolt's handles may all go before that
    Jobs::Job running = m_jobs.run(
        [job]
        {
            job->Execute(); // nothing if a barrier's waiter already ran it
            job->Release();
        });
    const std::scoped_lock lock(m_runningMutex);
    m_running.push_back(std::move(running));
}

void JoltJobSystem::QueueJobs(Job** jobs, JPH::uint count)
{
    for (JPH::uint i = 0; i < count; ++i)
    {
        QueueJob(jobs[i]);
    }
}

void JoltJobSystem::FreeJob(Job* job)
{
    m_pool.DestructObject(job);
}

void JoltJobSystem::endStep()
{
    std::vector<Jobs::Job> finished;
    {
        const std::scoped_lock lock(m_runningMutex);
        finished.swap(m_running);
    }
    finished.clear(); // each handle waits for its job: done by now, so this returns at once
    // Hand the (now empty) storage back, so the next step doesn't allocate it again.
    const std::scoped_lock lock(m_runningMutex);
    if (m_running.empty())
    {
        m_running.swap(finished);
    }
}
} // namespace Meow2::Physics
