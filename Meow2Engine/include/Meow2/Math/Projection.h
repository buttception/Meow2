#pragma once

/// @file
/// @brief Projection matrices with reversed Z (near → depth 1, far → depth 0), viewports and the Vulkan Y-flip.
///
/// **Conventions** (techstack.md §10): left-handed view space (+Z into the screen), row vectors (`point × matrix`),
/// clip-space depth 0..1, and **reversed Z**: the near plane maps to depth 1 and the far plane (or infinity) to 0.
/// The depth buffer is `D32_FLOAT`, cleared to 0, compared with `GREATER_EQUAL`.
///
/// Why reversed: a float has most of its precision near 0. Perspective division already crowds depth values towards
/// the near plane; storing `near/z` (reversed) instead of `1 − near/z` puts the float's fine steps where the
/// projection's coarse ones are, and the two errors cancel. Depth precision becomes nearly uniform out to infinity,
/// so the far plane can be dropped entirely (`perspectiveReversedInfiniteLH`) and z-fighting at a distance vanishes.
/// @code
/// using namespace Meow2::Math;
/// const DirectX::XMMATRIX projection = perspectiveReversedInfiniteLH(toRadians(60.0F), 16.0F / 9.0F, 0.1F);
/// @endcode
/// @see techstack.md §10, docs/wiki/Math.md

#include <Meow2/Math/Types.h>

#include <cmath>

namespace Meow2::Math
{
/// @brief Perspective projection with reversed Z and **no far plane**: the engine's default.
///
/// Depth is `nearZ / z`: 1 on the near plane, 0.5 at twice the distance, approaching 0 at infinity.
/// @param fovY   Vertical field of view in radians, 0 < fovY < π.
/// @param aspect Width / height of the viewport.
/// @param nearZ  Distance to the near plane, > 0.
/// @return The matrix: `clip = (x·xScale, y·yScale, nearZ, z)` for a view-space point (x, y, z).
[[nodiscard]] inline DirectX::XMMATRIX perspectiveReversedInfiniteLH(float fovY, float aspect, float nearZ) noexcept
{
    const float yScale = 1.0F / std::tan(0.5F * fovY);
    const float xScale = yScale / aspect;
    // DirectXMath has no infinite or reversed variant. clip.w = z (the perspective divide), clip.z = nearZ,
    // so depth = nearZ / z.
    return DirectX::XMMATRIX{xScale, 0.0F,   0.0F,  0.0F, //
                             0.0F,   yScale, 0.0F,  0.0F, //
                             0.0F,   0.0F,   0.0F,  1.0F, //
                             0.0F,   0.0F,   nearZ, 0.0F};
}

/// @brief Perspective projection with reversed Z and a far plane: depth 1 at `nearZ`, 0 at `farZ`.
///
/// For views that need a far plane anyway (shadow cascades, reflection probes). It's DirectX's standard projection
/// with near and far swapped.
/// @param fovY   Vertical field of view in radians.
/// @param aspect Width / height.
/// @param nearZ  Near plane distance, > 0.
/// @param farZ   Far plane distance, > nearZ.
/// @return The matrix.
[[nodiscard]] inline DirectX::XMMATRIX perspectiveReversedLH(float fovY, float aspect, float nearZ, float farZ) noexcept
{
    return DirectX::XMMatrixPerspectiveFovLH(fovY, aspect, farZ, nearZ);
}

/// @brief Orthographic projection (no perspective) with reversed Z: depth 1 at `nearZ`, 0 at `farZ`, linear between.
/// @param width  Width of the visible box in view-space units.
/// @param height Height of the visible box.
/// @param nearZ  Near plane distance (may be 0 or negative).
/// @param farZ   Far plane distance, > nearZ.
/// @return The matrix.
[[nodiscard]] inline DirectX::XMMATRIX orthographicReversedLH(float width, float height, float nearZ,
                                                              float farZ) noexcept
{
    return DirectX::XMMatrixOrthographicLH(width, height, farZ, nearZ);
}

/// @brief The view-space distance of a depth-buffer value written with perspectiveReversedInfiniteLH().
///
/// For effects that need real distances (fog, SSAO, soft particles).
/// @param depth The stored depth, 0 < depth ≤ 1 (0 is infinitely far).
/// @param nearZ The projection's near plane.
/// @return The distance along the view axis: `nearZ / depth`.
[[nodiscard]] inline float viewDistanceFromDepth(float depth, float nearZ) noexcept
{
    return nearZ / depth;
}

/// @brief A viewport: the pixel rectangle clip space is mapped to, and the depth range. Plain data.
///
/// Framebuffer coordinates have y pointing **down** (pixel 0 is the top row) on both D3D12 and Vulkan.
struct Viewport
{
    float x = 0.0F;        ///< Left edge in pixels.
    float y = 0.0F;        ///< Top edge in pixels.
    float width = 0.0F;    ///< Width in pixels.
    float height = 0.0F;   ///< Height in pixels; negative only in the Vulkan form (flipForVulkan()).
    float minDepth = 0.0F; ///< Depth that NDC z = 0 maps to.
    float maxDepth = 1.0F; ///< Depth that NDC z = 1 maps to.
};

/// @brief Where a point in normalised device coordinates lands in the framebuffer, by D3D12's viewport rule.
///
/// NDC +Y is up, framebuffer y is down: `screenY = y + (1 − ndcY) · height / 2`.
/// @param viewport The (D3D-form) viewport.
/// @param ndc      x, y in −1..1 and z (depth) in 0..1 after the perspective divide.
/// @return (pixel x, pixel y, depth).
[[nodiscard]] inline Vec3 ndcToScreen(const Viewport& viewport, const Vec3& ndc) noexcept
{
    return Vec3{viewport.x + (ndc.x + 1.0F) * 0.5F * viewport.width,
                viewport.y + (1.0F - ndc.y) * 0.5F * viewport.height,
                viewport.minDepth + ndc.z * (viewport.maxDepth - viewport.minDepth)};
}

/// @brief The same viewport in Vulkan's form, so both backends can use the same projection matrices.
///
/// Vulkan's viewport rule has no `1 − ndcY` (`screenY = y + (1 + ndcY) · height / 2`), so with the same matrix the
/// image would be upside down. A viewport with its origin at the bottom edge and a negative height
/// (`VK_KHR_maintenance1`, core since Vulkan 1.1) flips it back: every point lands on the same pixel as on D3D12.
/// @param viewport The D3D-form viewport.
/// @return The viewport to pass to `vkCmdSetViewport`.
[[nodiscard]] constexpr Viewport flipForVulkan(const Viewport& viewport) noexcept
{
    return Viewport{viewport.x,       viewport.y + viewport.height, viewport.width, -viewport.height, viewport.minDepth,
                    viewport.maxDepth};
}
} // namespace Meow2::Math
