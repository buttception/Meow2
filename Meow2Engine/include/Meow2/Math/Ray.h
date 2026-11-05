#pragma once

/// @file
/// @brief Rays: from a screen pixel through the camera, and ray tests against planes, spheres, boxes and triangles
///        (editor picking, line of sight, simple queries).
///
/// @code
/// using namespace Meow2::Math;
/// const Ray ray = rayFromScreen(mouseX, mouseY, cameraMatrices, viewport);
/// if (const std::optional<RayPick> pick = pickClosest(ray, entityBounds))
/// {
///     select(entities[pick->index]);
/// }
/// @endcode
/// Every test returns the **distance along the ray** to the first hit (`pointAt(ray, distance)` is the hit point), or
/// `std::nullopt` for a miss. Hits behind the origin don't count; a ray starting inside a volume hits it at 0.
/// @see techstack.md §10, docs/wiki/Math.md

#include <Meow2/Math/Bounds.h>
#include <Meow2/Math/Camera.h>
#include <Meow2/Math/Types.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <optional>
#include <span>
#include <utility>

namespace Meow2::Math
{
/// @brief A half-line: a start point and a unit direction. Plain data.
struct Ray
{
    Vec3 origin;    ///< Where the ray starts.
    Vec3 direction; ///< Unit direction.
};

/// @brief An infinite plane: the points p with `normal · p + distance = 0`. Plain data.
struct Plane
{
    Vec3 normal;           ///< Unit normal; the side it points to is "in front".
    float distance = 0.0F; ///< Signed offset: −(normal · any point on the plane).
};

/// @brief A ray hit on a triangle. Plain data.
struct TriangleHit
{
    float distance = 0.0F; ///< Distance along the ray.
    float u = 0.0F;        ///< Barycentric weight of the second vertex.
    float v = 0.0F;        ///< Barycentric weight of the third vertex (the first has 1 − u − v).
};

/// @brief The closest box a ray hits (pickClosest()). Plain data.
struct RayPick
{
    std::size_t index = 0; ///< Index of the box in the input.
    float distance = 0.0F; ///< Distance along the ray to it.
};

/// @brief The plane through a point with a normal.
/// @param point  A point on the plane.
/// @param normal The normal; normalised here.
/// @return The plane.
[[nodiscard]] inline Plane planeFromPointNormal(const Vec3& point, const Vec3& normal) noexcept
{
    using namespace DirectX;
    const XMVECTOR n = XMVector3Normalize(load(normal));
    return Plane{storeVec3(n), -XMVectorGetX(XMVector3Dot(n, load(point)))};
}

/// @brief The point at a distance along a ray.
/// @param ray      The ray.
/// @param distance Distance from its origin.
/// @return `origin + direction × distance`.
[[nodiscard]] inline Vec3 pointAt(const Ray& ray, float distance) noexcept
{
    using namespace DirectX;
    return storeVec3(XMVectorMultiplyAdd(load(ray.direction), XMVectorReplicate(distance), load(ray.origin)));
}

/// @brief The ray through a pixel: from the near plane into the scene (editor picking).
///
/// Works for perspective cameras (rays fan out from the eye) and orthographic ones (parallel rays).
/// @param screenX  Pixel x (may be fractional: 0.5 is the centre of the first pixel).
/// @param screenY  Pixel y, down from the top.
/// @param matrices The camera's matrices.
/// @param viewport The viewport the pixel is in.
/// @return The ray, starting on the near plane.
[[nodiscard]] inline Ray rayFromScreen(float screenX, float screenY, const CameraMatrices& matrices,
                                       const Viewport& viewport) noexcept
{
    using namespace DirectX;
    // Two points under the pixel: on the near plane (depth 1) and further in (depth 0.5; with the infinite
    // projection that's twice the near distance).
    const XMVECTOR nearPoint = load(screenToWorld(Vec3{screenX, screenY, 1.0F}, matrices, viewport));
    const XMVECTOR farPoint = load(screenToWorld(Vec3{screenX, screenY, 0.5F}, matrices, viewport));
    return Ray{storeVec3(nearPoint), storeVec3(XMVector3Normalize(XMVectorSubtract(farPoint, nearPoint)))};
}

/// @brief Where a ray meets a plane.
/// @param ray   The ray.
/// @param plane The plane (either side).
/// @return The distance, or `std::nullopt` if the ray is parallel to the plane or points away from it.
[[nodiscard]] inline std::optional<float> intersect(const Ray& ray, const Plane& plane) noexcept
{
    using namespace DirectX;
    const float facing = XMVectorGetX(XMVector3Dot(load(plane.normal), load(ray.direction)));
    if (std::abs(facing) < 1e-8F)
    {
        return std::nullopt;
    }
    const float t = -(XMVectorGetX(XMVector3Dot(load(plane.normal), load(ray.origin))) + plane.distance) / facing;
    if (t < 0.0F)
    {
        return std::nullopt;
    }
    return t;
}

/// @brief Where a ray first meets a sphere.
/// @param ray    The ray.
/// @param sphere The sphere.
/// @return The distance to the first hit (0 if the ray starts inside), or `std::nullopt`.
[[nodiscard]] inline std::optional<float> intersect(const Ray& ray, const Sphere& sphere) noexcept
{
    using namespace DirectX;
    // |o + t·d − c|² = r² with |d| = 1: t² + 2bt + c = 0.
    const XMVECTOR toOrigin = XMVectorSubtract(load(ray.origin), load(sphere.center));
    const float b = XMVectorGetX(XMVector3Dot(toOrigin, load(ray.direction)));
    const float c = XMVectorGetX(XMVector3LengthSq(toOrigin)) - sphere.radius * sphere.radius;
    if (c <= 0.0F)
    {
        return 0.0F;
    }
    const float discriminant = b * b - c;
    if (b > 0.0F || discriminant < 0.0F)
    {
        return std::nullopt; // outside and pointing away, or passing beside it
    }
    return -b - std::sqrt(discriminant);
}

/// @brief Where a ray first meets an axis-aligned box (slab method).
/// @param ray The ray.
/// @param box The box.
/// @return The distance to the first hit (0 if the ray starts inside), or `std::nullopt`.
[[nodiscard]] inline std::optional<float> intersect(const Ray& ray, const Aabb& box) noexcept
{
    // Clip the ray against the three pairs of parallel planes ("slabs"); it hits if the intervals overlap.
    // A zero direction component gives ±infinity in the division, which the comparisons handle correctly.
    float enter = 0.0F;
    float exit = std::numeric_limits<float>::infinity();
    const std::array<float, 3> origins{ray.origin.x, ray.origin.y, ray.origin.z};
    const std::array<float, 3> directions{ray.direction.x, ray.direction.y, ray.direction.z};
    const std::array<float, 3> centers{box.center.x, box.center.y, box.center.z};
    const std::array<float, 3> extents{box.extents.x, box.extents.y, box.extents.z};
    for (std::size_t axis = 0; axis < 3; ++axis)
    {
        const float lo = centers[axis] - extents[axis];
        const float hi = centers[axis] + extents[axis];
        if (directions[axis] == 0.0F)
        {
            if (origins[axis] < lo || origins[axis] > hi)
            {
                return std::nullopt; // parallel to this slab and outside it
            }
            continue;
        }
        const float inverse = 1.0F / directions[axis];
        float t0 = (lo - origins[axis]) * inverse;
        float t1 = (hi - origins[axis]) * inverse;
        if (t0 > t1)
        {
            std::swap(t0, t1);
        }
        enter = t0 > enter ? t0 : enter;
        exit = t1 < exit ? t1 : exit;
        if (enter > exit)
        {
            return std::nullopt;
        }
    }
    return enter;
}

/// @brief Where a ray first meets an oriented box: the AABB test in the box's own frame.
/// @param ray The ray.
/// @param box The box.
/// @return The distance to the first hit (0 if the ray starts inside), or `std::nullopt`.
[[nodiscard]] inline std::optional<float> intersect(const Ray& ray, const OrientedBox& box) noexcept
{
    using namespace DirectX;
    const XMVECTOR rotation = load(box.orientation);
    const Ray local{storeVec3(XMVector3InverseRotate(XMVectorSubtract(load(ray.origin), load(box.center)), rotation)),
                    storeVec3(XMVector3InverseRotate(load(ray.direction), rotation))};
    // A rotation keeps lengths, so distances in the box's frame are distances in the world.
    return intersect(local, Aabb{Vec3{}, box.extents});
}

/// @brief Where a ray meets a triangle (Möller–Trumbore).
/// @param ray       The ray.
/// @param v0        First vertex.
/// @param v1        Second vertex.
/// @param v2        Third vertex.
/// @param cullBack  If `true`, triangles seen from behind are missed. The front is the side from which v0 → v1 → v2
///                  is clockwise (the engine's left-handed front-face winding).
/// @return The hit (distance and barycentrics), or `std::nullopt`.
[[nodiscard]] inline std::optional<TriangleHit> intersect(const Ray& ray, const Vec3& v0, const Vec3& v1,
                                                          const Vec3& v2, bool cullBack = false) noexcept
{
    using namespace DirectX;
    const XMVECTOR a = load(v0);
    const XMVECTOR edge1 = XMVectorSubtract(load(v1), a);
    const XMVECTOR edge2 = XMVectorSubtract(load(v2), a);
    const XMVECTOR d = load(ray.direction);
    const XMVECTOR p = XMVector3Cross(d, edge2);
    const float determinant = XMVectorGetX(XMVector3Dot(edge1, p));
    // determinant > 0: the ray sees the clockwise (front) side in left-handed coordinates.
    if (cullBack ? determinant < 1e-8F : std::abs(determinant) < 1e-8F)
    {
        return std::nullopt;
    }
    const float inverse = 1.0F / determinant;
    const XMVECTOR s = XMVectorSubtract(load(ray.origin), a);
    const float u = XMVectorGetX(XMVector3Dot(s, p)) * inverse;
    if (u < 0.0F || u > 1.0F)
    {
        return std::nullopt;
    }
    const XMVECTOR q = XMVector3Cross(s, edge1);
    const float v = XMVectorGetX(XMVector3Dot(d, q)) * inverse;
    if (v < 0.0F || u + v > 1.0F)
    {
        return std::nullopt;
    }
    const float t = XMVectorGetX(XMVector3Dot(edge2, q)) * inverse;
    if (t < 0.0F)
    {
        return std::nullopt;
    }
    return TriangleHit{t, u, v};
}

/// @brief The closest of several boxes a ray hits (picking).
/// @param ray   The ray, e.g. from rayFromScreen().
/// @param boxes The boxes, e.g. the world bounds of the selectable objects.
/// @return The index and distance of the nearest hit, or `std::nullopt` if none is hit.
[[nodiscard]] inline std::optional<RayPick> pickClosest(const Ray& ray, std::span<const Aabb> boxes) noexcept
{
    std::optional<RayPick> best;
    for (std::size_t i = 0; i < boxes.size(); ++i)
    {
        const std::optional<float> distance = intersect(ray, boxes[i]);
        if (distance && (!best || *distance < best->distance))
        {
            best = RayPick{i, *distance};
        }
    }
    return best;
}
} // namespace Meow2::Math
