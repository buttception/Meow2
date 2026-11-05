#pragma once

/// @file
/// @brief The standard easing curves (quad, cubic, sine, expo, back, elastic, bounce, …) for tweens, UI and camera
///        moves.
///
/// An easing curve maps progress `t` (0..1, linear in time) to an eased progress, usually also 0..1: a door that
/// starts slowly and stops softly instead of moving at constant speed. Feed the result to lerp():
/// @code
/// using namespace Meow2::Math;
/// const float t = saturate(elapsed / duration);
/// const float eased = ease(EaseCurve::Cubic, EaseMode::InOut, t);
/// panel.x = lerp(hiddenX, shownX, eased);
/// @endcode
/// Curves and modes are two small enums, so they can be stored in components and config files and picked in the
/// editor. Visual reference: https://easings.net (the same formulas).
/// @see docs/wiki/Math.md

#include <Meow2/Math/Types.h>

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace Meow2::Math
{
/// @brief The shape of an easing curve. Each is defined by its "In" form; Out and InOut are derived (EaseMode).
enum class EaseCurve : std::uint8_t
{
    Linear,  ///< t: no easing.
    Quad,    ///< t²: gentle.
    Cubic,   ///< t³: the usual default for UI.
    Quart,   ///< t⁴: stronger.
    Sine,    ///< 1 − cos(t·π/2): the softest.
    Expo,    ///< 2^(10t − 10): very slow start, then fast.
    Circ,    ///< 1 − √(1 − t²): a quarter circle.
    Back,    ///< Pulls back slightly (below 0) before moving: anticipation.
    Elastic, ///< Springy wobble with growing amplitude (overshoots both ways).
    Bounce,  ///< Bounces like a dropped ball (Out is the ball landing).
};

/// @brief Which end of the curve is eased.
enum class EaseMode : std::uint8_t
{
    In,    ///< Starts slowly, ends fast.
    Out,   ///< Starts fast, ends slowly: `1 − in(1 − t)`.
    InOut, ///< Slow at both ends: `In` over the first half, `Out` over the second.
};

/// @cond
namespace Detail
{
/// @brief Bounce's Out form (it's defined that way round): four parabolic hops landing at 1.
inline float bounceOut(float t) noexcept
{
    constexpr float c_n = 7.5625F;
    constexpr float c_d = 2.75F;
    if (t < 1.0F / c_d)
    {
        return c_n * t * t;
    }
    if (t < 2.0F / c_d)
    {
        t -= 1.5F / c_d;
        return c_n * t * t + 0.75F;
    }
    if (t < 2.5F / c_d)
    {
        t -= 2.25F / c_d;
        return c_n * t * t + 0.9375F;
    }
    t -= 2.625F / c_d;
    return c_n * t * t + 0.984375F;
}

/// @brief The "In" form of a curve.
inline float easeIn(EaseCurve curve, float t) noexcept
{
    switch (curve)
    {
        case EaseCurve::Linear:
            return t;
        case EaseCurve::Quad:
            return t * t;
        case EaseCurve::Cubic:
            return t * t * t;
        case EaseCurve::Quart:
            return t * t * t * t;
        case EaseCurve::Sine:
            return 1.0F - std::cos(t * c_halfPi);
        case EaseCurve::Expo:
            return t <= 0.0F ? 0.0F : std::exp2(10.0F * t - 10.0F);
        case EaseCurve::Circ:
            return 1.0F - std::sqrt(std::max(0.0F, 1.0F - t * t));
        case EaseCurve::Back:
        {
            // 1.70158 makes the dip 10 % deep (Robert Penner's constant).
            constexpr float c_overshoot = 1.70158F;
            return (c_overshoot + 1.0F) * t * t * t - c_overshoot * t * t;
        }
        case EaseCurve::Elastic:
            if (t <= 0.0F || t >= 1.0F)
            {
                return t <= 0.0F ? 0.0F : 1.0F;
            }
            return -std::exp2(10.0F * t - 10.0F) * std::sin((10.0F * t - 10.75F) * (c_twoPi / 3.0F));
        case EaseCurve::Bounce:
            return 1.0F - bounceOut(1.0F - t);
    }
    return t;
}
} // namespace Detail
/// @endcond

/// @brief Applies an easing curve.
/// @param curve The curve's shape.
/// @param mode  Which end is eased.
/// @param t     Progress, 0..1 (values outside are clamped).
/// @return The eased progress: 0 at t = 0 and 1 at t = 1 for every curve; Back and Elastic go outside 0..1 in
///         between.
[[nodiscard]] inline float ease(EaseCurve curve, EaseMode mode, float t) noexcept
{
    t = t < 0.0F ? 0.0F : (t > 1.0F ? 1.0F : t);
    switch (mode)
    {
        case EaseMode::In:
            return Detail::easeIn(curve, t);
        case EaseMode::Out:
            return 1.0F - Detail::easeIn(curve, 1.0F - t);
        case EaseMode::InOut:
            return t < 0.5F ? 0.5F * Detail::easeIn(curve, 2.0F * t)
                            : 1.0F - 0.5F * Detail::easeIn(curve, 2.0F - 2.0F * t);
    }
    return t;
}
} // namespace Meow2::Math
