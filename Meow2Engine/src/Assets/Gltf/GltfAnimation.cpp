/// @file
/// @brief Sampling imported glTF animations (animatedTransform(), GltfImporter.h).

#include "Meow2/Assets/Gltf/GltfImporter.h"
#include "Meow2/Math/Quaternion.h"

#include <algorithm>

namespace Meow2::Assets
{
namespace
{
/// @brief A value at a keyframe: for cubic splines, the middle of its three entries.
Math::Vec4 keyValue(const ImportedChannel& channel, std::size_t key, std::size_t entry = 1)
{
    return channel.interpolation == AnimationInterpolation::CubicSpline ? channel.values[key * 3 + entry]
                                                                        : channel.values[key];
}

Math::Vec4 lerp(const Math::Vec4& a, const Math::Vec4& b, float t)
{
    return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t};
}

/// @brief The channel's value at `seconds`, per the glTF spec's interpolation rules (Appendix C).
Math::Vec4 sample(const ImportedChannel& channel, float seconds)
{
    const std::vector<float>& times = channel.times;
    if (seconds <= times.front() || times.size() == 1)
    {
        return keyValue(channel, 0);
    }
    if (seconds >= times.back())
    {
        return keyValue(channel, times.size() - 1);
    }
    // The keyframe interval [times[k], times[k + 1]) that holds `seconds`.
    const auto next = std::ranges::upper_bound(times, seconds);
    const auto k = static_cast<std::size_t>(next - times.begin()) - 1;
    const float delta = times[k + 1] - times[k];
    const float t = delta > 0.0F ? (seconds - times[k]) / delta : 0.0F;
    switch (channel.interpolation)
    {
        case AnimationInterpolation::Step:
            return keyValue(channel, k);
        case AnimationInterpolation::CubicSpline:
        {
            // Hermite: p0, its out-tangent b0, p1, its in-tangent a1; tangents are per second, so scaled by delta.
            const Math::Vec4 p0 = keyValue(channel, k);
            const Math::Vec4 b0 = keyValue(channel, k, 2);
            const Math::Vec4 a1 = keyValue(channel, k + 1, 0);
            const Math::Vec4 p1 = keyValue(channel, k + 1);
            const float t2 = t * t;
            const float t3 = t2 * t;
            const float h00 = 2.0F * t3 - 3.0F * t2 + 1.0F;
            const float h10 = (t3 - 2.0F * t2 + t) * delta;
            const float h01 = -2.0F * t3 + 3.0F * t2;
            const float h11 = (t3 - t2) * delta;
            return {
                h00 * p0.x + h10 * b0.x + h01 * p1.x + h11 * a1.x, h00 * p0.y + h10 * b0.y + h01 * p1.y + h11 * a1.y,
                h00 * p0.z + h10 * b0.z + h01 * p1.z + h11 * a1.z, h00 * p0.w + h10 * b0.w + h01 * p1.w + h11 * a1.w};
        }
        case AnimationInterpolation::Linear:
            break;
    }
    if (channel.path == AnimationPath::Rotation)
    {
        const Math::Vec4 a = keyValue(channel, k);
        const Math::Vec4 b = keyValue(channel, k + 1);
        const Math::Quat q = Math::slerp(Math::Quat{a.x, a.y, a.z, a.w}, Math::Quat{b.x, b.y, b.z, b.w}, t);
        return {q.x, q.y, q.z, q.w};
    }
    return lerp(keyValue(channel, k), keyValue(channel, k + 1), t);
}
} // namespace

Math::Transform animatedTransform(const ImportedAnimation& animation, std::int32_t node, const Math::Transform& rest,
                                  float seconds)
{
    Math::Transform transform = rest;
    for (const ImportedChannel& channel : animation.channels)
    {
        if (channel.node != node)
        {
            continue;
        }
        const Math::Vec4 value = sample(channel, seconds);
        switch (channel.path)
        {
            case AnimationPath::Translation:
                transform.position = {value.x, value.y, value.z};
                break;
            case AnimationPath::Rotation:
                // Cubic splines don't keep a quaternion's length; the spec asks for it to be normalised.
                transform.rotation = Math::normalize(Math::Quat{value.x, value.y, value.z, value.w});
                break;
            case AnimationPath::Scale:
                transform.scale = {value.x, value.y, value.z};
                break;
        }
    }
    return transform;
}
} // namespace Meow2::Assets
