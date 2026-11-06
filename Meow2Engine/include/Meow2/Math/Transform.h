#pragma once

/// @file
/// @brief `Transform`: position, rotation and scale (TRS), the placement of every entity, camera and bone.
///
/// A point is **scaled, then rotated, then moved**: `world = rotate(rotation, local × scale) + position`. The matrix
/// form is `S × R × T` (row vectors, techstack.md §10).
/// @code
/// using namespace Meow2::Math;
/// Transform door{.position = {2.0F, 0.0F, 5.0F}, .rotation = fromAxisAngle(c_up, toRadians(30.0F))};
/// const Vec3 handle = transformPoint(door, Vec3{0.9F, 1.0F, 0.0F});         // local → world
/// const Transform shown = lerp(previous, current, alpha);                     // render(alpha) interpolation
/// const DirectX::XMMATRIX world = toMatrix(shown);                            // for the renderer
/// @endcode
/// @see techstack.md §10, §23, docs/wiki/Math.md

#include <Meow2/Math/Quaternion.h>
#include <Meow2/Math/Types.h>

#include <algorithm>
#include <cmath>
#include <format>
#include <optional>
#include <string_view>

namespace Meow2::Math
{
/// @brief A position in the world.
///
/// One type for every world position, so that larger worlds can later switch it to doubles or add a floating origin
/// in one place instead of every system (techstack.md §10, large worlds). Today it's a `Vec3`: 1 mm precision 10 km
/// from the origin.
using WorldPosition = Vec3;

/// @brief Position, rotation and scale. Plain data; the identity by default.
///
/// @warning A transform can't represent shear. Combining a rotated child with a parent that scales non-uniformly
///          (e.g. (2, 1, 1)) would shear the child in reality; combine() and inverse() are exact only when the parent's
///          scale is uniform. Use matrices (toMatrix()) where exactness matters for non-uniform scales.
/// @par Thread safety
/// Plain value; any thread may read a value nobody writes.
struct Transform
{
    WorldPosition position;       ///< Where the origin of the local space ends up.
    Quat rotation;                ///< Orientation (applied after scale).
    Vec3 scale{1.0F, 1.0F, 1.0F}; ///< Scale per local axis (applied first). Negative values mirror.
};

/// @brief The transform as a matrix `S × R × T` (row vectors: `point × matrix`).
/// @param transform The transform.
/// @return The matrix in registers.
[[nodiscard]] inline DirectX::XMMATRIX toMatrix(const Transform& transform) noexcept
{
    using namespace DirectX;
    return XMMatrixScalingFromVector(load(transform.scale)) * XMMatrixRotationQuaternion(load(transform.rotation)) *
           XMMatrixTranslationFromVector(load(transform.position));
}

/// @brief The transform as a stored 4×4 matrix.
/// @param transform The transform.
/// @return The matrix.
[[nodiscard]] inline Mat4 toMat4(const Transform& transform) noexcept
{
    return storeMat4(toMatrix(transform));
}

/// @brief The transform as a stored 3×4 matrix (48 bytes; for GPU data).
/// @param transform The transform.
/// @return The matrix.
[[nodiscard]] inline Mat3x4 toMat3x4(const Transform& transform) noexcept
{
    return storeMat3x4(toMatrix(transform));
}

/// @brief Splits an affine matrix back into position, rotation and scale.
/// @param matrix A matrix made of scale, rotation and translation (no shear, no projection).
/// @return The transform, or `std::nullopt` if the matrix can't be decomposed (a zero scale). A mirrored matrix gives
///         a negative scale on one axis, not necessarily the axis it was built with.
[[nodiscard]] inline std::optional<Transform> fromMatrix(DirectX::FXMMATRIX matrix) noexcept
{
    // XMMatrixDecompose "succeeds" on a flattened matrix by inventing the missing axes, so reject those here: an affine
    // matrix with a zero scale has a zero determinant.
    if (std::abs(DirectX::XMVectorGetX(DirectX::XMMatrixDeterminant(matrix))) < 1e-12F)
    {
        return std::nullopt;
    }
    DirectX::XMVECTOR scale;
    DirectX::XMVECTOR rotation;
    DirectX::XMVECTOR translation;
    if (!DirectX::XMMatrixDecompose(&scale, &rotation, &translation, matrix))
    {
        return std::nullopt;
    }
    return Transform{storeVec3(translation), normalize(storeQuat(rotation)), storeVec3(scale)};
}

/// @brief Moves a point from the transform's local space to its parent's space: scale, rotate, then translate.
/// @param transform The transform.
/// @param point     The point in local space.
/// @return The point in the parent's (or world) space.
[[nodiscard]] inline Vec3 transformPoint(const Transform& transform, const Vec3& point) noexcept
{
    using namespace DirectX;
    const XMVECTOR scaled = XMVectorMultiply(load(point), load(transform.scale));
    return storeVec3(XMVectorAdd(XMVector3Rotate(scaled, load(transform.rotation)), load(transform.position)));
}

/// @brief Rotates a direction by the transform's rotation only (no scale, no translation), so a unit direction stays
///        unit length.
/// @param transform The transform.
/// @param direction The direction in local space.
/// @return The direction in the parent's space.
[[nodiscard]] inline Vec3 transformDirection(const Transform& transform, const Vec3& direction) noexcept
{
    return rotate(transform.rotation, direction);
}

/// @brief Moves a point from the parent's space into the transform's local space (the exact inverse of
///        transformPoint(), also for non-uniform scale).
/// @param transform The transform; no scale component may be zero.
/// @param point     The point in the parent's space.
/// @return The point in local space.
[[nodiscard]] inline Vec3 inverseTransformPoint(const Transform& transform, const Vec3& point) noexcept
{
    using namespace DirectX;
    const XMVECTOR local =
        XMVector3InverseRotate(XMVectorSubtract(load(point), load(transform.position)), load(transform.rotation));
    return storeVec3(XMVectorDivide(local, load(transform.scale)));
}

/// @brief The transform of `child` placed inside `parent`: what you get by applying `child`, then `parent`.
///
/// Used for hierarchies (a wheel's transform relative to its car → the wheel in the world).
/// @param child  The local transform, relative to the parent.
/// @param parent The parent's transform.
/// @return The combined transform. Exact when the parent's scale is uniform (see Transform's warning).
[[nodiscard]] inline Transform combine(const Transform& child, const Transform& parent) noexcept
{
    using namespace DirectX;
    return Transform{transformPoint(parent, child.position), normalize(combine(child.rotation, parent.rotation)),
                     storeVec3(XMVectorMultiply(load(child.scale), load(parent.scale)))};
}

/// @brief The transform that undoes `transform`: combine(transform, inverse(transform)) is the identity.
/// @param transform The transform; no scale component may be zero.
/// @return The inverse. Exact for uniform scale; with non-uniform scale use inverseTransformPoint() or the inverse
///         matrix instead.
[[nodiscard]] inline Transform inverse(const Transform& transform) noexcept
{
    using namespace DirectX;
    const Quat inverseRotation = inverse(transform.rotation);
    const XMVECTOR inverseScale = XMVectorReciprocal(load(transform.scale));
    const XMVECTOR position = XMVectorNegate(
        XMVectorMultiply(XMVector3Rotate(load(transform.position), load(inverseRotation)), inverseScale));
    return Transform{storeVec3(position), inverseRotation, storeVec3(inverseScale)};
}

/// @brief Interpolates two transforms: position and scale linearly, rotation by slerp.
///
/// This is what `render(alpha)` uses to draw between the previous and the current fixed step (techstack.md §23).
/// @param from Transform at t = 0.
/// @param to   Transform at t = 1.
/// @param t    Position between them, 0..1.
/// @return The interpolated transform.
[[nodiscard]] inline Transform lerp(const Transform& from, const Transform& to, float t) noexcept
{
    using namespace DirectX;
    return Transform{storeVec3(XMVectorLerp(load(from.position), load(to.position), t)),
                     slerp(from.rotation, to.rotation, t),
                     storeVec3(XMVectorLerp(load(from.scale), load(to.scale), t))};
}

/// @brief The direction the transform faces: local +Z in the parent's space.
/// @param transform The transform.
/// @return A unit vector.
[[nodiscard]] inline Vec3 forward(const Transform& transform) noexcept
{
    return rotate(transform.rotation, c_forward);
}

/// @brief The transform's right: local +X in the parent's space.
/// @param transform The transform.
/// @return A unit vector.
[[nodiscard]] inline Vec3 right(const Transform& transform) noexcept
{
    return rotate(transform.rotation, c_right);
}

/// @brief The transform's up: local +Y in the parent's space.
/// @param transform The transform.
/// @return A unit vector.
[[nodiscard]] inline Vec3 up(const Transform& transform) noexcept
{
    return rotate(transform.rotation, c_up);
}

/// @brief Whether two transforms are nearly the same (rotations compared as rotations, so q and −q are equal).
/// @param a         A transform.
/// @param b         Another transform.
/// @param tolerance Tolerance for position and scale (nearlyEqual()) and, in radians, for the rotation.
/// @return `true` if they're equal within it.
[[nodiscard]] inline bool nearlyEqual(const Transform& a, const Transform& b,
                                      float tolerance = c_defaultTolerance) noexcept
{
    return nearlyEqual(a.position, b.position, tolerance) && nearlyEqual(a.scale, b.scale, tolerance) &&
           nearlySameRotation(a.rotation, b.rotation, std::max(tolerance, 1e-3F));
}
} // namespace Meow2::Math

/// @cond
template <>
struct std::formatter<Meow2::Math::Transform> : std::formatter<std::string_view>
{
    auto format(const Meow2::Math::Transform& t, std::format_context& context) const
    {
        return std::format_to(context.out(), "{{position {}, rotation {}, scale {}}}", t.position, t.rotation, t.scale);
    }
};
/// @endcond
