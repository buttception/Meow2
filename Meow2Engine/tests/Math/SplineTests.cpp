/// @file
/// @brief Tests for the spline segments (Catmull-Rom, Bézier, Hermite, rotation squad) and SplinePath's
///        constant-speed travel.

#include "MathPrinters.h"

#include <Meow2/Math/Spline.h>

#include <gtest/gtest.h>

#include <format>
#include <vector>

using namespace Meow2::Math;

namespace
{
float distanceBetween(const Vec3& a, const Vec3& b)
{
    using namespace DirectX;
    return XMVectorGetX(XMVector3Length(load(a) - load(b)));
}
} // namespace

TEST(Spline, CurvesHitTheirPoints)
{
    const Vec3 p0{0.0F, 0.0F, 0.0F};
    const Vec3 p1{1.0F, 2.0F, 0.0F};
    const Vec3 p2{3.0F, 2.0F, 1.0F};
    const Vec3 p3{4.0F, 0.0F, 1.0F};
    EXPECT_TRUE(nearlyEqual(catmullRom(p0, p1, p2, p3, 0.0F), p1)) << "Catmull-Rom passes through p1…";
    EXPECT_TRUE(nearlyEqual(catmullRom(p0, p1, p2, p3, 1.0F), p2)) << "…and p2";

    EXPECT_TRUE(nearlyEqual(bezier(p0, p1, p2, p3, 0.0F), p0)) << "Bézier starts on p0";
    EXPECT_TRUE(nearlyEqual(bezier(p0, p1, p2, p3, 1.0F), p3)) << "and ends on p3";
    EXPECT_TRUE(nearlyEqual(bezier(p0, p1, p2, p3, 0.5F), Vec3{2.0F, 1.5F, 0.5F})) << "(p0 + 3p1 + 3p2 + p3) / 8";
    EXPECT_TRUE(nearlyEqual(bezierTangent(p0, p1, p2, p3, 0.0F), Vec3{3.0F, 6.0F, 0.0F}))
        << "leaves towards p1: 3(p1 − p0)";

    const Vec3 m0{5.0F, 0.0F, 0.0F};
    const Vec3 m1{0.0F, 0.0F, 5.0F};
    EXPECT_TRUE(nearlyEqual(hermite(p0, m0, p3, m1, 0.0F), p0));
    EXPECT_TRUE(nearlyEqual(hermite(p0, m0, p3, m1, 1.0F), p3));
    // Its slope at the start is m0: a small step moves by about m0 × step.
    const float h = 1e-3F;
    using namespace DirectX;
    const Vec3 step = storeVec3((load(hermite(p0, m0, p3, m1, h)) - load(p0)) / h);
    EXPECT_TRUE(nearlyEqual(step, m0, 1e-2F)) << std::format("{}", step);
}

TEST(Spline, RotationCatmullRom)
{
    const Quat q0 = fromAxisAngle(c_up, 0.0F);
    const Quat q1 = fromAxisAngle(c_up, 0.5F);
    const Quat q2 = fromAxisAngle(c_up, 1.0F);
    const Quat q3 = fromAxisAngle(c_up, 1.5F);
    EXPECT_TRUE(nearlySameRotation(catmullRom(q0, q1, q2, q3, 0.0F), q1));
    EXPECT_TRUE(nearlySameRotation(catmullRom(q0, q1, q2, q3, 1.0F), q2));
    for (float t = 0.0F; t <= 1.0F; t += 0.25F)
    {
        const Quat q = catmullRom(q0, q1, q2, q3, t);
        EXPECT_NEAR(std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w), 1.0F, 1e-5F) << "unit length";
        EXPECT_TRUE(nearlySameRotation(q, slerp(q1, q2, t), 2e-3F))
            << "evenly spaced keys about one axis: same as slerp at t = " << t;
    }
    // Keys given with flipped signs (same rotations) still take the short way.
    const Quat flipped{-q2.x, -q2.y, -q2.z, -q2.w};
    EXPECT_NEAR(angleBetween(q1, catmullRom(q0, q1, flipped, q3, 0.5F)), 0.25F, 2e-3F);
}

TEST(Spline, PathConstantSpeed)
{
    const std::vector<Vec3> straight{{0.0F, 0.0F, 0.0F}, {2.0F, 0.0F, 0.0F}, {10.0F, 0.0F, 0.0F}};
    const SplinePath line{straight};
    EXPECT_NEAR(line.length(), 10.0F, 1e-3F) << "a straight path is exactly its length";
    EXPECT_TRUE(nearlyEqual(line.pointAtDistance(5.0F), Vec3{5.0F, 0.0F, 0.0F}, 1e-3F))
        << "uneven point spacing doesn't change the speed: " << std::format("{}", line.pointAtDistance(5.0F));

    const std::vector<Vec3> waypoints{
        {0.0F, 0.0F, 0.0F}, {5.0F, 0.0F, 1.0F}, {6.0F, 3.0F, 8.0F}, {0.0F, 4.0F, 9.0F}, {-2.0F, 0.0F, 3.0F}};
    const SplinePath path{waypoints};
    EXPECT_EQ(path.segmentCount(), 4U);
    EXPECT_TRUE(nearlyEqual(path.pointAtDistance(0.0F), waypoints.front()));
    EXPECT_TRUE(nearlyEqual(path.pointAtDistance(path.length()), waypoints.back(), 1e-4F));
    EXPECT_TRUE(nearlyEqual(path.pointAtDistance(path.length() + 5.0F), waypoints.back(), 1e-4F)) << "clamped";
    EXPECT_TRUE(nearlyEqual(path.pointAt(2.0F), waypoints[2])) << "passes through every waypoint";

    // Equal distance steps → equal spacing (measured as chords of small steps).
    constexpr int c_steps = 200;
    const float stepLength = path.length() / c_steps;
    Vec3 previous = path.pointAtDistance(0.0F);
    for (int i = 1; i <= c_steps; ++i)
    {
        const Vec3 current = path.pointAtDistance(stepLength * static_cast<float>(i));
        ASSERT_NEAR(distanceBetween(previous, current), stepLength, stepLength * 0.002F)
            << "step " << i << " of " << distanceBetween(previous, current);
        previous = current;
    }

    const Vec3 tangent = path.tangentAtDistance(0.0F);
    EXPECT_NEAR(std::sqrt(tangent.x * tangent.x + tangent.y * tangent.y + tangent.z * tangent.z), 1.0F, 1e-5F);
    EXPECT_GT(tangent.x, 0.0F) << "leaves towards the second point";
}
