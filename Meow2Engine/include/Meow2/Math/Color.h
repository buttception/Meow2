#pragma once

/// @file
/// @brief Colours: linear float `Color` for maths and lighting, 8-bit sRGB `Color8` for storage and UI, and the exact
///        conversions between them, HSV, premultiplied alpha and named constants.
///
/// **Two encodings, on purpose.** Screens and image files store colours *gamma-encoded* (sRGB): the 256 steps of a
/// byte are spread so that they look evenly spaced to the eye, which means they aren't evenly spaced in light
/// intensity. Maths on light (blending, lighting, averaging, interpolation) is only correct on *linear* values.
/// - `Color8`: what a colour picker, a PNG or a UI theme gives you. sRGB-encoded RGB, linear alpha.
/// - `Color`: what the engine computes with. Linear float RGBA.
/// @code
/// using namespace Meow2::Math;
/// const Color tint = toLinear(fromHex(0xFF8000FFU));            // orange from a designer, now linear
/// const Color mixed = lerp(tint, Colors::c_white, 0.5F);         // blend in linear space
/// const Color8 forUi = toColor8(mixed);                          // back to sRGB bytes
/// @endcode
/// @see techstack.md §10, docs/wiki/Math.md

#include <Meow2/Math/Scalar.h>
#include <Meow2/Math/Types.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <format>
#include <string_view>

namespace Meow2::Math
{
/// @brief A colour in linear RGB with linear alpha, as floats. Plain data; opaque black by default.
///
/// Components are usually 0..1, but values above 1 are valid (HDR light). Alpha is straight (not premultiplied)
/// unless a function says otherwise.
/// @par Thread safety
/// Plain value; any thread may read a value nobody writes.
struct Color
{
    float r = 0.0F; ///< Red, linear.
    float g = 0.0F; ///< Green, linear.
    float b = 0.0F; ///< Blue, linear.
    float a = 1.0F; ///< Alpha (opacity), 0 = transparent, 1 = opaque.
};

/// @brief A colour as 8-bit sRGB-encoded RGB plus 8-bit linear alpha (the layout of RGBA8 images). Plain data; opaque
///        black by default.
/// @par Thread safety
/// Plain value; any thread may read a value nobody writes.
struct Color8
{
    std::uint8_t r = 0;   ///< Red, sRGB-encoded.
    std::uint8_t g = 0;   ///< Green, sRGB-encoded.
    std::uint8_t b = 0;   ///< Blue, sRGB-encoded.
    std::uint8_t a = 255; ///< Alpha, linear: 0 = transparent, 255 = opaque.

    /// @brief Equal if all four bytes are equal.
    friend constexpr bool operator==(const Color8&, const Color8&) noexcept = default;
};

/// @brief A colour as hue, saturation and value, the way colour pickers present it. Plain data.
///
/// HSV is a rearrangement of whatever RGB values it came from, so it's usually used on sRGB values (what the picker
/// shows), not on linear ones.
struct Hsv
{
    float hue = 0.0F;        ///< Hue in degrees, [0, 360): 0 red, 120 green, 240 blue.
    float saturation = 0.0F; ///< Saturation, 0 (grey) .. 1 (pure colour).
    float value = 0.0F;      ///< Value (brightness), 0 (black) .. 1.
};

/// @brief Decodes one sRGB-encoded channel to linear light (the exact IEC 61966-2-1 curve).
/// @param encoded The encoded value, 0..1.
/// @return The linear value, 0..1.
[[nodiscard]] inline float srgbToLinear(float encoded) noexcept
{
    // A short straight segment near black, then a 2.4 power curve; the pieces meet without a jump.
    return encoded <= 0.04045F ? encoded / 12.92F : std::pow((encoded + 0.055F) / 1.055F, 2.4F);
}

/// @brief Encodes one linear channel to sRGB (the inverse of srgbToLinear()).
/// @param linear The linear value, 0..1.
/// @return The sRGB-encoded value, 0..1.
[[nodiscard]] inline float linearToSrgb(float linear) noexcept
{
    return linear <= 0.0031308F ? linear * 12.92F : 1.055F * std::pow(linear, 1.0F / 2.4F) - 0.055F;
}

/// @brief Converts an 8-bit sRGB colour to a linear float colour.
/// @param color The 8-bit colour.
/// @return The same colour in linear RGB; alpha is scaled to 0..1 without a curve.
[[nodiscard]] inline Color toLinear(Color8 color) noexcept
{
    constexpr float c_toUnit = 1.0F / 255.0F;
    return {srgbToLinear(static_cast<float>(color.r) * c_toUnit), srgbToLinear(static_cast<float>(color.g) * c_toUnit),
            srgbToLinear(static_cast<float>(color.b) * c_toUnit), static_cast<float>(color.a) * c_toUnit};
}

/// @brief Converts a linear float colour to 8-bit sRGB, rounding to the nearest byte.
/// @param color The linear colour; components outside 0..1 are clamped (HDR values become 255).
/// @return The 8-bit colour. Converting any Color8 to linear and back gives the same bytes.
[[nodiscard]] inline Color8 toColor8(const Color& color) noexcept
{
    const auto toByte = [](float unit)
    {
        return static_cast<std::uint8_t>(saturate(unit) * 255.0F + 0.5F);
    };
    return {toByte(linearToSrgb(saturate(color.r))), toByte(linearToSrgb(saturate(color.g))),
            toByte(linearToSrgb(saturate(color.b))), toByte(color.a)};
}

/// @brief Makes an 8-bit sRGB colour from a hex value written `0xRRGGBBAA`, as in CSS and most tools.
/// @param rgba Red in the top byte, alpha in the bottom one.
/// @return The colour.
[[nodiscard]] constexpr Color8 fromHex(std::uint32_t rgba) noexcept
{
    return {static_cast<std::uint8_t>(rgba >> 24U), static_cast<std::uint8_t>(rgba >> 16U),
            static_cast<std::uint8_t>(rgba >> 8U), static_cast<std::uint8_t>(rgba)};
}

/// @brief Converts RGB to HSV (alpha is ignored).
/// @param color The colour; use the sRGB-encoded values for picker-style HSV.
/// @return Hue in [0, 360), saturation and value. Greys get hue 0.
[[nodiscard]] inline Hsv toHsv(const Color& color) noexcept
{
    const float maxValue = std::max({color.r, color.g, color.b});
    const float minValue = std::min({color.r, color.g, color.b});
    const float chroma = maxValue - minValue;
    Hsv result{0.0F, maxValue > 0.0F ? chroma / maxValue : 0.0F, maxValue};
    if (chroma > 0.0F)
    {
        float sector = 0.0F;
        if (maxValue == color.r)
        {
            sector = (color.g - color.b) / chroma;
        }
        else if (maxValue == color.g)
        {
            sector = 2.0F + (color.b - color.r) / chroma;
        }
        else
        {
            sector = 4.0F + (color.r - color.g) / chroma;
        }
        result.hue = sector * 60.0F;
        if (result.hue < 0.0F)
        {
            result.hue += 360.0F;
        }
    }
    return result;
}

/// @brief Converts HSV to RGB.
/// @param hsv   Hue in degrees (any value; wrapped to [0, 360)), saturation and value 0..1.
/// @param alpha The alpha of the result.
/// @return The colour, in the same encoding the HSV values came from.
[[nodiscard]] inline Color fromHsv(const Hsv& hsv, float alpha = 1.0F) noexcept
{
    const float hue = hsv.hue - 360.0F * std::floor(hsv.hue / 360.0F);
    const float chroma = hsv.value * hsv.saturation;
    const float sector = hue / 60.0F;
    const float second = chroma * (1.0F - std::abs(std::fmod(sector, 2.0F) - 1.0F));
    const float offset = hsv.value - chroma;
    float r = 0.0F;
    float g = 0.0F;
    float b = 0.0F;
    switch (static_cast<int>(sector))
    {
        case 0:
            r = chroma;
            g = second;
            break;
        case 1:
            r = second;
            g = chroma;
            break;
        case 2:
            g = chroma;
            b = second;
            break;
        case 3:
            g = second;
            b = chroma;
            break;
        case 4:
            r = second;
            b = chroma;
            break;
        default:
            r = chroma;
            b = second;
            break;
    }
    return {r + offset, g + offset, b + offset, alpha};
}

/// @brief Converts straight alpha to premultiplied alpha: RGB multiplied by alpha.
///
/// Premultiplied colours blend and filter correctly (a half-transparent red pixel next to a transparent one doesn't
/// bleed black when the texture is scaled), and blending becomes `src + dst × (1 − srcAlpha)`.
/// @param color A straight-alpha colour.
/// @return The premultiplied colour.
[[nodiscard]] constexpr Color premultiply(const Color& color) noexcept
{
    return {color.r * color.a, color.g * color.a, color.b * color.a, color.a};
}

/// @brief Converts premultiplied alpha back to straight alpha.
/// @param color A premultiplied colour.
/// @return The straight-alpha colour; fully transparent input gives transparent black (the colour is lost).
[[nodiscard]] constexpr Color unpremultiply(const Color& color) noexcept
{
    if (color.a <= 0.0F)
    {
        return {0.0F, 0.0F, 0.0F, 0.0F};
    }
    return {color.r / color.a, color.g / color.a, color.b / color.a, color.a};
}

/// @brief Interpolates two linear colours component by component (alpha too).
/// @param from Colour at t = 0.
/// @param to   Colour at t = 1.
/// @param t    Position between them.
/// @return The blend. Done in linear space, so a red-green blend passes through yellow, not a dark brown.
[[nodiscard]] constexpr Color lerp(const Color& from, const Color& to, float t) noexcept
{
    return {lerp(from.r, to.r, t), lerp(from.g, to.g, t), lerp(from.b, to.b, t), lerp(from.a, to.a, t)};
}

/// @brief Component-wise nearlyEqual() for colours.
/// @param a         A colour.
/// @param b         Another colour.
/// @param tolerance The tolerance.
/// @return `true` if all four components are equal within it.
[[nodiscard]] inline bool nearlyEqual(const Color& a, const Color& b, float tolerance = c_defaultTolerance) noexcept
{
    return nearlyEqual(a.r, b.r, tolerance) && nearlyEqual(a.g, b.g, tolerance) && nearlyEqual(a.b, b.b, tolerance) &&
           nearlyEqual(a.a, b.a, tolerance);
}

/// @brief Loads a colour into a SIMD register as (r, g, b, a), for DirectXMath (`XMColor*`, `XMVectorLerp`, …).
/// @param color The colour.
/// @return The register.
[[nodiscard]] inline DirectX::XMVECTOR load(const Color& color) noexcept
{
    return DirectX::XMVectorSet(color.r, color.g, color.b, color.a);
}

/// @brief Stores a register holding (r, g, b, a) as a colour.
/// @param value The register.
/// @return The colour.
[[nodiscard]] inline Color storeColor(DirectX::FXMVECTOR value) noexcept
{
    DirectX::XMFLOAT4 result;
    DirectX::XMStoreFloat4(&result, value);
    return {result.x, result.y, result.z, result.w};
}

/// @brief Named linear colours, all opaque except c_transparent.
namespace Colors
{
/// @brief (0, 0, 0, 0).
inline constexpr Color c_transparent{0.0F, 0.0F, 0.0F, 0.0F};
/// @brief Black.
inline constexpr Color c_black{0.0F, 0.0F, 0.0F, 1.0F};
/// @brief White.
inline constexpr Color c_white{1.0F, 1.0F, 1.0F, 1.0F};
/// @brief Middle grey as it *looks*: sRGB 128 (0.2158 linear), not linear 0.5, which looks light grey.
inline constexpr Color c_grey{0.2158605F, 0.2158605F, 0.2158605F, 1.0F};
/// @brief Pure red.
inline constexpr Color c_red{1.0F, 0.0F, 0.0F, 1.0F};
/// @brief Pure green.
inline constexpr Color c_green{0.0F, 1.0F, 0.0F, 1.0F};
/// @brief Pure blue.
inline constexpr Color c_blue{0.0F, 0.0F, 1.0F, 1.0F};
/// @brief Yellow (red + green).
inline constexpr Color c_yellow{1.0F, 1.0F, 0.0F, 1.0F};
/// @brief Cyan (green + blue).
inline constexpr Color c_cyan{0.0F, 1.0F, 1.0F, 1.0F};
/// @brief Magenta (red + blue).
inline constexpr Color c_magenta{1.0F, 0.0F, 1.0F, 1.0F};
} // namespace Colors
} // namespace Meow2::Math

/// @cond
template <>
struct std::formatter<Meow2::Math::Color> : std::formatter<std::string_view>
{
    auto format(const Meow2::Math::Color& c, std::format_context& context) const
    {
        return std::format_to(context.out(), "rgba({}, {}, {}, {})", c.r, c.g, c.b, c.a);
    }
};

template <>
struct std::formatter<Meow2::Math::Color8> : std::formatter<std::string_view>
{
    auto format(const Meow2::Math::Color8& c, std::format_context& context) const
    {
        return std::format_to(context.out(), "#{:02x}{:02x}{:02x}{:02x}", c.r, c.g, c.b, c.a);
    }
};
/// @endcond
