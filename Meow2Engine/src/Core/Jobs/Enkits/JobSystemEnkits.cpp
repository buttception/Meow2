/// @file
/// @brief JobSystem over enkiTS (the only file that includes it).

#include "Meow2/Core/Assert.h"
#include "Meow2/Core/Jobs/JobSystem.h"
#include "Meow2/Core/Memory/GlobalTracking.h"
#include "Meow2/Core/Memory/MemTag.h"
#include "Meow2/Core/Profiling/Profile.h"

#include <TaskScheduler.h>

#include <algorithm>
#include <format>
#include <optional>
#include <string>
#include <utility>

namespace Meow2::Jobs
{
namespace
{
using Memory::MemTag;
using Memory::MemTagScope;

// enkiTS's allocator hooks get a user-data pointer, but the tracker is process-wide anyway (techstack.md §6/§18).
void* enkiAllocate(std::size_t alignment, std::size_t size, void* /*userData*/, const char* /*file*/, int /*line*/)
{
    return Memory::globalTracker().allocateTagged(size, alignment, MemTag::Jobs);
}

void enkiFree(void* pointer, std::size_t /*size*/, void* /*userData*/, const char* /*file*/, int /*line*/)
{
    if (pointer != nullptr)
    {
        Memory::globalTracker().deallocateUnsized(pointer);
    }
}

/// @brief enkiTS calls this on each worker thread it starts: names it in the profiler, "Job thread <n>".
void nameJobThread(std::uint32_t threadIndex)
{
    if constexpr (Profiling::c_profilingEnabled)
    {
        const std::string name = std::format("Job thread {}", threadIndex);
        Profiling::setThreadName(name.c_str()); // Tracy copies it
    }
}

/// @brief A parallelFor() or run(): enkiTS calls ExecuteRange for each piece.
class RangeTask final : public enki::ITaskSet
{
public:
    RangeTask(std::uint32_t count, std::uint32_t minRange, JobSystem::RangeFunction function)
        : ITaskSet(count, minRange)
        , m_function(std::move(function))
    {
    }

    void ExecuteRange(enki::TaskSetPartition range, std::uint32_t threadIndex) override
    {
        MEOW2_PROFILE_ZONE_NAMED("Job");
        m_function(range.start, range.end, threadIndex);
    }

private:
    JobSystem::RangeFunction m_function;
};

/// @brief A runOnMainThread(): pinned to thread 0.
class MainThreadTask final : public enki::IPinnedTask
{
public:
    explicit MainThreadTask(std::function<void()> function)
        : IPinnedTask(0)
        , m_function(std::move(function))
    {
    }

    void Execute() override
    {
        m_function();
    }

private:
    std::function<void()> m_function;
};
} // namespace

/// @brief The enkiTS task behind a Job: one of the two kinds, and the completable to wait on.
struct Job::Task
{
    std::optional<RangeTask> range;
    std::optional<MainThreadTask> mainThread;

    [[nodiscard]] const enki::ICompletable* completable() const
    {
        return range ? static_cast<const enki::ICompletable*>(&*range)
                     : static_cast<const enki::ICompletable*>(&*mainThread);
    }
};

/// @brief The scheduler and the thread it was created on.
struct JobSystem::Impl
{
    enki::TaskScheduler scheduler;
    Log::Logger* log = nullptr;
};

Job::Job() = default;

Job::Job(JobSystem& system, std::unique_ptr<Task> task)
    : m_system(&system)
    , m_task(std::move(task))
{
}

Job::~Job()
{
    if (m_task)
    {
        // The task object must outlive its execution: enkiTS holds a pointer to it until it completes.
        m_system->wait(*this);
    }
}

Job::Job(Job&& other) noexcept
    : m_system(std::exchange(other.m_system, nullptr))
    , m_task(std::move(other.m_task))
{
}

Job& Job::operator=(Job&& other) noexcept
{
    if (this != &other)
    {
        if (m_task)
        {
            m_system->wait(*this);
        }
        m_system = std::exchange(other.m_system, nullptr);
        m_task = std::move(other.m_task);
    }
    return *this;
}

bool Job::done() const
{
    return !m_task || m_task->completable()->GetIsComplete();
}

JobSystem::JobSystem(const JobSystemDesc& desc, Log::Logger& log)
    : m_impl(std::make_unique<Impl>())
{
    const MemTagScope tag{MemTag::Jobs};
    m_impl->log = &log;

    const std::uint32_t threads =
        desc.threadCount != 0 ? desc.threadCount : std::max(enki::GetNumHardwareThreads(), 1u);
    enki::TaskSchedulerConfig config;
    config.numTaskThreadsToCreate = threads - 1; // the creating thread is job thread 0
    if constexpr (Memory::c_memoryTrackingAvailable)
    {
        config.customAllocator.alloc = enkiAllocate;
        config.customAllocator.free = enkiFree;
    }
    config.profilerCallbacks.threadStart = nameJobThread;
    m_impl->scheduler.Initialize(config);
    MEOW2_LOG_INFO(log, "Jobs: {} threads ({} workers + the main thread)", threadCount(), threadCount() - 1);
}

JobSystem::~JobSystem()
{
    MEOW2_ASSERT(threadIndex() == 0, "destroy the JobSystem on the thread that created it");
    m_impl->scheduler.WaitforAllAndShutdown();
}

Job JobSystem::parallelFor(std::uint32_t count, std::uint32_t minRange, RangeFunction function)
{
    const MemTagScope tag{MemTag::Jobs};
    auto task = std::make_unique<Job::Task>();
    task->range.emplace(count, std::max(minRange, 1u), std::move(function));
    if (count > 0)
    {
        m_impl->scheduler.AddTaskSetToPipe(&*task->range);
    }
    return Job{*this, std::move(task)}; // a 0-item task set is complete from the start
}

Job JobSystem::run(std::function<void()> function)
{
    return parallelFor(1, 1,
                       [function = std::move(function)](std::uint32_t, std::uint32_t, std::uint32_t) { function(); });
}

Job JobSystem::runOnMainThread(std::function<void()> function)
{
    const MemTagScope tag{MemTag::Jobs};
    auto task = std::make_unique<Job::Task>();
    task->mainThread.emplace(std::move(function));
    m_impl->scheduler.AddPinnedTask(&*task->mainThread);
    return Job{*this, std::move(task)};
}

void JobSystem::wait(const Job& job)
{
    if (!job.m_task)
    {
        return;
    }
    MEOW2_ASSERT(threadIndex() != c_noThreadIndex, "wait() only works on job threads");
    // WaitforTask runs other jobs (and, on thread 0, main-thread tasks) on this thread until the job completes.
    m_impl->scheduler.WaitforTask(job.m_task->completable());
}

void JobSystem::runPinnedTasks()
{
    MEOW2_ASSERT(threadIndex() == 0, "runPinnedTasks() runs main-thread tasks: call it on the main thread");
    m_impl->scheduler.RunPinnedTasks();
}

std::uint32_t JobSystem::threadCount() const
{
    return m_impl->scheduler.GetNumTaskThreads();
}

std::uint32_t JobSystem::threadIndex() const
{
    const std::uint32_t index = m_impl->scheduler.GetThreadNum();
    return index == enki::NO_THREAD_NUM ? c_noThreadIndex : index;
}
} // namespace Meow2::Jobs
