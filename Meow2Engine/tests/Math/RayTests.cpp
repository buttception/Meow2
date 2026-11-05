/// @file
/// @brief Tests for rays: from a screen pixel, against planes, spheres, boxes and triangles, and picking the closest
///        box.

#include "MathPrinters.h"

#include <Meow2/Math/Ray.h>

#include <gtest/gtest.h>

#include <cmath>
#include <format>
#include <vector>

namespace Meow2::Math
{
namespace
{
constexpr Viewport c_viewport{0.0F, 0.0F, 1000.0F, 500.0F, 0.0F, 1.0F};

/// @brief A camera at (0, 1, −10) looking down +Z: 90° vertical fov, aspect 2, near 0.5.
CameraMatrices someCamera(ProjectionType type = ProjectionType::Perspective)
{
    const CameraLens lens{type, toRadians(90.0F), 10.0F, 0.5F, 100.0F};
    return cameraMatrices(Transform{{0.0F, 1.0F, -10.0F}, Quat{}}, lens, 2.0F);
}
} // namespace

TEST(Ray, FromScreen)
{
    const CameraMatrices matrices = someCamera();
    const Ray centre = rayFromScreen(500.0F, 250.0F, matrices, c_viewport);
    EXPECT_TRUE(nearlyEqual(centre.origin, Vec3{0.0F, 1.0F, -9.5F}, 1e-4F)) << "starts on the near plane";
    EXPECT_TRUE(nearlyEqual(centre.direction, c_forward, 1e-5F)) << "straight ahead";

    // The top-right corner: with a 90° vertical fov and aspect 2 the ray goes 2 right and 1 up per 1 ahead.
    const Ray corner = rayFromScreen(1000.0F, 0.0F, matrices, c_viewport);
    const float length = std::sqrt(6.0F);
    EXPECT_TRUE(nearlyEqual(corner.direction, Vec3{2.0F / length, 1.0F / length, 1.0F / length}, 1e-4F))
        << std::format("{}", corner.direction);

    const CameraMatrices orthographic = someCamera(ProjectionType::Orthographic);
    const Ray left = rayFromScreen(100.0F, 250.0F, orthographic, c_viewport);
    const Ray right = rayFromScreen(900.0F, 250.0F, orthographic, c_viewport);
    EXPECT_TRUE(nearlyEqual(left.direction, right.direction, 1e-5F)) << "orthographic rays are parallel";
    EXPECT_TRUE(nearlyEqual(left.direction, c_forward, 1e-5F));
}

TEST(Ray, PlaneAndSphere)
{
    const Ray down{{0.0F, 10.0F, 0.0F}, {0.0F, -1.0F, 0.0F}};
    const Plane ground = planeFromPointNormal({0.0F, 2.0F, 0.0F}, {0.0F, 3.0F, 0.0F});
    ASSERT_TRUE(intersect(down, ground).has_value());
    EXPECT_NEAR(*intersect(down, ground), 8.0F, 1e-5F);
    EXPECT_FALSE(intersect(Ray{{0.0F, 10.0F, 0.0F}, c_right}, ground).has_value()) << "parallel";
    EXPECT_FALSE(intersect(Ray{{0.0F, 10.0F, 0.0F}, c_up}, ground).has_value()) << "pointing away";
    EXPECT_TRUE(nearlyEqual(pointAt(down, 8.0F), Vec3{0.0F, 2.0F, 0.0F}));

    const Sphere ball{{0.0F, 0.0F, 10.0F}, 2.0F};
    const Ray ahead{Vec3{}, c_forward};
    ASSERT_TRUE(intersect(ahead, ball).has_value());
    EXPECT_NEAR(*intersect(ahead, ball), 8.0F, 1e-5F);
    EXPECT_FALSE(intersect(Ray{{3.0F, 0.0F, 0.0F}, c_forward}, ball).has_value()) << "passes beside";
    EXPECT_FALSE(intersect(Ray{{0.0F, 0.0F, 20.0F}, c_forward}, ball).has_value()) << "behind the ray";
    EXPECT_EQ(intersect(Ray{{0.0F, 0.0F, 10.5F}, c_forward}, ball), 0.0F) << "starting inside hits at 0";
}

TEST(Ray, BoxesSlab)
{
    const Aabb box{{0.0F, 0.0F, 10.0F}, {1.0F, 1.0F, 1.0F}};
    ASSERT_TRUE(intersect(Ray{Vec3{}, c_forward}, box).has_value());
    EXPECT_NEAR(*intersect(Ray{Vec3{}, c_forward}, box), 9.0F, 1e-5F) << "the near face";
    EXPECT_FALSE(intersect(Ray{{1.5F, 0.0F, 0.0F}, c_forward}, box).has_value()) << "axis-parallel, beside it";
    EXPECT_TRUE(intersect(Ray{{1.0F, 1.0F, 0.0F}, c_forward}, box).has_value()) << "grazing an edge counts";
    EXPECT_EQ(intersect(Ray{{0.0F, 0.0F, 10.0F}, c_up}, box), 0.0F) << "inside";
    EXPECT_FALSE(intersect(Ray{{0.0F, 0.0F, 20.0F}, c_forward}, box).has_value()) << "behind";

    const Ray slanted{{-10.0F, 0.0F, 0.0F},
                      storeVec3(DirectX::XMVector3Normalize(DirectX::XMVectorSet(1.0F, 0.0F, 1.0F, 0.0F)))};
    const std::optional<float> slantedHit = intersect(slanted, box);
    ASSERT_TRUE(slantedHit.has_value());
    EXPECT_TRUE(nearlyEqual(pointAt(slanted, *slantedHit), Vec3{-1.0F, 0.0F, 9.0F}, 1e-4F))
        << "enters through the left face";

    // A thin bar along X, turned 90° about up: it now lies along Z, from z = 7 to 13.
    const OrientedBox bar{{0.0F, 0.0F, 10.0F}, {3.0F, 0.2F, 0.2F}, fromAxisAngle(c_up, c_halfPi)};
    ASSERT_TRUE(intersect(Ray{Vec3{}, c_forward}, bar).has_value());
    EXPECT_NEAR(*intersect(Ray{Vec3{}, c_forward}, bar), 7.0F, 1e-4F) << "its end faces the ray";
    EXPECT_FALSE(intersect(Ray{{1.0F, 0.0F, 0.0F}, c_forward}, bar).has_value()) << "it's thin along X now";
    ASSERT_TRUE(intersect(Ray{{-10.0F, 0.0F, 12.0F}, c_right}, bar).has_value());
    EXPECT_NEAR(*intersect(Ray{{-10.0F, 0.0F, 12.0F}, c_right}, bar), 9.8F, 1e-4F) << "from the side";
}

TEST(Ray, Triangle)
{
    // Clockwise seen from −Z: the front side for a ray going +Z.
    const Vec3 v0{0.0F, 0.0F, 5.0F};
    const Vec3 v1{0.0F, 2.0F, 5.0F};
    const Vec3 v2{2.0F, 0.0F, 5.0F};
    const std::optional<TriangleHit> hit = intersect(Ray{{0.5F, 0.5F, 0.0F}, c_forward}, v0, v1, v2);
    ASSERT_TRUE(hit.has_value());
    EXPECT_NEAR(hit->distance, 5.0F, 1e-5F);
    EXPECT_NEAR(hit->u, 0.25F, 1e-5F) << "weight of v1 (y = 0.5 of 2)";
    EXPECT_NEAR(hit->v, 0.25F, 1e-5F) << "weight of v2 (x = 0.5 of 2)";
    EXPECT_TRUE(intersect(Ray{{1.0F, 1.0F, 0.0F}, c_forward}, v0, v1, v2).has_value()) << "on the long edge";
    EXPECT_FALSE(intersect(Ray{{1.5F, 1.5F, 0.0F}, c_forward}, v0, v1, v2).has_value()) << "outside";

    const Ray fromBehind{{0.5F, 0.5F, 10.0F}, {0.0F, 0.0F, -1.0F}};
    EXPECT_TRUE(intersect(fromBehind, v0, v1, v2).has_value()) << "two-sided by default";
    EXPECT_FALSE(intersect(fromBehind, v0, v1, v2, true).has_value()) << "back face culled";
    EXPECT_TRUE(intersect(Ray{{0.5F, 0.5F, 0.0F}, c_forward}, v0, v1, v2, true).has_value()) << "front face kept";
}

TEST(Ray, PickClosestFromScreen)
{
    // Three boxes behind each other on the view axis and one off to the side: a click in the middle of the screen
    // must pick the nearest of the three, a click half way to the left edge the side one, and the sky nothing.
    const std::vector<Aabb> boxes{
        {{0.0F, 1.0F, 20.0F}, {1.0F, 1.0F, 1.0F}},
        {{0.0F, 1.0F, 5.0F}, {1.0F, 1.0F, 1.0F}},
        {{0.0F, 1.0F, 12.0F}, {3.0F, 3.0F, 1.0F}},
        {{-15.0F, 1.0F, 5.0F}, {1.0F, 1.0F, 1.0F}},
    };
    const CameraMatrices matrices = someCamera();
    const std::optional<RayPick> middle = pickClosest(rayFromScreen(500.0F, 250.0F, matrices, c_viewport), boxes);
    ASSERT_TRUE(middle.has_value());
    EXPECT_EQ(middle->index, 1U);
    EXPECT_NEAR(middle->distance, 13.5F, 1e-3F) << "from the near plane at z = −9.5 to the face at z = 4";

    // Box 3 is 15 to the left and 15 ahead of the camera: x / z = −1, which is NDC −0.5 at aspect 2 → pixel 250.
    const std::optional<RayPick> left = pickClosest(rayFromScreen(250.0F, 250.0F, matrices, c_viewport), boxes);
    ASSERT_TRUE(left.has_value());
    EXPECT_EQ(left->index, 3U);
    EXPECT_FALSE(pickClosest(rayFromScreen(500.0F, 10.0F, matrices, c_viewport), boxes).has_value()) << "sky";
}
} // namespace Meow2::Math
