#pragma once

/// @file
/// @brief Rotation helpers on Quat: build rotations from an axis and angle, Euler angles, a look direction or two
///        vectors; combine, invert, apply, compare and interpolate them.
///
/// A unit quaternion (x, y, z, w) stores a rotation of `angle` about a unit `axis` as
/// `(axis × sin(angle/2), cos(angle/2))`. Unlike Euler angles it has no gimbal lock and interpolates smoothly; unlike a
/// matrix it's 16 bytes and stays a pure rotation after many operations (just normalise). `q` and `−q` are the same
/// rotation.
///
/// **Conventions** (left-handed, techstack.md §10): a positive angle about an axis turns clockwise seen from the
/// axis's positive end, looking back at the origin (DirectXMath's rule). So a positive turn about +Y (up) turns forward
/// (+Z) towards right (+X), and a positive pitch (about +X) tilts forward down.
/// @code
/// using namespace Meow2::Math;
/// const Quat turn = fromAxisAngle(c_up, toRadians(90.0F));
/// const Vec3 dir = rotate(turn, c_forward);                         // (1, 0, 0): now facing right
/// const Quat aim = lookRotation(target - eye);                       // +Z towards the target, +Y kept up
/// const Quat blend = slerp(fromRotation, toRotation, t);             // constant angular speed
/// @endcode
/// @see techstack.md §10, docs/wiki/Math.md

#include <Meow2/Core/Assert.h>
#include <Meow2/Math/Scalar.h>
#include <Meow2/Math/Types.h>

#include <cmath>

namespace Meow2::Math
{
/// @brief A rotation as an axis and an angle. Plain data.
struct AxisAngle
{
    Vec3 axis;          ///< Unit rotation axis.
    float angle = 0.0F; ///< Angle in radians, 0..π.
};

/// @brief Normalises a quaternion back to length 1 (rounding drifts it away after many operations).
/// @param q A non-zero quaternion.
/// @return The unit quaternion.
[[nodiscard]] inline Quat normalize(const Quat& q) noexcept
{
    return storeQuat(DirectX::XMQuaternionNormalize(load(q)));
}

/// @brief The rotation of `angle` radians about `axis`.
/// @param axis  The axis; normalised here, must not be zero (asserted).
/// @param angle Angle in radians; positive is clockwise seen from the axis's positive end.
/// @return The rotation.
[[nodiscard]] inline Quat fromAxisAngle(const Vec3& axis, float angle) noexcept
{
    const DirectX::XMVECTOR axisVector = load(axis);
    MEOW2_ASSERT(DirectX::XMVectorGetX(DirectX::XMVector3LengthSq(axisVector)) > 0.0F, "the axis must not be zero");
    return storeQuat(DirectX::XMQuaternionRotationNormal(DirectX::XMVector3Normalize(axisVector), angle));
}

/// @brief Splits a rotation into its axis and angle.
/// @param q A unit quaternion.
/// @return Axis and angle in 0..π (the shorter way round). The identity gives axis +Y (any axis would do) and angle 0.
[[nodiscard]] inline AxisAngle toAxisAngle(const Quat& q) noexcept
{
    // Use the representative with w >= 0, so the angle is the short way (0..π).
    const float sign = q.w < 0.0F ? -1.0F : 1.0F;
    const float w = clamp(q.w * sign, 0.0F, 1.0F);
    const float sinHalf = std::sqrt(1.0F - w * w);
    if (sinHalf < 1e-6F)
    {
        return {c_up, 0.0F};
    }
    return {Vec3{q.x * sign / sinHalf, q.y * sign / sinHalf, q.z * sign / sinHalf}, 2.0F * std::acos(w)};
}

/// @brief The rotation from Euler angles: **roll about Z first, then pitch about X, then yaw about Y**.
///
/// This is the order cameras and characters need: yaw turns left/right about the world's up axis, pitch then tilts
/// about the (already turned) right axis, and roll banks about the view direction. It's DirectXMath's
/// `XMQuaternionRotationRollPitchYaw` order.
/// @param radians (x = pitch, y = yaw, z = roll) in radians: each component is the angle about that axis.
/// @return The rotation.
[[nodiscard]] inline Quat fromEuler(const Vec3& radians) noexcept
{
    return storeQuat(DirectX::XMQuaternionRotationRollPitchYaw(radians.x, radians.y, radians.z));
}

/// @brief The Euler angles of a rotation, in fromEuler()'s order.
///
/// Euler angles aren't unique: pitch is returned in [−π/2, π/2], yaw and roll in [−π, π]. Looking straight up or down
/// (pitch ±π/2, "gimbal lock") yaw and roll turn about the same axis, so roll is returned as 0 and yaw takes the whole
/// turn.
/// @param q A unit quaternion.
/// @return (x = pitch, y = yaw, z = roll) in radians, such that fromEuler() of it gives the same rotation.
[[nodiscard]] inline Vec3 toEuler(const Quat& q) noexcept
{
    const DirectX::XMVECTOR rotation = load(q);
    const Vec3 forward = storeVec3(DirectX::XMVector3Rotate(load(c_forward), rotation));
    const Vec3 right = storeVec3(DirectX::XMVector3Rotate(load(c_right), rotation));
    const Vec3 up = storeVec3(DirectX::XMVector3Rotate(load(c_up), rotation));
    // forward = (sin yaw · cos pitch, −sin pitch, cos yaw · cos pitch); right.y = sin roll · cos pitch;
    // up.y = cos roll · cos pitch.
    const float pitch = std::asin(clamp(-forward.y, -1.0F, 1.0F));
    if (std::abs(forward.y) > 0.99999F)
    {
        // Gimbal lock: with roll 0, right = (cos yaw, 0, −sin yaw).
        return {pitch, std::atan2(-right.z, right.x), 0.0F};
    }
    return {pitch, std::atan2(forward.x, forward.z), std::atan2(right.y, up.y)};
}

/// @brief The rotation that turns +Z (forward) to `forward` and keeps +Y as close to `up` as possible.
///
/// For cameras, characters and turrets: "face this direction, stay upright". The result never rolls.
/// @param forward The direction to face; must not be zero (asserted).
/// @param up      Which way is up; if it's parallel to `forward` (looking straight up or down), +Z or +X is used
///                instead, so the result is still valid.
/// @return The rotation.
[[nodiscard]] inline Quat lookRotation(const Vec3& forward, const Vec3& up = c_up) noexcept
{
    using namespace DirectX;
    const XMVECTOR forwardVector = load(forward);
    MEOW2_ASSERT(XMVectorGetX(XMVector3LengthSq(forwardVector)) > 0.0F, "lookRotation needs a direction");
    const XMVECTOR z = XMVector3Normalize(forwardVector);
    XMVECTOR x = XMVector3Cross(load(up), z); // left-handed: up × forward = right
    if (XMVectorGetX(XMVector3LengthSq(x)) < 1e-10F)
    {
        // Up is parallel to forward: any perpendicular up works; prefer +Z, else +X.
        x = XMVector3Cross(load(std::abs(XMVectorGetZ(z)) < 0.9F ? c_forward : c_right), z);
    }
    x = XMVector3Normalize(x);
    const XMVECTOR y = XMVector3Cross(z, x);
    // Rows of the rotation matrix are where +X, +Y and +Z end up (row vectors).
    const XMMATRIX basis{x, y, z, g_XMIdentityR3};
    return normalize(storeQuat(XMQuaternionRotationMatrix(basis)));
}

/// @brief The shortest rotation that turns direction `from` into direction `to`.
/// @param from A direction; must not be zero.
/// @param to   Another direction; must not be zero.
/// @return The rotation. For opposite directions (a half turn, about any perpendicular axis) a perpendicular axis is
///         chosen.
[[nodiscard]] inline Quat fromToRotation(const Vec3& from, const Vec3& to) noexcept
{
    using namespace DirectX;
    const XMVECTOR a = XMVector3Normalize(load(from));
    const XMVECTOR b = XMVector3Normalize(load(to));
    const float cosine = XMVectorGetX(XMVector3Dot(a, b));
    if (cosine < -0.999999F)
    {
        XMVECTOR axis = XMVector3Cross(load(c_right), a);
        if (XMVectorGetX(XMVector3LengthSq(axis)) < 1e-6F)
        {
            axis = XMVector3Cross(load(c_up), a);
        }
        return storeQuat(XMQuaternionRotationNormal(XMVector3Normalize(axis), c_pi));
    }
    // Half-way trick: (a × b, 1 + a·b) is the rotation by twice the half angle; normalising makes it a unit quaternion.
    const XMVECTOR axis = XMVector3Cross(a, b);
    return normalize(storeQuat(XMVectorSetW(axis, 1.0F + cosine)));
}

/// @brief The rotation that applies `first`, then `then`.
///
/// Named instead of `operator*` because quaternion multiplication order is a classic source of bugs: in DirectXMath's
/// `XMQuaternionMultiply(a, b)` *a* is applied first, while in maths notation `b * a` means the same.
/// @param first The rotation applied first.
/// @param then  The rotation applied after it.
/// @return The combined rotation.
[[nodiscard]] inline Quat combine(const Quat& first, const Quat& then) noexcept
{
    return storeQuat(DirectX::XMQuaternionMultiply(load(first), load(then)));
}

/// @brief The opposite rotation: combine(q, inverse(q)) is the identity.
/// @param q A unit quaternion.
/// @return Its inverse (the conjugate, since it's a unit quaternion).
[[nodiscard]] inline Quat inverse(const Quat& q) noexcept
{
    return Quat{-q.x, -q.y, -q.z, q.w};
}

/// @brief Rotates a vector.
/// @param q A unit quaternion.
/// @param v The vector (a direction or a point relative to the rotation's centre).
/// @return The rotated vector.
[[nodiscard]] inline Vec3 rotate(const Quat& q, const Vec3& v) noexcept
{
    return storeVec3(DirectX::XMVector3Rotate(load(v), load(q)));
}

/// @brief Spherical linear interpolation: turns from `from` to `to` at constant angular speed, the short way round.
/// @param from Rotation at t = 0.
/// @param to   Rotation at t = 1.
/// @param t    Position between them, 0..1.
/// @return The interpolated unit rotation.
[[nodiscard]] inline Quat slerp(const Quat& from, const Quat& to, float t) noexcept
{
    // DirectXMath flips `to` when the dot product is negative, which is what makes it take the short way.
    return normalize(storeQuat(DirectX::XMQuaternionSlerp(load(from), load(to), t)));
}

/// @brief The angle of the smallest rotation that turns `a` into `b`.
/// @param a A unit quaternion.
/// @param b Another unit quaternion.
/// @return The angle in radians, 0..π.
[[nodiscard]] inline float angleBetween(const Quat& a, const Quat& b) noexcept
{
    // The rotation from a to b is inverse(a) then b; its angle is 2·atan2(|xyz|, |w|). atan2 stays precise for tiny
    // angles, where acos of a dot product near 1 loses most of its digits.
    const Quat delta = combine(inverse(a), b);
    const float sinHalf = std::sqrt(delta.x * delta.x + delta.y * delta.y + delta.z * delta.z);
    return 2.0F * std::atan2(sinHalf, std::abs(delta.w));
}

/// @brief Whether two quaternions are the same rotation, within an angle (q and −q count as the same).
/// @param a                 A unit quaternion.
/// @param b                 Another unit quaternion.
/// @param toleranceRadians  The largest angle between them that still counts as the same.
/// @return `true` if they rotate (nearly) the same way.
[[nodiscard]] inline bool nearlySameRotation(const Quat& a, const Quat& b, float toleranceRadians = 1e-3F) noexcept
{
    return angleBetween(a, b) <= toleranceRadians;
}
} // namespace Meow2::Math
