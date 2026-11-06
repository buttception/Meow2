/// @file
/// @brief Tests for the easing curves: endpoints, known values, and how Out and InOut derive from In.

#include <Meow2/Math/Easing.h>
#include <Meow2/Math/Scalar.h>

#include <gtest/gtest.h>

#include <array>

using namespace Meow2::Math;

namespace
{
constexpr std::array c_curves{EaseCurve::Linear,  EaseCurve::Quad,  EaseCurve::Cubic, EaseCurve::Quart,
                              EaseCurve::Sine,    EaseCurve::Expo,  EaseCurve::Circ,  EaseCurve::Back,
                              EaseCurve::Elastic, EaseCurve::Bounce};
constexpr std::array c_modes{EaseMode::In, EaseMode::Out, EaseMode::InOut};
} // namespace

TEST(Easing, EndpointsAndKnownValues)
{
    for (const EaseCurve curve : c_curves)
    {
        for (const EaseMode mode : c_modes)
        {
            EXPECT_NEAR(ease(curve, mode, 0.0F), 0.0F, 1e-3F)
                << static_cast<int>(curve) << "/" << static_cast<int>(mode);
            EXPECT_NEAR(ease(curve, mode, 1.0F), 1.0F, 1e-3F)
                << static_cast<int>(curve) << "/" << static_cast<int>(mode);
            EXPECT_EQ(ease(curve, mode, -1.0F), ease(curve, mode, 0.0F)) << "t is clamped";
            EXPECT_EQ(ease(curve, mode, 2.0F), ease(curve, mode, 1.0F)) << "t is clamped";
        }
    }
    EXPECT_FLOAT_EQ(ease(EaseCurve::Quad, EaseMode::In, 0.5F), 0.25F);
    EXPECT_FLOAT_EQ(ease(EaseCurve::Cubic, EaseMode::Out, 0.5F), 0.875F);
    EXPECT_NEAR(ease(EaseCurve::Sine, EaseMode::InOut, 0.25F), 0.1464466F, 1e-5F);
    EXPECT_NEAR(ease(EaseCurve::Bounce, EaseMode::Out, 0.5F), 0.765625F, 1e-5F) << "easings.net value";
    EXPECT_NEAR(ease(EaseCurve::Expo, EaseMode::In, 0.5F), 0.03125F, 1e-6F);
}

TEST(Easing, OutAndInOutMirrorIn)
{
    for (const EaseCurve curve : c_curves)
    {
        for (float t = 0.0F; t <= 1.0F; t += 0.05F)
        {
            const float in = ease(curve, EaseMode::In, t);
            EXPECT_NEAR(ease(curve, EaseMode::Out, 1.0F - t), 1.0F - in, 1e-5F) << "out mirrors in";
            EXPECT_NEAR(ease(curve, EaseMode::InOut, t) + ease(curve, EaseMode::InOut, 1.0F - t), 1.0F, 1e-5F)
                << "InOut is point-symmetric about (0.5, 0.5)";
        }
        EXPECT_NEAR(ease(curve, EaseMode::InOut, 0.5F), 0.5F, 1e-5F);
    }
    EXPECT_LT(ease(EaseCurve::Back, EaseMode::In, 0.3F), 0.0F) << "Back pulls back first";
    EXPECT_GT(ease(EaseCurve::Back, EaseMode::Out, 0.7F), 1.0F) << "and overshoots at the end of Out";

    // The plain power curves never leave 0..1 and only ever grow.
    float previous = 0.0F;
    for (float t = 0.0F; t <= 1.0F; t += 0.01F)
    {
        const float value = ease(EaseCurve::Cubic, EaseMode::InOut, t);
        ASSERT_GE(value, previous);
        previous = value;
    }
}
