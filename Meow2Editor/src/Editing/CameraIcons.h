#pragma once

/// @file
/// @brief The camera's icon in the editor viewport (an EntityIcons kind): a wireframe camera, and what it sees when
///        selected.

#include "Editing/EntityIcons.h"

#include <Meow2/Math/Camera.h>

namespace Meow2::Editor
{
/// @brief How far a selected camera's view volume is drawn, in metres (a perspective camera has no far plane).
inline constexpr float c_cameraFrustumLength = 10.0F;

/// @brief A camera's icon: a box for the body, the lens opening out at the camera's field of view and `aspect`, a
///        filled triangle on top saying which way is up; its view volume, from the near plane out to
///        c_cameraFrustumLength, for when it's selected.
/// @param lens   The camera's projection.
/// @param main   The game's main camera (drawn brighter).
/// @param aspect The width / height it's seen at (the game viewport's).
/// @return The shape.
[[nodiscard]] IconShape cameraIconShape(const Math::CameraLens& lens, bool main, float aspect);

/// @brief Adds the camera kind: every entity with a `Scene::Camera`.
/// @param icons Where to add it.
void registerCameraIcon(EntityIcons& icons);
} // namespace Meow2::Editor
