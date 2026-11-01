/// @file
/// @brief Tests for JobSystem and Job.

#include <Meow2/Core/Jobs/JobSystem.h>
#include <Meow2/Core/Log/LogSystem.h>
#include <Meow2/Core/Memory/GlobalTracking.h>

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <format>
#include <mutex>
#include <set>
#include <system_error>
#include <thread>
#include <vector>

namespace
{
using Meow2::Jobs::Job;
using Meow2::Jobs::JobSystem;

/// @brief A logger in a fresh folder; each test creates its own JobSystem.
class JobSystemTest : public ::testing::Test
{
protected:
    JobSystemTest()
        : m_logSystem({.appName = "Test", .directory = logDirectory(), .console = false})
        , m_log(m_logSystem.createLogger("Engine"))
    {
    }

    Meow2::Log::LogSystem m_logSystem;
    Meow2::Log::Logger m_log;

private:
    static std::filesystem::path logDirectory()
    {
        const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
        const std::filesystem::path directory =
            std::filesystem::temp_directory_path() / std::format("meow2-jobs-{}-{}", Meow2::c_configName, info->name());
        std::error_code ignored;
        std::filesystem::remove_all(directory, ignored);
        return directory;
    }
};

/// @brief Burns a little CPU so pieces take long enough for other threads to pick some up.
void busyWork()
{
    const auto end = std::chrono::steady_clock::now() + std::chrono::microseconds{200};
    while (std::chrono::steady_clock::now() < end)
    {
    }
}
} // namespace

TEST_F(JobSystemTest, ParallelForCoversRange)
{
    JobSystem jobs{{}, m_log};
    constexpr std::uint32_t c_count = 100'000;
    std::vector<std::atomic<int>> visits(c_count);
    const Job job = jobs.parallelFor(c_count, 256,
                                     [&](std::uint32_t begin, std::uint32_t end, std::uint32_t)
                                     {
                                         for (std::uint32_t i = begin; i < end; ++i)
                                         {
                                             visits[i].fetch_add(1);
                                         }
                                     });
    jobs.wait(job);
    EXPECT_TRUE(job.done());
    for (std::uint32_t i = 0; i < c_count; ++i)
    {
        ASSERT_EQ(visits[i].load(), 1) << "index " << i;
    }
}

TEST_F(JobSystemTest, ThreadIndexInRange)
{
    JobSystem jobs{{.threadCount = 4}, m_log};
    EXPECT_EQ(jobs.threadCount(), 4u);
    EXPECT_EQ(jobs.threadIndex(), 0u) << "the creating thread is job thread 0";

    std::mutex mutex;
    std::set<std::uint32_t> seen;
    const Job job = jobs.parallelFor(64, 1,
                                     [&](std::uint32_t, std::uint32_t, std::uint32_t threadIndex)
                                     {
                                         busyWork();
                                         EXPECT_EQ(threadIndex, jobs.threadIndex());
                                         const std::scoped_lock lock{mutex};
                                         seen.insert(threadIndex);
                                     });
    jobs.wait(job);
    for (const std::uint32_t index : seen)
    {
        EXPECT_LT(index, jobs.threadCount());
    }
    EXPECT_GT(seen.size(), 1u) << "with 4 threads and 64 slow pieces, more than one thread takes part";

    std::uint32_t foreign = 0;
    std::thread{[&]
                {
                    foreign = jobs.threadIndex();
                }}
        .join();
    EXPECT_EQ(foreign, Meow2::Jobs::c_noThreadIndex);
}

TEST_F(JobSystemTest, RunAndWait)
{
    JobSystem jobs{{}, m_log};
    std::atomic<bool> ran{false};
    const Job job = jobs.run([&] { ran = true; });
    jobs.wait(job);
    EXPECT_TRUE(ran.load());
}

TEST_F(JobSystemTest, NestedJobs)
{
    JobSystem jobs{{.threadCount = 3}, m_log};
    std::atomic<int> total{0};
    const Job outer = jobs.run(
        [&]
        {
            const Job inner = jobs.parallelFor(1000, 10, [&](std::uint32_t begin, std::uint32_t end, std::uint32_t)
                                               { total.fetch_add(static_cast<int>(end - begin)); });
            jobs.wait(inner); // a job waiting for another job: runs work itself instead of blocking a thread
        });
    jobs.wait(outer);
    EXPECT_EQ(total.load(), 1000);
}

TEST_F(JobSystemTest, MainThreadTask)
{
    JobSystem jobs{{.threadCount = 3}, m_log};
    const std::thread::id mainThread = std::this_thread::get_id();
    std::atomic<bool> ranOnMain{false};
    std::atomic<std::uint32_t> indexOnMain{99};

    // A worker asks for something on the main thread; the main thread runs it while it waits for the worker's job.
    Job pinned;
    const Job worker = jobs.run(
        [&]
        {
            pinned = jobs.runOnMainThread(
                [&]
                {
                    ranOnMain = std::this_thread::get_id() == mainThread;
                    indexOnMain = jobs.threadIndex();
                });
        });
    jobs.wait(worker);
    jobs.wait(pinned);
    EXPECT_TRUE(ranOnMain.load());
    EXPECT_EQ(indexOnMain.load(), 0u);

    // And explicitly, from the main thread itself.
    bool ranExplicitly = false;
    const Job direct = jobs.runOnMainThread([&] { ranExplicitly = true; });
    jobs.runPinnedTasks();
    EXPECT_TRUE(ranExplicitly);
    EXPECT_TRUE(direct.done());
}

TEST_F(JobSystemTest, SingleThread)
{
    JobSystem jobs{{.threadCount = 1}, m_log};
    EXPECT_EQ(jobs.threadCount(), 1u);
    std::uint32_t sum = 0; // no other thread exists: no atomics needed
    const Job job = jobs.parallelFor(100, 10,
                                     [&](std::uint32_t begin, std::uint32_t end, std::uint32_t threadIndex)
                                     {
                                         EXPECT_EQ(threadIndex, 0u);
                                         sum += end - begin;
                                     });
    jobs.wait(job);
    EXPECT_EQ(sum, 100u);
}

TEST_F(JobSystemTest, JobDestructorWaits)
{
    JobSystem jobs{{.threadCount = 2}, m_log};
    std::atomic<int> finished{0};
    {
        const Job job = jobs.parallelFor(8, 1,
                                         [&](std::uint32_t begin, std::uint32_t end, std::uint32_t)
                                         {
                                             busyWork();
                                             // pieces are ranges (enkiTS may hand out several items at once)
                                             finished.fetch_add(static_cast<int>(end - begin));
                                         });
    } // destroyed unfinished: must wait, the task object can't go away under a running job
    EXPECT_EQ(finished.load(), 8);

    const Job empty;
    EXPECT_TRUE(empty.done());
    const Job none = jobs.parallelFor(0, 1, [](std::uint32_t, std::uint32_t, std::uint32_t) {});
    EXPECT_TRUE(none.done()) << "an empty range is done at once";
}

TEST_F(JobSystemTest, MemoryIsTagged)
{
    if constexpr (!Meow2::Memory::c_memoryTrackingAvailable)
    {
        GTEST_SKIP() << "the enkiTS allocator hook is only installed with memory tracking";
    }
    using Meow2::Memory::globalTracker;
    using Meow2::Memory::MemTag;
    const auto before = globalTracker().stats(MemTag::Jobs).liveAllocations;
    {
        JobSystem jobs{{.threadCount = 2}, m_log};
        EXPECT_GT(globalTracker().stats(MemTag::Jobs).liveAllocations, before) << "the scheduler's own memory";
    }
    EXPECT_EQ(globalTracker().stats(MemTag::Jobs).liveAllocations, before) << "all of it freed at shutdown";
}
