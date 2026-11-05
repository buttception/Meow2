/// @file
/// @brief Tests for the CPU feature check: detection on this machine, the build's requirements, the message text.

#include <Meow2/Core/CpuFeatures.h>

#include <gtest/gtest.h>

#include <string>

using Meow2::CpuFeatures;

TEST(CpuFeatures, DetectsThisMachine)
{
    const CpuFeatures features = Meow2::detectCpuFeatures();
#if defined(_M_X64) || defined(__x86_64__)
    EXPECT_TRUE(features.sse42) << "every x64 machine CI and developers use has SSE4.2";
#if defined(__AVX2__)
    // This test binary itself uses AVX2; if it's running, the CPU has it, and detection must agree.
    EXPECT_TRUE(features.avx);
    EXPECT_TRUE(features.avx2);
    EXPECT_TRUE(features.fma3);
    EXPECT_TRUE(features.f16c);
#endif
    EXPECT_TRUE(!features.avx2 || features.avx) << "AVX2 implies AVX";
#else
    EXPECT_FALSE(features.avx2) << "no x64 features on other architectures";
#endif
}

TEST(CpuFeatures, DescribesMissing)
{
    CpuFeatures required;
    required.avx2 = true;
    required.fma3 = true;
    required.sse42 = true;
    CpuFeatures available;
    available.sse42 = true;

    char text[64];
    EXPECT_EQ(Meow2::describeMissingCpuFeatures(required, available, text, sizeof text), 2U);
    EXPECT_EQ(std::string{text}, "AVX2, FMA3");

    EXPECT_EQ(Meow2::describeMissingCpuFeatures(required, required, text, sizeof text), 0U);
    EXPECT_EQ(std::string{text}, "") << "nothing missing, nothing written";

    char tiny[6];
    EXPECT_EQ(Meow2::describeMissingCpuFeatures(required, available, tiny, sizeof tiny), 2U)
        << "the count is right even when the text doesn't fit";
    EXPECT_EQ(std::string{tiny}, "AVX2,") << "truncated, still null-terminated";
}

TEST(CpuFeatures, RequiredMatchesBuild)
{
    const CpuFeatures required = Meow2::requiredCpuFeatures();
#if defined(__AVX2__)
    EXPECT_TRUE(required.avx2 && required.avx && required.fma3 && required.f16c && required.sse42);
#elif defined(_M_X64) || defined(__x86_64__)
    EXPECT_TRUE(required.sse42);
    EXPECT_FALSE(required.avx2) << "an SSE4.2 build doesn't require AVX2";
#else
    EXPECT_FALSE(required.sse42);
#endif
    EXPECT_TRUE(Meow2::checkCpuFeatures("Meow2EngineCoreTests")) << "the machine running the tests can run the build";
}
