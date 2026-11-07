/// @file
/// @brief Tests for FrameArena: slots reset separately, and each thread has its own arena.

#include <Meow2/Core/Memory/FrameArena.h>

#include <gtest/gtest.h>

#include <memory_resource>

namespace Meow2::Memory
{
TEST(FrameArena, ThreadsGetTheirOwnArenas)
{
    FrameArena arena{*std::pmr::get_default_resource(), 2, 3};
    arena.beginFrame(0);
    EXPECT_NE(&arena.current(0), &arena.current(1));
    EXPECT_NE(&arena.current(1), &arena.current(2));
}

TEST(FrameArena, SlotsHaveDifferentArenas)
{
    FrameArena arena{*std::pmr::get_default_resource(), 2, 1};
    arena.beginFrame(0);
    const BlockArena* first = &arena.current(0);
    arena.beginFrame(1);
    EXPECT_NE(&arena.current(0), first);
}

TEST(FrameArena, BeginFrameResetsOnlyThatSlot)
{
    FrameArena arena{*std::pmr::get_default_resource(), 2, 1};
    arena.beginFrame(1);
    BlockArena& slotOne = arena.current(0);
    static_cast<void>(slotOne.allocate(128));

    arena.beginFrame(0); // the GPU may still be reading slot 1's data
    BlockArena& slotZero = arena.current(0);
    static_cast<void>(slotZero.allocate(64));
    EXPECT_GT(slotOne.usedBytes(), 0U);

    arena.beginFrame(1);
    EXPECT_EQ(slotOne.usedBytes(), 0U);
    EXPECT_GT(slotZero.usedBytes(), 0U);
}

} // namespace Meow2::Memory
