#pragma once

/// @file
/// @brief Camera maths: the lens, the view and projection matrices, screen ↔ world conversion and camera-relative
///        positions.
///
/// A camera is a Transform (where it is and where it looks: local +Z) plus a CameraLens (how it projects). The view
/// matrix moves the world so the camera sits at the origin looking down +Z; the projection then maps that to clip
/// space with reversed Z (Projection.h).
/// @code
/// using namespace Meow2::Math;
/// const CameraMatrices matrices = cameraMatrices(cameraTransform, lens, viewportAspect);
/// if (const std::optional<Vec3> screen = worldToScreen(enemyHead, matrices, viewport))
/// {
///     drawHealthBar(screen->x, screen->y);
/// }
/// @endcode
/// **Camera-relative rendering** (techstack.md §10, large worlds): a float has about 8 mm of precision 100 km from the
/// origin, and multiplying such positions by a view matrix that subtracts 100 km again loses most of what's left.
/// Subtracting the camera position first (cameraRelative(), one subtraction per object) and using a view matrix
/// without translation (cameraRelativeViewMatrix()) keeps every number the GPU sees small and precise.
/// @see techstack.md §10, docs/wiki/Math.md

#include <Meow2/Math/Projection.h>
#include <Meow2/Math/Scalar.h>
#include <Meow2/Math/Transform.h>
#include <Meow2/Math/Types.h>

#include <cstdint>
#include <optional>

namespace Meow2::Math
{
/// @brief How a camera projects.
enum class ProjectionType : std::uint8_t
{
    Perspective,  ///< Things shrink with distance; reversed Z with no far plane.
    Orthographic, ///< No perspective (2D, editor side views, shadow maps); reversed Z between near and far.
};

/// @brief A camera's lens. Plain data; a 60° perspective lens by default.
struct CameraLens
{
    ProjectionType type = ProjectionType::Perspective; ///< Perspective or orthographic.
    float verticalFov = toRadians(60.0F);              ///< Perspective: vertical field of view in radians.
    float orthographicHeight = 10.0F;                  ///< Orthographic: visible height in world units.
    float nearZ = 0.1F;                                ///< Near plane distance (> 0 for perspective).
    float farZ = 1000.0F;                              ///< Orthographic only: far plane (perspective has none).
};

/// @brief The matrices of one camera for one frame. Plain data.
struct CameraMatrices
{
    Mat4 view;                  ///< World → view space.
    Mat4 projection;            ///< View → clip space.
    Mat4 viewProjection;        ///< World → clip space (view × projection).
    Mat4 inverseViewProjection; ///< Clip → world space, for screenToWorld().
};

/// @brief The view matrix of a camera placed by a transform: the inverse of its position and rotation.
/// @param camera The camera's transform; its scale is ignored (a scaled camera would distort the view).
/// @return World → view space: the camera at the origin, looking down +Z with +Y up.
[[nodiscard]] inline DirectX::XMMATRIX viewMatrix(const Transform& camera) noexcept
{
    return DirectX::XMMatrixLookToLH(load(camera.position), load(forward(camera)), load(up(camera)));
}

/// @brief The view matrix for camera-relative positions: rotation only, no translation.
/// @param rotation The camera's rotation.
/// @return View matrix for positions that already had the camera's position subtracted (cameraRelative()).
[[nodiscard]] inline DirectX::XMMATRIX cameraRelativeViewMatrix(const Quat& rotation) noexcept
{
    return DirectX::XMMatrixLookToLH(DirectX::XMVectorZero(), load(rotate(rotation, c_forward)),
                                     load(rotate(rotation, c_up)));
}

/// @brief A world position relative to the camera: the one subtraction that keeps rendering precise far from the
///        origin.
/// @param position       A position in the world.
/// @param cameraPosition The camera's position.
/// @return `position − cameraPosition`, small for everything near the camera.
[[nodiscard]] inline Vec3 cameraRelative(const WorldPosition& position, const WorldPosition& cameraPosition) noexcept
{
    return Vec3{position.x - cameraPosition.x, position.y - cameraPosition.y, position.z - cameraPosition.z};
}

/// @brief The projection matrix of a lens.
/// @param lens   The lens.
/// @param aspect Viewport width / height.
/// @return perspectiveReversedInfiniteLH() or orthographicReversedLH() for the lens.
[[nodiscard]] inline DirectX::XMMATRIX projectionMatrix(const CameraLens& lens, float aspect) noexcept
{
    if (lens.type == ProjectionType::Orthographic)
    {
        return orthographicReversedLH(lens.orthographicHeight * aspect, lens.orthographicHeight, lens.nearZ, lens.farZ);
    }
    return perspectiveReversedInfiniteLH(lens.verticalFov, aspect, lens.nearZ);
}

/// @brief All matrices of a camera for one frame.
/// @param camera The camera's transform (scale ignored).
/// @param lens   Its lens.
/// @param aspect Viewport width / height.
/// @return View, projection, their product and its inverse.
[[nodiscard]] inline CameraMatrices cameraMatrices(const Transform& camera, const CameraLens& lens,
                                                   float aspect) noexcept
{
    using namespace DirectX;
    const XMMATRIX view = viewMatrix(camera);
    const XMMATRIX projection = projectionMatrix(lens, aspect);
    const XMMATRIX viewProjection = XMMatrixMultiply(view, projection);
    return CameraMatrices{storeMat4(view), storeMat4(projection), storeMat4(viewProjection),
                          storeMat4(XMMatrixInverse(nullptr, viewProjection))};
}

/// @brief Where a world position appears on screen.
/// @param world    The position.
/// @param matrices The camera's matrices.
/// @param viewport The (D3D-form) viewport.
/// @return (pixel x, pixel y, depth), or `std::nullopt` if the point is behind the camera (or on its plane). Points
///         outside the viewport are returned too: check the bounds if only visible points matter.
[[nodiscard]] inline std::optional<Vec3> worldToScreen(const Vec3& world, const CameraMatrices& matrices,
                                                       const Viewport& viewport) noexcept
{
    using namespace DirectX;
    const XMVECTOR clip = XMVector4Transform(XMVectorSetW(load(world), 1.0F), load(matrices.viewProjection));
    const float w = XMVectorGetW(clip);
    if (w <= 0.0F)
    {
        return std::nullopt;
    }
    const Vec3 ndc = storeVec3(XMVectorScale(clip, 1.0F / w));
    return ndcToScreen(viewport, ndc);
}

/// @brief The world position at a pixel and depth: the inverse of worldToScreen().
/// @param screen   (pixel x, pixel y, depth); depth must be > 0 for a perspective camera (0 is infinitely far).
///                 Depth 1 is on the near plane: use it to start a picking ray (rays come with Ray.h).
/// @param matrices The camera's matrices.
/// @param viewport The viewport the pixel is in.
/// @return The world position.
[[nodiscard]] inline Vec3 screenToWorld(const Vec3& screen, const CameraMatrices& matrices,
                                        const Viewport& viewport) noexcept
{
    const Vec3 ndc{(screen.x - viewport.x) / viewport.width * 2.0F - 1.0F,
                   1.0F - (screen.y - viewport.y) / viewport.height * 2.0F,
                   (screen.z - viewport.minDepth) / (viewport.maxDepth - viewport.minDepth)};
    return storeVec3(DirectX::XMVector3TransformCoord(load(ndc), load(matrices.inverseViewProjection)));
}
} // namespace Meow2::Math
