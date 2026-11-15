#pragma once

/// @file
/// @brief The Camera component: an entity the game is seen through, and how to find the main one.

#include <Meow2/Math/Camera.h>
#include <Meow2/Math/Transform.h>
#include <Meow2/Scene/Scene.h>

#include <optional>

namespace Meow2::Scene
{
/// @brief Makes an entity a camera: the game can be seen through it, from its WorldTransform. Plain data.
///
/// The camera looks along its entity's +Z with +Y up (left-handed, techstack.md §10). Scale in the world transform is
/// ignored. One camera is the scene's **main camera**, the one the editor's game viewport and Meow2Player show.
struct Camera
{
    Math::CameraLens lens{}; ///< Projection: perspective (field of view, near plane) or orthographic.
    bool main = false;       ///< The one the game is seen through; if no camera is main, the first one found is.
};

/// @brief The scene's main camera: the one marked `main` (the first such, if several are), or else any camera.
/// @param scene The scene; Camera must be registered.
/// @return The entity, or nothing if the scene has no camera.
[[nodiscard]] std::optional<entt::entity> findMainCamera(Scene& scene);

/// @brief Where a camera entity is and which way it looks, from its WorldTransform (scale dropped).
/// @param scene  The scene.
/// @param camera The camera entity.
/// @return Its transform, or nothing if it has no world transform (or a degenerate one).
[[nodiscard]] std::optional<Math::Transform> cameraTransform(Scene& scene, entt::entity camera);
} // namespace Meow2::Scene
