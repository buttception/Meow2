/// @file
/// @brief The debug overlays' maths, on the CPU: the frustum's lines and the grid's constants.

#include "DebugLines.h"
#include "InfiniteGrid.h"

#include <Meow2/Math/Camera.h>

#include <DirectXMath.h>
#include <gtest/gtest.h>

#include <cmath>

namespace Meow2::RendererApp
{
namespace
{
/// @brief A camera at (1, 2, 3) looking along +Z, square, 90° vertical field of view, near plane 0.5.
Shader::FrameConstants testCamera()
{
    Math::Transform camera{};
    camera.position = {1.0F, 2.0F, 3.0F};
    Math::CameraLens lens{};
    lens.verticalFov = Math::toRadians(90.0F);
    lens.nearZ = 0.5F;
    const Math::CameraMatrices matrices = Math::cameraMatrices(camera, lens, 1.0F);
    Shader::FrameConstants frame{};
    frame.view = matrices.view;
    frame.projection = matrices.projection;
    frame.viewProjection = matrices.viewProjection;
    frame.cameraPosition = {1.0F, 2.0F, 3.0F, 0.0F};
    return frame;
}

void expectNear(const Math::Vec3& actual, const Math::Vec3& expected)
{
    EXPECT_NEAR(actual.x, expected.x, 1e-3F);
    EXPECT_NEAR(actual.y, expected.y, 1e-3F);
    EXPECT_NEAR(actual.z, expected.z, 1e-3F);
}
} // namespace

TEST(DebugOverlays, FrustumLinesReachTheChosenDistance)
{
    const Math::Color yellow{.r = 1.0F, .g = 1.0F, .b = 0.0F, .a = 1.0F};
    const std::array<DebugLine, 12> lines = frustumLines(testCamera(), 10.0F, yellow);
    // 90° and square: at distance d the far rectangle's corners are d to each side.
    expectNear(lines[0].from, {1.0F - 0.5F, 2.0F - 0.5F, 3.5F});
    expectNear(lines[4].from, {1.0F - 10.0F, 2.0F - 10.0F, 13.0F});
    expectNear(lines[6].from, {1.0F + 10.0F, 2.0F + 10.0F, 13.0F});
    for (std::size_t i = 8; i < 12; ++i)
    {
        expectNear(lines[i].from, {1.0F, 2.0F, 3.0F});
        EXPECT_NEAR(lines[i].to.z, 13.0F, 1e-3F) << "and end on the far rectangle";
    }
    for (const DebugLine& line : lines)
    {
        EXPECT_EQ(line.color.b, 0.0F);
    }
    // Each rectangle is closed: every edge ends where the next starts.
    for (std::size_t i = 0; i < 4; ++i)
    {
        expectNear(lines[i].to, lines[(i + 1) % 4].from);
        expectNear(lines[4 + i].to, lines[4 + (i + 1) % 4].from);
    }
}

TEST(DebugOverlays, GridConstantsInvertTheCamera)
{
    using namespace DirectX;
    const Shader::FrameConstants frame = testCamera();
    const GridConstants constants = InfiniteGrid::constantsFor(frame, 640, 480);
    EXPECT_EQ(constants.width, 640U);
    EXPECT_EQ(constants.height, 480U);
    EXPECT_EQ(constants.camera.x, 1.0F);
    EXPECT_EQ(constants.camera.y, 2.0F);
    EXPECT_EQ(constants.camera.z, 3.0F);
    const XMMATRIX product =
        XMMatrixMultiply(XMLoadFloat4x4(&constants.viewProjection), XMLoadFloat4x4(&constants.inverseViewProjection));
    Math::Mat4 identity{};
    XMStoreFloat4x4(&identity, product);
    for (int row = 0; row < 4; ++row)
    {
        for (int column = 0; column < 4; ++column)
        {
            EXPECT_NEAR(identity.m[row][column], row == column ? 1.0F : 0.0F, 1e-4F) << row << ", " << column;
        }
    }
}
} // namespace Meow2::RendererApp
