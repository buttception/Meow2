#pragma once

/// @file
/// @brief Bounding volumes (axis-aligned box, sphere, oriented box), the camera frustum, containment and intersection
///        tests, and batch frustum culling.
///
/// Bounding volumes are cheap stand-ins for complex shapes: if a mesh's box is outside the camera's frustum, none of
/// its triangles can be visible, so it isn't drawn (frustum culling). The same tests drive picking, triggers and
/// broad-phase queries.
/// @code
/// using namespace Meow2::Math;
/// const Frustum frustum = frustumFromCamera(cameraMatrices);
/// for (const Mesh& mesh : meshes)
/// {
///     if (test(frustum, transformAabb(mesh.localBounds, mesh.worldMatrix)) != Containment::Outside) { draw(mesh); }
/// }
/// @endcode
/// @see techstack.md §10, docs/wiki/Math.md

#include <Meow2/Core/Assert.h>
#include <Meow2/Math/Batch.h>
#include <Meow2/Math/Camera.h>
#include <Meow2/Math/Transform.h>
#include <Meow2/Math/Types.h>

#include <DirectXCollision.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace Meow2::Math
{
/// @brief An axis-aligned bounding box as centre and half-size. Plain data.
struct Aabb
{
    Vec3 center;  ///< Centre of the box.
    Vec3 extents; ///< Half the size along each axis (≥ 0).
};

/// @brief A bounding sphere. Plain data.
struct Sphere
{
    Vec3 center;         ///< Centre.
    float radius = 0.0F; ///< Radius (≥ 0).
};

/// @brief A box rotated in space: an Aabb in its own frame, placed by a rotation. Plain data.
struct OrientedBox
{
    Vec3 center;      ///< Centre.
    Vec3 extents;     ///< Half the size along the box's own axes.
    Quat orientation; ///< Rotation from the box's axes to the world's.
};

/// @brief A camera's view volume as six planes, each `(normal, d)` with the inside where `normal · p + d ≥ 0`.
///        Plain data.
///
/// Built from a view-projection matrix (frustumFromMatrix()), so it works for any projection, including the engine's
/// reversed infinite one (whose far "plane" is at infinity and is stored as a plane that contains everything).
struct Frustum
{
    std::array<Vec4, 6> planes; ///< Left, right, bottom, top, near, far; (x, y, z) = unit inward normal, w = d.
};

/// @brief How a volume relates to another one (usually a frustum).
enum class Containment : std::uint8_t
{
    Outside,    ///< Completely outside: cull it.
    Intersects, ///< Partly inside.
    Inside,     ///< Completely inside.
};

/// @brief The box between two corners.
/// @param minCorner Smallest x, y, z.
/// @param maxCorner Largest x, y, z.
/// @return The box.
[[nodiscard]] inline Aabb aabbFromMinMax(const Vec3& minCorner, const Vec3& maxCorner) noexcept
{
    using namespace DirectX;
    const XMVECTOR lo = load(minCorner);
    const XMVECTOR hi = load(maxCorner);
    return Aabb{storeVec3(XMVectorScale(XMVectorAdd(lo, hi), 0.5F)),
                storeVec3(XMVectorScale(XMVectorSubtract(hi, lo), 0.5F))};
}

/// @brief The smallest box around points.
/// @param points The points.
/// @return The box, or `std::nullopt` for no points.
[[nodiscard]] inline std::optional<Aabb> aabbFromPoints(std::span<const Vec3> points) noexcept
{
    const std::optional<MinMax> corners = computeMinMax(points);
    if (!corners)
    {
        return std::nullopt;
    }
    return aabbFromMinMax(corners->min, corners->max);
}

/// @brief The corners of a box.
/// @param box The box.
/// @return Its smallest and largest corner.
[[nodiscard]] inline MinMax toMinMax(const Aabb& box) noexcept
{
    using namespace DirectX;
    return MinMax{storeVec3(XMVectorSubtract(load(box.center), load(box.extents))),
                  storeVec3(XMVectorAdd(load(box.center), load(box.extents)))};
}

/// @brief A sphere around points (not always the smallest: within a few percent of it).
/// @param points The points; at least one (asserted).
/// @return A sphere containing every point.
[[nodiscard]] inline Sphere sphereFromPoints(std::span<const Vec3> points) noexcept
{
    MEOW2_ASSERT(!points.empty(), "a bounding sphere needs points");
    DirectX::BoundingSphere sphere;
    DirectX::BoundingSphere::CreateFromPoints(sphere, points.size(), points.data(), sizeof(Vec3));
    return Sphere{Vec3{sphere.Center.x, sphere.Center.y, sphere.Center.z}, sphere.Radius};
}

/// @brief The axis-aligned box around a box after a transform (rotation makes it grow).
///
/// Arvo's method: the new extents are the old ones through the absolute values of the matrix, so it's exact (the
/// tightest box around the transformed box) and needs no corners.
/// @param box    The box.
/// @param matrix An affine transform (no projection).
/// @return The box around the transformed box.
[[nodiscard]] inline Aabb transformAabb(const Aabb& box, DirectX::FXMMATRIX matrix) noexcept
{
    using namespace DirectX;
    const XMVECTOR center = XMVector3TransformCoord(load(box.center), matrix);
    const XMVECTOR e = load(box.extents);
    // Row vectors: the new extent along each world axis is |row0|·ex + |row1|·ey + |row2|·ez.
    const XMVECTOR extents = XMVectorAdd(XMVectorAdd(XMVectorMultiply(XMVectorAbs(matrix.r[0]), XMVectorSplatX(e)),
                                                     XMVectorMultiply(XMVectorAbs(matrix.r[1]), XMVectorSplatY(e))),
                                         XMVectorMultiply(XMVectorAbs(matrix.r[2]), XMVectorSplatZ(e)));
    return Aabb{storeVec3(center), storeVec3(extents)};
}

/// @brief A local box placed in the world by a transform, as an oriented box (tighter than transformAabb()).
/// @param localBox  The box in the object's own space.
/// @param transform The object's transform.
/// @return The oriented box.
[[nodiscard]] inline OrientedBox orientedBox(const Aabb& localBox, const Transform& transform) noexcept
{
    using namespace DirectX;
    const XMVECTOR absScale = XMVectorAbs(load(transform.scale));
    return OrientedBox{transformPoint(transform, localBox.center),
                       storeVec3(XMVectorMultiply(load(localBox.extents), absScale)), transform.rotation};
}

/// @cond
namespace Detail
{
inline DirectX::BoundingOrientedBox toDirectX(const OrientedBox& box) noexcept
{
    return DirectX::BoundingOrientedBox{box.center, box.extents, box.orientation};
}
} // namespace Detail
/// @endcond

/// @brief Whether a point is inside or on a box.
/// @param box   The box.
/// @param point The point.
/// @return `true` if inside or on the surface.
[[nodiscard]] inline bool contains(const Aabb& box, const Vec3& point) noexcept
{
    return std::abs(point.x - box.center.x) <= box.extents.x && std::abs(point.y - box.center.y) <= box.extents.y &&
           std::abs(point.z - box.center.z) <= box.extents.z;
}

/// @brief Whether a point is inside or on a sphere.
/// @param sphere The sphere.
/// @param point  The point.
/// @return `true` if inside or on the surface.
[[nodiscard]] inline bool contains(const Sphere& sphere, const Vec3& point) noexcept
{
    const float dx = point.x - sphere.center.x;
    const float dy = point.y - sphere.center.y;
    const float dz = point.z - sphere.center.z;
    return dx * dx + dy * dy + dz * dz <= sphere.radius * sphere.radius;
}

/// @brief Whether a point is inside or on an oriented box.
/// @param box   The box.
/// @param point The point.
/// @return `true` if inside or on the surface.
[[nodiscard]] inline bool contains(const OrientedBox& box, const Vec3& point) noexcept
{
    const Vec3 local = storeVec3(DirectX::XMVector3InverseRotate(
        DirectX::XMVectorSubtract(load(point), load(box.center)), load(box.orientation)));
    return contains(Aabb{Vec3{}, box.extents}, local);
}

/// @brief Whether two boxes overlap (touching counts).
/// @param a A box.
/// @param b Another box.
/// @return `true` if they overlap.
[[nodiscard]] inline bool intersects(const Aabb& a, const Aabb& b) noexcept
{
    return std::abs(a.center.x - b.center.x) <= a.extents.x + b.extents.x &&
           std::abs(a.center.y - b.center.y) <= a.extents.y + b.extents.y &&
           std::abs(a.center.z - b.center.z) <= a.extents.z + b.extents.z;
}

/// @brief Whether two spheres overlap (touching counts).
/// @param a A sphere.
/// @param b Another sphere.
/// @return `true` if they overlap.
[[nodiscard]] inline bool intersects(const Sphere& a, const Sphere& b) noexcept
{
    const float reach = a.radius + b.radius;
    const float dx = a.center.x - b.center.x;
    const float dy = a.center.y - b.center.y;
    const float dz = a.center.z - b.center.z;
    return dx * dx + dy * dy + dz * dz <= reach * reach;
}

/// @brief Whether a box and a sphere overlap (touching counts).
/// @param box    The box.
/// @param sphere The sphere.
/// @return `true` if they overlap.
[[nodiscard]] inline bool intersects(const Aabb& box, const Sphere& sphere) noexcept
{
    // Distance from the sphere's centre to the closest point of the box.
    const auto gap = [](float c, float boxCenter, float extent)
    {
        const float d = std::abs(c - boxCenter) - extent;
        return d > 0.0F ? d : 0.0F;
    };
    const float dx = gap(sphere.center.x, box.center.x, box.extents.x);
    const float dy = gap(sphere.center.y, box.center.y, box.extents.y);
    const float dz = gap(sphere.center.z, box.center.z, box.extents.z);
    return dx * dx + dy * dy + dz * dz <= sphere.radius * sphere.radius;
}

/// @brief Whether two oriented boxes overlap (separating-axis test, DirectXCollision).
/// @param a A box.
/// @param b Another box.
/// @return `true` if they overlap.
[[nodiscard]] inline bool intersects(const OrientedBox& a, const OrientedBox& b) noexcept
{
    return Detail::toDirectX(a).Intersects(Detail::toDirectX(b));
}

/// @brief The frustum of a view-projection matrix (Gribb-Hartmann plane extraction).
///
/// Clip space keeps `−w ≤ x ≤ w`, `−w ≤ y ≤ w` and `0 ≤ z ≤ w`; each inequality is a plane in world space, read off
/// the matrix's columns. For the reversed infinite projection the `z ≥ 0` plane has no normal (everything in front
/// is closer than infinity); it's stored as `(0, 0, 0, 1)`, which every point passes.
/// @param viewProjection World → clip space (row vectors).
/// @return The frustum, with normalised planes.
[[nodiscard]] inline Frustum frustumFromMatrix(DirectX::FXMMATRIX viewProjection) noexcept
{
    using namespace DirectX;
    const XMMATRIX columns = XMMatrixTranspose(viewProjection);
    const std::array<XMVECTOR, 6> raw{
        XMVectorAdd(columns.r[3], columns.r[0]),      // left:   x ≥ −w
        XMVectorSubtract(columns.r[3], columns.r[0]), // right:  x ≤ w
        XMVectorAdd(columns.r[3], columns.r[1]),      // bottom: y ≥ −w
        XMVectorSubtract(columns.r[3], columns.r[1]), // top:    y ≤ w
        XMVectorSubtract(columns.r[3], columns.r[2]), // near:   z ≤ w (depth 1 with reversed Z)
        columns.r[2],                                 // far:    z ≥ 0 (depth 0)
    };
    Frustum frustum;
    for (std::size_t i = 0; i < raw.size(); ++i)
    {
        const float length = XMVectorGetX(XMVector3Length(raw[i]));
        frustum.planes[i] =
            length > 1e-12F ? storeVec4(XMVectorScale(raw[i], 1.0F / length)) : Vec4{0.0F, 0.0F, 0.0F, 1.0F};
    }
    return frustum;
}

/// @brief The world-space frustum of a camera.
/// @param matrices The camera's matrices (cameraMatrices()).
/// @return The frustum.
[[nodiscard]] inline Frustum frustumFromCamera(const CameraMatrices& matrices) noexcept
{
    return frustumFromMatrix(load(matrices.viewProjection));
}

/// @brief Where a sphere is relative to a frustum.
/// @param frustum The frustum.
/// @param sphere  The sphere.
/// @return Outside, Intersects or Inside.
[[nodiscard]] inline Containment test(const Frustum& frustum, const Sphere& sphere) noexcept
{
    Containment result = Containment::Inside;
    for (const Vec4& plane : frustum.planes)
    {
        const float distance =
            plane.x * sphere.center.x + plane.y * sphere.center.y + plane.z * sphere.center.z + plane.w;
        if (distance < -sphere.radius)
        {
            return Containment::Outside;
        }
        if (distance < sphere.radius)
        {
            result = Containment::Intersects;
        }
    }
    return result;
}

/// @brief Where a box is relative to a frustum.
///
/// Per plane, the box's projected radius `|n|·extents` says how far it reaches towards the plane. Conservative: a box
/// near a frustum corner can be reported as Intersects although it's just outside, never the other way round.
/// @param frustum The frustum.
/// @param box     The box.
/// @return Outside, Intersects or Inside.
[[nodiscard]] inline Containment test(const Frustum& frustum, const Aabb& box) noexcept
{
    Containment result = Containment::Inside;
    for (const Vec4& plane : frustum.planes)
    {
        const float distance = plane.x * box.center.x + plane.y * box.center.y + plane.z * box.center.z + plane.w;
        const float reach =
            std::abs(plane.x) * box.extents.x + std::abs(plane.y) * box.extents.y + std::abs(plane.z) * box.extents.z;
        if (distance < -reach)
        {
            return Containment::Outside;
        }
        if (distance < reach)
        {
            result = Containment::Intersects;
        }
    }
    return result;
}

/// @brief Frustum-culls many spheres at once: `visible[i]` = 1 unless sphere `i` is completely outside.
///
/// Tests one sphere against all six planes per iteration with SIMD (the planes are kept in registers as
/// x/y/z/w vectors), with no branches per plane. For very large arrays, split the range across
/// jobs.
/// @param frustum The frustum.
/// @param spheres The spheres.
/// @param[out] visible Receives 1 (possibly visible) or 0 (outside) per sphere; same size as `spheres` (asserted).
inline void cullSpheres(const Frustum& frustum, std::span<const Sphere> spheres,
                        std::span<std::uint8_t> visible) noexcept
{
    using namespace DirectX;
    MEOW2_ASSERT(visible.size() == spheres.size(), "one result per sphere");
    // The six planes as structure-of-arrays (all x, all y, all z, all w), padded to eight with (0, 0, 0, 1), which
    // every sphere passes: two registers per component test four planes at once.
    std::array<float, 8> px{};
    std::array<float, 8> py{};
    std::array<float, 8> pz{};
    std::array<float, 8> pw{0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 1.0F};
    for (std::size_t i = 0; i < frustum.planes.size(); ++i)
    {
        px[i] = frustum.planes[i].x;
        py[i] = frustum.planes[i].y;
        pz[i] = frustum.planes[i].z;
        pw[i] = frustum.planes[i].w;
    }
    const XMVECTOR ax = XMVectorSet(px[0], px[1], px[2], px[3]);
    const XMVECTOR ay = XMVectorSet(py[0], py[1], py[2], py[3]);
    const XMVECTOR az = XMVectorSet(pz[0], pz[1], pz[2], pz[3]);
    const XMVECTOR aw = XMVectorSet(pw[0], pw[1], pw[2], pw[3]);
    const XMVECTOR bx = XMVectorSet(px[4], px[5], px[6], px[7]);
    const XMVECTOR by = XMVectorSet(py[4], py[5], py[6], py[7]);
    const XMVECTOR bz = XMVectorSet(pz[4], pz[5], pz[6], pz[7]);
    const XMVECTOR bw = XMVectorSet(pw[4], pw[5], pw[6], pw[7]);
    for (std::size_t i = 0; i < spheres.size(); ++i)
    {
        const Sphere& s = spheres[i];
        const XMVECTOR cx = XMVectorReplicate(s.center.x);
        const XMVECTOR cy = XMVectorReplicate(s.center.y);
        const XMVECTOR cz = XMVectorReplicate(s.center.z);
        const XMVECTOR negativeRadius = XMVectorReplicate(-s.radius);
        const XMVECTOR da = XMVectorMultiplyAdd(ax, cx, XMVectorMultiplyAdd(ay, cy, XMVectorMultiplyAdd(az, cz, aw)));
        const XMVECTOR db = XMVectorMultiplyAdd(bx, cx, XMVectorMultiplyAdd(by, cy, XMVectorMultiplyAdd(bz, cz, bw)));
        const XMVECTOR outside = XMVectorOrInt(XMVectorLess(da, negativeRadius), XMVectorLess(db, negativeRadius));
        visible[i] = XMVector4EqualInt(outside, XMVectorFalseInt()) ? 1 : 0;
    }
}
} // namespace Meow2::Math
