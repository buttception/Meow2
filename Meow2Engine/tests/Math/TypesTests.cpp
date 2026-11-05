/// @file
/// @brief Tests for the math vocabulary: layout, load/store, the left-handed constants, nearlyEqual, formatting and
///        that DirectXMath's SIMD path is the one compiled.

#include "MathPrinters.h"

#include <Meow2/Math/Types.h>

#include <gtest/gtest.h>

#include <format>
#include <type_traits>

using namespace Meow2::Math;

TEST(Types, Layout)
{
    // Storage types must stay exactly the size of their floats: they're stored in components, files and GPU data.
    static_assert(sizeof(Vec2) == 8 && sizeof(Vec3) == 12 && sizeof(Vec4) == 16);
    static_assert(sizeof(Quat) == 16 && sizeof(Mat4) == 64 && sizeof(Mat3x4) == 48);
    static_assert(std::is_trivially_copyable_v<Vec3> && std::is_trivially_copyable_v<Mat4>);
    static_assert(!std::is_same_v<Quat, Vec4> && !std::is_convertible_v<Vec4, Quat>,
                  "a rotation is its own type, not a 4-vector");

    EXPECT_TRUE(nearlyEqual(Vec3{}, Vec3{0.0F, 0.0F, 0.0F})) << "vectors default to zero";
    EXPECT_TRUE(nearlyEqual(Quat{}, Quat{0.0F, 0.0F, 0.0F, 1.0F})) << "rotations default to the identity";
    EXPECT_TRUE(nearlyEqual(Mat4{}, storeMat4(DirectX::XMMatrixIdentity()))) << "matrices default to the identity";
}

TEST(LoadStore, RoundTrip)
{
    const Vec2 v2{1.5F, -2.0F};
    const Vec3 v3{1.0F, 2.0F, 3.0F};
    const Vec4 v4{1.0F, 2.0F, 3.0F, 4.0F};
    const Quat q{0.0F, 0.70710677F, 0.0F, 0.70710677F};
    const Mat4 m{1, 2, 3, 0, 4, 5, 6, 0, 7, 8, 9, 0, 10, 11, 12, 1};
    EXPECT_TRUE(nearlyEqual(storeVec2(load(v2)), v2));
    EXPECT_TRUE(nearlyEqual(storeVec3(load(v3)), v3));
    EXPECT_TRUE(nearlyEqual(storeVec4(load(v4)), v4));
    EXPECT_TRUE(nearlyEqual(storeQuat(load(q)), q));
    EXPECT_TRUE(nearlyEqual(storeMat4(load(m)), m));
    EXPECT_TRUE(nearlyEqual(storeMat4(load(storeMat3x4(load(m)))), m)) << "an affine matrix survives a 3×4 trip";
    EXPECT_FLOAT_EQ(DirectX::XMVectorGetW(load(v3)), 0.0F) << "a Vec3 loads with w = 0";
}

TEST(Constants, LeftHanded)
{
    // In a left-handed system, right × up = forward (in a right-handed one it'd point backwards).
    const Vec3 forward = storeVec3(DirectX::XMVector3Cross(load(c_right), load(c_up)));
    EXPECT_TRUE(nearlyEqual(forward, c_forward)) << std::format("right x up = {}", forward);
    EXPECT_FLOAT_EQ(c_pi, 3.14159265F);
    EXPECT_FLOAT_EQ(c_twoPi, 2.0F * c_pi);
    EXPECT_FLOAT_EQ(c_halfPi, c_pi / 2.0F);
}

TEST(NearlyEqual, AbsoluteAndRelative)
{
    EXPECT_TRUE(nearlyEqual(0.0F, 1e-6F)) << "near zero the tolerance is absolute";
    EXPECT_FALSE(nearlyEqual(0.0F, 1e-3F));
    EXPECT_TRUE(nearlyEqual(100000.0F, 100000.5F)) << "for large values it's relative";
    EXPECT_FALSE(nearlyEqual(100000.0F, 100002.0F));
    EXPECT_TRUE(nearlyEqual(1.0F, 1.01F, 0.1F)) << "a custom tolerance";

    EXPECT_FALSE(nearlyEqual(Vec3{1, 2, 3}, Vec3{1, 2, 3.1F})) << "one component off is enough";
    EXPECT_FALSE(nearlyEqual(Quat{}, Quat{0, 0, 0, -1})) << "q and -q compare as different (documented)";
    Mat4 off;
    off.m[3][2] = 0.5F;
    EXPECT_FALSE(nearlyEqual(Mat4{}, off));
}

TEST(Format, ReadableOutput)
{
    EXPECT_EQ(std::format("{}", Vec2{1.0F, -0.5F}), "(1, -0.5)");
    EXPECT_EQ(std::format("{}", Vec3{1.0F, 2.5F, -3.0F}), "(1, 2.5, -3)");
    EXPECT_EQ(std::format("{}", Vec4{1.0F, 2.0F, 3.0F, 4.0F}), "(1, 2, 3, 4)");
    EXPECT_EQ(std::format("{}", Quat{}), "quat(0, 0, 0, 1)");
    EXPECT_EQ(std::format("{}", Mat4{}), "[(1, 0, 0, 0), (0, 1, 0, 0), (0, 0, 1, 0), (0, 0, 0, 1)]");
}

TEST(Simd, IntrinsicsOn)
{
#if defined(_XM_NO_INTRINSICS_)
    FAIL() << "DirectXMath is in its slow scalar mode";
#elif defined(_M_X64) || defined(__x86_64__)
#if defined(_XM_SSE_INTRINSICS_)
    SUCCEED() << "x64 uses SSE intrinsics";
#else
    FAIL() << "x64 without SSE intrinsics";
#endif
#else
    GTEST_SKIP() << "not an x64 build";
#endif
}
