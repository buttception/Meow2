/// @file
/// @brief Camera rigs (CameraRig.h).

#include "Meow2/Math/CameraRig.h"

#include "Meow2/Math/Quaternion.h"
#include "Meow2/Math/Scalar.h"

#include <algorithm>
#include <cmath>

namespace Meow2::Math
{
namespace
{
constexpr float c_turnPerPixel = 0.005F; // radians per pixel of mouse drag
constexpr float c_pitchLimit = 1.5F;     // just short of straight up or down, where yaw has no meaning
constexpr float c_spinPerSecond = 0.2F;  // the orbit's own turn, radians per second
constexpr float c_zoomPerNotch = 0.9F;   // distance factor per wheel notch
constexpr float c_speedPerNotch = 1.25F; // fly speed factor per wheel notch

Vec3 add(const Vec3& a, const Vec3& b, float scale = 1.0F)
{
    return {a.x + b.x * scale, a.y + b.y * scale, a.z + b.z * scale};
}

/// @brief The camera's right, for a yaw (horizontal: flying sideways doesn't climb).
Vec3 rightOf(float yaw)
{
    return {std::cos(yaw), 0.0F, -std::sin(yaw)};
}

/// @brief The yaw and pitch of the direction a transform looks in (its +Z).
void yawPitchOf(const Transform& camera, float& yaw, float& pitch)
{
    const Vec3 forward = rotate(camera.rotation, Vec3{0.0F, 0.0F, 1.0F});
    yaw = std::atan2(forward.x, forward.z);
    pitch = std::asin(std::clamp(forward.y, -1.0F, 1.0F));
}
} // namespace

Vec3 lookDirection(float yaw, float pitch)
{
    return {std::cos(pitch) * std::sin(yaw), std::sin(pitch), std::cos(pitch) * std::cos(yaw)};
}

void updateOrbit(OrbitCamera& camera, const CameraInput& input, float deltaSeconds)
{
    if (camera.spin)
    {
        camera.yaw += c_spinPerSecond * deltaSeconds;
    }
    if (input.leftDrag)
    {
        // Dragging right moves the camera right around the target: the view direction turns left.
        camera.yaw -= input.mouseDelta.x * c_turnPerPixel;
        camera.pitch = std::clamp(camera.pitch + input.mouseDelta.y * c_turnPerPixel, -c_pitchLimit, c_pitchLimit);
    }
    if (input.middleDrag)
    {
        // Panning moves the target with the mouse, at a speed that keeps it under the cursor at about this distance.
        const float scale = camera.distance * 0.0015F;
        const Vec3 forward = lookDirection(camera.yaw, camera.pitch);
        const Vec3 right = rightOf(camera.yaw);
        const Vec3 up{forward.y * right.z - forward.z * right.y, forward.z * right.x - forward.x * right.z,
                      forward.x * right.y - forward.y * right.x};
        camera.target = add(camera.target, right, -input.mouseDelta.x * scale);
        camera.target = add(camera.target, up, input.mouseDelta.y * scale);
    }
    if (input.wheel != 0.0F)
    {
        camera.distance = std::max(camera.distance * std::pow(c_zoomPerNotch, input.wheel), 0.001F);
    }
    camera.yaw = wrapAngle(camera.yaw);
}

Transform orbitTransform(const OrbitCamera& camera)
{
    const Vec3 forward = lookDirection(camera.yaw, camera.pitch);
    Transform transform;
    transform.position = add(camera.target, forward, -camera.distance);
    transform.rotation = lookRotation(forward);
    return transform;
}

OrbitCamera frameScene(const Vec3& center, float radius)
{
    OrbitCamera camera;
    camera.target = center;
    camera.distance = radius * 1.7F;
    return camera;
}

CameraLens sceneLens(float radius)
{
    CameraLens lens{};
    lens.nearZ = std::clamp(radius * 0.01F, 1e-6F, 0.1F); // reversed infinite Z keeps precision at any near plane
    return lens;
}

void updateFly(FlyCamera& camera, const CameraInput& input, float deltaSeconds)
{
    if (input.rightDrag)
    {
        camera.yaw = wrapAngle(camera.yaw + input.mouseDelta.x * c_turnPerPixel);
        camera.pitch = std::clamp(camera.pitch - input.mouseDelta.y * c_turnPerPixel, -c_pitchLimit, c_pitchLimit);
    }
    if (input.wheel != 0.0F)
    {
        camera.speed = std::clamp(camera.speed * std::pow(c_speedPerNotch, input.wheel), 0.01F, 10000.0F);
    }
    const float step = camera.speed * (input.fast ? 4.0F : 1.0F) * deltaSeconds;
    // Forward goes where the camera looks (pitch included), sideways stays level, and up is the world's up.
    camera.position = add(camera.position, lookDirection(camera.yaw, camera.pitch), input.move.z * step);
    camera.position = add(camera.position, rightOf(camera.yaw), input.move.x * step);
    camera.position = add(camera.position, Vec3{0.0F, 1.0F, 0.0F}, input.move.y * step);
}

Transform flyTransform(const FlyCamera& camera)
{
    Transform transform;
    transform.position = camera.position;
    transform.rotation = lookRotation(lookDirection(camera.yaw, camera.pitch));
    return transform;
}

FlyCamera flyFrom(const Transform& camera, float speed)
{
    FlyCamera fly;
    fly.position = camera.position;
    yawPitchOf(camera, fly.yaw, fly.pitch);
    fly.pitch = std::clamp(fly.pitch, -c_pitchLimit, c_pitchLimit);
    fly.speed = speed;
    return fly;
}

OrbitCamera orbitFrom(const Transform& camera, float distance)
{
    OrbitCamera orbit;
    yawPitchOf(camera, orbit.yaw, orbit.pitch);
    orbit.pitch = std::clamp(orbit.pitch, -c_pitchLimit, c_pitchLimit);
    orbit.distance = std::max(distance, 0.001F);
    orbit.target = add(camera.position, lookDirection(orbit.yaw, orbit.pitch), orbit.distance);
    orbit.spin = false;
    return orbit;
}
} // namespace Meow2::Math
