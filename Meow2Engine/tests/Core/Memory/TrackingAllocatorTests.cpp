/// @file
/// @brief Tests for Meow2/Core/Memory/TrackingAllocator.h: per-tag counters, explicit tags, alignment, thread safety,
///        leak report, debug fill and guard bytes, misuse checks, and arena blocks counted under the arena's tag.

#include "CountingResource.h"

#include <Meow2/Core/Assert.h>
#include <Meow2/Core/Memory/BlockArena.h>
#include <Meow2/Core/Memory/MallocResource.h>
#include <Meow2/Core/Memory/TrackingAllocator.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

namespace
{
using Meow2::Memory::MemTag;
using Meow2::Memory::MemTagScope;
using Meow2::Memory::MemTagStats;
using Meow2::Memory::TrackingAllocator;

// GLOBAL: AssertHandler is a plain function pointer (no captures), so the recording handler counts here. Reset by
// the fixture before every test.
int s_assertCalls = 0;
std::string s_lastAssertMessage;

Meow2::AssertAction recordAndContinue(const Meow2::AssertInfo& info)
{
    ++s_assertCalls;
    s_lastAssertMessage = info.message;
    return Meow2::AssertAction::Continue;
}

/// @brief Installs the recording assert handler for one test and restores the previous one afterwards.
class TrackingAllocatorChecks : public ::testing::Test
{
protected:
    void SetUp() override
    {
        if (!Meow2::c_assertsEnabled)
        {
            GTEST_SKIP() << "asserts are compiled out of this configuration (Release)";
        }
        s_assertCalls = 0;
        s_lastAssertMessage.clear();
        m_previous = Meow2::setAssertHandler(&recordAndContinue);
    }

    void TearDown() override
    {
        if (Meow2::c_assertsEnabled)
        {
            Meow2::setAssertHandler(m_previous);
        }
    }

private:
    Meow2::AssertHandler m_previous = nullptr;
};
} // namespace

TEST(TrackingAllocator, CountsPerTag)
{
    Meow2::Tests::CountingResource upstream;
    TrackingAllocator tracker{upstream};

    void* first = nullptr;
    void* second = nullptr;
    {
        const MemTagScope scope{MemTag::Physics};
        first = tracker.allocate(100, 8);
        second = tracker.allocate(200, 8);
    }
    void* general = tracker.allocate(50, 8); // outside any scope: General

    MemTagStats physics = tracker.stats(MemTag::Physics);
    EXPECT_EQ(physics.currentBytes, 300u);
    EXPECT_EQ(physics.peakBytes, 300u);
    EXPECT_EQ(physics.liveAllocations, 2u);
    EXPECT_EQ(physics.totalAllocations, 2u);
    EXPECT_EQ(tracker.stats(MemTag::General).currentBytes, 50u);
    EXPECT_EQ(tracker.stats(MemTag::Renderer).totalAllocations, 0u);

    // Freed under another scope: the tag comes from the allocation, not from the freeing code.
    {
        const MemTagScope scope{MemTag::Editor};
        tracker.deallocate(first, 100, 8);
    }
    physics = tracker.stats(MemTag::Physics);
    EXPECT_EQ(physics.currentBytes, 200u);
    EXPECT_EQ(physics.peakBytes, 300u) << "the peak stays";
    EXPECT_EQ(physics.liveAllocations, 1u);
    EXPECT_EQ(physics.totalAllocations, 2u);
    EXPECT_EQ(tracker.stats(MemTag::Editor).currentBytes, 0u);

    tracker.deallocate(second, 200, 8);
    tracker.deallocate(general, 50, 8);
    const MemTagStats total = tracker.totalStats();
    EXPECT_EQ(total.currentBytes, 0u);
    EXPECT_EQ(total.liveAllocations, 0u);
    EXPECT_EQ(total.totalAllocations, 3u);
}

TEST(TrackingAllocator, AllocateTaggedUsesTheGivenTag)
{
    Meow2::Tests::CountingResource upstream;
    TrackingAllocator tracker{upstream};
    const MemTagScope scope{MemTag::Physics};

    void* pointer = tracker.allocateTagged(64, 16, MemTag::ImGui);
    EXPECT_EQ(tracker.stats(MemTag::ImGui).currentBytes, 64u);
    EXPECT_EQ(tracker.stats(MemTag::Physics).currentBytes, 0u);
    tracker.deallocate(pointer, 64, 16);
    EXPECT_EQ(tracker.stats(MemTag::ImGui).currentBytes, 0u);
}

TEST(TrackingAllocator, HonoursAlignment)
{
    Meow2::Memory::MallocResource mallocResource;
    TrackingAllocator tracker{mallocResource};
    const std::size_t alignments[] = {1, 2, 8, 16, 64, 256, 4096, 8192};
    for (const std::size_t alignment : alignments)
    {
        void* pointer = tracker.allocate(100, alignment);
        EXPECT_TRUE(Meow2::Tests::isAligned(pointer, alignment)) << "alignment " << alignment;
        std::memset(pointer, 0x11, 100);
        tracker.deallocate(pointer, 100, alignment);
    }
    EXPECT_FALSE(tracker.hasLeaks());
}

TEST(TrackingAllocator, ReturnsEverythingUpstream)
{
    Meow2::Tests::CountingResource upstream;
    TrackingAllocator tracker{upstream};
    std::vector<std::pair<void*, std::size_t>> allocations;
    for (std::size_t size = 0; size < 5000; size += 333)
    {
        allocations.emplace_back(tracker.allocate(size, 16), size);
    }
    EXPECT_GT(upstream.outstandingBytes, 0u);
    for (const auto& [pointer, size] : allocations)
    {
        tracker.deallocate(pointer, size, 16);
    }
    EXPECT_EQ(upstream.outstandingBytes, 0u);
    EXPECT_EQ(upstream.deallocations, upstream.allocations.size());
}

TEST(TrackingAllocator, CountsCorrectlyFromManyThreads)
{
    Meow2::Memory::MallocResource mallocResource;
    TrackingAllocator tracker{mallocResource};
    constexpr int c_threads = 8;
    constexpr int c_iterations = 5000;

    std::vector<std::thread> threads;
    for (int t = 0; t < c_threads; ++t)
    {
        threads.emplace_back(
            [&tracker, t]
            {
                const MemTagScope scope{t % 2 == 0 ? MemTag::Jobs : MemTag::Events};
                std::vector<void*> live;
                for (int i = 0; i < c_iterations; ++i)
                {
                    live.push_back(tracker.allocate(32, 8));
                    if (live.size() == 16)
                    {
                        for (void* pointer : live)
                        {
                            tracker.deallocate(pointer, 32, 8);
                        }
                        live.clear();
                    }
                }
                for (void* pointer : live)
                {
                    tracker.deallocate(pointer, 32, 8);
                }
            });
    }
    for (std::thread& thread : threads)
    {
        thread.join();
    }

    const MemTagStats jobs = tracker.stats(MemTag::Jobs);
    const MemTagStats events = tracker.stats(MemTag::Events);
    EXPECT_EQ(jobs.totalAllocations, std::uint64_t{c_threads / 2 * c_iterations});
    EXPECT_EQ(events.totalAllocations, std::uint64_t{c_threads / 2 * c_iterations});
    EXPECT_EQ(jobs.currentBytes + events.currentBytes, 0u);
    EXPECT_EQ(jobs.liveAllocations + events.liveAllocations, 0u);
    EXPECT_GE(jobs.peakBytes, 16u * 32u);
    EXPECT_LE(jobs.peakBytes, std::size_t{c_threads / 2} * 16u * 32u);
}

TEST(TrackingAllocator, LeakReportListsLiveAllocationsByTag)
{
    Meow2::Memory::MallocResource mallocResource;
    TrackingAllocator tracker{mallocResource};
    EXPECT_FALSE(tracker.hasLeaks());
    EXPECT_EQ(tracker.leakReport(), "");

    void* physicsA = tracker.allocateTagged(1000, 8, MemTag::Physics);
    void* physicsB = tracker.allocateTagged(536, 8, MemTag::Physics);
    void* assets = tracker.allocateTagged(10, 8, MemTag::Assets);

    EXPECT_TRUE(tracker.hasLeaks());
    const std::string report = tracker.leakReport();
    EXPECT_NE(report.find("3 allocations, 1546 bytes"), std::string::npos) << report;
    EXPECT_NE(report.find("Physics: 2 allocations, 1536 bytes"), std::string::npos) << report;
    EXPECT_NE(report.find("Assets: 1 allocations, 10 bytes"), std::string::npos) << report;
    EXPECT_EQ(report.find("General"), std::string::npos) << "tags without live allocations aren't listed";

    tracker.deallocate(physicsA, 1000, 8);
    tracker.deallocate(physicsB, 536, 8);
    tracker.deallocate(assets, 10, 8);
    EXPECT_FALSE(tracker.hasLeaks());
    EXPECT_EQ(tracker.leakReport(), "");
}

TEST_F(TrackingAllocatorChecks, DebugFillAndGuardBytes)
{
    if (!Meow2::Memory::c_debugFillAvailable)
    {
        GTEST_SKIP() << "debug fill is compiled out of this configuration";
    }
    Meow2::Memory::MallocResource mallocResource;
    TrackingAllocator tracker{mallocResource};
    EXPECT_TRUE(tracker.debugFill());

    auto* pointer = static_cast<std::byte*>(tracker.allocate(100, 8));
    EXPECT_TRUE(std::all_of(pointer, pointer + 100, [](std::byte b) { return b == Meow2::Memory::c_allocatedFill; }));

    // Writing inside the allocation is fine.
    std::memset(pointer, 0x11, 100);
    tracker.deallocate(pointer, 100, 8);
    EXPECT_EQ(s_assertCalls, 0);

    // One byte past the end: caught on free.
    pointer = static_cast<std::byte*>(tracker.allocate(100, 8));
    pointer[100] = std::byte{0};
    tracker.deallocate(pointer, 100, 8);
    EXPECT_EQ(s_assertCalls, 1);
    EXPECT_NE(s_lastAssertMessage.find("heap overrun"), std::string::npos) << s_lastAssertMessage;
    EXPECT_FALSE(tracker.hasLeaks()) << "the block is still freed and counted";

    // Without debug fill there are no guards to check.
    tracker.setDebugFill(false);
    pointer = static_cast<std::byte*>(tracker.allocate(100, 8));
    pointer[100] = std::byte{0};
    tracker.deallocate(pointer, 100, 8);
    EXPECT_EQ(s_assertCalls, 1);
}

TEST_F(TrackingAllocatorChecks, SizeMismatchOnDeallocateAsserts)
{
    Meow2::Memory::MallocResource mallocResource;
    TrackingAllocator tracker{mallocResource};
    void* pointer = tracker.allocate(100, 8);
    tracker.deallocate(pointer, 99, 8);
    EXPECT_EQ(s_assertCalls, 1);
    EXPECT_NE(s_lastAssertMessage.find("size"), std::string::npos) << s_lastAssertMessage;
    EXPECT_EQ(tracker.stats(MemTag::General).currentBytes, 0u) << "the recorded size is what's subtracted";
}

TEST(TrackingAllocator, CountsArenaBlocksUnderTheArenaTag)
{
    Meow2::Memory::MallocResource mallocResource;
    TrackingAllocator tracker{mallocResource};
    {
        Meow2::Memory::BlockArena arena{tracker, MemTag::Renderer};
        const MemTagScope scope{MemTag::Physics}; // the arena's tag wins
        (void)arena.allocate(100, 8);
        (void)arena.allocate(500 * 1024, 16); // dedicated block

        const MemTagStats renderer = tracker.stats(MemTag::Renderer);
        EXPECT_EQ(renderer.liveAllocations, 2u);
        EXPECT_GE(renderer.currentBytes, Meow2::Memory::BlockArena::c_smallBlockSize + 500 * 1024);
        EXPECT_EQ(renderer.currentBytes, arena.reservedBytes());
        EXPECT_EQ(tracker.stats(MemTag::Physics).totalAllocations, 0u);
    }
    EXPECT_FALSE(tracker.hasLeaks()) << "the arena returned its blocks";
}
