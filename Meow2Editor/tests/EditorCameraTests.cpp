/// @file
/// @brief Tests for EditorCamera: orbit, flying on the right button, modes and framing.

#include "Viewports/EditorCamera.h"

#include <Meow2/Core/Events/EventBus.h>
#include <Meow2/Core/Memory/GlobalTracking.h>
#include <Meow2/Math/Quaternion.h>
#include <Meow2/Platform/Input/InputSystem.h>

#include <gtest/gtest.h>

#include <cmath>

namespace
{
using Meow2::Editor::CameraMode;
using Meow2::Editor::EditorCamera;
namespace Math = Meow2::Math;

Math::Vec3 forwardOf(const Math::Transform& camera)
{
    return Math::rotate(camera.rotation, Math::Vec3{0.0F, 0.0F, 1.0F});
}

float distance(const Math::Vec3& a, const Math::Vec3& b)
{
    return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y) + (a.z - b.z) * (a.z - b.z));
}
} // namespace

TEST(EditorCameraTest, FlyAndOrbit)
{
    EditorCamera camera;
    camera.focus({0.0F, 0.0F, 0.0F}, 2.0F);
    const Math::Transform start = camera.transform();
    // A 2 m sphere fits a 60° view from 2 / sin(30°) = 4 m; with 10 % to spare, 4.4 m.
    EXPECT_NEAR(distance(start.position, {0.0F, 0.0F, 0.0F}), 4.4F, 1e-3F) << "far enough to see all of it";

    // Right drag + W flies forward from where the orbit was, without a jump.
    camera.update({.rightDrag = true, .move = {0.0F, 0.0F, 1.0F}}, 0.0F);
    EXPECT_NEAR(distance(camera.transform().position, start.position), 0.0F, 1e-3F) << "switching doesn't jump";
    camera.update({.rightDrag = true, .move = {0.0F, 0.0F, 1.0F}}, 1.0F);
    const Math::Transform flown = camera.transform();
    EXPECT_LT(distance(flown.position, {0.0F, 0.0F, 0.0F}), 4.4F) << "moved towards what it looks at";

    // Letting go orbits again, around the point ahead, from where it flew to.
    camera.update({}, 0.0F);
    EXPECT_NEAR(distance(camera.transform().position, flown.position), 0.0F, 1e-3F);
    camera.update({.mouseDelta = {100.0F, 0.0F}, .leftDrag = true}, 0.016F);
    EXPECT_GT(distance(camera.transform().position, flown.position), 0.1F) << "left drag turns around the point";
    EXPECT_EQ(camera.mode(), CameraMode::Orbit);
}

TEST(EditorCameraTest, ModesAndFocus)
{
    EditorCamera camera;
    camera.setMode(CameraMode::Fly);
    EXPECT_EQ(camera.mode(), CameraMode::Fly);
    const Math::Transform before = camera.transform();
    camera.update({.move = {1.0F, 0.0F, 0.0F}}, 1.0F); // D, no button needed in Fly mode
    EXPECT_GT(distance(camera.transform().position, before.position), 0.1F);

    camera.focus({10.0F, 0.0F, 0.0F}, 1.0F);
    const Math::Vec3 toCentre{10.0F - camera.transform().position.x, -camera.transform().position.y,
                              -camera.transform().position.z};
    const float length = distance(camera.transform().position, {10.0F, 0.0F, 0.0F});
    const Math::Vec3 forward = forwardOf(camera.transform());
    EXPECT_NEAR(forward.x, toCentre.x / length, 1e-3F) << "framing looks at the centre, in Fly mode too";
    EXPECT_NEAR(forward.z, toCentre.z / length, 1e-3F);
    EXPECT_LT(camera.lens().nearZ, 0.1F) << "the near plane follows the framed size";
    EXPECT_EQ(Meow2::Editor::cameraModeName(CameraMode::Fly), QStringLiteral("fly"));
}

TEST(EditorCameraTest, OrbitNeedsAlt)
{
    // A plain left drag is a click to select (or a gizmo drag, later), not an orbit: only Alt + left orbits.
    namespace Input = Meow2::Input;
    Meow2::Events::EventBus events{Meow2::Memory::globalTracker()};
    Input::InputSystem input{events};
    input.submit(Input::MouseButtonInput{.button = Input::MouseButton::Left, .down = true});
    input.submit(Input::MouseMotionInput{.x = 50.0F, .y = 10.0F, .dx = 40.0F, .dy = 0.0F});
    input.beginFrame();
    const Math::CameraInput plain = Meow2::Editor::cameraInputFrom(input);
    EXPECT_FALSE(plain.leftDrag) << "without Alt";

    EditorCamera camera;
    const Math::Transform before = camera.transform();
    camera.update(plain, 0.016F);
    EXPECT_NEAR(distance(camera.transform().position, before.position), 0.0F, 1e-4F) << "didn't orbit";

    input.submit(Input::KeyInput{.key = Input::Key::LeftAlt, .down = true});
    input.submit(Input::MouseMotionInput{.x = 90.0F, .y = 10.0F, .dx = 40.0F, .dy = 0.0F});
    input.beginFrame();
    const Math::CameraInput withAlt = Meow2::Editor::cameraInputFrom(input);
    EXPECT_TRUE(withAlt.leftDrag);
    camera.update(withAlt, 0.016F);
    EXPECT_GT(distance(camera.transform().position, before.position), 0.01F) << "Alt + left drag orbits";

    input.submit(Input::KeyInput{.key = Input::Key::LeftAlt, .down = false});
    input.submit(Input::KeyInput{.key = Input::Key::RightAlt, .down = true});
    input.beginFrame();
    EXPECT_TRUE(Meow2::Editor::cameraInputFrom(input).leftDrag) << "either Alt";
}
