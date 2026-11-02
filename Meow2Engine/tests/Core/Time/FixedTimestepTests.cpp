/// @file
/// @brief Tests for FixedTimestep: whole steps, the carried remainder, the step cap and alpha.

#include <Meow2/Core/Time/FixedTimestep.h>

#include <gtest/gtest.h>

#include <cstdint>

using Meow2::Time::FixedStepResult;
using Meow2::Time::FixedTimestep;

namespace
{
constexpr double c_tolerance = 1e-9;
} // namespace

TEST(FixedTimestep, RunsWholeSteps)
{
    FixedTimestep timestep{0.010, 8};
    const FixedStepResult result = timestep.advance(0.035);
    EXPECT_EQ(result.steps, 3U);
    EXPECT_FALSE(result.droppedTime);
    EXPECT_NEAR(timestep.alpha(), 0.5, c_tolerance) << "5 ms of a 10 ms step are left over";
    EXPECT_DOUBLE_EQ(timestep.stepSeconds(), 0.010);
}

TEST(FixedTimestep, CarriesRemainder)
{
    FixedTimestep timestep{0.010, 8};
    EXPECT_EQ(timestep.advance(0.006).steps, 0U);
    EXPECT_NEAR(timestep.alpha(), 0.6, c_tolerance);
    EXPECT_EQ(timestep.advance(0.006).steps, 1U) << "6 + 6 ms make one 10 ms step";
    EXPECT_NEAR(timestep.alpha(), 0.2, c_tolerance);
    EXPECT_NEAR(timestep.accumulatedSeconds(), 0.002, c_tolerance);
}

TEST(FixedTimestep, CapsStepsAndDropsTime)
{
    FixedTimestep timestep{0.010, 4};
    const FixedStepResult result = timestep.advance(0.105);
    EXPECT_EQ(result.steps, 4U);
    EXPECT_TRUE(result.droppedTime) << "the other 6 steps are dropped, not carried into the next frames";
    EXPECT_GE(timestep.alpha(), 0.0);
    EXPECT_LT(timestep.alpha(), 1.0);
    EXPECT_NEAR(timestep.alpha(), 0.5, c_tolerance) << "only the fraction below one step is kept";

    const FixedStepResult next = timestep.advance(0.010);
    EXPECT_EQ(next.steps, 1U) << "the next frame is normal again";
    EXPECT_FALSE(next.droppedTime);
}

TEST(FixedTimestep, ZeroDeltaRunsNothing)
{
    FixedTimestep timestep{1.0 / 60.0, 8};
    const FixedStepResult result = timestep.advance(0.0);
    EXPECT_EQ(result.steps, 0U);
    EXPECT_FALSE(result.droppedTime);
    EXPECT_DOUBLE_EQ(timestep.alpha(), 0.0);
}

TEST(FixedTimestep, AlphaStaysInRange)
{
    constexpr double c_step = 1.0 / 60.0;
    FixedTimestep timestep{c_step, 64};
    // Uneven frame times, like a real game: 7..23 ms.
    const double frames[] = {0.007, 0.016, 0.023, 0.011, 0.017, 0.0167, 0.009, 0.021, 0.0166, 0.013};
    double totalTime = 0.0;
    std::uint64_t totalSteps = 0;
    for (int round = 0; round < 100; ++round)
    {
        for (const double frame : frames)
        {
            totalTime += frame;
            totalSteps += timestep.advance(frame).steps;
            ASSERT_GE(timestep.alpha(), 0.0);
            ASSERT_LT(timestep.alpha(), 1.0);
        }
    }
    // Every bit of time is either simulated or still waiting in the accumulator: nothing is lost or invented.
    EXPECT_NEAR(static_cast<double>(totalSteps) * c_step + timestep.accumulatedSeconds(), totalTime, 1e-6);
}
