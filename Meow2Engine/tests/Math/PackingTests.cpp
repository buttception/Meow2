/// @file
/// @brief Tests for vertex packing: half floats, normalised integers, octahedral normals, packed tangent frames and
///        quantised positions, each against its documented error.

#include "MathPrinters.h"

#include <Meow2/Math/Packing.h>
#include <Meow2/Math/Random.h>

#include <gtest/gtest.h>

#include <cmath>
#include <format>
#include <limits>

namespace Meow2::Math
{
namespace
{
float angleBetweenVectors(const Vec3& a, const Vec3& b)
{
    using namespace DirectX;
    const XMVECTOR va = load(a);
    const XMVECTOR vb = load(b);
    // atan2(|a × b|, a · b): precise for tiny angles, unlike acos.
    return std::atan2(XMVectorGetX(XMVector3Length(XMVector3Cross(va, vb))), XMVectorGetX(XMVector3Dot(va, vb)));
}
} // namespace

TEST(Packing, HalfFloat)
{
    EXPECT_EQ(toHalf(1.0F), 0x3C00U);
    EXPECT_EQ(toHalf(-2.0F), 0xC000U);
    EXPECT_EQ(toHalf(65504.0F), 0x7BFFU) << "the largest half";
    EXPECT_EQ(fromHalf(toHalf(1e6F)), std::numeric_limits<float>::infinity()) << "too large becomes infinity";
    Random rng{16U};
    for (int i = 0; i < 10000; ++i)
    {
        const float value = rng.range(-1000.0F, 1000.0F);
        const float back = fromHalf(toHalf(value));
        ASSERT_LE(std::abs(back - value), std::abs(value) * (1.0F / 2048.0F) + 1e-7F) << value;
    }
}

TEST(Packing, NormalisedIntegers)
{
    for (int i = 0; i < 256; ++i)
    {
        const auto byte = static_cast<std::uint8_t>(i);
        ASSERT_EQ(packUnorm8(unpackUnorm8(byte)), byte);
    }
    for (int i = -127; i <= 127; ++i)
    {
        const auto value = static_cast<std::int8_t>(i);
        ASSERT_EQ(packSnorm8(unpackSnorm8(value)), value);
    }
    EXPECT_EQ(packUnorm8(0.0F), 0U);
    EXPECT_EQ(packUnorm8(1.0F), 255U);
    EXPECT_EQ(packUnorm8(2.0F), 255U) << "clamped";
    EXPECT_EQ(packSnorm8(-1.0F), -127) << "−128 is never produced";
    EXPECT_EQ(packSnorm8(0.0F), 0);
    EXPECT_EQ(unpackSnorm8(-128), -1.0F) << "the GPU rule: −128 also decodes to −1";
    EXPECT_EQ(unpackSnorm16(-32768), -1.0F);

    for (float value = 0.0F; value <= 1.0F; value += 0.001F)
    {
        ASSERT_LE(std::abs(unpackUnorm16(packUnorm16(value)) - value), 0.5F / 65535.0F + 1e-7F);
        ASSERT_LE(std::abs(unpackSnorm16(packSnorm16(2.0F * value - 1.0F)) - (2.0F * value - 1.0F)),
                  0.5F / 32767.0F + 1e-7F);
    }
}

TEST(Packing, OctahedralNormals)
{
    for (const Vec3 axis :
         {c_right, c_up, c_forward, Vec3{-1.0F, 0.0F, 0.0F}, Vec3{0.0F, -1.0F, 0.0F}, Vec3{0.0F, 0.0F, -1.0F}})
    {
        EXPECT_TRUE(nearlyEqual(unpackOctahedral(packOctahedral(axis)), axis, 1e-6F)) << std::format("{}", axis);
    }
    Random rng{3U};
    float worst = 0.0F;
    for (int i = 0; i < 10000; ++i)
    {
        const Vec3 normal = rng.unitVector();
        const Vec3 back = unpackOctahedral(packOctahedral(normal));
        worst = std::max(worst, angleBetweenVectors(normal, back));
        ASSERT_NEAR(std::sqrt(back.x * back.x + back.y * back.y + back.z * back.z), 1.0F, 1e-6F);
    }
    EXPECT_LT(worst, 1e-4F) << "worst error " << worst << " rad";
}

TEST(Packing, TangentFrame)
{
    Random rng{11U};
    for (int i = 0; i < 2000; ++i)
    {
        const Vec3 normal = rng.unitVector();
        // Any tangent perpendicular to the normal.
        const Vec3 tangent =
            storeVec3(DirectX::XMVector3Normalize(DirectX::XMVector3Cross(load(normal), load(rng.unitVector()))));
        const float handedness = (i % 2 == 0) ? 1.0F : -1.0F;
        const TangentFrame back = unpackTangentFrame(packTangentFrame(normal, tangent, handedness));
        ASSERT_LT(angleBetweenVectors(normal, back.normal), 1e-3F) << i;
        ASSERT_LT(angleBetweenVectors(tangent, back.tangent), 1e-3F) << i;
        ASSERT_EQ(back.handedness, handedness) << i;
    }

    // A half turn about up has w = 0: the sign must still survive.
    const Vec3 normal{0.0F, 0.0F, -1.0F};
    const Vec3 tangent{-1.0F, 0.0F, 0.0F};
    for (const float handedness : {1.0F, -1.0F})
    {
        const TangentFrame back = unpackTangentFrame(packTangentFrame(normal, tangent, handedness));
        EXPECT_EQ(back.handedness, handedness);
        EXPECT_TRUE(nearlyEqual(back.normal, normal, 1e-3F)) << std::format("{}", back.normal);
        EXPECT_TRUE(nearlyEqual(back.tangent, tangent, 1e-3F)) << std::format("{}", back.tangent);
    }
}

TEST(Packing, QuantizedPositions)
{
    const Aabb bounds = aabbFromMinMax({-1.0F, 0.0F, -3.0F}, {1.0F, 2.0F, 5.0F});
    const MinMax corners = toMinMax(bounds);
    EXPECT_TRUE(nearlyEqual(dequantizePosition(quantizePosition(corners.min, bounds), bounds), corners.min, 1e-6F));
    EXPECT_TRUE(nearlyEqual(dequantizePosition(quantizePosition(corners.max, bounds), bounds), corners.max, 1e-6F));

    const Vec3 error = positionQuantizationError(bounds);
    Random rng{21U};
    for (int i = 0; i < 10000; ++i)
    {
        const Vec3 p{rng.range(-1.0F, 1.0F), rng.range(0.0F, 2.0F), rng.range(-3.0F, 5.0F)};
        const Vec3 back = dequantizePosition(quantizePosition(p, bounds), bounds);
        ASSERT_LE(std::abs(back.x - p.x), error.x * 1.01F) << i;
        ASSERT_LE(std::abs(back.y - p.y), error.y * 1.01F) << i;
        ASSERT_LE(std::abs(back.z - p.z), error.z * 1.01F) << i;
    }

    const Aabb flat = aabbFromMinMax({0.0F, 1.0F, 0.0F}, {4.0F, 1.0F, 4.0F});
    EXPECT_TRUE(nearlyEqual(dequantizePosition(quantizePosition({2.0F, 1.0F, 3.0F}, flat), flat),
                            Vec3{2.0F, 1.0F, 3.0F}, 1e-4F))
        << "a flat axis (a floor) keeps its one value";
}
} // namespace Meow2::Math
