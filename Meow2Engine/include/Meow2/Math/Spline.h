#pragma once

/// @file
/// @brief Splines: smooth curves through or near control points, for paths, camera rails and rotation keys, and
///        `SplinePath` for moving along a path at constant speed.
///
/// | Curve | Passes through | Controlled by | Typical use |
/// |---|---|---|---|
/// | Catmull-Rom | every point | the points themselves (the tangent at a point comes from its neighbours) | paths
/// through waypoints, camera rails | | Cubic Bézier | its first and last point | two handles in between | hand-shaped
/// curves (editor handles, UI) | | Hermite | both ends | an explicit tangent at each end | animation curves, joining
/// pieces smoothly |
///
/// All segment functions take `t` in 0..1 along one segment. `t` is not distance: speed varies along a curve, so
/// moving `t` at a constant rate looks uneven; SplinePath fixes that with an arc-length table.
/// @code
/// using namespace Meow2::Math;
/// const SplinePath rail{waypoints};
/// distance += speed * dt;                                 // metres along the path
/// camera.position = rail.pointAtDistance(distance);
/// camera.rotation = lookRotation(rail.tangentAtDistance(distance));
/// @endcode
/// @see docs/wiki/Math.md

#include <Meow2/Math/Quaternion.h>
#include <Meow2/Math/Types.h>

#include <cstddef>
#include <span>
#include <vector>

namespace Meow2::Math
{
/// @brief A point on a uniform Catmull-Rom segment between `p1` (t = 0) and `p2` (t = 1).
/// @param p0 The point before the segment (shapes the tangent at `p1`).
/// @param p1 Start of the segment.
/// @param p2 End of the segment.
/// @param p3 The point after the segment (shapes the tangent at `p2`).
/// @param t  Position along the segment, 0..1.
/// @return The point.
[[nodiscard]] inline Vec3 catmullRom(const Vec3& p0, const Vec3& p1, const Vec3& p2, const Vec3& p3, float t) noexcept
{
    return storeVec3(DirectX::XMVectorCatmullRom(load(p0), load(p1), load(p2), load(p3), t));
}

/// @brief A point on a cubic Bézier curve.
/// @param p0 Start (t = 0).
/// @param p1 First handle: the curve leaves `p0` towards it.
/// @param p2 Second handle: the curve arrives at `p3` from its direction.
/// @param p3 End (t = 1).
/// @param t  Position along the curve, 0..1.
/// @return The point.
[[nodiscard]] inline Vec3 bezier(const Vec3& p0, const Vec3& p1, const Vec3& p2, const Vec3& p3, float t) noexcept
{
    using namespace DirectX;
    // Bernstein form: (1−t)³ p0 + 3(1−t)²t p1 + 3(1−t)t² p2 + t³ p3.
    const float u = 1.0F - t;
    const XMVECTOR result = XMVectorScale(load(p0), u * u * u) + XMVectorScale(load(p1), 3.0F * u * u * t) +
                            XMVectorScale(load(p2), 3.0F * u * t * t) + XMVectorScale(load(p3), t * t * t);
    return storeVec3(result);
}

/// @brief The derivative (velocity) of a cubic Bézier curve with respect to `t`.
/// @param p0 Start.
/// @param p1 First handle.
/// @param p2 Second handle.
/// @param p3 End.
/// @param t  Position along the curve, 0..1.
/// @return The tangent vector (not normalised; its length is the speed in units per `t`).
[[nodiscard]] inline Vec3 bezierTangent(const Vec3& p0, const Vec3& p1, const Vec3& p2, const Vec3& p3,
                                        float t) noexcept
{
    using namespace DirectX;
    const float u = 1.0F - t;
    const XMVECTOR result = XMVectorScale(load(p1) - load(p0), 3.0F * u * u) +
                            XMVectorScale(load(p2) - load(p1), 6.0F * u * t) +
                            XMVectorScale(load(p3) - load(p2), 3.0F * t * t);
    return storeVec3(result);
}

/// @brief A point on a cubic Hermite segment: from `p0` with tangent `m0` to `p1` with tangent `m1`.
/// @param p0 Start (t = 0).
/// @param m0 Tangent (velocity per unit `t`) at the start.
/// @param p1 End (t = 1).
/// @param m1 Tangent at the end.
/// @param t  Position along the segment, 0..1.
/// @return The point.
[[nodiscard]] inline Vec3 hermite(const Vec3& p0, const Vec3& m0, const Vec3& p1, const Vec3& m1, float t) noexcept
{
    return storeVec3(DirectX::XMVectorHermite(load(p0), load(m0), load(p1), load(m1), t));
}

/// @brief Smooth interpolation through rotation keys: the rotation counterpart of catmullRom() (squad).
///
/// Slerp between keys turns at constant speed but changes direction abruptly at each key; this eases through them.
/// @param q0 The key before the segment.
/// @param q1 Start of the segment (t = 0).
/// @param q2 End of the segment (t = 1).
/// @param q3 The key after the segment.
/// @param t  Position along the segment, 0..1.
/// @return The unit rotation.
[[nodiscard]] inline Quat catmullRom(const Quat& q0, const Quat& q1, const Quat& q2, const Quat& q3, float t) noexcept
{
    using namespace DirectX;
    XMVECTOR a;
    XMVECTOR b;
    XMVECTOR c;
    // SquadSetup also flips keys onto the same hemisphere, so the curve takes the short way between them.
    XMQuaternionSquadSetup(&a, &b, &c, load(q0), load(q1), load(q2), load(q3));
    return normalize(storeQuat(XMQuaternionSquad(load(q1), a, b, c, t)));
}

/// @brief A Catmull-Rom path through points that can be followed at constant speed (by distance, not `t`).
///
/// On construction it samples every segment and stores the accumulated length at each sample (an arc-length table).
/// pointAtDistance() then finds the sample by binary search, interpolates `t` between neighbouring samples and refines
/// it with one Newton step, so equal distance steps give equally spaced points (within 0.2 % with the default 32
/// samples per segment).
///
/// The path starts at the first point and ends at the last; the ends are extended by repeating the end points, so the
/// curve leaves and arrives along the first and last segment.
/// @par Thread safety
/// Immutable after construction: any thread may read it.
class SplinePath
{
public:
    /// @brief Builds the path and its arc-length table.
    /// @param points            The points to pass through, at least 2 (asserted).
    /// @param samplesPerSegment Samples per segment for the arc-length table; more is more exact, at 4 bytes each.
    explicit SplinePath(std::span<const Vec3> points, std::size_t samplesPerSegment = 32);

    /// @brief Total length of the path, in the points' units.
    /// @return The length (≥ 0).
    [[nodiscard]] float length() const noexcept;

    /// @brief The point at a distance along the path.
    /// @param distance Distance from the start; clamped to 0..length().
    /// @return The point.
    [[nodiscard]] Vec3 pointAtDistance(float distance) const noexcept;

    /// @brief The direction of travel at a distance along the path.
    /// @param distance Distance from the start; clamped to 0..length().
    /// @return A unit vector (or zero where the path doesn't move, e.g. repeated points).
    [[nodiscard]] Vec3 tangentAtDistance(float distance) const noexcept;

    /// @brief The point at a curve parameter: segment index plus fraction (`1.5` = half way along the second
    ///        segment). Not constant speed.
    /// @param t 0..segmentCount(); clamped.
    /// @return The point.
    [[nodiscard]] Vec3 pointAt(float t) const noexcept;

    /// @brief Number of segments (points − 1).
    /// @return The count.
    [[nodiscard]] std::size_t segmentCount() const noexcept;

private:
    /// @brief The curve parameter (segment + fraction) at a distance, from the arc-length table.
    [[nodiscard]] float parameterAtDistance(float distance) const noexcept;

    std::vector<Vec3> m_points;          ///< The control points.
    std::vector<float> m_lengths;        ///< Accumulated length at each sample; front() = 0, back() = length().
    std::size_t m_samplesPerSegment = 1; ///< Samples per segment in m_lengths.
};
} // namespace Meow2::Math
