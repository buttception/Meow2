/// @file
/// @brief The sun's cascades on the CPU: where they split, that each covers its slice, and that small camera moves keep
///        them (and their cached static casters) in place.

#include <Meow2/Graphics/Renderer/ShadowCascades.h>
#include <Meow2/Math/Camera.h>
#include <Meow2/Math/Quaternion.h>

#include <DirectXMath.h>
#include <gtest/gtest.h>

#include <cmath>
#include <vector>

namespace Meow2::Renderer
{
namespace
{
/// @brief A camera at a position, looking along a direction, 60° vertical field of view, 16:9, near plane 0.1.
CascadeCamera cameraAt(const Math::Vec3& position, const Math::Vec3& forward)
{
    Math::Transform transform{};
    transform.position = position;
    transform.rotation = Math::lookRotation(forward);
    const Math::CameraMatrices matrices = Math::cameraMatrices(transform, Math::CameraLens{}, 16.0F / 9.0F);
    return {.view = matrices.view,
            .tanHalfX = 1.0F / matrices.projection._11,
            .tanHalfY = 1.0F / matrices.projection._22,
            .nearZ = matrices.projection._43};
}

/// @brief A sun from up and to one side.
const Math::Vec3 c_sun = []
{
    Math::Vec3 sun{0.3F, 0.8F, 0.2F};
    DirectX::XMStoreFloat3(&sun, DirectX::XMVector3Normalize(Math::load(sun)));
    return sun;
}();

/// @brief A scene 100 m across around the origin.
constexpr Math::Sphere c_scene{.center = {0.0F, 0.0F, 0.0F}, .radius = 50.0F};
} // namespace

TEST(ShadowCascades, SplitsCoverTheRange)
{
    for (const float lambda : {0.0F, 0.5F, 0.8F, 1.0F})
    {
        const auto splits = cascadeSplits(0.1F, 100.0F, 4, lambda);
        EXPECT_FLOAT_EQ(splits[0], 0.1F);
        EXPECT_FLOAT_EQ(splits[4], 100.0F) << "the last cascade ends where shadows do";
        for (std::size_t i = 1; i <= 4; ++i)
        {
            EXPECT_GT(splits[i], splits[i - 1]) << "lambda " << lambda;
        }
    }
    EXPECT_FLOAT_EQ(cascadeSplits(1.0F, 100.0F, 2, 1.0F)[1], 10.0F) << "logarithmic: the geometric middle";
    EXPECT_FLOAT_EQ(cascadeSplits(1.0F, 101.0F, 2, 0.0F)[1], 51.0F) << "uniform: the arithmetic middle";
}

TEST(ShadowCascades, SphereContainsTheSlice)
{
    using namespace DirectX;
    // Whatever way the camera looks, every corner of every slice lies inside its cascade's box.
    for (const Math::Vec3 forward :
         {Math::Vec3{0, 0, 1}, Math::Vec3{1, -0.5F, 0.2F}, Math::Vec3{-0.3F, 0.9F, -1}, Math::Vec3{0.01F, -1, 0.0F}})
    {
        ShadowCascades cascades({.count = 4, .resolution = 1024, .distance = 60.0F});
        const CascadeCamera camera = cameraAt({3.0F, 2.0F, -5.0F}, forward);
        static_cast<void>(cascades.update(camera, c_sun, c_scene));
        const XMMATRIX inverseView = XMMatrixInverse(nullptr, XMLoadFloat4x4(&camera.view));
        for (const ShadowCascade& cascade : cascades.cascades())
        {
            const XMMATRIX viewProjection = XMLoadFloat4x4(&cascade.viewProjection);
            for (const float distance : {cascade.splitNear, cascade.splitFar})
            {
                for (const float sx : {-1.0F, 1.0F})
                {
                    for (const float sy : {-1.0F, 1.0F})
                    {
                        const XMVECTOR corner =
                            XMVector3Transform(XMVectorSet(sx * distance * camera.tanHalfX,
                                                           sy * distance * camera.tanHalfY, distance, 1.0F),
                                               inverseView);
                        const XMVECTOR clip = XMVector3Transform(corner, viewProjection);
                        EXPECT_LE(std::abs(XMVectorGetX(clip)), 1.0F) << "inside the box sideways";
                        EXPECT_LE(std::abs(XMVectorGetY(clip)), 1.0F);
                        EXPECT_GE(XMVectorGetZ(clip), 0.0F) << "inside its depth (reversed Z: 0..1)";
                        EXPECT_LE(XMVectorGetZ(clip), 1.0F);
                    }
                }
            }
        }
    }
    // The slice's sphere doesn't depend on where the camera looks: the box keeps its texel size as the camera turns.
    EXPECT_FLOAT_EQ(sliceRadius(1.0F, 10.0F, 0.5F, 0.3F), std::sqrt(25.0F + 9.0F + 20.25F));
}

TEST(ShadowCascades, SmallMovesKeepTheCascade)
{
    ShadowCascades cascades({.count = 2, .resolution = 1024, .distance = 40.0F});
    const Math::Vec3 forward{0.0F, -0.3F, 1.0F};
    EXPECT_EQ(cascades.update(cameraAt({0.0F, 2.0F, 0.0F}, forward), c_sun, c_scene), 0b11U) << "new: both drawn";
    const std::vector<ShadowCascade> before(cascades.cascades().begin(), cascades.cascades().end());

    // A step much smaller than the padding: nothing moves, nothing is drawn again.
    EXPECT_EQ(cascades.update(cameraAt({0.01F, 2.0F, 0.01F}, forward), c_sun, c_scene), 0U);
    for (std::size_t i = 0; i < before.size(); ++i)
    {
        EXPECT_EQ(cascades.cascades()[i].lightCenter.x, before[i].lightCenter.x);
        EXPECT_EQ(cascades.cascades()[i].lightCenter.y, before[i].lightCenter.y);
    }

    // A large move: the near cascade has to follow, by whole texels.
    const std::uint32_t moved = cascades.update(cameraAt({30.0F, 2.0F, 25.0F}, forward), c_sun, c_scene);
    EXPECT_NE(moved & 1U, 0U);
    const ShadowCascade& near = cascades.cascades()[0];
    const float steps = (near.lightCenter.x - before[0].lightCenter.x) / near.texelSize;
    EXPECT_NEAR(steps, std::round(steps), 1e-2F) << "moved by a whole number of texels";

    // A new sun direction moves every cascade.
    const Math::Vec3 otherSun{0.0F, 1.0F, 0.0F};
    EXPECT_EQ(cascades.update(cameraAt({30.0F, 2.0F, 25.0F}, forward), otherSun, c_scene), 0b11U);
}
} // namespace Meow2::Renderer
