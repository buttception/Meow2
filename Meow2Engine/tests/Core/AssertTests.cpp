/// @file
/// @brief Tests for Meow2/Core/Assert.h: enabled/disabled behaviour per configuration, the default handler (death
///        tests) and custom handlers.

#include <Meow2/Core/Assert.h>

#include <gtest/gtest.h>

#include <cstring>

namespace
{
// GLOBAL: AssertHandler is a plain function pointer (no captures), so the recording handler
// stores what it saw here. Reset by the fixture before every test.
Meow2::AssertInfo s_lastInfo{};
int s_handlerCalls = 0;

Meow2::AssertAction recordAndContinue(const Meow2::AssertInfo& info)
{
    s_lastInfo = info;
    ++s_handlerCalls;
    return Meow2::AssertAction::Continue;
}

/// @brief Installs the recording handler for one test and restores the previous handler afterwards.
class AssertWithHandler : public ::testing::Test
{
protected:
    void SetUp() override
    {
        s_lastInfo = {};
        s_handlerCalls = 0;
        m_previous = Meow2::setAssertHandler(&recordAndContinue);
    }

    void TearDown() override
    {
        Meow2::setAssertHandler(m_previous);
    }

private:
    Meow2::AssertHandler m_previous = nullptr;
};
} // namespace

TEST(Assert, PassesWhenTrue)
{
    int value = 1;
    MEOW2_ASSERT(value == 1);
    MEOW2_ASSERT(value == 1, "never shown");
    MEOW2_VERIFY(value == 1);
    MEOW2_ASSERT_SLOW(value == 1);
    SUCCEED();
}

TEST(Assert, FailureAbortsWithMessage)
{
    if constexpr (!Meow2::c_assertsEnabled)
    {
        GTEST_SKIP() << "asserts are compiled out in this configuration";
    }
    int value = 1;
    // No debugger in the child process, so the default handler prints and aborts.
    EXPECT_DEATH(MEOW2_ASSERT(value == 2), "Assertion failed: value == 2");
    EXPECT_DEATH(MEOW2_ASSERT(value == 2, "boom"), "Message: boom");
}

TEST_F(AssertWithHandler, CustomHandlerReceivesInfo)
{
    if constexpr (!Meow2::c_assertsEnabled)
    {
        GTEST_SKIP() << "asserts are compiled out in this configuration";
    }
    int value = 1;
    bool reachedAfterAssert = false;

    const int expectedLine = __LINE__ + 1;
    MEOW2_ASSERT(value == 2, "custom message");
    reachedAfterAssert = true; // AssertAction::Continue: execution goes on

    EXPECT_TRUE(reachedAfterAssert);
    ASSERT_EQ(s_handlerCalls, 1);
    EXPECT_STREQ(s_lastInfo.expression, "value == 2");
    EXPECT_STREQ(s_lastInfo.message, "custom message");
    EXPECT_NE(std::strstr(s_lastInfo.file, "AssertTests.cpp"), nullptr);
    EXPECT_EQ(s_lastInfo.line, expectedLine);
    EXPECT_NE(std::strstr(s_lastInfo.function, "TestBody"), nullptr);
}

TEST_F(AssertWithHandler, EmptyMessageIsEmptyString)
{
    if constexpr (!Meow2::c_assertsEnabled)
    {
        GTEST_SKIP() << "asserts are compiled out in this configuration";
    }
    int value = 1;
    MEOW2_ASSERT(value == 2);
    ASSERT_EQ(s_handlerCalls, 1);
    ASSERT_NE(s_lastInfo.message, nullptr);
    EXPECT_STREQ(s_lastInfo.message, "");
}

TEST(Assert, DisabledDoesNotEvaluate)
{
    int evaluations = 0;
    MEOW2_ASSERT(++evaluations == 1);
    EXPECT_EQ(evaluations, Meow2::c_assertsEnabled ? 1 : 0);
}

TEST(Assert, SlowOnlyInDebug)
{
    int evaluations = 0;
    MEOW2_ASSERT_SLOW(++evaluations == 1);
    EXPECT_EQ(evaluations, Meow2::c_slowAssertsEnabled ? 1 : 0);
    EXPECT_EQ(Meow2::c_slowAssertsEnabled, Meow2::c_isDebugBuild);
}

TEST(Assert, EnabledOutsideRelease)
{
    EXPECT_EQ(Meow2::c_assertsEnabled, MEOW2_CONFIG_RELEASE == 0);
}

TEST(Verify, AlwaysEvaluates)
{
    int evaluations = 0;
    MEOW2_VERIFY(++evaluations == 1);
    EXPECT_EQ(evaluations, 1);
}

TEST(Verify, FailureAbortsWhenEnabled)
{
    if constexpr (!Meow2::c_assertsEnabled)
    {
        GTEST_SKIP() << "asserts are compiled out in this configuration";
    }
    int value = 1;
    EXPECT_DEATH(MEOW2_VERIFY(value == 2), "Assertion failed: value == 2");
}

TEST(Assert, SetHandlerReturnsPrevious)
{
    const Meow2::AssertHandler original = Meow2::setAssertHandler(&recordAndContinue);
    EXPECT_EQ(original, &Meow2::defaultAssertHandler);

    // nullptr restores the default and hands back the handler that was installed.
    EXPECT_EQ(Meow2::setAssertHandler(nullptr), &recordAndContinue);
    EXPECT_EQ(Meow2::setAssertHandler(nullptr), &Meow2::defaultAssertHandler);
}
