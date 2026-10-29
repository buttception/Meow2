/// @file
/// @brief Tests for Meow2/Core/Memory/BlockArena.h and MemTag.h: size classes, reset/reuse, trim, alignment and use
///        through std::pmr containers. Upstream calls are observed with a counting resource.

#include <Meow2/Core/Memory/BlockArena.h>
#include <Meow2/Core/Memory/MemTag.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory_resource>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
using Meow2::Memory::BlockArena;
using Meow2::Memory::MemTag;

constexpr std::size_t c_kib = 1024;

/// @brief Upstream resource that records every allocation it serves, so tests can see what the arena asks for.
/// @par Thread safety
/// Not thread-safe.
class CountingResource final : public std::pmr::memory_resource
{
public:
    /// @brief Sizes of all allocations so far, in order.
    std::vector<std::size_t> allocations;
    /// @brief Number of deallocations so far.
    std::size_t deallocations = 0;
    /// @brief Bytes currently allocated and not freed.
    std::size_t outstandingBytes = 0;

    /// @brief Clears the recorded history; outstanding bytes are kept.
    void clearHistory()
    {
        allocations.clear();
        deallocations = 0;
    }

private:
    void* do_allocate(std::size_t bytes, std::size_t alignment) override
    {
        allocations.push_back(bytes);
        outstandingBytes += bytes;
        return std::pmr::new_delete_resource()->allocate(bytes, alignment);
    }

    void do_deallocate(void* pointer, std::size_t bytes, std::size_t alignment) override
    {
        ++deallocations;
        outstandingBytes -= bytes;
        std::pmr::new_delete_resource()->deallocate(pointer, bytes, alignment);
    }

    [[nodiscard]] bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override
    {
        return this == &other;
    }
};

bool isAligned(const void* pointer, std::size_t alignment)
{
    return reinterpret_cast<std::uintptr_t>(pointer) % alignment == 0;
}
} // namespace

TEST(BlockArena, AllocationsAreAlignedAndDisjoint)
{
    CountingResource upstream;
    BlockArena arena{upstream, MemTag::General};

    struct Allocation
    {
        std::byte* pointer;
        std::size_t size;
    };
    std::vector<Allocation> allocations;
    const std::size_t sizes[] = {1, 3, 8, 24, 100, 4 * c_kib, 8 * c_kib, 20 * c_kib, 200 * c_kib};
    const std::size_t alignments[] = {1, 2, 8, 16, 64, 256};
    std::size_t index = 0;
    for (int round = 0; round < 4; ++round)
    {
        for (const std::size_t size : sizes)
        {
            const std::size_t alignment = alignments[index++ % std::size(alignments)];
            auto* pointer = static_cast<std::byte*>(arena.allocate(size, alignment));
            ASSERT_NE(pointer, nullptr);
            EXPECT_TRUE(isAligned(pointer, alignment)) << size << " bytes, alignment " << alignment;
            std::memset(pointer, static_cast<int>(allocations.size() & 0xFF), size);
            allocations.push_back({pointer, size});
        }
    }

    // Every allocation still holds its own fill pattern, so no two of them overlap.
    for (std::size_t i = 0; i < allocations.size(); ++i)
    {
        const auto expected = static_cast<std::byte>(i & 0xFF);
        const Allocation& allocation = allocations[i];
        EXPECT_TRUE(std::all_of(allocation.pointer, allocation.pointer + allocation.size,
                                [expected](std::byte value) { return value == expected; }))
            << "allocation " << i << " was overwritten";
    }
}

TEST(BlockArena, SmallRequestsShareOneBlock)
{
    CountingResource upstream;
    BlockArena arena{upstream, MemTag::General};
    EXPECT_EQ(arena.blockCount(), 0u);
    EXPECT_EQ(arena.reservedBytes(), 0u);

    for (int i = 0; i < 100; ++i)
    {
        (void)arena.allocate(64, 8);
    }

    ASSERT_EQ(upstream.allocations.size(), 1u);
    EXPECT_EQ(upstream.allocations[0], BlockArena::c_smallBlockSize);
    EXPECT_EQ(arena.blockCount(), 1u);
    EXPECT_EQ(arena.reservedBytes(), BlockArena::c_smallBlockSize);
    EXPECT_EQ(arena.usedBytes(), 6400u);
}

TEST(BlockArena, RequestsPickTheirSizeClass)
{
    CountingResource upstream;
    BlockArena arena{upstream, MemTag::General};

    (void)arena.allocate(BlockArena::c_smallRequestLimit, 16);
    ASSERT_EQ(upstream.allocations.size(), 1u);
    EXPECT_EQ(upstream.allocations.back(), BlockArena::c_smallBlockSize);

    (void)arena.allocate(BlockArena::c_smallRequestLimit + 1, 16);
    ASSERT_EQ(upstream.allocations.size(), 2u);
    EXPECT_EQ(upstream.allocations.back(), BlockArena::c_mediumBlockSize);

    (void)arena.allocate(BlockArena::c_mediumRequestLimit, 16);
    EXPECT_EQ(upstream.allocations.size(), 2u) << "fits in the existing 1 MiB block";

    // Dedicated: the request plus a small header, nowhere near a whole medium block.
    const std::size_t largeSize = BlockArena::c_mediumRequestLimit + 1;
    (void)arena.allocate(largeSize, 16);
    ASSERT_EQ(upstream.allocations.size(), 3u);
    EXPECT_GE(upstream.allocations.back(), largeSize);
    EXPECT_LE(upstream.allocations.back(), largeSize + 256);
    EXPECT_EQ(arena.blockCount(), 3u);
}

TEST(BlockArena, FullBlockMovesOnToANewBlock)
{
    CountingResource upstream;
    BlockArena arena{upstream, MemTag::General};

    // Eight 8 KiB requests can't all fit: the block also holds its header.
    for (int i = 0; i < 8; ++i)
    {
        (void)arena.allocate(BlockArena::c_smallRequestLimit, 16);
    }

    EXPECT_EQ(upstream.allocations.size(), 2u);
    EXPECT_EQ(arena.blockCount(), 2u);
    EXPECT_EQ(arena.reservedBytes(), 2 * BlockArena::c_smallBlockSize);
}

TEST(BlockArena, ResetKeepsBlocksAndReusesThem)
{
    CountingResource upstream;
    BlockArena arena{upstream, MemTag::General};
    const std::size_t sizes[] = {32, 5 * c_kib, 8 * c_kib, 40 * c_kib, 8 * c_kib, 300 * c_kib, 16};

    std::vector<void*> firstRound;
    for (const std::size_t size : sizes)
    {
        firstRound.push_back(arena.allocate(size, 16));
    }
    const std::size_t reserved = arena.reservedBytes();
    const std::size_t blocks = arena.blockCount();
    upstream.clearHistory();

    for (int cycle = 0; cycle < 3; ++cycle)
    {
        arena.reset();
        EXPECT_EQ(arena.usedBytes(), 0u);
        for (std::size_t i = 0; i < std::size(sizes); ++i)
        {
            EXPECT_EQ(arena.allocate(sizes[i], 16), firstRound[i]) << "cycle " << cycle << ", request " << i;
        }
    }

    EXPECT_TRUE(upstream.allocations.empty());
    EXPECT_EQ(upstream.deallocations, 0u);
    EXPECT_EQ(arena.reservedBytes(), reserved);
    EXPECT_EQ(arena.blockCount(), blocks);
}

TEST(BlockArena, DeallocateIsANoOp)
{
    CountingResource upstream;
    BlockArena arena{upstream, MemTag::General};
    void* small = arena.allocate(100, 8);
    void* large = arena.allocate(500 * c_kib, 8);
    const std::size_t used = arena.usedBytes();

    arena.deallocate(small, 100, 8);
    arena.deallocate(large, 500 * c_kib, 8);

    EXPECT_EQ(upstream.deallocations, 0u);
    EXPECT_EQ(arena.usedBytes(), used);
    EXPECT_NE(arena.allocate(100, 8), small) << "freed memory is not handed out again before reset()";
}

TEST(BlockArena, DedicatedBlocksAreReusedAfterReset)
{
    CountingResource upstream;
    BlockArena arena{upstream, MemTag::General};
    void* big = arena.allocate(200 * c_kib, 16);
    void* bigger = arena.allocate(400 * c_kib, 16);
    upstream.clearHistory();

    arena.reset();
    EXPECT_EQ(arena.allocate(150 * c_kib, 16), big) << "smallest dedicated block that fits";
    EXPECT_EQ(arena.allocate(300 * c_kib, 16), bigger);
    EXPECT_TRUE(upstream.allocations.empty());

    (void)arena.allocate(150 * c_kib, 16); // both dedicated blocks are in use now
    EXPECT_EQ(upstream.allocations.size(), 1u);
}

TEST(BlockArena, OverAlignedRequestsAreHonoured)
{
    CountingResource upstream;
    BlockArena arena{upstream, MemTag::General};
    (void)arena.allocate(3, 1); // move the cursor off any natural alignment

    EXPECT_TRUE(isAligned(arena.allocate(100, 64), 64));
    EXPECT_TRUE(isAligned(arena.allocate(100, BlockArena::c_maxSharedAlignment), BlockArena::c_maxSharedAlignment));
    EXPECT_EQ(upstream.allocations.size(), 1u) << "alignments up to 4 KiB share the small blocks";

    void* pageAligned = arena.allocate(100, 8 * c_kib);
    EXPECT_TRUE(isAligned(pageAligned, 8 * c_kib));
    EXPECT_EQ(upstream.allocations.size(), 2u) << "larger alignments get a dedicated block";
}

TEST(BlockArena, ZeroByteRequestsGetDistinctPointers)
{
    CountingResource upstream;
    BlockArena arena{upstream, MemTag::General};
    void* first = arena.allocate(0, 1);
    void* second = arena.allocate(0, 1);
    EXPECT_NE(first, nullptr);
    EXPECT_NE(second, nullptr);
    EXPECT_NE(first, second);
}

TEST(BlockArena, TrimReturnsUnusedBlocks)
{
    CountingResource upstream;
    BlockArena arena{upstream, MemTag::General};
    for (int i = 0; i < 20; ++i)
    {
        (void)arena.allocate(8 * c_kib, 16); // 20 × 8 KiB → 3 small blocks
    }
    (void)arena.allocate(500 * c_kib, 16); // 1 dedicated block
    EXPECT_EQ(arena.blockCount(), 4u);

    // Nothing is unused yet: trim can't free anything.
    arena.trim(0);
    EXPECT_EQ(arena.blockCount(), 4u);
    EXPECT_EQ(upstream.deallocations, 0u);

    // After a reset with a smaller workload, the unused blocks can go, down to the budget.
    arena.reset();
    void* kept = arena.allocate(8 * c_kib, 16);
    std::memset(kept, 0x5A, 8 * c_kib);
    arena.trim(BlockArena::c_smallBlockSize);
    EXPECT_EQ(arena.blockCount(), 1u);
    EXPECT_EQ(arena.reservedBytes(), BlockArena::c_smallBlockSize);
    EXPECT_EQ(upstream.outstandingBytes, BlockArena::c_smallBlockSize);
    EXPECT_EQ(static_cast<unsigned char*>(kept)[8 * c_kib - 1], 0x5A) << "the block in use survives";

    // The arena keeps working after a trim, and grows again when needed.
    for (int i = 0; i < 20; ++i)
    {
        (void)arena.allocate(8 * c_kib, 16);
    }
    EXPECT_EQ(arena.blockCount(), 3u) << "small blocks only: the dedicated block was trimmed";

    arena.reset();
    arena.trim(0);
    EXPECT_EQ(arena.blockCount(), 0u);
    EXPECT_EQ(arena.reservedBytes(), 0u);
    EXPECT_EQ(upstream.outstandingBytes, 0u);
    EXPECT_NE(arena.allocate(16, 16), nullptr);
}

TEST(BlockArena, DestructorReturnsEverything)
{
    CountingResource upstream;
    {
        BlockArena arena{upstream, MemTag::General};
        for (int i = 0; i < 30; ++i)
        {
            (void)arena.allocate(static_cast<std::size_t>(i) * 3 * c_kib, 16);
        }
        (void)arena.allocate(2 * c_kib * c_kib, 16);
        EXPECT_GT(upstream.outstandingBytes, 0u);
    }
    EXPECT_EQ(upstream.outstandingBytes, 0u);
    EXPECT_EQ(upstream.deallocations, upstream.allocations.size());
}

TEST(BlockArena, WorksWithPmrContainers)
{
    CountingResource upstream;
    BlockArena arena{upstream, MemTag::Events};
    EXPECT_EQ(arena.tag(), MemTag::Events);
    {
        std::pmr::vector<std::pmr::string> names{&arena};
        names.reserve(64);
        for (int i = 0; i < 64; ++i)
        {
            names.emplace_back(std::string(100, static_cast<char>('a' + i % 26))); // longer than any SSO buffer
        }

        // The nested strings inherit the vector's resource (uses-allocator construction).
        for (const std::pmr::string& name : names)
        {
            EXPECT_EQ(name.get_allocator().resource(), &arena);
            EXPECT_EQ(name.size(), 100u);
        }
        EXPECT_EQ(std::string_view{names[27]}, std::string(100, 'b'));
        EXPECT_GE(arena.usedBytes(), 64u * 100u);
    }
    EXPECT_EQ(upstream.allocations.size(), 1u) << "everything fits in one small block";
    arena.reset();
}

TEST(BlockArena, IsEqualOnlyToItself)
{
    CountingResource upstream;
    BlockArena first{upstream, MemTag::General};
    BlockArena second{upstream, MemTag::General};
    EXPECT_TRUE(first.is_equal(first));
    EXPECT_FALSE(first.is_equal(second));
    EXPECT_FALSE(first.is_equal(upstream));
}

TEST(MemTag, EveryTagHasAName)
{
    std::vector<std::string_view> names;
    for (std::size_t i = 0; i < Meow2::Memory::c_memTagCount; ++i)
    {
        const std::string_view name = Meow2::Memory::memTagName(static_cast<MemTag>(i));
        EXPECT_FALSE(name.empty());
        EXPECT_NE(name, "Unknown") << "tag " << i;
        names.push_back(name);
    }
    std::sort(names.begin(), names.end());
    EXPECT_EQ(std::adjacent_find(names.begin(), names.end()), names.end()) << "names are unique";
    EXPECT_EQ(Meow2::Memory::memTagName(MemTag::Physics), "Physics");
    EXPECT_EQ(Meow2::Memory::memTagName(MemTag::Count), "Unknown");
}
