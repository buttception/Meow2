#include <Meow2/Core/Platform.h>

#include <gtest/gtest.h>

#include <string>

TEST(Platform, ExactlyOnePlatform)
{
    EXPECT_EQ(MEOW2_PLATFORM_WINDOWS + MEOW2_PLATFORM_LINUX + MEOW2_PLATFORM_ANDROID, 1);
}

TEST(Platform, ExactlyOneCompiler)
{
    EXPECT_EQ(MEOW2_COMPILER_MSVC + MEOW2_COMPILER_CLANG + MEOW2_COMPILER_GCC, 1);
}

TEST(Platform, ExactlyOneArchitecture)
{
    EXPECT_EQ(MEOW2_ARCH_X64 + MEOW2_ARCH_ARM64, 1);
}

TEST(Platform, ExactlyOneConfig)
{
    EXPECT_EQ(MEOW2_CONFIG_DEBUG + MEOW2_CONFIG_RELWITHDEBINFO + MEOW2_CONFIG_RELEASE, 1);
}

TEST(Platform, ConfigMatchesBuildType)
{
    const std::string c_buildType = MEOW2_TEST_BUILD_TYPE;
    const std::string c_expected = c_buildType == "MinSizeRel" ? "Release" : c_buildType;

    EXPECT_EQ(std::string(Meow2::c_configName), c_expected);
    EXPECT_EQ(Meow2::c_isDebugBuild, c_buildType == "Debug");
}

TEST(Platform, MatchesHost)
{
    // CMAKE_SYSTEM_NAME is the platform CMake is building for.
    EXPECT_EQ(std::string(Meow2::c_platformName), std::string(MEOW2_TEST_SYSTEM_NAME));
}
