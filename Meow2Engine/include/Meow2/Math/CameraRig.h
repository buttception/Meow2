#pragma once

/// @file
/// @brief Camera rigs: an orbit around a point and a free-flying (noclip) camera, driven by plain input data. Used by
///        Meow2Renderer and the editor's viewport; the rigs know nothing of the input system or the window.

#include <Meow2/Math/Camera.h>
#include <Meow2/Math/Transform.h>
#include <Meow2/Math/Types.h>

namespace Meow2::Math
{
/// @brief What the user did this frame, from the input system. Plain data, so the rigs can be tested without it.
struct CameraInput
{
    Vec2 mouseDelta{0.0F, 0.0F}; ///< Pixels the mouse moved (+x right, +y down).
    float wheel = 0.0F;          ///< Wheel notches (+ away from the user).
    bool leftDrag = false;       ///< Left button held: orbit turns.
    bool middleDrag = false;     ///< Middle button held: orbit pans.
    bool rightDrag = false;      ///< Right button held: fly looks around.
    Vec3 move{0.0F, 0.0F, 0.0F}; ///< Fly: -1..1 per axis: x right, y up, z forward (WASD, E/Q).
    bool fast = false;           ///< Shift: fly four times as fast.
};

/// @brief An orbiting camera. Plain data.
struct OrbitCamera
{
    Vec3 target{0.0F, 0.0F, 0.0F}; ///< What it circles and looks at.
    float yaw = 3.14159265F;       ///< Radians around +Y of the direction it looks in; π looks towards −Z.
    float pitch = -0.245F;         ///< Radians above (+) or below (−) the horizon it looks; ±1.5 at most.
    float distance = 5.0F;         ///< From the target, in world units.
    bool spin = true;              ///< Turns by itself, slowly.
};

/// @brief A free-flying camera. Plain data.
struct FlyCamera
{
    Vec3 position{0.0F, 0.0F, -5.0F}; ///< Where it is.
    float yaw = 0.0F;                 ///< Radians around +Y of the direction it looks in; 0 looks towards +Z.
    float pitch = 0.0F;               ///< Radians above (+) or below (−) the horizon; ±1.5 at most.
    float speed = 2.0F;               ///< World units per second (four times this with Shift).
};

/// @brief The unit direction for a yaw and pitch (left-handed: yaw 0 is +Z, π/2 is +X; positive pitch looks up).
/// @param yaw   Radians around +Y.
/// @param pitch Radians above the horizon.
/// @return The direction.
[[nodiscard]] Vec3 lookDirection(float yaw, float pitch);

/// @brief Applies a frame's input to an orbiting camera (and its spin).
/// @param camera       The camera.
/// @param input        The frame's input.
/// @param deltaSeconds The frame's length.
void updateOrbit(OrbitCamera& camera, const CameraInput& input, float deltaSeconds);

/// @brief Where an orbiting camera is and which way it looks.
/// @param camera The camera.
/// @return Its transform (scale 1).
[[nodiscard]] Transform orbitTransform(const OrbitCamera& camera);

/// @brief The orbit a scene opens with: around its centre from +Z (glTF models face +Z, so this is the front),
///        far enough to see all of it. Golden images use it too, so they show what the Renderer shows first.
/// @param center The centre of a sphere around the scene.
/// @param radius Its radius; > 0.
/// @return The orbit, spinning.
[[nodiscard]] OrbitCamera frameScene(const Vec3& center, float radius);

/// @brief The lens for a scene of a given size: the near plane follows it, so small models aren't clipped and big
///        ones keep depth precision.
/// @param radius The radius of a sphere around the scene.
/// @return The default lens with that near plane.
[[nodiscard]] CameraLens sceneLens(float radius);

/// @brief Applies a frame's input to a flying camera.
/// @param camera       The camera.
/// @param input        The frame's input.
/// @param deltaSeconds The frame's length.
void updateFly(FlyCamera& camera, const CameraInput& input, float deltaSeconds);

/// @brief Where a flying camera is and which way it looks.
/// @param camera The camera.
/// @return Its transform (scale 1).
[[nodiscard]] Transform flyTransform(const FlyCamera& camera);

/// @brief A flying camera at another camera's place, looking the same way, so switching rigs doesn't jump.
/// @param camera Where the camera is and which way it looks (roll is dropped: the fly camera has none).
/// @param speed  The fly camera's speed.
/// @return The fly camera.
[[nodiscard]] FlyCamera flyFrom(const Transform& camera, float speed);

/// @brief An orbit around the point `distance` in front of a camera, seen from where the camera is, so switching
///        rigs doesn't jump. It doesn't spin.
/// @param camera   Where the camera is and which way it looks.
/// @param distance How far in front the orbit's target is; > 0.
/// @return The orbit.
[[nodiscard]] OrbitCamera orbitFrom(const Transform& camera, float distance);
} // namespace Meow2::Math
