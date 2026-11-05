/// @file
/// @brief Tests for the SIMD baseline and the batch helpers (transform points/directions, min/max).

#include "MathPrinters.h"

#include <Meow2/Math/Batch.h>
#include <Meow2/Math/Types.h>

#include <gtest/gtest.h>

#include <cstddef>
#include <format>
#include <vector>

using namespace Meow2::Math;

namespace
{
/// @brief 1000 deterministic, uneven points.
std::vector<Vec3> makePoints()
{
    std::vector<Vec3> points;
    for (int i = 0; i < 1000; ++i)
    {
        const auto f = static_cast<float>(i);
        points.emplace_back(f * 0.37F - 100.0F, (f * 1.7F) - 50.0F * static_cast<float>(i % 7), 3.0F - f * 0.01F);
    }
    return points;
}

DirectX::XMMATRIX someTransform()
{
    return DirectX::XMMatrixScaling(2.0F, 0.5F, 3.0F) * DirectX::XMMatrixRotationRollPitchYaw(0.3F, 1.1F, -0.4F) *
           DirectX::XMMatrixTranslation(10.0F, -20.0F, 5.0F);
}
} // namespace

TEST(Simd, BaselineMatchesBuild)
{
#if defined(__AVX2__)
#if !defined(_XM_AVX2_INTRINSICS_) || !defined(_XM_FMA3_INTRINSICS_) || !defined(_XM_F16C_INTRINSICS_)
    FAIL() << "an AVX2 build must use DirectXMath's AVX2, FMA3 and F16C paths";
#endif
    SUCCEED();
#elif defined(_M_X64) || defined(__x86_64__)
#if !defined(_XM_SSE4_INTRINSICS_)
    FAIL() << "an SSE4.2 build must use DirectXMath's SSE4 paths";
#endif
    SUCCEED();
#else
    GTEST_SKIP() << "not an x64 build";
#endif
}

TEST(Batch, TransformPointsMatchesOneByOne)
{
    const std::vector<Vec3> points = makePoints();
    const DirectX::XMMATRIX matrix = someTransform();
    std::vector<Vec3> batch(points.size());
    transformPoints(points, matrix, batch);
    for (std::size_t i = 0; i < points.size(); ++i)
    {
        const Vec3 single = storeVec3(DirectX::XMVector3TransformCoord(load(points[i]), matrix));
        ASSERT_TRUE(nearlyEqual(batch[i], single, 1e-4F))
            << "point " << i << ": " << std::format("{} vs {}", batch[i], single);
    }
}

TEST(Batch, TransformDirectionsIgnoresTranslation)
{
    const std::vector<Vec3> directions{c_right, c_up, c_forward};
    const DirectX::XMMATRIX moveOnly = DirectX::XMMatrixTranslation(100.0F, 200.0F, 300.0F);
    std::vector<Vec3> out(directions.size());
    transformDirections(directions, moveOnly, out);
    for (std::size_t i = 0; i < directions.size(); ++i)
    {
        EXPECT_TRUE(nearlyEqual(out[i], directions[i])) << "a translation doesn't change a direction";
    }

    transformDirections(directions, DirectX::XMMatrixRotationY(c_halfPi), out);
    EXPECT_TRUE(nearlyEqual(out[2], Vec3{1.0F, 0.0F, 0.0F}, 1e-5F))
        << "forward turned a quarter turn about +Y (left-handed, clockwise seen from above) points right: "
        << std::format("{}", out[2]);
}

TEST(Batch, MinMax)
{
    EXPECT_FALSE(computeMinMax({}).has_value()) << "no points, no box";
    const std::vector<Vec3> points{{1, 2, 3}, {-4, 5, 0}, {2, -6, 9}};
    const std::optional<MinMax> box = computeMinMax(points);
    ASSERT_TRUE(box.has_value());
    EXPECT_TRUE(nearlyEqual(box->min, Vec3{-4, -6, 0}));
    EXPECT_TRUE(nearlyEqual(box->max, Vec3{2, 5, 9}));
}
