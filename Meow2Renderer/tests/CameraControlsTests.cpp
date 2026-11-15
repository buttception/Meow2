/// @file
/// @brief The Renderer's glTF cameras (the orbit and fly rigs are tested with the engine: Math.CameraRig.*).

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

} // namespace Meow2::RendererApp
