/// @file
/// @brief Tests for the quaternion helpers: axis-angle, Euler order and round trip, look/from-to rotations, combining,
///        slerp and rotation comparison.

#include "MathPrinters.h"

#include <Meow2/Math/Quaternion.h>
#include <Meow2/Math/Random.h>

#include <gtest/gtest.h>

#include <format>

using namespace Meow2::Math;

TEST(Quaternion, AxisAngle)
{
    const Quat quarter = fromAxisAngle(c_up, c_halfPi);
    EXPECT_TRUE(nearlyEqual(rotate(quarter, c_forward), c_right))
        << "left-handed: a positive turn about up turns forward to right";
    EXPECT_TRUE(nearlyEqual(rotate(fromAxisAngle(c_right, c_halfPi), c_forward), Vec3{0.0F, -1.0F, 0.0F}))
        << "positive pitch tilts forward down";

    const AxisAngle back = toAxisAngle(fromAxisAngle(Vec3{0.0F, 2.0F, 0.0F}, 1.2F));
    EXPECT_TRUE(nearlyEqual(back.axis, c_up)) << "the axis is normalised";
    EXPECT_TRUE(nearlyEqual(back.angle, 1.2F));
    const AxisAngle shortWay = toAxisAngle(fromAxisAngle(c_up, 1.5F * c_pi));
    EXPECT_TRUE(nearlyEqual(shortWay.angle, 0.5F * c_pi, 1e-4F)) << "270° one way is 90° the other";
    EXPECT_TRUE(nearlyEqual(shortWay.axis, Vec3{0.0F, -1.0F, 0.0F}, 1e-4F));
    EXPECT_EQ(toAxisAngle(Quat{}).angle, 0.0F);
}

TEST(Quaternion, EulerOrderAndRoundTrip)
{
    EXPECT_TRUE(nearlyEqual(rotate(fromEuler({0.0F, c_halfPi, 0.0F}), c_forward), c_right)) << "yaw about Y";
    EXPECT_TRUE(nearlyEqual(rotate(fromEuler({c_halfPi, 0.0F, 0.0F}), c_forward), Vec3{0.0F, -1.0F, 0.0F}))
        << "pitch about X";
    EXPECT_TRUE(nearlyEqual(rotate(fromEuler({0.0F, 0.0F, c_halfPi}), c_right), c_up))
        << "roll about Z turns right towards up";

    // Order: roll first, then pitch, then yaw.
    const Vec3 angles{0.3F, -1.1F, 0.7F};
    const Quat composed = combine(combine(fromAxisAngle(c_forward, angles.z), fromAxisAngle(c_right, angles.x)),
                                  fromAxisAngle(c_up, angles.y));
    EXPECT_TRUE(nearlySameRotation(fromEuler(angles), composed));

    for (float pitch = -1.5F; pitch <= 1.5F; pitch += 0.25F)
    {
        for (float yaw = -3.0F; yaw <= 3.0F; yaw += 0.5F)
        {
            for (float roll = -3.0F; roll <= 3.0F; roll += 0.75F)
            {
                const Vec3 original{pitch, yaw, roll};
                const Vec3 back = toEuler(fromEuler(original));
                ASSERT_TRUE(nearlyEqual(back, original, 1e-3F)) << std::format("{} → {}", original, back);
            }
        }
    }

    // Gimbal lock: looking straight down, yaw and roll turn about the same axis; the rotation must survive.
    const Quat lookingDown = fromEuler({c_halfPi, 0.4F, 0.3F});
    const Vec3 locked = toEuler(lookingDown);
    EXPECT_EQ(locked.z, 0.0F) << "roll folded into yaw";
    EXPECT_TRUE(nearlySameRotation(fromEuler(locked), lookingDown, 2e-3F)) << std::format("{}", locked);
}

TEST(Quaternion, LookAndFromTo)
{
    const Vec3 target{3.0F, 2.0F, -4.0F};
    const Quat look = lookRotation(target);
    const Vec3 forward = rotate(look, c_forward);
    const float length = std::sqrt(29.0F);
    EXPECT_TRUE(nearlyEqual(forward, Vec3{3.0F / length, 2.0F / length, -4.0F / length}, 1e-5F));
    EXPECT_NEAR(rotate(look, c_right).y, 0.0F, 1e-5F) << "no roll: right stays horizontal";
    EXPECT_GT(rotate(look, c_up).y, 0.0F) << "up stays up";
    EXPECT_TRUE(nearlySameRotation(lookRotation(c_forward), Quat{})) << "looking forward is no rotation";

    const Quat straightDown = lookRotation(Vec3{0.0F, -1.0F, 0.0F});
    EXPECT_TRUE(nearlyEqual(rotate(straightDown, c_forward), Vec3{0.0F, -1.0F, 0.0F}, 1e-5F))
        << "forward parallel to up still works";

    const Vec3 from{1.0F, 0.0F, 0.0F};
    const Vec3 to{0.0F, 0.6F, 0.8F};
    EXPECT_TRUE(nearlyEqual(rotate(fromToRotation(from, to), from), to, 1e-5F));
    EXPECT_TRUE(nearlyEqual(rotate(fromToRotation(c_forward, Vec3{0.0F, 0.0F, -1.0F}), c_forward),
                            Vec3{0.0F, 0.0F, -1.0F}, 1e-5F))
        << "opposite directions: a half turn";
    EXPECT_TRUE(nearlySameRotation(fromToRotation(c_up, c_up), Quat{}));
}

TEST(Quaternion, CombineAndInverse)
{
    const Quat yaw = fromAxisAngle(c_up, c_halfPi);
    const Quat pitch = fromAxisAngle(c_right, c_halfPi);
    // Pitch first: forward → down; then yaw about up leaves down as down.
    EXPECT_TRUE(nearlyEqual(rotate(combine(pitch, yaw), c_forward), Vec3{0.0F, -1.0F, 0.0F}));
    // Yaw first: forward → right; then pitch about +X leaves right as right.
    EXPECT_TRUE(nearlyEqual(rotate(combine(yaw, pitch), c_forward), c_right));

    const Quat q = fromEuler({0.2F, 0.9F, -0.4F});
    EXPECT_TRUE(nearlySameRotation(combine(q, inverse(q)), Quat{}));
    const Vec3 v{1.0F, 2.0F, 3.0F};
    EXPECT_TRUE(nearlyEqual(rotate(inverse(q), rotate(q, v)), v, 1e-5F));
    EXPECT_TRUE(nearlyEqual(normalize(Quat{0.0F, 0.0F, 0.0F, 2.0F}), Quat{}));
}

TEST(Quaternion, SlerpShortestPath)
{
    const Quat from = fromAxisAngle(c_up, 0.2F);
    const Quat to = fromAxisAngle(c_up, 1.4F);
    EXPECT_TRUE(nearlySameRotation(slerp(from, to, 0.0F), from));
    EXPECT_TRUE(nearlySameRotation(slerp(from, to, 1.0F), to));
    for (float t = 0.0F; t <= 1.0F; t += 0.125F)
    {
        EXPECT_NEAR(angleBetween(from, slerp(from, to, t)), 1.2F * t, 1e-4F) << "constant angular speed at t = " << t;
    }

    // −to is the same rotation; slerp must still take the 1.2 rad way, not the 2π − 1.2 one.
    const Quat negated{-to.x, -to.y, -to.z, -to.w};
    EXPECT_NEAR(angleBetween(from, slerp(from, negated, 0.5F)), 0.6F, 1e-4F);
}

TEST(Quaternion, AngleAndSameRotation)
{
    const Quat a = fromAxisAngle(c_forward, 0.5F);
    const Quat b = fromAxisAngle(c_forward, 0.75F);
    EXPECT_NEAR(angleBetween(a, b), 0.25F, 1e-5F);
    EXPECT_NEAR(angleBetween(a, fromAxisAngle(c_forward, 0.5F + 1e-4F)), 1e-4F, 2e-6F) << "precise for tiny angles";
    const Quat negated{-a.x, -a.y, -a.z, -a.w};
    EXPECT_FALSE(nearlyEqual(a, negated)) << "different numbers…";
    EXPECT_TRUE(nearlySameRotation(a, negated)) << "…same rotation";
    EXPECT_FALSE(nearlySameRotation(a, b));

    Random rng{8U};
    for (int i = 0; i < 100; ++i)
    {
        const Quat q = fromAxisAngle(rng.unitVector(), rng.range(-c_pi, c_pi));
        const Quat r = fromAxisAngle(rng.unitVector(), rng.range(-c_pi, c_pi));
        ASSERT_NEAR(angleBetween(q, r), angleBetween(r, q), 1e-4F) << "symmetric";
    }
}
