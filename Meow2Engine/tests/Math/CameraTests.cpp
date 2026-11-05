/// @file
/// @brief Tests for camera maths: view matrix, lens projection, world ↔ screen and camera-relative precision.

#include "MathPrinters.h"

#include <Meow2/Math/Camera.h>

#include <gtest/gtest.h>

#include <cmath>
#include <format>

namespace Meow2::Math
{
namespace
{
Vec3 throughMatrix(const Vec3& point, DirectX::FXMMATRIX matrix)
{
    return storeVec3(DirectX::XMVector3TransformCoord(load(point), matrix));
}

/// @brief NDC of a world point through a view matrix and a projection.
Vec3 ndcOf(const Vec3& point, DirectX::FXMMATRIX view, DirectX::CXMMATRIX projection)
{
    return throughMatrix(point, DirectX::XMMatrixMultiply(view, projection));
}
} // namespace

TEST(Camera, ViewMatrixInvertsTransform)
{
    const Transform camera{{10.0F, 5.0F, -3.0F}, fromEuler({0.2F, 1.0F, 0.0F}), {4.0F, 4.0F, 4.0F}};
    const DirectX::XMMATRIX view = viewMatrix(camera);
    EXPECT_TRUE(nearlyEqual(throughMatrix(camera.position, view), Vec3{}, 1e-5F)) << "the camera sits at the origin";
    const Vec3 ahead = transformPoint(Transform{camera.position, camera.rotation}, Vec3{0.0F, 0.0F, 7.0F});
    EXPECT_TRUE(nearlyEqual(throughMatrix(ahead, view), Vec3{0.0F, 0.0F, 7.0F}, 1e-4F))
        << "what the camera faces is on +Z, at its real distance: the scale is ignored";
    const Vec3 above = transformPoint(Transform{camera.position, camera.rotation}, c_up);
    EXPECT_TRUE(nearlyEqual(throughMatrix(above, view), c_up, 1e-4F)) << "camera up is view +Y";
}

TEST(Camera, ProjectionFromLens)
{
    const CameraLens perspective;
    EXPECT_TRUE(nearlyEqual(storeMat4(projectionMatrix(perspective, 1.5F)),
                            storeMat4(perspectiveReversedInfiniteLH(toRadians(60.0F), 1.5F, 0.1F))));
    const CameraLens orthographic{ProjectionType::Orthographic, 0.0F, 20.0F, 1.0F, 50.0F};
    EXPECT_TRUE(nearlyEqual(storeMat4(projectionMatrix(orthographic, 2.0F)),
                            storeMat4(orthographicReversedLH(40.0F, 20.0F, 1.0F, 50.0F))));
}

TEST(Camera, WorldToScreenAndBack)
{
    const Transform camera{{0.0F, 2.0F, -10.0F}, Quat{}, {1.0F, 1.0F, 1.0F}};
    const Viewport viewport{0.0F, 0.0F, 1600.0F, 900.0F, 0.0F, 1.0F};
    const CameraMatrices matrices = cameraMatrices(camera, CameraLens{}, 1600.0F / 900.0F);

    const std::optional<Vec3> centre = worldToScreen(Vec3{0.0F, 2.0F, 5.0F}, matrices, viewport);
    ASSERT_TRUE(centre.has_value());
    EXPECT_TRUE(nearlyEqual(*centre, Vec3{800.0F, 450.0F, 0.1F / 15.0F}, 1e-4F))
        << "straight ahead → the middle pixel, depth near/distance: " << std::format("{}", *centre);
    const std::optional<Vec3> higher = worldToScreen(Vec3{0.0F, 4.0F, 5.0F}, matrices, viewport);
    ASSERT_TRUE(higher.has_value());
    EXPECT_LT(higher->y, 450.0F) << "higher in the world is higher on screen (smaller y)";
    EXPECT_FALSE(worldToScreen(Vec3{0.0F, 2.0F, -20.0F}, matrices, viewport).has_value()) << "behind the camera";

    for (const Vec3 world : {Vec3{3.0F, 1.0F, 4.0F}, Vec3{-7.0F, 6.0F, 30.0F}, Vec3{0.5F, 2.5F, -9.5F}})
    {
        const std::optional<Vec3> screen = worldToScreen(world, matrices, viewport);
        ASSERT_TRUE(screen.has_value());
        EXPECT_TRUE(nearlyEqual(screenToWorld(*screen, matrices, viewport), world, 1e-3F)) << std::format("{}", world);
    }
    EXPECT_TRUE(
        nearlyEqual(screenToWorld(Vec3{800.0F, 450.0F, 1.0F}, matrices, viewport), Vec3{0.0F, 2.0F, -9.9F}, 1e-4F))
        << "depth 1 is on the near plane";
}

TEST(Camera, CameraRelativeKeepsPrecision)
{
    // The same scene twice: once at the origin, once 100 km away. Positions are exactly representable floats, so any
    // difference in the result comes from the maths, not from storing the positions.
    const Quat rotation = fromEuler({0.3F, 0.7F, 0.0F});
    const Vec3 offset{1.25F, -0.5F, 12.0F};
    const DirectX::XMMATRIX projection = perspectiveReversedInfiniteLH(1.0F, 1.0F, 0.1F);
    const Vec3 reference = ndcOf(offset, viewMatrix(Transform{Vec3{}, rotation}), projection);

    const WorldPosition far{100000.0F, 250.0F, 100000.0F};
    const WorldPosition point{far.x + offset.x, far.y + offset.y, far.z + offset.z};
    const Vec3 relative = ndcOf(cameraRelative(point, far), cameraRelativeViewMatrix(rotation), projection);
    const Vec3 absolute = ndcOf(point, viewMatrix(Transform{far, rotation}), projection);

    EXPECT_EQ(relative.x, reference.x) << "camera-relative: identical to the scene at the origin";
    EXPECT_EQ(relative.y, reference.y);
    const float absoluteError = std::abs(absolute.x - reference.x) + std::abs(absolute.y - reference.y);
    EXPECT_GT(absoluteError, 1e-5F) << "absolute positions lose precision 100 km out";
    EXPECT_LT(absoluteError, 1e-2F) << "but both still show the same point";
}
} // namespace Meow2::Math
