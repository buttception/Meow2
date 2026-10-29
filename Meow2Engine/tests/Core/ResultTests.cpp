/// @file
/// @brief Tests for Meow2/Core/Result.h: values and errors, Result<void>, the std::expected-compatible monadic
///        operations, formatting and the routed TL_ASSERT.

#include <Meow2/Core/Result.h>

#include <gtest/gtest.h>

#include <format>
#include <set>
#include <string>

namespace
{
using Meow2::Error;
using Meow2::ErrorCode;
using Meow2::Result;

Result<int> parsePositive(int input)
{
    if (input <= 0)
    {
        return Meow2::fail(ErrorCode::InvalidArgument, std::format("{} is not positive", input));
    }
    return input;
}

Result<void> touch(bool succeed)
{
    if (!succeed)
    {
        return Meow2::fail(ErrorCode::AccessDenied);
    }
    return {};
}
} // namespace

TEST(Result, HoldsValueOrError)
{
    const Result<int> success = parsePositive(5);
    ASSERT_TRUE(success.has_value());
    EXPECT_TRUE(static_cast<bool>(success));
    EXPECT_EQ(*success, 5);
    EXPECT_EQ(success.value_or(0), 5);

    const Result<int> failure = parsePositive(-1);
    ASSERT_FALSE(failure.has_value());
    EXPECT_EQ(failure.error().code, ErrorCode::InvalidArgument);
    EXPECT_EQ(failure.error().message, "-1 is not positive");
    EXPECT_EQ(failure.value_or(0), 0);
}

TEST(Result, VoidResult)
{
    EXPECT_TRUE(touch(true).has_value());
    const Result<void> failure = touch(false);
    ASSERT_FALSE(failure.has_value());
    EXPECT_EQ(failure.error(), (Error{ErrorCode::AccessDenied, ""}));
}

TEST(Result, MonadicStdSubset)
{
    // and_then: chain another fallible step; transform: change the value; both skip on error.
    const auto doubled = [](int value)
    {
        return value * 2;
    };
    const auto checkedHalf = [](int value) -> Result<int>
    {
        if (value % 2 != 0)
        {
            return Meow2::fail(ErrorCode::InvalidArgument, "odd");
        }
        return value / 2;
    };

    EXPECT_EQ(parsePositive(3).transform(doubled).and_then(checkedHalf), Result<int>{3});
    EXPECT_EQ(parsePositive(-3).transform(doubled).and_then(checkedHalf).error().code, ErrorCode::InvalidArgument);
    EXPECT_EQ(parsePositive(3).and_then(checkedHalf).error().message, "odd");

    // or_else: recover from an error; transform_error: change the error.
    const Result<int> recovered = parsePositive(-1).or_else([](const Error&) -> Result<int> { return 1; });
    EXPECT_EQ(recovered, Result<int>{1});

    const Result<int> renamed = parsePositive(-1).transform_error(
        [](Error error)
        {
            error.code = ErrorCode::ParseError;
            return error;
        });
    EXPECT_EQ(renamed.error().code, ErrorCode::ParseError);
}

TEST(Result, ErrorFormats)
{
    EXPECT_EQ(std::format("{}", Error{ErrorCode::NotFound, "config.toml"}), "NotFound: config.toml");
    EXPECT_EQ(std::format("{}", Error{ErrorCode::NotFound, ""}), "NotFound");
    const Result<int> timedOut = Meow2::fail(ErrorCode::Timeout, "GPU fence");
    EXPECT_EQ(std::format("[{}]", timedOut.error()), "[Timeout: GPU fence]");
}

TEST(Result, ErrorCodeNames)
{
    std::set<std::string_view> names;
    for (int code = static_cast<int>(ErrorCode::Unknown); code <= static_cast<int>(ErrorCode::Cancelled); ++code)
    {
        const std::string_view name = Meow2::toString(static_cast<ErrorCode>(code));
        EXPECT_FALSE(name.empty());
        names.insert(name);
    }
    EXPECT_EQ(names.size(), static_cast<std::size_t>(ErrorCode::Cancelled) + 1); // all distinct
}

TEST(Result, DereferencingErrorAsserts)
{
    if constexpr (!Meow2::c_assertsEnabled)
    {
        GTEST_SKIP() << "asserts are compiled out in this configuration";
    }
    EXPECT_DEATH(
        {
            const Result<int> failure = parsePositive(-1);
            [[maybe_unused]] const int value = *failure;
        },
        "Assertion failed: has_value\\(\\)");
}
