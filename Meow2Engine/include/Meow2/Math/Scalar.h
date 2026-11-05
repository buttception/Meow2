#pragma once

/// @file
/// @brief Scalar helpers: angle conversion and wrapping, interpolation, clamping and smoothstep.
///
/// All of them work on `float`, the engine's maths type. For whole vectors use DirectXMath's versions on `XMVECTOR`
/// (`XMVectorLerp`, `XMVectorClamp`, `XMVectorSaturate`), which do every component in one instruction.
/// @code
/// using namespace Meow2::Math;
/// const float fov = toRadians(70.0F);
/// const float t = saturate(inverseLerp(nearDistance, farDistance, distance)); // 0..1 between the two
/// const float alpha = smoothstep(0.0F, 1.0F, t);                                // eased fade
/// heading = wrapAngle(heading + turnRate * dt);                                  // stays in [-π, π)
/// @endcode
/// @see techstack.md §10, docs/wiki/Math.md

#include <Meow2/Core/Assert.h>
#include <Meow2/Math/Types.h>

#include <cmath>

namespace Meow2::Math
{
/// @brief Converts degrees to radians.
/// @param degrees An angle in degrees.
/// @return The same angle in radians (180° → π).
[[nodiscard]] constexpr float toRadians(float degrees) noexcept
{
    return degrees * (c_pi / 180.0F);
}

/// @brief Converts radians to degrees.
/// @param radians An angle in radians.
/// @return The same angle in degrees (π → 180°).
[[nodiscard]] constexpr float toDegrees(float radians) noexcept
{
    return radians * (180.0F / c_pi);
}

/// @brief Linear interpolation from `a` to `b`.
///
/// Computed as `(1 - t)·a + t·b`, so the result is exactly `a` at t = 0 and exactly `b` at t = 1 (the shorter
/// `a + t·(b - a)` can miss `b` by a rounding step).
/// @param a Value at t = 0.
/// @param b Value at t = 1.
/// @param t Position between them; values outside 0..1 extrapolate (clamp `t` with saturate() to prevent that).
/// @return The interpolated value.
/// @note With libstdc++ (Linux), `std::lerp` is also visible as a global `::lerp`. After `using namespace Meow2::Math;`
/// at
///       global or function scope an unqualified `lerp(float, float, float)` is ambiguous: write `Math::lerp`, or call
///       it from inside `namespace Meow2`.
[[nodiscard]] constexpr float lerp(float a, float b, float t) noexcept
{
    return (1.0F - t) * a + t * b;
}

/// @brief The inverse of lerp(): where `value` lies between `a` and `b`.
/// @param a     Value that maps to 0.
/// @param b     Value that maps to 1; `b < a` is fine (a reversed range).
/// @param value The value to locate.
/// @return 0 at `a`, 1 at `b`, and below 0 or above 1 outside the range (not clamped).
/// @pre `a != b` (asserted): an empty range has no position.
[[nodiscard]] inline float inverseLerp(float a, float b, float value) noexcept
{
    MEOW2_ASSERT(a != b, "inverseLerp needs a non-empty range");
    return (value - a) / (b - a);
}

/// @brief Maps `value` from the range [inMin, inMax] to the range [outMin, outMax], linearly.
///
/// `remap(v, 0, 100, 1, 0)` turns a 0..100 health value into a 1..0 "damage" factor. Not clamped: a value outside the
/// input range lands outside the output range.
/// @param value  The value to map.
/// @param inMin  Start of the input range.
/// @param inMax  End of the input range.
/// @param outMin Where `inMin` maps to.
/// @param outMax Where `inMax` maps to.
/// @return The mapped value.
/// @pre `inMin != inMax` (asserted).
[[nodiscard]] inline float remap(float value, float inMin, float inMax, float outMin, float outMax) noexcept
{
    return lerp(outMin, outMax, inverseLerp(inMin, inMax, value));
}

/// @brief Limits `value` to [minValue, maxValue].
/// @param value    The value.
/// @param minValue Lower limit.
/// @param maxValue Upper limit.
/// @return `minValue` if `value` is below it, `maxValue` if above, `value` otherwise. A NaN `value` stays NaN.
/// @pre `minValue <= maxValue` (not checked, so it stays `constexpr`).
[[nodiscard]] constexpr float clamp(float value, float minValue, float maxValue) noexcept
{
    return value < minValue ? minValue : (value > maxValue ? maxValue : value);
}

/// @brief Limits `value` to 0..1 (HLSL's `saturate`).
/// @param value The value.
/// @return The value clamped to [0, 1].
[[nodiscard]] constexpr float saturate(float value) noexcept
{
    return clamp(value, 0.0F, 1.0F);
}

/// @brief Wraps an angle to [-π, π): the same direction, as the smallest turn either way.
///
/// Use it after adding to an angle every frame (a heading, a spinning wheel), so the value never grows until it loses
/// precision, and before comparing two angles (`wrapAngle(a - b)` is the signed shortest difference).
/// @param radians Any finite angle in radians.
/// @return The equivalent angle in [-π, π). π itself becomes -π.
/// @note Precision: a float of 10,000 radians has steps of about 0.001 rad, and wrapping can't restore what's already
///       lost. Wrap regularly rather than once at the end.
[[nodiscard]] inline float wrapAngle(float radians) noexcept
{
    // floor (not fmod or remainder): one formula for negative and positive input, with a half-open result range.
    float wrapped = radians - c_twoPi * std::floor((radians + c_pi) / c_twoPi);
    // Rounding in the division can leave the result one step outside the range.
    if (wrapped >= c_pi)
    {
        wrapped -= c_twoPi;
    }
    else if (wrapped < -c_pi)
    {
        wrapped += c_twoPi;
    }
    return wrapped;
}

/// @brief Smooth Hermite step from 0 to 1 between two edges (HLSL's `smoothstep`).
///
/// 0 at or below `edge0`, 1 at or above `edge1`, and the S-curve `3t² − 2t³` between them, whose slope is zero at
/// both edges: fades and blends start and stop without a visible jump.
/// @param edge0 Where the curve starts (result 0).
/// @param edge1 Where it ends (result 1); `edge1 < edge0` gives the falling curve.
/// @param value The input.
/// @return The curve's value, 0..1.
/// @pre `edge0 != edge1` (asserted).
[[nodiscard]] inline float smoothstep(float edge0, float edge1, float value) noexcept
{
    const float t = saturate(inverseLerp(edge0, edge1, value));
    return t * t * (3.0F - 2.0F * t);
}
} // namespace Meow2::Math
