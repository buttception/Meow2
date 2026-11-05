/// @file
/// @brief Tests for bounding volumes, their containment and intersection tests, frustum extraction (reversed infinite
///        Z) and the batch sphere cull.

#include "MathPrinters.h"

#include <Meow2/Math/Bounds.h>
#include <Meow2/Math/Random.h>

#include <gtest/gtest.h>

#include <format>
#include <vector>

namespace Meow2::Math
{
namespace
{
/// @brief A camera at (0, 0, −10) looking down +Z, 90° vertical fov, square viewport.
Frustum testFrustum()
{
    const CameraLens lens{ProjectionType::Perspective, toRadians(90.0F), 10.0F, 0.5F, 1000.0F};
    return frustumFromCamera(cameraMatrices(Transform{{0.0F, 0.0F, -10.0F}, Quat{}}, lens, 1.0F));
}
} // namespace

TEST(Bounds, BuildAndContain)
{
    const Aabb box = aabbFromMinMax({-1.0F, 0.0F, 2.0F}, {3.0F, 4.0F, 6.0F});
    EXPECT_TRUE(nearlyEqual(box.center, Vec3{1.0F, 2.0F, 4.0F}));
    EXPECT_TRUE(nearlyEqual(box.extents, Vec3{2.0F, 2.0F, 2.0F}));
    EXPECT_TRUE(nearlyEqual(toMinMax(box).max, Vec3{3.0F, 4.0F, 6.0F}));
    EXPECT_TRUE(contains(box, Vec3{1.0F, 2.0F, 4.0F}));
    EXPECT_TRUE(contains(box, Vec3{3.0F, 4.0F, 6.0F})) << "the surface counts";
    EXPECT_FALSE(contains(box, Vec3{3.1F, 2.0F, 4.0F}));

    const std::vector<Vec3> points{{1.0F, 1.0F, 1.0F}, {-2.0F, 0.5F, 3.0F}, {0.0F, -4.0F, 2.0F}, {5.0F, 2.0F, -1.0F}};
    const std::optional<Aabb> around = aabbFromPoints(points);
    ASSERT_TRUE(around.has_value());
    const Sphere sphere = sphereFromPoints(points);
    for (const Vec3& p : points)
    {
        EXPECT_TRUE(contains(*around, p));
        EXPECT_TRUE(contains(Sphere{sphere.center, sphere.radius * 1.0001F}, p)) << std::format("{}", p);
    }
    EXPECT_FALSE(aabbFromPoints({}).has_value());

    const OrientedBox turned{Vec3{}, Vec3{2.0F, 0.5F, 0.5F}, fromAxisAngle(c_up, c_halfPi)};
    EXPECT_TRUE(contains(turned, Vec3{0.0F, 0.0F, 1.9F})) << "the long axis now points along Z";
    EXPECT_FALSE(contains(turned, Vec3{1.9F, 0.0F, 0.0F}));
}

TEST(Bounds, Intersections)
{
    const Aabb a{Vec3{}, Vec3{1.0F, 1.0F, 1.0F}};
    EXPECT_TRUE(intersects(a, Aabb{{2.0F, 0.0F, 0.0F}, {1.0F, 1.0F, 1.0F}})) << "touching faces";
    EXPECT_FALSE(intersects(a, Aabb{{2.1F, 0.0F, 0.0F}, {1.0F, 1.0F, 1.0F}}));
    EXPECT_TRUE(intersects(Sphere{Vec3{}, 1.0F}, Sphere{{1.5F, 0.0F, 0.0F}, 0.5F}));
    EXPECT_FALSE(intersects(Sphere{Vec3{}, 1.0F}, Sphere{{1.6F, 0.0F, 0.0F}, 0.5F}));
    EXPECT_TRUE(intersects(a, Sphere{{1.5F, 0.0F, 0.0F}, 0.6F})) << "near a face";
    EXPECT_FALSE(intersects(a, Sphere{{1.5F, 1.5F, 1.5F}, 0.8F})) << "near a corner: √0.75 ≈ 0.87 away";
    EXPECT_TRUE(intersects(a, Sphere{{1.5F, 1.5F, 1.5F}, 0.9F}));

    const OrientedBox bar{Vec3{}, Vec3{3.0F, 0.2F, 0.2F}, Quat{}};
    const OrientedBox diagonal{{2.0F, 0.0F, 2.0F}, Vec3{0.2F, 0.2F, 0.2F}, fromAxisAngle(c_up, 0.785F)};
    EXPECT_FALSE(intersects(bar, diagonal));
    const OrientedBox crossing{Vec3{1.0F, 0.0F, 0.0F}, Vec3{3.0F, 0.2F, 0.2F}, fromAxisAngle(c_up, c_halfPi)};
    EXPECT_TRUE(intersects(bar, crossing)) << "a cross";
}

TEST(Bounds, TransformAabb)
{
    const Aabb box{{1.0F, 0.0F, 0.0F}, {2.0F, 1.0F, 0.5F}};
    const Transform t{{10.0F, 0.0F, 0.0F}, fromAxisAngle(c_up, c_halfPi), {2.0F, 2.0F, 2.0F}};
    const Aabb moved = transformAabb(box, toMatrix(t));
    // A quarter turn about up swaps the X and Z sizes exactly; scale 2 doubles them; the centre moves with the box.
    EXPECT_TRUE(nearlyEqual(moved.extents, Vec3{1.0F, 2.0F, 4.0F}, 1e-5F)) << std::format("{}", moved.extents);
    EXPECT_TRUE(nearlyEqual(moved.center, transformPoint(t, box.center), 1e-5F));

    const Transform odd{{0.0F, 3.0F, 0.0F}, fromEuler({0.4F, 0.9F, -0.3F}), {1.0F, 2.0F, 0.5F}};
    const Aabb around = transformAabb(box, toMatrix(odd));
    const MinMax corners = toMinMax(box);
    for (int i = 0; i < 8; ++i)
    {
        const Vec3 corner{(i & 1) != 0 ? corners.max.x : corners.min.x, (i & 2) != 0 ? corners.max.y : corners.min.y,
                          (i & 4) != 0 ? corners.max.z : corners.min.z};
        const Vec3 p = transformPoint(odd, corner);
        EXPECT_TRUE(contains(Aabb{around.center, storeVec3(DirectX::XMVectorAdd(load(around.extents),
                                                                                DirectX::XMVectorReplicate(1e-4F)))},
                             p))
            << "corner " << i;
    }
    EXPECT_TRUE(contains(orientedBox(box, odd), transformPoint(odd, Vec3{2.9F, 0.9F, 0.4F})));
}

TEST(Bounds, FrustumReversedInfinite)
{
    const Frustum frustum = testFrustum();
    EXPECT_EQ(test(frustum, Sphere{{0.0F, 0.0F, 0.0F}, 0.1F}), Containment::Inside) << "straight ahead";
    EXPECT_EQ(test(frustum, Sphere{{0.0F, 0.0F, -20.0F}, 0.1F}), Containment::Outside) << "behind";
    EXPECT_EQ(test(frustum, Sphere{{0.0F, 0.0F, 1e6F}, 1.0F}), Containment::Inside) << "no far plane";
    EXPECT_EQ(test(frustum, Sphere{{30.0F, 0.0F, 10.0F}, 1.0F}), Containment::Outside)
        << "beside: the 90° frustum is 20 wide 10 in front of the camera";
    EXPECT_EQ(test(frustum, Sphere{{0.0F, 0.0F, -9.7F}, 0.1F}), Containment::Outside) << "inside the near distance";
}

TEST(Bounds, FrustumVsVolumes)
{
    const Frustum frustum = testFrustum();
    // 10 in front of the camera the frustum spans x, y in −10..10.
    EXPECT_EQ(test(frustum, Aabb{{0.0F, 0.0F, 0.0F}, {1.0F, 1.0F, 1.0F}}), Containment::Inside);
    EXPECT_EQ(test(frustum, Aabb{{10.0F, 0.0F, 0.0F}, {1.0F, 1.0F, 1.0F}}), Containment::Intersects) << "on the edge";
    EXPECT_EQ(test(frustum, Aabb{{15.0F, 0.0F, 0.0F}, {1.0F, 1.0F, 1.0F}}), Containment::Outside);
    EXPECT_EQ(test(frustum, Aabb{{0.0F, -10.0F, 0.0F}, {1.0F, 1.0F, 1.0F}}), Containment::Intersects) << "bottom";
    EXPECT_EQ(test(frustum, Sphere{{0.0F, 10.0F, 0.0F}, 2.0F}), Containment::Intersects) << "top";
    EXPECT_EQ(test(frustum, Aabb{{0.0F, 0.0F, -10.0F}, {1.0F, 1.0F, 1.0F}}), Containment::Intersects)
        << "around the camera: crosses the near plane";
}

TEST(Bounds, BatchCullMatchesSingle)
{
    const Frustum frustum = testFrustum();
    Random rng{404U};
    std::vector<Sphere> spheres(10000);
    for (Sphere& s : spheres)
    {
        s = Sphere{Vec3{rng.range(-60.0F, 60.0F), rng.range(-60.0F, 60.0F), rng.range(-40.0F, 80.0F)},
                   rng.range(0.0F, 3.0F)};
    }
    std::vector<std::uint8_t> visible(spheres.size());
    cullSpheres(frustum, spheres, visible);
    int visibleCount = 0;
    for (std::size_t i = 0; i < spheres.size(); ++i)
    {
        const bool expected = test(frustum, spheres[i]) != Containment::Outside;
        ASSERT_EQ(visible[i] != 0, expected) << "sphere " << i << " " << std::format("{}", spheres[i].center);
        visibleCount += visible[i];
    }
    EXPECT_GT(visibleCount, 100) << "the test sees both outcomes";
    EXPECT_LT(visibleCount, 9900);
}
} // namespace Meow2::Math
