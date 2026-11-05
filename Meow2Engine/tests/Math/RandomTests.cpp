/// @file
/// @brief Tests for the PCG32 generator: the reference sequence, bit-exact golden values (the same on every platform),
///        bounds, bias and the vector distributions.

#include <Meow2/Math/Random.h>

#include <gtest/gtest.h>

#include <array>
#include <bit>
#include <cmath>
#include <concepts>
#include <cstdint>
#include <random>

using namespace Meow2::Math;

TEST(Random, MatchesPcgReference)
{
    // pcg32-global-demo / pcg32_srandom_r(42, 54) from the reference C implementation (pcg-random.org).
    Random rng{42U, 54U};
    constexpr std::array<std::uint32_t, 6> c_expected{0xa15c02b7U, 0x7b47f409U, 0xba1d3330U,
                                                      0x83d2f293U, 0xbfa4784bU, 0xcbed606eU};
    for (const std::uint32_t expected : c_expected)
    {
        EXPECT_EQ(rng.nextU32(), expected);
    }
}

TEST(Random, SameSeedSameSequence)
{
    Random a{1234U, 7U};
    Random b{1234U, 7U};
    Random otherStream{1234U, 8U};
    int sameAsOtherStream = 0;
    for (int i = 0; i < 1000; ++i)
    {
        const std::uint32_t value = a.nextU32();
        ASSERT_EQ(value, b.nextU32()) << "same seed and stream, same sequence";
        sameAsOtherStream += value == otherStream.nextU32() ? 1 : 0;
    }
    EXPECT_LT(sameAsOtherStream, 3) << "another stream is an unrelated sequence";
    EXPECT_EQ(a.seed(), 1234U);
}

TEST(Random, GoldenValues)
{
    // Bit-exact values for seed 2026. The same numbers must come out on every platform and compiler (CI checks
    // Linux with GCC and Clang). If this fails after a change to Random, saved replays and seeds no longer match.
    Random rng{2026U};
    std::array<std::uint32_t, 12> actual{};
    actual[0] = static_cast<std::uint32_t>(rng.range(-1000, 1000));
    actual[1] = static_cast<std::uint32_t>(rng.range(1, 6));
    actual[2] = std::bit_cast<std::uint32_t>(rng.nextFloat());
    actual[3] = std::bit_cast<std::uint32_t>(rng.range(-5.0F, 5.0F));
    const Vec3 direction = rng.unitVector();
    actual[4] = std::bit_cast<std::uint32_t>(direction.x);
    actual[5] = std::bit_cast<std::uint32_t>(direction.y);
    actual[6] = std::bit_cast<std::uint32_t>(direction.z);
    const Vec3 point = rng.pointInSphere();
    actual[7] = std::bit_cast<std::uint32_t>(point.x);
    actual[8] = std::bit_cast<std::uint32_t>(point.y);
    actual[9] = std::bit_cast<std::uint32_t>(point.z);
    actual[10] = rng.chance(0.5F) ? 1U : 0U;
    actual[11] = static_cast<std::uint32_t>(rng.nextU64() >> 32U);

    constexpr std::array<std::uint32_t, 12> c_expected{
        467U,        6U,                       // range(-1000, 1000), range(1, 6)
        0x3e20a2b4U, 0xbf9d4220U,              // nextFloat(), range(-5, 5)
        0xbe95b6deU, 0x3f6fd868U, 0xbe443a99U, // unitVector()
        0x3df28470U, 0xbdf02f90U, 0xbeabf6f8U, // pointInSphere()
        0U,          0x8e3ade04U,              // chance(0.5), nextU64() high half
    };
    for (std::size_t i = 0; i < actual.size(); ++i)
    {
        EXPECT_EQ(actual[i], c_expected[i]) << "value " << i << " = 0x" << std::hex << actual[i];
    }
}

TEST(Random, RangesInBounds)
{
    Random rng{99U};
    for (int i = 0; i < 10000; ++i)
    {
        const std::int32_t value = rng.range(-3, 3);
        ASSERT_GE(value, -3);
        ASSERT_LE(value, 3);
        const float f = rng.range(2.0F, 4.0F);
        ASSERT_GE(f, 2.0F);
        ASSERT_LT(f, 4.0F);
        const float unit = rng.nextFloat();
        ASSERT_GE(unit, 0.0F);
        ASSERT_LT(unit, 1.0F);
    }
    EXPECT_EQ(rng.range(7, 7), 7) << "an empty-width range returns its only value";
    // The full int32 range must not overflow.
    bool sawNegative = false;
    bool sawPositive = false;
    for (int i = 0; i < 100; ++i)
    {
        const std::int32_t value = rng.range(INT32_MIN, INT32_MAX);
        sawNegative = sawNegative || value < 0;
        sawPositive = sawPositive || value > 0;
    }
    EXPECT_TRUE(sawNegative && sawPositive);
    EXPECT_FALSE(rng.chance(0.0F));
    EXPECT_TRUE(rng.chance(1.0F));
}

TEST(Random, IntRangeIsUnbiased)
{
    Random rng{5U};
    std::array<int, 6> counts{};
    constexpr int c_rolls = 600000;
    for (int i = 0; i < c_rolls; ++i)
    {
        ++counts[static_cast<std::size_t>(rng.range(1, 6) - 1)];
    }
    for (std::size_t face = 0; face < counts.size(); ++face)
    {
        EXPECT_NEAR(counts[face], c_rolls / 6, c_rolls / 600) << "face " << face + 1;
    }
}

TEST(Random, UnitVectorsAndSphere)
{
    Random rng{17U};
    Vec3 sum;
    constexpr int c_count = 20000;
    for (int i = 0; i < c_count; ++i)
    {
        const Vec3 v = rng.unitVector();
        ASSERT_TRUE(nearlyEqual(std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z), 1.0F, 1e-6F));
        sum = {sum.x + v.x, sum.y + v.y, sum.z + v.z};
        const Vec3 p = rng.pointInSphere();
        ASSERT_LE(p.x * p.x + p.y * p.y + p.z * p.z, 1.0F);
    }
    // Uniform directions average to the centre: each component's mean is within a few standard errors of 0.
    const float limit = 4.0F / std::sqrt(3.0F * static_cast<float>(c_count));
    EXPECT_LT(std::abs(sum.x / c_count), limit);
    EXPECT_LT(std::abs(sum.y / c_count), limit);
    EXPECT_LT(std::abs(sum.z / c_count), limit);
}

TEST(Random, WorksWithStd)
{
    static_assert(std::uniform_random_bit_generator<Random>);
    Random rng{3U};
    std::uniform_int_distribution<int> distribution{1, 10};
    const int value = distribution(rng);
    EXPECT_GE(value, 1);
    EXPECT_LE(value, 10);
}
