#include <Meow2/Core/Version.h>

#include <gtest/gtest.h>

#include <string>

TEST(Version, MatchesProjectVersion)
{
    EXPECT_EQ(Meow2::c_versionMajor, MEOW2_TEST_VERSION_MAJOR);
    EXPECT_EQ(Meow2::c_versionMinor, MEOW2_TEST_VERSION_MINOR);
    EXPECT_EQ(Meow2::c_versionPatch, MEOW2_TEST_VERSION_PATCH);
}

TEST(Version, StringIsMajorMinorPatch)
{
    const std::string c_expected = std::to_string(MEOW2_TEST_VERSION_MAJOR) + "." +
                                   std::to_string(MEOW2_TEST_VERSION_MINOR) + "." +
                                   std::to_string(MEOW2_TEST_VERSION_PATCH);

    // The header constant (what this test was compiled against) ...
    EXPECT_EQ(std::string(Meow2::c_versionString), c_expected);
    // ... and the library function (what was actually linked) must agree.
    EXPECT_EQ(std::string(Meow2::getVersionString()), c_expected);
}
