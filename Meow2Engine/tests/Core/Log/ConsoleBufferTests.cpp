/// @file
/// @brief ConsoleBuffer: the ring of recent log lines behind in-app consoles.

#include <Meow2/Core/Log/ConsoleBuffer.h>

#include <gtest/gtest.h>

#include <format>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace Meow2::Log
{
namespace
{
LogLine line(std::string message)
{
    return {.sequence = 0,
            .time = "00:00:00.000",
            .level = LogLevel::Info,
            .logger = "Test",
            .message = std::move(message)};
}
} // namespace

TEST(ConsoleBuffer, KeepsTheNewestLines)
{
    ConsoleBuffer buffer{3};
    for (int i = 0; i < 5; ++i)
    {
        buffer.add(line(std::format("line {}", i)));
    }

    std::vector<LogLine> lines;
    EXPECT_EQ(buffer.linesSince(0, lines), 2U) << "lines 0 and 1 were overwritten";
    ASSERT_EQ(lines.size(), 3U);
    EXPECT_EQ(lines[0].message, "line 2");
    EXPECT_EQ(lines[2].message, "line 4");
    EXPECT_EQ(lines[0].sequence, 2U);
    EXPECT_EQ(buffer.nextSequence(), 5U);
    EXPECT_EQ(buffer.capacity(), 3U);
}

TEST(ConsoleBuffer, ReadsOnlyNewLines)
{
    ConsoleBuffer buffer{4};
    buffer.add(line("a"));
    buffer.add(line("b"));
    std::vector<LogLine> lines;
    EXPECT_EQ(buffer.linesSince(0, lines), 0U);
    std::uint64_t next = buffer.nextSequence();
    ASSERT_EQ(lines.size(), 2U);

    lines.clear();
    buffer.add(line("c"));
    EXPECT_EQ(buffer.linesSince(next, lines), 0U);
    ASSERT_EQ(lines.size(), 1U);
    EXPECT_EQ(lines[0].message, "c");
    next = buffer.nextSequence();

    lines.clear();
    for (int i = 0; i < 6; ++i)
    {
        buffer.add(line(std::format("x{}", i)));
    }
    EXPECT_EQ(buffer.linesSince(next, lines), 2U) << "6 new lines in a ring of 4: the first 2 are gone";
    ASSERT_EQ(lines.size(), 4U);
    EXPECT_EQ(lines.back().message, "x5");

    lines.clear();
    EXPECT_EQ(buffer.linesSince(buffer.nextSequence(), lines), 0U);
    EXPECT_TRUE(lines.empty()) << "nothing new";
}

TEST(ConsoleBuffer, ManyWritersAtOnce)
{
    constexpr int c_threads = 4;
    constexpr int c_perThread = 500;
    ConsoleBuffer buffer{c_threads * c_perThread};
    std::vector<std::thread> threads;
    for (int t = 0; t < c_threads; ++t)
    {
        threads.emplace_back(
            [&buffer, t]
            {
                for (int i = 0; i < c_perThread; ++i)
                {
                    buffer.add(line(std::format("thread {} line {}", t, i)));
                }
            });
    }
    // A reader at the same time, as the console window is.
    std::vector<LogLine> seen;
    std::uint64_t next = 0;
    while (next < static_cast<std::uint64_t>(c_threads * c_perThread))
    {
        static_cast<void>(buffer.linesSince(next, seen));
        next = seen.empty() ? 0 : seen.back().sequence + 1;
    }
    for (std::thread& thread : threads)
    {
        thread.join();
    }

    ASSERT_EQ(seen.size(), static_cast<std::size_t>(c_threads * c_perThread));
    for (std::size_t i = 0; i < seen.size(); ++i)
    {
        EXPECT_EQ(seen[i].sequence, i);
        EXPECT_TRUE(seen[i].message.starts_with("thread ")) << seen[i].message;
    }
}

TEST(ConsoleBuffer, CutsLongMessagesOnACharacterBoundary)
{
    ConsoleBuffer buffer{2};
    // 999 ASCII bytes then a 3-byte character: the limit (1000) falls inside it, so the whole character goes.
    std::string message(ConsoleBuffer::c_maxMessageBytes - 1, 'a');
    message += "\xE2\x82\xAC tail"; // the euro sign
    buffer.add(LogLevel::Warn, "12:00:00.000", "AVeryLongLoggerNameThatGoesOnAndOnAndOn", message);
    buffer.add(LogLevel::Info, "12:00:00.001", "Test", "short");

    std::vector<LogLine> lines;
    static_cast<void>(buffer.linesSince(0, lines));
    ASSERT_EQ(lines.size(), 2U);
    EXPECT_EQ(lines[0].message, std::string(ConsoleBuffer::c_maxMessageBytes - 1, 'a') + "\xE2\x80\xA6")
        << "cut before the euro sign, then an ellipsis";
    EXPECT_EQ(lines[0].logger.size(), ConsoleBuffer::c_maxLoggerBytes);
    EXPECT_EQ(lines[0].level, LogLevel::Warn);
    EXPECT_EQ(lines[1].message, "short") << "short lines are kept as they are";
}
} // namespace Meow2::Log
