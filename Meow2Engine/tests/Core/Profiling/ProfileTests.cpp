/// @file
/// @brief Tests for the profiling macros. No viewer is connected, so they check that everything runs (and compiles
///        to nothing in Release), not what Tracy records.

#include <Meow2/Core/Jobs/JobSystem.h>
#include <Meow2/Core/Log/LogSystem.h>
#include <Meow2/Core/Memory/GlobalTracking.h>
#include <Meow2/Core/Platform.h>
#include <Meow2/Core/Profiling/Profile.h>

#include <gtest/gtest.h>

#include <atomic>
#include <filesystem>
#include <format>

namespace
{
int profiledWork(int depth)
{
    MEOW2_PROFILE_ZONE();
    if (depth == 0)
    {
        return 1;
    }
    MEOW2_PROFILE_ZONE_NAMED("Recursion");
    return 1 + profiledWork(depth - 1);
}
} // namespace

TEST(Profiling, MacrosCompileAndRun)
{
    MEOW2_PROFILE_THREAD_NAME("Test main");
    for (int frame = 0; frame < 3; ++frame)
    {
        EXPECT_EQ(profiledWork(5), 6) << "nested zones, several per function";
        MEOW2_PROFILE_MEMORY(Meow2::Memory::globalTracker());
        MEOW2_PROFILE_FRAME_MARK();
    }
}

TEST(Profiling, EnabledMatchesConfig)
{
    // Debug and RelWithDebInfo profile (unless MEOW2_PROFILING=OFF, e.g. sanitizer builds); Release never does.
    if constexpr (MEOW2_CONFIG_RELEASE == 1)
    {
        EXPECT_FALSE(Meow2::Profiling::c_profilingEnabled);
    }
    EXPECT_EQ(Meow2::Profiling::c_profilingEnabled, MEOW2_PROFILING == 1);
}

TEST(Profiling, ZonesFromJobThreads)
{
    const std::filesystem::path directory =
        std::filesystem::temp_directory_path() / std::format("meow2-profiling-{}", Meow2::c_configName);
    Meow2::Log::LogSystem logSystem{{.appName = "Test", .directory = directory, .console = false}};
    Meow2::Log::Logger log = logSystem.createLogger("Engine");
    Meow2::Jobs::JobSystem jobs{{.threadCount = 4}, log}; // workers are named by the thread-start callback

    std::atomic<int> total{0};
    const Meow2::Jobs::Job job = jobs.parallelFor(256, 8,
                                                  [&](std::uint32_t begin, std::uint32_t end, std::uint32_t)
                                                  {
                                                      MEOW2_PROFILE_ZONE_NAMED("Test work");
                                                      for (std::uint32_t i = begin; i < end; ++i)
                                                      {
                                                          total.fetch_add(profiledWork(2));
                                                      }
                                                  });
    jobs.wait(job);
    EXPECT_EQ(total.load(), 256 * 3);
}
