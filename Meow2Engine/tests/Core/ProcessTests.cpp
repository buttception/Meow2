/// @file
/// @brief Tests for Meow2/Core/Process.h.

#include <Meow2/Core/Process.h>

#include <gtest/gtest.h>

TEST(Process, ExecutablePathIsThisTestProgram)
{
    const std::filesystem::path path = Meow2::executablePath();
    ASSERT_FALSE(path.empty());
    EXPECT_TRUE(path.is_absolute());
    EXPECT_TRUE(std::filesystem::is_regular_file(path));
    EXPECT_EQ(path.stem(), "Meow2EngineCoreTests");
}
