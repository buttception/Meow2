/// @file
/// @brief Tests for TaggedResource: allocations through it are counted under its tag, whatever the thread's tag is.

#include <Meow2/Core/Memory/MallocResource.h>
#include <Meow2/Core/Memory/MemTag.h>
#include <Meow2/Core/Memory/TaggedResource.h>
#include <Meow2/Core/Memory/TrackingAllocator.h>

#include <gtest/gtest.h>

#include <memory_resource>
#include <vector>

using Meow2::Memory::MemTag;

TEST(TaggedResource, CountsUnderTag)
{
    Meow2::Memory::MallocResource malloc;
    Meow2::Memory::TrackingAllocator tracker{malloc};
    Meow2::Memory::TaggedResource physics{tracker, MemTag::Physics};
    EXPECT_EQ(physics.tag(), MemTag::Physics);

    {
        const Meow2::Memory::MemTagScope scope{MemTag::Logging}; // the thread says "Logging" ...
        std::pmr::vector<int> values{&physics};
        values.resize(1000);
        EXPECT_GE(tracker.stats(MemTag::Physics).currentBytes, 1000 * sizeof(int)); // ... the resource wins
        EXPECT_EQ(tracker.stats(MemTag::Logging).liveAllocations, 0U);
    }
    EXPECT_EQ(tracker.stats(MemTag::Physics).liveAllocations, 0U) << "freed under the same tag";
    EXPECT_EQ(tracker.stats(MemTag::Physics).currentBytes, 0U);
}
