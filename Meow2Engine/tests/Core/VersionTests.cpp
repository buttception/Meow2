#include <Meow2/Core/Version.h>

#include <gtest/gtest.h>

#include <regex>
#include <string>

namespace
{
/// @brief "MAJOR.MINOR.PATCH" from CMake's project(VERSION), passed in by the test target.
std::string projectVersion()
{
    return std::to_string(MEOW2_TEST_VERSION_MAJOR) + "." + std::to_string(MEOW2_TEST_VERSION_MINOR) + "." +
           std::to_string(MEOW2_TEST_VERSION_PATCH);
}
} // namespace

TEST(Version, MatchesProjectVersion)
{
    EXPECT_EQ(Meow2::c_versionMajor, MEOW2_TEST_VERSION_MAJOR);
    EXPECT_EQ(Meow2::c_versionMinor, MEOW2_TEST_VERSION_MINOR);
    EXPECT_EQ(Meow2::c_versionPatch, MEOW2_TEST_VERSION_PATCH);
}

TEST(Version, StringIsMajorMinorPatch)
{
    // The header constant (what this test was compiled against) has no build metadata.
    EXPECT_EQ(std::string(Meow2::c_versionString), projectVersion());
}

TEST(Version, FullStringHasBuildMetadata)
{
    // The library (what was actually linked): the same version, plus the commit when built from a git repository.
    const std::string metadata = Meow2::getBuildMetadata();
    const std::string full = Meow2::getVersionString();
    if (metadata.empty())
    {
        EXPECT_EQ(full, projectVersion());
    }
    else
    {
        EXPECT_TRUE(std::regex_match(metadata, std::regex{"g[0-9a-f]{7,}(\\.dirty)?"})) << metadata;
        EXPECT_EQ(full, projectVersion() + "+" + metadata);
    }
}
