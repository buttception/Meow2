/// @file
/// @brief Tests for FrameClock. All but one use explicit time points, so they're exact and never flaky.

#include <Meow2/Core/Time/FrameClock.h>

#include <gtest/gtest.h>

#include <chrono>
#include <thread>

namespace
{
using Meow2::Time::FrameClock;
using std::chrono::milliseconds;

/// @brief A fixed starting point; the value doesn't matter, only the differences.
const FrameClock::TimePoint c_start = FrameClock::TimePoint{} + std::chrono::hours{1};
} // namespace

TEST(FrameClock, StartsAtZero)
{
    const FrameClock clock{c_start};
    EXPECT_EQ(clock.frameCount(), 0u);
    EXPECT_EQ(clock.totalSeconds(), 0.0);
    EXPECT_EQ(clock.deltaSeconds(), 0.0);
    EXPECT_FALSE(clock.wasClamped());
    EXPECT_EQ(clock.maxFrameDeltaSeconds(), FrameClock::c_maxFrameDeltaSeconds);
}

TEST(FrameClock, MeasuresDelta)
{
    FrameClock clock{c_start};
    EXPECT_DOUBLE_EQ(clock.tick(c_start + milliseconds{16}), 0.016);
    EXPECT_EQ(clock.frameCount(), 1u);
    EXPECT_DOUBLE_EQ(clock.tick(c_start + milliseconds{50}), 0.034);
    EXPECT_EQ(clock.frameCount(), 2u);
    EXPECT_DOUBLE_EQ(clock.deltaSeconds(), 0.034);
    EXPECT_FLOAT_EQ(clock.deltaSecondsFloat(), 0.034f);
    EXPECT_DOUBLE_EQ(clock.totalSeconds(), 0.050);
}

TEST(FrameClock, ClampsLongFrames)
{
    FrameClock clock{c_start};
    EXPECT_DOUBLE_EQ(clock.tick(c_start + std::chrono::seconds{3}), 0.25) << "a breakpoint isn't a 3 s step";
    EXPECT_TRUE(clock.wasClamped());
    EXPECT_DOUBLE_EQ(clock.unclampedDeltaSeconds(), 3.0) << "the real time is kept for statistics";

    clock.tick(c_start + std::chrono::seconds{3} + milliseconds{10});
    EXPECT_FALSE(clock.wasClamped());
    EXPECT_DOUBLE_EQ(clock.deltaSeconds(), 0.010) << "the next frame is measured from the late tick";
}

TEST(FrameClock, TotalIsClampedSum)
{
    FrameClock clock{c_start};
    clock.tick(c_start + milliseconds{100});
    clock.tick(c_start + milliseconds{3100});
    EXPECT_DOUBLE_EQ(clock.totalSeconds(), 0.35) << "0.1 + 0.25 (clamped), not 3.1";
}

TEST(FrameClock, BackwardsTimeIsZero)
{
    FrameClock clock{c_start};
    clock.tick(c_start + milliseconds{100});
    EXPECT_EQ(clock.tick(c_start + milliseconds{40}), 0.0);
    EXPECT_EQ(clock.unclampedDeltaSeconds(), 0.0);
    EXPECT_EQ(clock.frameCount(), 2u);
    EXPECT_DOUBLE_EQ(clock.tick(c_start + milliseconds{120}), 0.020) << "measured from the latest time seen";
}

TEST(FrameClock, CustomClamp)
{
    FrameClock clock{c_start, 0.1};
    EXPECT_DOUBLE_EQ(clock.tick(c_start + milliseconds{500}), 0.1);
    EXPECT_TRUE(clock.wasClamped());
}

TEST(FrameClock, RealClockAdvances)
{
    FrameClock clock;
    std::this_thread::sleep_for(milliseconds{5});
    const double delta = clock.tick();
    EXPECT_GT(delta, 0.0);
    EXPECT_LE(delta, FrameClock::c_maxFrameDeltaSeconds);
}
