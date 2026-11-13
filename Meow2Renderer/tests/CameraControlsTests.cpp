/// @file
/// @brief The Renderer's orbit, fly and glTF cameras.

#include "CameraControls.h"

#include <Meow2/Math/Quaternion.h>
#include <Meow2/Math/Scalar.h>

#include <gtest/gtest.h>

#include <cmath>

namespace Meow2::RendererApp
{
namespace
{
Math::Vec3 forwardOf(const Math::Transform& camera)
{
    return Math::rotate(camera.rotation, Math::Vec3{0.0F, 0.0F, 1.0F});
}

void expectNear(const Math::Vec3& actual, const Math::Vec3& expected, float tolerance = 1e-4F)
{
    EXPECT_NEAR(actual.x, expected.x, tolerance);
    EXPECT_NEAR(actual.y, expected.y, tolerance);
    EXPECT_NEAR(actual.z, expected.z, tolerance);
}
} // namespace

TEST(CameraControls, OrbitLooksAtItsTarget)
{
    OrbitCamera orbit;
    orbit.target = {1.0F, 2.0F, 3.0F};
    orbit.distance = 4.0F;
    orbit.spin = false;
    orbit.pitch = 0.0F;
    orbit.yaw = Math::c_pi; // looking towards -Z, so it stands on +Z of the target

    Math::Transform camera = orbitTransform(orbit);
    expectNear(camera.position, {1.0F, 2.0F, 7.0F});
    expectNear(forwardOf(camera), {0.0F, 0.0F, -1.0F});

    // Dragging and zooming keep it aimed at the target.
    updateOrbit(orbit, {.mouseDelta = {120.0F, -40.0F}, .wheel = 2.0F, .leftDrag = true}, 1.0F / 60.0F);
    camera = orbitTransform(orbit);
    const Math::Vec3 toTarget{orbit.target.x - camera.position.x, orbit.target.y - camera.position.y,
                              orbit.target.z - camera.position.z};
    const float length = std::sqrt(toTarget.x * toTarget.x + toTarget.y * toTarget.y + toTarget.z * toTarget.z);
    EXPECT_NEAR(length, 4.0F * 0.81F, 1e-4F) << "two wheel notches closer";
    expectNear(forwardOf(camera), {toTarget.x / length, toTarget.y / length, toTarget.z / length});
}

TEST(CameraControls, OrbitSpinsByItself)
{
    OrbitCamera orbit;
    const float before = orbit.yaw;
    updateOrbit(orbit, {}, 1.0F);
    EXPECT_NE(orbit.yaw, before);
    orbit.spin = false;
    const float stopped = orbit.yaw;
    updateOrbit(orbit, {}, 1.0F);
    EXPECT_EQ(orbit.yaw, stopped);
}

TEST(CameraControls, FlyMovesWhereItLooks)
{
    FlyCamera fly;
    fly.position = {0.0F, 0.0F, 0.0F};
    fly.yaw = Math::c_pi / 2.0F; // looking towards +X
    fly.speed = 2.0F;

    updateFly(fly, {.move = {0.0F, 0.0F, 1.0F}}, 0.5F); // W for half a second
    expectNear(fly.position, {1.0F, 0.0F, 0.0F});
    updateFly(fly, {.move = {1.0F, 0.0F, 0.0F}}, 0.5F); // D: to its right, -Z when facing +X (left-handed)
    expectNear(fly.position, {1.0F, 0.0F, -1.0F});
    updateFly(fly, {.move = {0.0F, 1.0F, 0.0F}, .fast = true}, 0.5F); // E with Shift: up, four times as fast
    expectNear(fly.position, {1.0F, 4.0F, -1.0F});
    expectNear(forwardOf(flyTransform(fly)), {1.0F, 0.0F, 0.0F});

    updateFly(fly, {.mouseDelta = {0.0F, -100.0F}, .rightDrag = true}, 0.0F); // mouse up looks up
    EXPECT_GT(forwardOf(flyTransform(fly)).y, 0.3F);
    updateFly(fly, {.mouseDelta = {0.0F, 100000.0F}, .rightDrag = true}, 0.0F);
    EXPECT_GE(fly.pitch, -1.5F) << "never past straight down";
}

TEST(CameraControls, SceneCameraLooksAlongItsNodesMinusZ)
{
    // A glTF camera node at (0, 1, -5) with no rotation looks along glTF's -Z; the importer keeps Z, so in the engine
    // it looks towards -Z too, with +X still on its right.
    Math::Transform node;
    node.position = {0.0F, 1.0F, -5.0F};
    const Math::Transform camera = sceneCameraTransform(Math::toMat4(node));
    expectNear(camera.position, {0.0F, 1.0F, -5.0F});
    expectNear(forwardOf(camera), {0.0F, 0.0F, -1.0F});
    expectNear(Math::rotate(camera.rotation, Math::Vec3{1.0F, 0.0F, 0.0F}), {-1.0F, 0.0F, 0.0F});

    Assets::ImportedCamera ortho;
    ortho.perspective = false;
    ortho.halfHeight = 3.0F;
    const Math::CameraLens lens = sceneCameraLens(ortho, 10.0F);
    EXPECT_EQ(lens.type, Math::ProjectionType::Orthographic);
    EXPECT_FLOAT_EQ(lens.orthographicHeight, 6.0F);
    EXPECT_GT(lens.farZ, lens.nearZ) << "an orthographic camera without zfar still gets a far plane";
}

TEST(CameraControls, FrameSceneLooksAtTheCentre)
{
    const Math::Vec3 center{1.0F, 2.0F, 3.0F};
    constexpr float c_radius = 2.0F;
    const Math::Transform camera = orbitTransform(frameScene(center, c_radius));

    // From the front (+Z side, a little above), aimed at the centre.
    EXPECT_GT(camera.position.z, center.z);
    EXPECT_GT(camera.position.y, center.y);
    const Math::Vec3 toCentre{center.x - camera.position.x, center.y - camera.position.y, center.z - camera.position.z};
    const float distance = std::sqrt(toCentre.x * toCentre.x + toCentre.y * toCentre.y + toCentre.z * toCentre.z);
    expectNear(forwardOf(camera), {toCentre.x / distance, toCentre.y / distance, toCentre.z / distance});

    // 1.7 radii away: the bounding sphere is larger than most models, so this shows a model whole and fills the view.
    EXPECT_NEAR(distance, c_radius * 1.7F, 1e-4F);
    const Math::CameraLens lens = sceneLens(c_radius);
    EXPECT_LT(lens.nearZ, distance - c_radius) << "nothing of it is clipped by the near plane";
}
} // namespace Meow2::RendererApp
