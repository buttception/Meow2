/// @file
/// @brief Tests for Transform: identity, matrix round trip, the scale-rotate-translate order, combining, inverting,
///        interpolation and the direction helpers.

#include "MathPrinters.h"

#include <Meow2/Math/Transform.h>

#include <gtest/gtest.h>

#include <format>

using namespace Meow2::Math;

namespace
{
/// @brief A transform with every part non-trivial (uniform scale, so combine/inverse are exact).
Transform someTransform()
{
    return Transform{{3.0F, -2.0F, 7.0F}, fromEuler({0.3F, 1.2F, -0.5F}), {2.0F, 2.0F, 2.0F}};
}

Vec3 throughMatrix(const Transform& transform, const Vec3& point)
{
    return storeVec3(DirectX::XMVector3TransformCoord(load(point), toMatrix(transform)));
}
} // namespace

TEST(Transform, DefaultIsIdentity)
{
    const Transform identity;
    EXPECT_TRUE(nearlyEqual(toMat4(identity), Mat4{}));
    EXPECT_TRUE(nearlyEqual(transformPoint(identity, Vec3{1.0F, 2.0F, 3.0F}), Vec3{1.0F, 2.0F, 3.0F}));
    EXPECT_EQ(std::format("{}", identity), "{position (0, 0, 0), rotation quat(0, 0, 0, 1), scale (1, 1, 1)}");
}

TEST(Transform, MatrixRoundTrip)
{
    const Transform original{{1.0F, 2.0F, 3.0F}, fromEuler({-0.4F, 2.0F, 0.9F}), {0.5F, 3.0F, 1.5F}};
    const std::optional<Transform> back = fromMatrix(toMatrix(original));
    ASSERT_TRUE(back.has_value());
    EXPECT_TRUE(nearlyEqual(*back, original, 1e-4F)) << std::format("{} vs {}", *back, original);

    const Vec3 point{0.3F, -1.0F, 2.0F};
    EXPECT_TRUE(nearlyEqual(transformPoint(original, point), throughMatrix(original, point), 1e-5F))
        << "transformPoint and the matrix agree";
    EXPECT_TRUE(nearlyEqual(toMat3x4(original).m[0][3], toMat4(original).m[3][0]))
        << "Mat3x4 holds the affine part transposed: its first row ends with the x translation";
    EXPECT_FALSE(fromMatrix(DirectX::XMMatrixScaling(0.0F, 1.0F, 1.0F)).has_value()) << "zero scale can't decompose";
}

TEST(Transform, ScaleRotateTranslateOrder)
{
    const Transform t{{10.0F, 0.0F, 0.0F}, fromAxisAngle(c_up, c_halfPi), {2.0F, 1.0F, 1.0F}};
    // (1, 0, 0) → scaled (2, 0, 0) → quarter turn about up: right becomes back (0, 0, −2) → moved (10, 0, −2).
    EXPECT_TRUE(nearlyEqual(transformPoint(t, c_right), Vec3{10.0F, 0.0F, -2.0F}, 1e-5F))
        << std::format("{}", transformPoint(t, c_right));
}

TEST(Transform, CombineMatchesMatrices)
{
    const Transform parent = someTransform();
    const Transform child{{0.0F, 1.0F, -4.0F}, fromAxisAngle(c_right, 0.7F), {1.0F, 0.5F, 3.0F}};
    const Transform world = combine(child, parent);
    const Vec3 point{1.0F, 1.0F, 1.0F};
    EXPECT_TRUE(nearlyEqual(transformPoint(world, point), transformPoint(parent, transformPoint(child, point)), 1e-4F))
        << "child first, then parent";
    const Vec3 viaMatrices =
        storeVec3(DirectX::XMVector3TransformCoord(load(point), toMatrix(child) * toMatrix(parent)));
    EXPECT_TRUE(nearlyEqual(transformPoint(world, point), viaMatrices, 1e-4F))
        << "same as child matrix × parent matrix";
}

TEST(Transform, InverseUndoes)
{
    const Transform t = someTransform();
    EXPECT_TRUE(nearlyEqual(combine(t, inverse(t)), Transform{}, 1e-5F));
    EXPECT_TRUE(nearlyEqual(combine(inverse(t), t), Transform{}, 1e-5F));
    const Vec3 point{-5.0F, 0.5F, 9.0F};
    EXPECT_TRUE(nearlyEqual(transformPoint(inverse(t), transformPoint(t, point)), point, 1e-4F));

    const Transform nonUniform{{1.0F, 2.0F, 3.0F}, fromEuler({0.5F, -0.2F, 1.0F}), {2.0F, 0.5F, 4.0F}};
    EXPECT_TRUE(nearlyEqual(inverseTransformPoint(nonUniform, transformPoint(nonUniform, point)), point, 1e-4F))
        << "inverseTransformPoint is exact for non-uniform scale too";
}

TEST(Transform, LerpForRenderAlpha)
{
    const Transform from{{0.0F, 0.0F, 0.0F}, Quat{}, {1.0F, 1.0F, 1.0F}};
    const Transform to{{4.0F, 0.0F, 2.0F}, fromAxisAngle(c_up, 1.0F), {3.0F, 3.0F, 3.0F}};
    EXPECT_TRUE(nearlyEqual(lerp(from, to, 0.0F), from));
    EXPECT_TRUE(nearlyEqual(lerp(from, to, 1.0F), to));
    const Transform half = lerp(from, to, 0.5F);
    EXPECT_TRUE(nearlyEqual(half.position, Vec3{2.0F, 0.0F, 1.0F}));
    EXPECT_TRUE(nearlyEqual(half.scale, Vec3{2.0F, 2.0F, 2.0F}));
    EXPECT_NEAR(angleBetween(from.rotation, half.rotation), 0.5F, 1e-4F) << "rotation slerped half way";
}

TEST(Transform, Directions)
{
    const Transform t{{5.0F, 5.0F, 5.0F}, fromAxisAngle(c_up, c_halfPi), {3.0F, 3.0F, 3.0F}};
    EXPECT_TRUE(nearlyEqual(forward(t), c_right));
    EXPECT_TRUE(nearlyEqual(right(t), Vec3{0.0F, 0.0F, -1.0F}));
    EXPECT_TRUE(nearlyEqual(up(t), c_up));
    EXPECT_TRUE(nearlyEqual(transformDirection(t, c_forward), c_right)) << "no scale, no translation";
}
