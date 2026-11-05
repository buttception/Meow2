#pragma once

/// @file
/// @brief Math over whole arrays at once: the SIMD batch helpers for code that processes thousands of items.
///
/// Transforming 10,000 points one `load` / `XMVector3TransformCoord` / `store` at a time pays the loop and memory
/// overhead per point. These wrap DirectXMath's *stream* functions, which run over the array in tight SIMD loops (with
/// AVX2 paths in this build) and leave the data where it is: no copies, no allocations.
/// @code
/// std::vector<Meow2::Math::Vec3> world(local.size());
/// Meow2::Math::transformPoints(local, Meow2::Math::load(modelMatrix), world);
/// @endcode
/// For very large arrays, split the range across jobs (JobSystem::parallelFor) and call the helper per range.
/// @see techstack.md §10, docs/wiki/Math.md

#include <Meow2/Core/Assert.h>
#include <Meow2/Math/Types.h>

#include <optional>
#include <span>

namespace Meow2::Math
{
/// @brief The smallest and largest corner of a set of points. Plain data.
struct MinMax
{
    Vec3 min; ///< Smallest x, y and z of the points.
    Vec3 max; ///< Largest x, y and z of the points.
};

/// @brief Transforms points by a matrix (with translation and the perspective divide), all at once.
/// @param points The input points.
/// @param matrix The transform (row vectors: `p' = p × M`).
/// @param[out] out Receives the transformed points; must not overlap `points`.
/// @pre `out.size() == points.size()` (asserted).
inline void transformPoints(std::span<const Vec3> points, DirectX::FXMMATRIX matrix, std::span<Vec3> out) noexcept
{
    MEOW2_ASSERT(out.size() == points.size(), "one output per point");
    if (points.empty())
    {
        return;
    }
    DirectX::XMVector3TransformCoordStream(out.data(), sizeof(Vec3), points.data(), sizeof(Vec3), points.size(),
                                           matrix);
}

/// @brief Transforms directions (normals, velocities) by a matrix's rotation and scale, ignoring its translation.
/// @note Not renormalised: for normals under non-uniform scale, use the inverse transpose and normalise afterwards.
/// @param directions The input directions.
/// @param matrix     The transform.
/// @param[out] out   Receives the transformed directions; must not overlap `directions`.
/// @pre `out.size() == directions.size()` (asserted).
inline void transformDirections(std::span<const Vec3> directions, DirectX::FXMMATRIX matrix,
                                std::span<Vec3> out) noexcept
{
    MEOW2_ASSERT(out.size() == directions.size(), "one output per direction");
    if (directions.empty())
    {
        return;
    }
    DirectX::XMVector3TransformNormalStream(out.data(), sizeof(Vec3), directions.data(), sizeof(Vec3),
                                            directions.size(), matrix);
}

/// @brief The corners of the axis-aligned box around a set of points, e.g. a mesh's bounds.
/// @param points The points.
/// @return The min and max corners, or empty for no points.
[[nodiscard]] inline std::optional<MinMax> computeMinMax(std::span<const Vec3> points) noexcept
{
    if (points.empty())
    {
        return std::nullopt;
    }
    DirectX::XMVECTOR minimum = load(points.front());
    DirectX::XMVECTOR maximum = minimum;
    for (const Vec3& point : points.subspan(1))
    {
        const DirectX::XMVECTOR value = load(point);
        minimum = DirectX::XMVectorMin(minimum, value); // x, y and z in one instruction each
        maximum = DirectX::XMVectorMax(maximum, value);
    }
    return MinMax{.min = storeVec3(minimum), .max = storeVec3(maximum)};
}
} // namespace Meow2::Math
