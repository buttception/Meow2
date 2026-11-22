/// @file
/// @brief The camera's icon (CameraIcons.h).

#include "Editing/CameraIcons.h"

#include <Meow2/Scene/Camera.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

namespace Meow2::Editor
{
IconShape cameraIconShape(const Math::CameraLens& lens, bool main, float aspect)
{
    IconShape shape;
    shape.emphasised = main;
    shape.centre = {0.0F, 0.0F, -0.35F}; // the middle of body and lens
    aspect = std::clamp(aspect, 0.25F, 4.0F);
    const bool perspective = lens.type == Math::ProjectionType::Perspective;

    // The body: a box behind the lens, its twelve edges (corners differing in one bit).
    std::array<Math::Vec3, 8> body{};
    for (int i = 0; i < 8; ++i)
    {
        body[static_cast<std::size_t>(i)] = {(i & 1) != 0 ? 0.35F : -0.35F, (i & 2) != 0 ? 0.25F : -0.25F,
                                             (i & 4) != 0 ? -0.2F : -1.0F};
    }
    for (int i = 0; i < 8; ++i)
    {
        for (const int bit : {1, 2, 4})
        {
            if ((i & bit) == 0)
            {
                shape.edges.emplace_back(body[static_cast<std::size_t>(i)], body[static_cast<std::size_t>(i | bit)]);
            }
        }
    }
    // The lens: a small opening at the body's front widening to the field of view, so a wide lens looks wide (a fixed
    // shape for orthographic cameras, which have none).
    const float halfHeight = perspective ? std::clamp(std::tan(lens.verticalFov * 0.5F) * 0.5F, 0.05F, 0.6F) : 0.25F;
    const float halfWidth = halfHeight * aspect;
    std::array<Math::Vec3, 4> back{};
    std::array<Math::Vec3, 4> front{};
    for (int i = 0; i < 4; ++i)
    {
        const float sx = (i & 1) != 0 ? 1.0F : -1.0F;
        const float sy = (i & 2) != 0 ? 1.0F : -1.0F;
        back[static_cast<std::size_t>(i)] = {sx * 0.12F, sy * 0.1F, -0.2F};
        front[static_cast<std::size_t>(i)] = {sx * halfWidth, sy * halfHeight, 0.3F};
    }
    constexpr std::array<std::pair<std::size_t, std::size_t>, 4> c_rectangle{{{0, 1}, {1, 3}, {3, 2}, {2, 0}}};
    for (const auto& [a, b] : c_rectangle)
    {
        shape.edges.emplace_back(back[a], back[b]);
        shape.edges.emplace_back(front[a], front[b]);
    }
    for (std::size_t i = 0; i < 4; ++i)
    {
        shape.edges.emplace_back(back[i], front[i]);
    }
    // Up: a filled triangle on the body's top, so an upside-down camera shows.
    shape.fills.push_back(
        {Math::Vec3{-0.15F, 0.3F, -0.6F}, Math::Vec3{0.15F, 0.3F, -0.6F}, Math::Vec3{0.0F, 0.5F, -0.6F}});

    // What it sees, in metres: the near plane out to c_cameraFrustumLength (an orthographic camera's far plane if
    // nearer).
    const float nearZ = std::max(lens.nearZ, 0.0F);
    const float farZ = perspective ? c_cameraFrustumLength : std::min(lens.farZ, c_cameraFrustumLength);
    std::array<Math::Vec3, 8> volume{};
    for (int i = 0; i < 8; ++i)
    {
        const float z = (i & 4) != 0 ? farZ : nearZ;
        const float h = perspective ? std::tan(lens.verticalFov * 0.5F) * z : lens.orthographicHeight * 0.5F;
        const float w = h * aspect;
        volume[static_cast<std::size_t>(i)] = {(i & 1) != 0 ? w : -w, (i & 2) != 0 ? h : -h, z};
    }
    for (int i = 0; i < 8; ++i)
    {
        for (const int bit : {1, 2, 4})
        {
            if ((i & bit) == 0)
            {
                shape.selectedLines.emplace_back(volume[static_cast<std::size_t>(i)],
                                                 volume[static_cast<std::size_t>(i | bit)]);
            }
        }
    }
    return shape;
}

void registerCameraIcon(EntityIcons& icons)
{
    icons.add<Scene::Camera>(
        "Camera",
        [](Scene::Scene& scene, entt::entity entity, const IconContext& context) -> std::optional<IconShape>
        {
            const std::optional<entt::entity> main = Scene::findMainCamera(scene);
            return cameraIconShape(scene.registry().get<Scene::Camera>(entity).lens, main == entity, context.aspect);
        });
}
} // namespace Meow2::Editor
