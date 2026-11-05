#pragma once

/// @file
/// @brief The engine's math vocabulary: storage types, the bridge to SIMD registers, the coordinate-system constants,
///        tolerant comparisons and readable formatting.
///
/// **Store compact, compute in registers** (DirectXMath's model, techstack.md §10):
/// - Components, files and arrays hold the storage types here: `Vec3` is 3 floats (12 bytes), `Quat` 4, `Mat4` 16.
/// - Maths runs on `DirectX::XMVECTOR` / `XMMATRIX`, which live in SIMD registers. load() brings a value in;
///   storeVec3(), storeQuat(), … write the result back.
/// @code
/// using namespace Meow2::Math;
/// Vec3 position{1.0F, 2.0F, 3.0F};
/// const DirectX::XMVECTOR moved = DirectX::XMVectorAdd(load(position), load(c_forward));
/// position = storeVec3(moved);                  // (1, 2, 4)
/// MEOW2_LOG_INFO(log, "Player at {}", position); // "Player at (1, 2, 4)"
/// @endcode
/// **Coordinates:** left-handed, +X right, +Y up, +Z forward (c_right, c_up, c_forward).
/// @see techstack.md §10, docs/wiki/Math.md

#include <DirectXMath.h>

#include <algorithm>
#include <cmath>
#include <format>
#include <numbers>
#include <string_view>

namespace Meow2::Math
{
/// @brief Two floats (8 bytes): screen positions, texture coordinates. Zero by default.
/// @par Thread safety
/// Plain value; any thread may read a value nobody writes.
struct Vec2 : DirectX::XMFLOAT2
{
    /// @brief (0, 0).
    constexpr Vec2() noexcept
        : XMFLOAT2(0.0F, 0.0F)
    {
    }

    /// @brief (x, y).
    /// @param xValue X component.
    /// @param yValue Y component.
    constexpr Vec2(float xValue, float yValue) noexcept
        : XMFLOAT2(xValue, yValue)
    {
    }
};

/// @brief Three floats (12 bytes): positions, directions, scales, Euler angles. Zero by default.
/// @par Thread safety
/// Plain value; any thread may read a value nobody writes.
struct Vec3 : DirectX::XMFLOAT3
{
    /// @brief (0, 0, 0).
    constexpr Vec3() noexcept
        : XMFLOAT3(0.0F, 0.0F, 0.0F)
    {
    }

    /// @brief (x, y, z).
    /// @param xValue X component (right).
    /// @param yValue Y component (up).
    /// @param zValue Z component (forward).
    constexpr Vec3(float xValue, float yValue, float zValue) noexcept
        : XMFLOAT3(xValue, yValue, zValue)
    {
    }
};

/// @brief Four floats (16 bytes): homogeneous points, colours, or a Vec3 plus a value (a sphere's radius). Zero by
///        default. Rotations are Quat, not Vec4.
/// @par Thread safety
/// Plain value; any thread may read a value nobody writes.
struct Vec4 : DirectX::XMFLOAT4
{
    /// @brief (0, 0, 0, 0).
    constexpr Vec4() noexcept
        : XMFLOAT4(0.0F, 0.0F, 0.0F, 0.0F)
    {
    }

    /// @brief (x, y, z, w).
    /// @param xValue X component.
    /// @param yValue Y component.
    /// @param zValue Z component.
    /// @param wValue W component.
    constexpr Vec4(float xValue, float yValue, float zValue, float wValue) noexcept
        : XMFLOAT4(xValue, yValue, zValue, wValue)
    {
    }
};

/// @brief A rotation as a unit quaternion (x, y, z, w), w being the real part. The identity (no rotation) by
///        default.
///
/// Its own type, not a Vec4, so a rotation can't be passed where a vector is expected; it still loads into an
/// `XMVECTOR` for DirectXMath's quaternion functions (`XMQuaternionMultiply`, `XMQuaternionSlerp`, …).
/// @par Thread safety
/// Plain value; any thread may read a value nobody writes.
struct Quat : DirectX::XMFLOAT4
{
    /// @brief The identity rotation (0, 0, 0, 1).
    constexpr Quat() noexcept
        : XMFLOAT4(0.0F, 0.0F, 0.0F, 1.0F)
    {
    }

    /// @brief A quaternion from its components; not normalised here.
    /// @param xValue Imaginary i component.
    /// @param yValue Imaginary j component.
    /// @param zValue Imaginary k component.
    /// @param wValue Real component.
    constexpr Quat(float xValue, float yValue, float zValue, float wValue) noexcept
        : XMFLOAT4(xValue, yValue, zValue, wValue)
    {
    }
};

/// @brief A 4×4 matrix (64 bytes), row-major with row vectors (DirectXMath's convention: `v' = v × M`). The identity
///        by default.
/// @par Thread safety
/// Plain value; any thread may read a value nobody writes.
struct Mat4 : DirectX::XMFLOAT4X4
{
    /// @brief The identity matrix.
    constexpr Mat4() noexcept
        : XMFLOAT4X4(1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F)
    {
    }

    /// @brief A matrix from its 16 elements, row by row.
    /// @param m00 Row 0, column 0. @param m01 Row 0, column 1. @param m02 Row 0, column 2. @param m03 Row 0, column 3.
    /// @param m10 Row 1, column 0. @param m11 Row 1, column 1. @param m12 Row 1, column 2. @param m13 Row 1, column 3.
    /// @param m20 Row 2, column 0. @param m21 Row 2, column 1. @param m22 Row 2, column 2. @param m23 Row 2, column 3.
    /// @param m30 Row 3, column 0. @param m31 Row 3, column 1. @param m32 Row 3, column 2. @param m33 Row 3, column 3.
    constexpr Mat4(float m00, float m01, float m02, float m03, float m10, float m11, float m12, float m13, float m20,
                   float m21, float m22, float m23, float m30, float m31, float m32, float m33) noexcept
        : XMFLOAT4X4(m00, m01, m02, m03, m10, m11, m12, m13, m20, m21, m22, m23, m30, m31, m32, m33)
    {
    }
};

/// @brief An affine transform as a 3×4 matrix (48 bytes): a Mat4 without its constant last column (0, 0, 0, 1), as
///        stored for world transforms and sent to the GPU. Stored transposed (3 rows of 4), like `XMFLOAT3X4`. The
///        identity by default.
/// @par Thread safety
/// Plain value; any thread may read a value nobody writes.
struct Mat3x4 : DirectX::XMFLOAT3X4
{
    /// @brief The identity transform.
    constexpr Mat3x4() noexcept
        : XMFLOAT3X4(1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F)
    {
    }
};

/// @brief π.
inline constexpr float c_pi = std::numbers::pi_v<float>;

/// @brief 2π: a full turn in radians.
inline constexpr float c_twoPi = 2.0F * c_pi;

/// @brief π/2: a quarter turn in radians.
inline constexpr float c_halfPi = 0.5F * c_pi;

/// @brief +X, the direction to the right (left-handed coordinates).
inline constexpr Vec3 c_right{1.0F, 0.0F, 0.0F};

/// @brief +Y, up.
inline constexpr Vec3 c_up{0.0F, 1.0F, 0.0F};

/// @brief +Z, forward: into the screen for a camera with no rotation.
inline constexpr Vec3 c_forward{0.0F, 0.0F, 1.0F};

/// @brief Default tolerance of nearlyEqual(): about 100 float steps around 1.
inline constexpr float c_defaultTolerance = 1e-5F;

/// @brief Loads a Vec2 into a register (z and w are 0).
/// @param value The value.
/// @return It as an XMVECTOR.
[[nodiscard]] inline DirectX::XMVECTOR load(const Vec2& value) noexcept
{
    return DirectX::XMLoadFloat2(&value);
}

/// @brief Loads a Vec3 into a register (w is 0).
/// @param value The value.
/// @return It as an XMVECTOR.
[[nodiscard]] inline DirectX::XMVECTOR load(const Vec3& value) noexcept
{
    return DirectX::XMLoadFloat3(&value);
}

/// @brief Loads a Vec4 into a register.
/// @param value The value.
/// @return It as an XMVECTOR.
[[nodiscard]] inline DirectX::XMVECTOR load(const Vec4& value) noexcept
{
    return DirectX::XMLoadFloat4(&value);
}

/// @brief Loads a Quat into a register, for DirectXMath's `XMQuaternion*` functions.
/// @param value The value.
/// @return It as an XMVECTOR (x, y, z, w).
[[nodiscard]] inline DirectX::XMVECTOR load(const Quat& value) noexcept
{
    return DirectX::XMLoadFloat4(&value);
}

/// @brief Loads a Mat4 into registers.
/// @param value The value.
/// @return It as an XMMATRIX.
[[nodiscard]] inline DirectX::XMMATRIX load(const Mat4& value) noexcept
{
    return DirectX::XMLoadFloat4x4(&value);
}

/// @brief Loads a Mat3x4 into registers, adding the constant last column (0, 0, 0, 1).
/// @param value The value.
/// @return It as an XMMATRIX.
[[nodiscard]] inline DirectX::XMMATRIX load(const Mat3x4& value) noexcept
{
    return DirectX::XMLoadFloat3x4(&value);
}

/// @brief Stores a register's x and y.
/// @param value The register.
/// @return A Vec2.
[[nodiscard]] inline Vec2 storeVec2(DirectX::FXMVECTOR value) noexcept
{
    Vec2 result;
    DirectX::XMStoreFloat2(&result, value);
    return result;
}

/// @brief Stores a register's x, y and z.
/// @param value The register.
/// @return A Vec3.
[[nodiscard]] inline Vec3 storeVec3(DirectX::FXMVECTOR value) noexcept
{
    Vec3 result;
    DirectX::XMStoreFloat3(&result, value);
    return result;
}

/// @brief Stores all four components of a register.
/// @param value The register.
/// @return A Vec4.
[[nodiscard]] inline Vec4 storeVec4(DirectX::FXMVECTOR value) noexcept
{
    Vec4 result;
    DirectX::XMStoreFloat4(&result, value);
    return result;
}

/// @brief Stores a register holding a quaternion.
/// @param value The register (x, y, z, w).
/// @return A Quat.
[[nodiscard]] inline Quat storeQuat(DirectX::FXMVECTOR value) noexcept
{
    Quat result;
    DirectX::XMStoreFloat4(&result, value);
    return result;
}

/// @brief Stores a matrix.
/// @param value The matrix.
/// @return A Mat4.
[[nodiscard]] inline Mat4 storeMat4(DirectX::FXMMATRIX value) noexcept
{
    Mat4 result;
    DirectX::XMStoreFloat4x4(&result, value);
    return result;
}

/// @brief Stores an affine matrix, dropping its last column (which must be (0, 0, 0, 1)).
/// @param value The matrix.
/// @return A Mat3x4.
[[nodiscard]] inline Mat3x4 storeMat3x4(DirectX::FXMMATRIX value) noexcept
{
    Mat3x4 result;
    DirectX::XMStoreFloat3x4(&result, value);
    return result;
}

/// @brief Whether two floats are equal within a tolerance that grows with their size.
///
/// Uses `|a - b| <= tolerance × max(1, |a|, |b|)`: an absolute tolerance near zero (where relative error is
/// meaningless) and a relative one for large values (where float steps are larger).
/// @param a         A value.
/// @param b         Another value.
/// @param tolerance The tolerance; c_defaultTolerance by default.
/// @return `true` if they're equal within it.
[[nodiscard]] inline bool nearlyEqual(float a, float b, float tolerance = c_defaultTolerance) noexcept
{
    return std::abs(a - b) <= tolerance * std::max({1.0F, std::abs(a), std::abs(b)});
}

/// @brief Component-wise nearlyEqual() for Vec2.
/// @param a A value. @param b Another value. @param tolerance The tolerance per component.
/// @return `true` if every component is equal within it.
[[nodiscard]] inline bool nearlyEqual(const Vec2& a, const Vec2& b, float tolerance = c_defaultTolerance) noexcept
{
    return nearlyEqual(a.x, b.x, tolerance) && nearlyEqual(a.y, b.y, tolerance);
}

/// @brief Component-wise nearlyEqual() for Vec3.
/// @param a A value. @param b Another value. @param tolerance The tolerance per component.
/// @return `true` if every component is equal within it.
[[nodiscard]] inline bool nearlyEqual(const Vec3& a, const Vec3& b, float tolerance = c_defaultTolerance) noexcept
{
    return nearlyEqual(a.x, b.x, tolerance) && nearlyEqual(a.y, b.y, tolerance) && nearlyEqual(a.z, b.z, tolerance);
}

/// @brief Component-wise nearlyEqual() for Vec4.
/// @param a A value. @param b Another value. @param tolerance The tolerance per component.
/// @return `true` if every component is equal within it.
[[nodiscard]] inline bool nearlyEqual(const Vec4& a, const Vec4& b, float tolerance = c_defaultTolerance) noexcept
{
    return nearlyEqual(a.x, b.x, tolerance) && nearlyEqual(a.y, b.y, tolerance) && nearlyEqual(a.z, b.z, tolerance) &&
           nearlyEqual(a.w, b.w, tolerance);
}

/// @brief Component-wise nearlyEqual() for Quat.
/// @note q and −q are the same rotation but compare as different here; compare rotations by angle when that matters.
/// @param a A value. @param b Another value. @param tolerance The tolerance per component.
/// @return `true` if every component is equal within it.
[[nodiscard]] inline bool nearlyEqual(const Quat& a, const Quat& b, float tolerance = c_defaultTolerance) noexcept
{
    return nearlyEqual(a.x, b.x, tolerance) && nearlyEqual(a.y, b.y, tolerance) && nearlyEqual(a.z, b.z, tolerance) &&
           nearlyEqual(a.w, b.w, tolerance);
}

/// @brief Element-wise nearlyEqual() for Mat4.
/// @param a A value. @param b Another value. @param tolerance The tolerance per element.
/// @return `true` if all 16 elements are equal within it.
[[nodiscard]] inline bool nearlyEqual(const Mat4& a, const Mat4& b, float tolerance = c_defaultTolerance) noexcept
{
    for (int row = 0; row < 4; ++row)
    {
        for (int column = 0; column < 4; ++column)
        {
            if (!nearlyEqual(a.m[row][column], b.m[row][column], tolerance))
            {
                return false;
            }
        }
    }
    return true;
}
} // namespace Meow2::Math

// Doxygen can't place specializations of std templates (it has no model of namespace std), so these are hidden from
// the API reference; the file comment says the types format as "(x, y, z)".
/// @cond
template <>
struct std::formatter<Meow2::Math::Vec2> : std::formatter<std::string_view>
{
    auto format(const Meow2::Math::Vec2& v, std::format_context& context) const
    {
        return std::format_to(context.out(), "({}, {})", v.x, v.y);
    }
};

template <>
struct std::formatter<Meow2::Math::Vec3> : std::formatter<std::string_view>
{
    auto format(const Meow2::Math::Vec3& v, std::format_context& context) const
    {
        return std::format_to(context.out(), "({}, {}, {})", v.x, v.y, v.z);
    }
};

template <>
struct std::formatter<Meow2::Math::Vec4> : std::formatter<std::string_view>
{
    auto format(const Meow2::Math::Vec4& v, std::format_context& context) const
    {
        return std::format_to(context.out(), "({}, {}, {}, {})", v.x, v.y, v.z, v.w);
    }
};

template <>
struct std::formatter<Meow2::Math::Quat> : std::formatter<std::string_view>
{
    auto format(const Meow2::Math::Quat& q, std::format_context& context) const
    {
        return std::format_to(context.out(), "quat({}, {}, {}, {})", q.x, q.y, q.z, q.w);
    }
};

template <>
struct std::formatter<Meow2::Math::Mat4> : std::formatter<std::string_view>
{
    auto format(const Meow2::Math::Mat4& m, std::format_context& context) const
    {
        auto out = context.out();
        for (int row = 0; row < 4; ++row)
        {
            out = std::format_to(out, "{}({}, {}, {}, {})", row == 0 ? "[" : ", ", m.m[row][0], m.m[row][1],
                                 m.m[row][2], m.m[row][3]);
        }
        return std::format_to(out, "]");
    }
};
/// @endcond
