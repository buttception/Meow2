/// @file
/// @brief Tests for the reversed-Z projections (against hand-derived values and DirectXMath), depth linearisation and
///        the Vulkan viewport flip.

#include "MathPrinters.h"

#include <Meow2/Math/Projection.h>
#include <Meow2/Math/Scalar.h>

#include <gtest/gtest.h>

#include <cmath>
#include <format>

namespace Meow2::Math
{
namespace
{
/// @brief Projects a view-space point and divides by w: normalised device coordinates.
Vec3 toNdc(const Vec3& viewPoint, DirectX::FXMMATRIX projection)
{
    return storeVec3(DirectX::XMVector3TransformCoord(load(viewPoint), projection));
}
} // namespace

TEST(Projection, InfiniteHandChecked)
{
    const float fovY = toRadians(90.0F);
    const float aspect = 2.0F;
    const float nearZ = 0.5F;
    const Mat4 m = storeMat4(perspectiveReversedInfiniteLH(fovY, aspect, nearZ));
    // tan(45°) = 1, so yScale = 1 and xScale = 1 / aspect.
    EXPECT_NEAR(m.m[0][0], 0.5F, 1e-6F);
    EXPECT_NEAR(m.m[1][1], 1.0F, 1e-6F);
    EXPECT_EQ(m.m[2][2], 0.0F);
    EXPECT_EQ(m.m[2][3], 1.0F) << "w = z: the perspective divide";
    EXPECT_EQ(m.m[3][2], nearZ) << "clip z = near, so depth = near / z";
    EXPECT_EQ(m.m[3][3], 0.0F);

    const DirectX::XMMATRIX projection = perspectiveReversedInfiniteLH(fovY, aspect, nearZ);
    EXPECT_NEAR(toNdc({0.0F, 0.0F, nearZ}, projection).z, 1.0F, 1e-6F) << "near plane → 1";
    EXPECT_NEAR(toNdc({0.0F, 0.0F, 2.0F * nearZ}, projection).z, 0.5F, 1e-6F) << "twice as far → 0.5";
    EXPECT_LT(toNdc({0.0F, 0.0F, 1e6F}, projection).z, 1e-6F) << "very far → almost 0";
    EXPECT_GT(toNdc({0.0F, 0.0F, 1e6F}, projection).z, 0.0F) << "but never behind the far 'plane'";
    EXPECT_NEAR(toNdc({0.0F, 10.0F, 10.0F}, projection).y, 1.0F, 1e-6F) << "the top edge of a 90° fov";
    EXPECT_NEAR(toNdc({20.0F, 0.0F, 10.0F}, projection).x, 1.0F, 1e-6F) << "the right edge, wider by the aspect";
}

TEST(Projection, FiniteMatchesDirectXSwapped)
{
    const DirectX::XMMATRIX projection = perspectiveReversedLH(1.0F, 1.5F, 0.1F, 500.0F);
    EXPECT_TRUE(
        nearlyEqual(storeMat4(projection), storeMat4(DirectX::XMMatrixPerspectiveFovLH(1.0F, 1.5F, 500.0F, 0.1F))));
    EXPECT_NEAR(toNdc({0.0F, 0.0F, 0.1F}, projection).z, 1.0F, 1e-5F) << "near → 1";
    EXPECT_NEAR(toNdc({0.0F, 0.0F, 500.0F}, projection).z, 0.0F, 1e-5F) << "far → 0";
}

TEST(Projection, OrthographicReversed)
{
    const DirectX::XMMATRIX projection = orthographicReversedLH(20.0F, 10.0F, 1.0F, 101.0F);
    EXPECT_NEAR(toNdc({0.0F, 0.0F, 1.0F}, projection).z, 1.0F, 1e-6F);
    EXPECT_NEAR(toNdc({0.0F, 0.0F, 101.0F}, projection).z, 0.0F, 1e-6F);
    EXPECT_NEAR(toNdc({0.0F, 0.0F, 51.0F}, projection).z, 0.5F, 1e-6F) << "linear in between";
    EXPECT_TRUE(nearlyEqual(toNdc({10.0F, 5.0F, 30.0F}, projection), Vec3{1.0F, 1.0F, 0.71F}, 1e-5F))
        << "box edges → ±1, no perspective";
}

TEST(Projection, DepthLinearises)
{
    const float nearZ = 0.1F;
    const DirectX::XMMATRIX projection = perspectiveReversedInfiniteLH(1.0F, 1.0F, nearZ);
    for (const float distance : {0.1F, 1.0F, 37.5F, 1000.0F, 1e5F})
    {
        const float depth = toNdc({0.0F, 0.0F, distance}, projection).z;
        EXPECT_TRUE(nearlyEqual(viewDistanceFromDepth(depth, nearZ), distance, 1e-5F)) << distance;
    }
}

TEST(Projection, VulkanYFlip)
{
    const Viewport d3d{10.0F, 20.0F, 1920.0F, 1080.0F, 0.0F, 1.0F};
    const Viewport vulkan = flipForVulkan(d3d);
    EXPECT_EQ(vulkan.y, 1100.0F);
    EXPECT_EQ(vulkan.height, -1080.0F);

    EXPECT_TRUE(nearlyEqual(ndcToScreen(d3d, {-1.0F, 1.0F, 1.0F}), Vec3{10.0F, 20.0F, 1.0F}))
        << "NDC top-left → the viewport's top-left pixel (y down)";
    // Vulkan's own rule: screenY = y + (1 + ndcY) · height / 2. With the flipped viewport it must match D3D's.
    for (const Vec3 ndc : {Vec3{-1.0F, 1.0F, 0.0F}, Vec3{0.25F, -0.5F, 0.3F}, Vec3{1.0F, -1.0F, 1.0F}})
    {
        const Vec3 viaD3d = ndcToScreen(d3d, ndc);
        const float vulkanY = vulkan.y + (1.0F + ndc.y) * 0.5F * vulkan.height;
        EXPECT_NEAR(vulkanY, viaD3d.y, 1e-3F) << std::format("{}", ndc);
    }
}
} // namespace Meow2::Math
