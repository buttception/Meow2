/// @file
/// @brief ShadowCascades (ShadowCascades.h).

#include <Meow2/Core/Assert.h>
#include <Meow2/Graphics/Renderer/ShadowCascades.h>
#include <Meow2/Math/Projection.h>

#include <DirectXMath.h>

#include <algorithm>
#include <cmath>

namespace Meow2::Renderer
{
namespace
{
/// @brief The sun's rotation: world → a space that looks from the sun towards the scene (+Z away from the sun).
DirectX::XMMATRIX lightRotation(const Math::Vec3& sunDirection)
{
    using namespace DirectX;
    const XMVECTOR forward = XMVectorNegate(XMVector3Normalize(Math::load(sunDirection)));
    // Any up that isn't parallel to the light: world up, or X when the sun is straight overhead or below.
    const XMVECTOR up =
        std::abs(sunDirection.y) > 0.99F ? XMVectorSet(1.0F, 0.0F, 0.0F, 0.0F) : XMVectorSet(0.0F, 1.0F, 0.0F, 0.0F);
    return XMMatrixLookToLH(XMVectorZero(), forward, up);
}
} // namespace

std::array<float, c_maxShadowCascades + 1> cascadeSplits(float nearZ, float distance, std::uint32_t count, float lambda)
{
    MEOW2_ASSERT(count >= 1 && count <= c_maxShadowCascades, "1 to c_maxShadowCascades cascades");
    MEOW2_ASSERT(nearZ > 0.0F && distance > nearZ, "0 < nearZ < distance");
    std::array<float, c_maxShadowCascades + 1> splits{};
    splits[0] = nearZ;
    for (std::uint32_t i = 1; i <= count; ++i)
    {
        const float fraction = static_cast<float>(i) / static_cast<float>(count);
        const float logarithmic = nearZ * std::pow(distance / nearZ, fraction);
        const float uniform = nearZ + (distance - nearZ) * fraction;
        splits[i] = lambda * logarithmic + (1.0F - lambda) * uniform;
    }
    splits[count] = distance; // exact, whatever the rounding
    return splits;
}

float sliceRadius(float splitNear, float splitFar, float tanHalfX, float tanHalfY)
{
    // The far corners are the farthest from the centre on the axis: (±f·tx, ±f·ty) sideways, (f − n) / 2 along.
    const float sideX = splitFar * tanHalfX;
    const float sideY = splitFar * tanHalfY;
    const float along = (splitFar - splitNear) * 0.5F;
    return std::sqrt(sideX * sideX + sideY * sideY + along * along);
}

ShadowCascades::ShadowCascades(const CascadeSettings& settings)
    : m_settings(settings)
{
}

void ShadowCascades::setSettings(const CascadeSettings& settings)
{
    m_settings = settings;
    m_valid = false;
}

std::uint32_t ShadowCascades::update(const CascadeCamera& camera, const Math::Vec3& sunDirection,
                                     const Math::Sphere& casters)
{
    using namespace DirectX;
    MEOW2_ASSERT(m_settings.count >= 1 && m_settings.count <= c_maxShadowCascades, "1 to c_maxShadowCascades cascades");
    const float distance = std::max(m_settings.distance, camera.nearZ * 2.0F);
    const std::array<float, c_maxShadowCascades + 1> splits =
        cascadeSplits(camera.nearZ, distance, m_settings.count, m_settings.splitLambda);

    // A new sun direction moves every cascade: the boxes are aligned to the light.
    const bool sunMoved =
        !m_valid || XMVectorGetX(XMVector3Dot(Math::load(sunDirection), Math::load(m_sunDirection))) < 0.99999F;
    m_sunDirection = sunDirection;
    const XMMATRIX rotation = lightRotation(sunDirection);
    const XMMATRIX inverseView = XMMatrixInverse(nullptr, XMLoadFloat4x4(&camera.view));
    Math::Vec3 casterCenter;
    XMStoreFloat3(&casterCenter, XMVector3Transform(Math::load(casters.center), rotation));

    std::uint32_t moved = 0;
    for (std::uint32_t i = 0; i < m_settings.count; ++i)
    {
        ShadowCascade& cascade = m_cascades[i];
        const float splitNear = splits[i];
        const float splitFar = splits[i + 1];
        const float slice = sliceRadius(splitNear, splitFar, camera.tanHalfX, camera.tanHalfY);
        const float radius = slice * m_settings.padding;
        const float texel = 2.0F * radius / static_cast<float>(m_settings.resolution);

        // The slice's centre, in the light's rotated space.
        const XMVECTOR centerWorld =
            XMVector3Transform(XMVectorSet(0.0F, 0.0F, (splitNear + splitFar) * 0.5F, 1.0F), inverseView);
        Math::Vec3 center;
        XMStoreFloat3(&center, XMVector3Transform(centerWorld, rotation));

        // The box stays while the slice's sphere is still inside it, sideways and in depth, and nothing about its size
        // changed; the casters must still fit its depth too.
        const float casterNear = casterCenter.z - casters.radius;
        const float casterFar = casterCenter.z + casters.radius;
        const bool fits =
            !sunMoved && cascade.radius == radius &&
            std::max(std::abs(center.x - cascade.lightCenter.x), std::abs(center.y - cascade.lightCenter.y)) + slice <=
                radius &&
            center.z - slice >= cascade.nearZ && center.z + slice <= cascade.farZ &&
            casterNear >= cascade.nearZ - 1e-3F && casterFar <= cascade.farZ + 1e-3F;
        if (!fits)
        {
            // Snapped to whole texels, so the map's texels land on the same spots in the world whenever it moves: the
            // shadow's edges don't crawl as the camera does.
            cascade.lightCenter = {std::round(center.x / texel) * texel, std::round(center.y / texel) * texel, 0.0F};
            cascade.radius = radius;
            cascade.nearZ = std::min(casterNear, center.z - radius);
            cascade.farZ = std::max(casterFar, center.z + radius);
            cascade.texelSize = texel;
            const XMMATRIX view =
                XMMatrixMultiply(rotation, XMMatrixTranslation(-cascade.lightCenter.x, -cascade.lightCenter.y, 0.0F));
            const XMMATRIX projection =
                Math::orthographicReversedLH(2.0F * radius, 2.0F * radius, cascade.nearZ, cascade.farZ);
            XMStoreFloat4x4(&cascade.viewProjection, XMMatrixMultiply(view, projection));
            moved |= 1U << i;
        }
        cascade.splitNear = splitNear;
        cascade.splitFar = splitFar;
    }
    m_valid = true;
    return moved;
}
} // namespace Meow2::Renderer
