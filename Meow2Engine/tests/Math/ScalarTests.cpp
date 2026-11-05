/// @file
/// @brief Tests for the scalar helpers: angle conversion and wrapping, lerp and its inverse, remap, clamp, saturate
///        and smoothstep.

#include <Meow2/Math/Scalar.h>

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <limits>

using namespace Meow2::Math;

TEST(Scalar, AngleConversion)
{
    static_assert(toRadians(180.0F) == c_pi, "usable at compile time");
    EXPECT_TRUE(nearlyEqual(toRadians(90.0F), c_halfPi));
    EXPECT_TRUE(nearlyEqual(toDegrees(c_pi), 180.0F));
    EXPECT_TRUE(nearlyEqual(toDegrees(toRadians(-37.5F)), -37.5F)) << "round trip";
}

TEST(Scalar, Lerp)
{
    static_assert(lerp(2.0F, 4.0F, 0.5F) == 3.0F, "usable at compile time");
    // Values where a + t·(b - a) at t = 1 wouldn't give b exactly.
    const float a = 0.1F;
    const float b = 1e8F;
    EXPECT_EQ(lerp(a, b, 0.0F), a) << "exactly a at t = 0";
    EXPECT_EQ(lerp(a, b, 1.0F), b) << "exactly b at t = 1";
    EXPECT_TRUE(nearlyEqual(lerp(10.0F, 20.0F, 1.5F), 25.0F)) << "extrapolates above 1";
    EXPECT_TRUE(nearlyEqual(lerp(10.0F, 20.0F, -0.5F), 5.0F)) << "extrapolates below 0";
}

TEST(Scalar, InverseLerpAndRemap)
{
    EXPECT_TRUE(nearlyEqual(inverseLerp(10.0F, 20.0F, 15.0F), 0.5F));
    EXPECT_TRUE(nearlyEqual(inverseLerp(10.0F, 20.0F, 30.0F), 2.0F)) << "not clamped";
    EXPECT_TRUE(nearlyEqual(inverseLerp(20.0F, 10.0F, 12.0F), 0.8F)) << "reversed range";
    for (const float t : {-1.0F, 0.0F, 0.25F, 1.0F, 3.0F})
    {
        EXPECT_TRUE(nearlyEqual(inverseLerp(-4.0F, 6.0F, lerp(-4.0F, 6.0F, t)), t)) << "inverse of lerp at t = " << t;
    }

    EXPECT_TRUE(nearlyEqual(remap(25.0F, 0.0F, 100.0F, 1.0F, 0.0F), 0.75F)) << "health 25 → factor 0.75";
    EXPECT_TRUE(nearlyEqual(remap(0.0F, -1.0F, 1.0F, 0.0F, 255.0F), 127.5F));
    EXPECT_TRUE(nearlyEqual(remap(2.0F, 0.0F, 1.0F, 10.0F, 20.0F), 30.0F)) << "not clamped";
}

TEST(Scalar, ClampAndSaturate)
{
    static_assert(clamp(5.0F, 0.0F, 1.0F) == 1.0F && saturate(-2.0F) == 0.0F, "usable at compile time");
    EXPECT_EQ(clamp(0.5F, -1.0F, 1.0F), 0.5F);
    EXPECT_EQ(clamp(-3.0F, -1.0F, 1.0F), -1.0F);
    EXPECT_EQ(clamp(3.0F, -1.0F, 1.0F), 1.0F);
    EXPECT_EQ(saturate(0.25F), 0.25F);
    EXPECT_EQ(saturate(1.5F), 1.0F);
    EXPECT_TRUE(std::isnan(clamp(std::numeric_limits<float>::quiet_NaN(), 0.0F, 1.0F))) << "NaN isn't hidden";
}

TEST(Scalar, WrapAngle)
{
    struct Case
    {
        float input;
        float expected;
    };
    const std::array<Case, 9> cases{{
        {0.0F, 0.0F},
        {c_halfPi, c_halfPi},
        {c_twoPi, 0.0F},
        {-c_twoPi, 0.0F},
        {c_pi, -c_pi}, // the range is half-open: π becomes -π
        {-c_pi, -c_pi},
        {c_pi + 0.5F, -c_pi + 0.5F},
        {-c_pi - 0.5F, c_pi - 0.5F},
        {10.0F * c_twoPi + 1.0F, 1.0F},
    }};
    for (const Case& c : cases)
    {
        const float wrapped = wrapAngle(c.input);
        EXPECT_TRUE(nearlyEqual(wrapped, c.expected, 1e-4F)) << c.input << " → " << wrapped << ", not " << c.expected;
    }

    // Every result is in [-π, π), including the inputs where rounding is closest to the edges.
    for (float angle = -1000.0F; angle < 1000.0F; angle += 0.37F)
    {
        const float wrapped = wrapAngle(angle);
        ASSERT_GE(wrapped, -c_pi) << angle;
        ASSERT_LT(wrapped, c_pi) << angle;
        ASSERT_TRUE(nearlyEqual(std::sin(wrapped), std::sin(angle), 1e-3F)) << "same direction for " << angle;
    }
    for (const float edge : {c_pi, -c_pi, 3.0F * c_pi, -3.0F * c_pi, std::nextafter(c_pi, 0.0F)})
    {
        const float wrapped = wrapAngle(edge);
        EXPECT_GE(wrapped, -c_pi) << edge;
        EXPECT_LT(wrapped, c_pi) << edge;
    }
}

TEST(Scalar, Smoothstep)
{
    EXPECT_EQ(smoothstep(1.0F, 2.0F, 0.0F), 0.0F) << "0 below the first edge";
    EXPECT_EQ(smoothstep(1.0F, 2.0F, 5.0F), 1.0F) << "1 above the second edge";
    EXPECT_TRUE(nearlyEqual(smoothstep(1.0F, 2.0F, 1.5F), 0.5F)) << "half way in the middle";
    EXPECT_TRUE(nearlyEqual(smoothstep(0.0F, 1.0F, 0.25F), 0.15625F)) << "3t² − 2t³ (HLSL)";
    EXPECT_TRUE(nearlyEqual(smoothstep(2.0F, 1.0F, 1.25F), 0.84375F)) << "reversed edges give the falling curve";

    // Zero slope at both edges: a tiny step away from an edge barely changes the result.
    const float h = 1e-3F;
    EXPECT_LT(smoothstep(0.0F, 1.0F, h) / h, 0.01F);
    EXPECT_LT((1.0F - smoothstep(0.0F, 1.0F, 1.0F - h)) / h, 0.01F);
}
