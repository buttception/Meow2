/// @file
/// @brief Tests for Meow2/Core/Memory/MallocResource.h and the MemTagScope part of MemTag.h.

#include "CountingResource.h"

#include <Meow2/Core/Memory/MallocResource.h>
#include <Meow2/Core/Memory/MemTag.h>

#include <gtest/gtest.h>

#include <cstddef>
#include <cstring>
#include <thread>
#include <vector>

using Meow2::Memory::MemTag;

TEST(MallocResource, AllocatesAlignedMemory)
{
    Meow2::Memory::MallocResource resource;
    const std::size_t sizes[] = {0, 1, 7, 64, 4096, 100 * 1024, 1024 * 1024};
    const std::size_t alignments[] = {1, 8, 16, 32, 64, 256, 4096, 8192};

    struct Allocation
    {
        void* pointer;
        std::size_t size;
        std::size_t alignment;
    };
    std::vector<Allocation> allocations;
    for (const std::size_t size : sizes)
    {
        for (const std::size_t alignment : alignments)
        {
            void* pointer = resource.allocate(size, alignment);
            ASSERT_NE(pointer, nullptr);
            EXPECT_TRUE(Meow2::Tests::isAligned(pointer, alignment)) << size << " bytes, alignment " << alignment;
            std::memset(pointer, 0x5A, size); // writable over its whole size
            allocations.push_back({pointer, size, alignment});
        }
    }
    for (const Allocation& allocation : allocations)
    {
        resource.deallocate(allocation.pointer, allocation.size, allocation.alignment);
    }
}

TEST(MallocResource, IsEqualOnlyToItself)
{
    Meow2::Memory::MallocResource first;
    Meow2::Memory::MallocResource second;
    EXPECT_TRUE(first.is_equal(first));
    EXPECT_FALSE(first.is_equal(second));
}

TEST(MemTagScope, SetsAndRestoresTheCurrentTag)
{
    EXPECT_EQ(Meow2::Memory::currentMemTag(), MemTag::General);
    {
        const Meow2::Memory::MemTagScope outer{MemTag::Physics};
        EXPECT_EQ(Meow2::Memory::currentMemTag(), MemTag::Physics);
        {
            const Meow2::Memory::MemTagScope inner{MemTag::Assets};
            EXPECT_EQ(Meow2::Memory::currentMemTag(), MemTag::Assets);
        }
        EXPECT_EQ(Meow2::Memory::currentMemTag(), MemTag::Physics);
    }
    EXPECT_EQ(Meow2::Memory::currentMemTag(), MemTag::General);
}

TEST(MemTagScope, IsPerThread)
{
    const Meow2::Memory::MemTagScope scope{MemTag::Renderer};
    MemTag otherThreadTag = MemTag::Count;
    std::thread other{[&otherThreadTag]
                      {
                          otherThreadTag = Meow2::Memory::currentMemTag();
                      }};
    other.join();
    EXPECT_EQ(otherThreadTag, MemTag::General);
    EXPECT_EQ(Meow2::Memory::currentMemTag(), MemTag::Renderer);
}
