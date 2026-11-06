/// @file
/// @brief Tests for colours: the sRGB curve, exact 8-bit round trips, hex parsing, HSV, premultiplied alpha, blending
///        and output.

#include <Meow2/Math/Color.h>

#include <gtest/gtest.h>

#include <format>

using namespace Meow2::Math;

TEST(Color, SrgbCurveKnownValues)
{
    EXPECT_EQ(srgbToLinear(0.0F), 0.0F);
    EXPECT_TRUE(nearlyEqual(srgbToLinear(1.0F), 1.0F));
    EXPECT_TRUE(nearlyEqual(srgbToLinear(0.5F), 0.2140411F, 1e-5F)) << "sRGB middle grey is about 21 % of the light";
    EXPECT_TRUE(nearlyEqual(srgbToLinear(0.02F), 0.02F / 12.92F)) << "the straight segment near black";
    EXPECT_TRUE(nearlyEqual(linearToSrgb(0.2140411F), 0.5F, 1e-5F));
    EXPECT_TRUE(nearlyEqual(linearToSrgb(0.001F), 0.01292F)) << "the straight segment near black";
    // The two pieces meet without a jump.
    EXPECT_TRUE(nearlyEqual(srgbToLinear(0.04045F), srgbToLinear(0.04046F), 1e-4F));
}

TEST(Color, Color8RoundTripsExactly)
{
    for (int i = 0; i < 256; ++i)
    {
        const auto value = static_cast<std::uint8_t>(i);
        const Color8 original{value, value, value, value};
        ASSERT_EQ(toColor8(toLinear(original)), original) << "byte " << i;
    }
    EXPECT_EQ(toColor8(Color{2.0F, -1.0F, 0.5F, 1.0F}), (Color8{255, 0, 188, 255})) << "out-of-range values clamp";
}

TEST(Color, FromHex)
{
    constexpr Color8 c_orange = fromHex(0xFF8000C0U);
    static_assert(c_orange.r == 0xFF && c_orange.g == 0x80 && c_orange.b == 0x00 && c_orange.a == 0xC0);
    EXPECT_TRUE(nearlyEqual(toLinear(fromHex(0xFFFFFFFFU)), Colors::c_white));
    EXPECT_TRUE(nearlyEqual(toLinear(fromHex(0x808080FFU)), Colors::c_grey, 1e-5F)) << "c_grey is sRGB 128";
}

TEST(Color, HsvRoundTrip)
{
    struct Case
    {
        Color rgb;
        Hsv hsv;
    };
    const Case cases[] = {
        {Colors::c_red, {0.0F, 1.0F, 1.0F}},
        {Colors::c_green, {120.0F, 1.0F, 1.0F}},
        {Colors::c_blue, {240.0F, 1.0F, 1.0F}},
        {Colors::c_yellow, {60.0F, 1.0F, 1.0F}},
        {Colors::c_magenta, {300.0F, 1.0F, 1.0F}},
        {Colors::c_black, {0.0F, 0.0F, 0.0F}},
        {{0.5F, 0.5F, 0.5F, 1.0F}, {0.0F, 0.0F, 0.5F}},
    };
    for (const Case& c : cases)
    {
        const Hsv hsv = toHsv(c.rgb);
        EXPECT_TRUE(nearlyEqual(hsv.hue, c.hsv.hue) && nearlyEqual(hsv.saturation, c.hsv.saturation) &&
                    nearlyEqual(hsv.value, c.hsv.value))
            << std::format("{} → ({}, {}, {})", c.rgb, hsv.hue, hsv.saturation, hsv.value);
        EXPECT_TRUE(nearlyEqual(fromHsv(c.hsv), c.rgb)) << std::format("{}", c.rgb);
    }
    const Color some{0.2F, 0.7F, 0.4F, 0.5F};
    EXPECT_TRUE(nearlyEqual(fromHsv(toHsv(some), 0.5F), some));
    EXPECT_TRUE(nearlyEqual(fromHsv({360.0F + 120.0F, 1.0F, 1.0F}), Colors::c_green)) << "hue wraps";
    EXPECT_TRUE(nearlyEqual(fromHsv({-120.0F, 1.0F, 1.0F}), Colors::c_blue)) << "negative hue wraps";
}

TEST(Color, Premultiply)
{
    constexpr Color c_halfRed{1.0F, 0.5F, 0.0F, 0.5F};
    static_assert(premultiply(c_halfRed).r == 0.5F && premultiply(c_halfRed).g == 0.25F);
    EXPECT_TRUE(nearlyEqual(unpremultiply(premultiply(c_halfRed)), c_halfRed));
    EXPECT_TRUE(nearlyEqual(unpremultiply(Color{0.3F, 0.3F, 0.3F, 0.0F}), Colors::c_transparent))
        << "nothing to recover at alpha 0";
}

TEST(Color, LerpAndFormat)
{
    const Color mid = lerp(Colors::c_red, Colors::c_green, 0.5F);
    EXPECT_TRUE(nearlyEqual(mid, Color{0.5F, 0.5F, 0.0F, 1.0F}));
    EXPECT_EQ(toColor8(mid), (Color8{188, 188, 0, 255})) << "linear blend: a bright yellow, not sRGB 128 brown";
    EXPECT_TRUE(nearlyEqual(storeColor(load(mid)), mid));
    EXPECT_EQ(std::format("{}", Color{1.0F, 0.5F, 0.0F, 1.0F}), "rgba(1, 0.5, 0, 1)");
    EXPECT_EQ(std::format("{}", fromHex(0xFF8000C0U)), "#ff8000c0");
}
