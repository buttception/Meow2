/// @file
/// @brief SplinePath: a Catmull-Rom path with an arc-length table for constant-speed travel.

#include <Meow2/Core/Assert.h>
#include <Meow2/Math/Spline.h>

#include <algorithm>
#include <cmath>

namespace Meow2::Math
{
SplinePath::SplinePath(std::span<const Vec3> points, std::size_t samplesPerSegment)
    : m_points(points.begin(), points.end())
    , m_samplesPerSegment(std::max<std::size_t>(samplesPerSegment, 1))
{
    using namespace DirectX;
    MEOW2_ASSERT(m_points.size() >= 2, "a path needs at least two points");
    const std::size_t sampleCount = segmentCount() * m_samplesPerSegment;
    m_lengths.reserve(sampleCount + 1);
    m_lengths.push_back(0.0F);
    Vec3 previous = m_points.front();
    for (std::size_t i = 1; i <= sampleCount; ++i)
    {
        const Vec3 current = pointAt(static_cast<float>(i) / static_cast<float>(m_samplesPerSegment));
        const float step = XMVectorGetX(XMVector3Length(load(current) - load(previous)));
        m_lengths.push_back(m_lengths.back() + step);
        previous = current;
    }
}

float SplinePath::length() const noexcept
{
    return m_lengths.back();
}

std::size_t SplinePath::segmentCount() const noexcept
{
    return m_points.size() - 1;
}

Vec3 SplinePath::pointAt(float t) const noexcept
{
    const auto segments = static_cast<float>(segmentCount());
    t = std::clamp(t, 0.0F, segments);
    const std::size_t segment = std::min(static_cast<std::size_t>(t), segmentCount() - 1);
    const float local = t - static_cast<float>(segment);
    // The ends repeat the first/last point as the missing neighbour, so the curve starts and ends on the path.
    const Vec3& p0 = m_points[segment == 0 ? 0 : segment - 1];
    const Vec3& p1 = m_points[segment];
    const Vec3& p2 = m_points[segment + 1];
    const Vec3& p3 = m_points[std::min(segment + 2, m_points.size() - 1)];
    return catmullRom(p0, p1, p2, p3, local);
}

float SplinePath::parameterAtDistance(float distance) const noexcept
{
    distance = std::clamp(distance, 0.0F, length());
    // First sample beyond the distance; the answer lies between it and the sample before.
    auto upper = std::upper_bound(m_lengths.begin(), m_lengths.end(), distance);
    if (upper == m_lengths.end())
    {
        --upper;
    }
    if (upper == m_lengths.begin())
    {
        ++upper;
    }
    const auto index = static_cast<std::size_t>(upper - m_lengths.begin());
    const float before = m_lengths[index - 1];
    const float span = m_lengths[index] - before;
    if (span <= 0.0F)
    {
        return static_cast<float>(index - 1) / static_cast<float>(m_samplesPerSegment);
    }
    const float sampleStep = 1.0F / static_cast<float>(m_samplesPerSegment);
    const float start = static_cast<float>(index - 1) * sampleStep;
    float t = start + (distance - before) / span * sampleStep;
    // The speed changes inside a sample, so the linear guess is off by up to about 1 %. One Newton step fixes most of
    // that: measure how far the guess really is from the sample (a chord, accurate over such a short piece), and
    // move by the remaining distance divided by the sample's average speed.
    const float reached = before + DirectX::XMVectorGetX(DirectX::XMVector3Length(
                                       DirectX::XMVectorSubtract(load(pointAt(t)), load(pointAt(start)))));
    t += (distance - reached) / span * sampleStep;
    return std::clamp(t, start, start + sampleStep);
}

Vec3 SplinePath::pointAtDistance(float distance) const noexcept
{
    return pointAt(parameterAtDistance(distance));
}

Vec3 SplinePath::tangentAtDistance(float distance) const noexcept
{
    using namespace DirectX;
    // Central difference in the curve parameter: the direction doesn't depend on the parameter's speed.
    constexpr float c_step = 1e-3F;
    const float t = parameterAtDistance(distance);
    const float segments = static_cast<float>(segmentCount());
    const XMVECTOR delta = load(pointAt(std::min(t + c_step, segments))) - load(pointAt(std::max(t - c_step, 0.0F)));
    if (XMVectorGetX(XMVector3LengthSq(delta)) < 1e-14F)
    {
        return Vec3{};
    }
    return storeVec3(XMVector3Normalize(delta));
}
} // namespace Meow2::Math
