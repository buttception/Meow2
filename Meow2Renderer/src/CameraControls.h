/// @file
/// @brief Meow2Renderer's cameras: orbit around the scene or fly through it (the engine's rigs, Math/CameraRig.h), or
///        look through one of the glTF file's cameras, optionally moved by one of its animations.

#pragma once

#include "UiTheme.h"

#include <Meow2/Assets/Gltf/GltfImporter.h>
#include <Meow2/Math/Camera.h>
#include <Meow2/Math/CameraRig.h>
#include <Meow2/Math/Transform.h>
#include <Meow2/Math/Types.h>

#include <cstdint>
#include <string>
#include <vector>

namespace Meow2::RendererApp
{
/// @brief How the camera is driven.
enum class CameraMode : std::uint8_t
{
    Orbit,      ///< Circles the scene's centre: drag to turn, wheel to zoom, middle-drag to pan; spins by itself.
    Fly,        ///< Noclip: WASD to move, Q and E down and up, right-drag to look, Shift to go faster.
    SceneCamera ///< One of the glTF file's cameras.
};

// The orbit and fly rigs live in the engine (Meow2/Math/CameraRig.h), shared with the editor's viewport.
using Math::CameraInput;
using Math::FlyCamera;
using Math::flyTransform;
using Math::frameScene;
using Math::lookDirection;
using Math::OrbitCamera;
using Math::orbitTransform;
using Math::sceneLens;
using Math::updateFly;
using Math::updateOrbit;

/// @brief The pose of a glTF camera from its node's world matrix.
///
/// glTF cameras look along their node's −Z with +X right. The importer mirrors X, so in the engine the node's −Z is
/// still the view direction and its −X is right: a half turn about the node's Y makes it an engine camera (+Z
/// forward, +X right).
/// @param nodeWorld The camera node's WorldTransform matrix.
/// @return Position and rotation (scale dropped).
[[nodiscard]] Math::Transform sceneCameraTransform(const Math::Mat4& nodeWorld);

/// @brief A glTF camera's lens for the renderer: perspective with infinite reversed depth, or orthographic.
/// @param camera      The camera.
/// @param sceneRadius The scene's size, for an orthographic camera without a far plane.
/// @return The lens.
[[nodiscard]] Math::CameraLens sceneCameraLens(const Assets::ImportedCamera& camera, float sceneRadius);

/// @brief A glTF camera in the open scene: what it's called and which node carries it. Plain data.
struct SceneCameraEntry
{
    std::string name{};            ///< Its name, or "Camera n".
    std::int32_t node = -1;        ///< Index into the model's nodes.
    Assets::ImportedCamera lens{}; ///< Its lens.
};

/// @brief What the Camera window chose this frame. Plain data.
struct CameraWindowState
{
    CameraMode mode = CameraMode::Orbit; ///< How the camera is driven.
    int sceneCamera = 0;                 ///< Index into the scene's cameras, in CameraMode::SceneCamera.
    int animation = -1;                  ///< Index into the scene's animations; -1 for none.
    bool playing = true;                 ///< The animation advances.
    bool loop = true;                    ///< Starts over at its end.
    float animationTime = 0.0F;          ///< Seconds into it.
};

/// @brief Draws the Camera window: the mode, the glTF cameras, the animation player, the orbit and fly settings.
/// @param state      Edited in place.
/// @param cameras    The scene's cameras.
/// @param animations The scene's animations (names and lengths).
/// @param orbit      The orbit camera, for its spin toggle.
/// @param fly        The fly camera, for its speed.
/// @param fonts      The theme's fonts.
void drawCameraWindow(CameraWindowState& state, const std::vector<SceneCameraEntry>& cameras,
                      const std::vector<Assets::ImportedAnimation>& animations, OrbitCamera& orbit, FlyCamera& fly,
                      const UiFonts& fonts = {});
} // namespace Meow2::RendererApp
