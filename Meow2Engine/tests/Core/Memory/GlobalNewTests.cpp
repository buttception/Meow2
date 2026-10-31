/// @file
/// @brief Tests for the global operator new/delete replacement (GlobalTracking.h) and ScopedLeakCheck. This suite's
///        executable is built with meow2_track_global_new(), so every `new` in it goes through globalTracker().

#include "CountingResource.h"

#include <Meow2/Core/Log/LogSystem.h>
#include <Meow2/Core/Memory/GlobalTracking.h>
#include <Meow2/Core/Memory/LeakCheck.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <memory>
#include <new>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

namespace
{
namespace fs = std::filesystem;
using Meow2::Memory::globalTracker;
using Meow2::Memory::MemTag;
using Meow2::Memory::MemTagScope;
using Meow2::Memory::MemTagStats;

/// @brief Skips the test where global new isn't replaced (Release).
#define MEOW2_REQUIRE_TRACKING()                                                                                       \
    if (!Meow2::Memory::c_memoryTrackingAvailable)                                                                     \
    {                                                                                                                  \
        GTEST_SKIP() << "global new/delete aren't tracked in this configuration (Release)";                            \
    }

/// @brief An over-aligned type, so `new` uses the std::align_val_t forms.
struct alignas(64) CacheLine
{
    std::byte bytes[64];
};

/// @brief A fresh, empty folder for one test's log.
fs::path testDirectory(std::string_view testName)
{
    const fs::path directory =
        fs::temp_directory_path() / std::format("meow2-globalnew-{}-{}", Meow2::c_configName, testName);
    std::error_code ignored;
    fs::remove_all(directory, ignored);
    return directory;
}

/// @brief Contents of the one log file in `directory`, or "" if there is none.
std::string readLogFile(const fs::path& directory)
{
    std::error_code error;
    for (const fs::directory_entry& entry : fs::directory_iterator(directory, error))
    {
        if (entry.path().extension() == ".log")
        {
            std::ifstream file(entry.path(), std::ios::binary);
            return std::string{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
        }
    }
    return {};
}
} // namespace

TEST(GlobalNew, IsTracked)
{
    MEOW2_REQUIRE_TRACKING();
    const MemTagStats before = globalTracker().stats(MemTag::General);

    auto value = std::make_unique<std::uint64_t>(42);
    const MemTagStats during = globalTracker().stats(MemTag::General);
    EXPECT_EQ(during.totalAllocations, before.totalAllocations + 1);
    EXPECT_EQ(during.liveAllocations, before.liveAllocations + 1);
    EXPECT_EQ(during.currentBytes, before.currentBytes + sizeof(std::uint64_t));

    value.reset();
    const MemTagStats after = globalTracker().stats(MemTag::General);
    EXPECT_EQ(after.liveAllocations, before.liveAllocations);
    EXPECT_EQ(after.currentBytes, before.currentBytes);
}

TEST(GlobalNew, UsesTheCurrentTag)
{
    MEOW2_REQUIRE_TRACKING();
    const MemTagStats before = globalTracker().stats(MemTag::Physics);
    {
        const MemTagScope scope{MemTag::Physics};
        std::vector<int> values(1000);
        EXPECT_GE(globalTracker().stats(MemTag::Physics).currentBytes, before.currentBytes + 1000 * sizeof(int));
    }
    EXPECT_EQ(globalTracker().stats(MemTag::Physics).currentBytes, before.currentBytes);
    EXPECT_GT(globalTracker().stats(MemTag::Physics).totalAllocations, before.totalAllocations);
}

TEST(GlobalNew, AllVariantsWork)
{
    MEOW2_REQUIRE_TRACKING();
    const MemTagStats before = globalTracker().totalStats();

    auto* single = new int{7};
    auto* array = new int[100]{};
    auto* aligned = new CacheLine{};
    auto* alignedArray = new CacheLine[4]{};
    auto* nothrowSingle = new (std::nothrow) int{8};
    auto* nothrowArray = new (std::nothrow) int[10]{};
    auto* nothrowAligned = new (std::nothrow) CacheLine{};
    void* raw = ::operator new(100);
    void* rawAligned = ::operator new(100, std::align_val_t{128});

    EXPECT_EQ(*single, 7);
    EXPECT_EQ(array[99], 0);
    EXPECT_TRUE(Meow2::Tests::isAligned(aligned, 64));
    EXPECT_TRUE(Meow2::Tests::isAligned(alignedArray, 64));
    ASSERT_NE(nothrowSingle, nullptr);
    ASSERT_NE(nothrowArray, nullptr);
    EXPECT_TRUE(Meow2::Tests::isAligned(nothrowAligned, 64));
    EXPECT_TRUE(Meow2::Tests::isAligned(rawAligned, 128));
    EXPECT_EQ(globalTracker().totalStats().liveAllocations, before.liveAllocations + 9);

    delete single;
    delete[] array;
    delete aligned;
    delete[] alignedArray;
    delete nothrowSingle;
    delete[] nothrowArray;
    delete nothrowAligned;
    ::operator delete(raw, std::size_t{100});                               // sized
    ::operator delete(rawAligned, std::size_t{100}, std::align_val_t{128}); // sized + aligned

    const MemTagStats after = globalTracker().totalStats();
    EXPECT_EQ(after.liveAllocations, before.liveAllocations);
    EXPECT_EQ(after.currentBytes, before.currentBytes);
}

TEST(GlobalNew, FreesOnAnotherThread)
{
    MEOW2_REQUIRE_TRACKING();
    const MemTagStats before = globalTracker().stats(MemTag::Jobs);
    std::unique_ptr<std::string> text;
    {
        const MemTagScope scope{MemTag::Jobs};
        text = std::make_unique<std::string>(200, 'x'); // string + its buffer (+ a debug proxy in MSVC Debug)
    }
    EXPECT_GE(globalTracker().stats(MemTag::Jobs).liveAllocations, before.liveAllocations + 2);

    std::thread other{[&text]
                      {
                          text.reset();
                      }}; // freed elsewhere, under no scope: still counted back to Jobs
    other.join();
    EXPECT_EQ(globalTracker().stats(MemTag::Jobs).liveAllocations, before.liveAllocations);
    EXPECT_EQ(globalTracker().stats(MemTag::Jobs).currentBytes, before.currentBytes);
}

TEST(GlobalNew, DebugFillsNewMemory)
{
    MEOW2_REQUIRE_TRACKING();
    if (!globalTracker().debugFill())
    {
        GTEST_SKIP() << "debug fill is off";
    }
    auto bytes = std::make_unique_for_overwrite<std::byte[]>(256); // left uninitialised by C++
    EXPECT_TRUE(
        std::all_of(bytes.get(), bytes.get() + 256, [](std::byte b) { return b == Meow2::Memory::c_allocatedFill; }));
}

TEST(ScopedLeakCheck, LogsNoLeaks)
{
    MEOW2_REQUIRE_TRACKING();
    const fs::path directory = testDirectory("NoLeaks");
    {
        Meow2::Log::LogSystem logSystem{{.appName = "Test", .directory = directory, .console = false}};
        Meow2::Log::Logger log = logSystem.createLogger("Engine");
        {
            const Meow2::Memory::ScopedLeakCheck leakCheck{globalTracker(), log};
            std::vector<std::string> balanced(50, std::string(100, 'a')); // allocated and freed inside
        }
    }
    const std::string text = readLogFile(directory);
    EXPECT_NE(text.find("<info> Engine: No memory leaks ("), std::string::npos) << text;
}

TEST(ScopedLeakCheck, LogsLeaksPerTag)
{
    MEOW2_REQUIRE_TRACKING();
    const fs::path directory = testDirectory("Leaks");
    std::unique_ptr<std::byte[]> leaked; // "leaked" while the check runs, freed after it so the test stays clean
    {
        Meow2::Log::LogSystem logSystem{{.appName = "Test", .directory = directory, .console = false}};
        Meow2::Log::Logger log = logSystem.createLogger("Engine");
        {
            const Meow2::Memory::ScopedLeakCheck leakCheck{globalTracker(), log};
            const MemTagScope scope{MemTag::Assets};
            leaked = std::make_unique<std::byte[]>(1234);
            EXPECT_NE(leakCheck.report().find("Assets: 1 allocations, 1234 bytes"), std::string::npos)
                << leakCheck.report();
        }
    }
    leaked.reset();
    const std::string text = readLogFile(directory);
    EXPECT_NE(text.find("<warn> Engine: Memory leaks: 1 allocations, 1234 bytes not freed"), std::string::npos) << text;
    EXPECT_NE(text.find("Assets: 1 allocations, 1234 bytes"), std::string::npos) << text;
}
