/// @file
/// @brief Meow2Renderer's cameras until the editor's camera rigs exist: orbit around the scene, fly through it
///        (noclip), or look through one of the glTF file's cameras, optionally moved by one of its animations.

#pragma once

#include "UiTheme.h"

#include <Meow2/Assets/Gltf/GltfImporter.h>
#include <Meow2/Math/Camera.h>
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

/// @brief What the user did this frame, from the input system. Plain data, so the cameras can be tested without it.
struct CameraInput
{
    Math::Vec2 mouseDelta{0.0F, 0.0F}; ///< Pixels the mouse moved (+x right, +y down).
    float wheel = 0.0F;                ///< Wheel notches (+ away from the user).
    bool leftDrag = false;             ///< Left button held: orbit turns.
    bool middleDrag = false;           ///< Middle button held: orbit pans.
    bool rightDrag = false;            ///< Right button held: fly looks around.
    Math::Vec3 move{0.0F, 0.0F, 0.0F}; ///< Fly: -1..1 per axis: x right, y up, z forward (WASD, E/Q).
    bool fast = false;                 ///< Shift: fly four times as fast.
};

/// @brief An orbiting camera. Plain data.
struct OrbitCamera
{
    Math::Vec3 target{0.0F, 0.0F, 0.0F}; ///< What it circles and looks at.
    float yaw = 3.14159265F;             ///< Radians around +Y of the direction it looks in; π looks towards −Z.
    float pitch = -0.245F;               ///< Radians above (+) or below (−) the horizon it looks; ±1.5 at most.
    float distance = 5.0F;               ///< From the target, in world units.
    bool spin = true;                    ///< Turns by itself, slowly.
};

/// @brief A free-flying camera. Plain data.
struct FlyCamera
{
    Math::Vec3 position{0.0F, 0.0F, -5.0F}; ///< Where it is.
    float yaw = 0.0F;                       ///< Radians around +Y of the direction it looks in; 0 looks towards +Z.
    float pitch = 0.0F;                     ///< Radians above (+) or below (−) the horizon; ±1.5 at most.
    float speed = 2.0F;                     ///< World units per second (four times this with Shift).
};

/// @brief The unit direction for a yaw and pitch (left-handed: yaw 0 is +Z, π/2 is +X; positive pitch looks up).
/// @param yaw   Radians around +Y.
/// @param pitch Radians above the horizon.
/// @return The direction.
[[nodiscard]] Math::Vec3 lookDirection(float yaw, float pitch);

/// @brief Applies a frame's input to an orbiting camera (and its spin).
/// @param camera       The camera.
/// @param input        The frame's input.
/// @param deltaSeconds The frame's length.
void updateOrbit(OrbitCamera& camera, const CameraInput& input, float deltaSeconds);

/// @brief Where an orbiting camera is and which way it looks.
/// @param camera The camera.
/// @return Its transform (scale 1).
[[nodiscard]] Math::Transform orbitTransform(const OrbitCamera& camera);

/// @brief Applies a frame's input to a flying camera.
/// @param camera       The camera.
/// @param input        The frame's input.
/// @param deltaSeconds The frame's length.
void updateFly(FlyCamera& camera, const CameraInput& input, float deltaSeconds);

/// @brief Where a flying camera is and which way it looks.
/// @param camera The camera.
/// @return Its transform (scale 1).
[[nodiscard]] Math::Transform flyTransform(const FlyCamera& camera);

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
