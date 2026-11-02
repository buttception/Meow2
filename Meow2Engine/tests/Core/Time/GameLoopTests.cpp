/// @file
/// @brief Tests for GameLoop (phase order, fixed steps, event dispatch points, long frames) and its config keys.

#include <Meow2/Core/Config/Config.h>
#include <Meow2/Core/Events/EventBus.h>
#include <Meow2/Core/Log/LogSystem.h>
#include <Meow2/Core/Platform.h>
#include <Meow2/Core/Time/FrameClock.h>
#include <Meow2/Core/Time/GameLoop.h>
#include <Meow2/Core/Time/GameLoopConfig.h>

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <memory_resource>
#include <string>
#include <system_error>
#include <vector>

namespace
{
namespace fs = std::filesystem;
using Meow2::Time::FrameClock;
using Meow2::Time::GameLoop;
using Meow2::Time::GameLoopCallbacks;
using Meow2::Time::GameLoopDesc;
using namespace std::chrono_literals;

constexpr double c_tolerance = 1e-9;

/// @brief An event for the dispatch tests.
struct Ping
{
    int value = 0; ///< Which ping.
};

/// @brief A clock that starts at a fixed time, a bus, and a record of what the loop called.
class GameLoopTest : public ::testing::Test
{
protected:
    /// @brief Callbacks that append to m_calls (and record the values they got).
    GameLoopCallbacks recordingCallbacks()
    {
        return {
            .pumpEvents = [this] { m_calls.emplace_back("pump"); },
            .fixedUpdate =
                [this](double dt)
            {
                m_calls.emplace_back("fixed");
                m_fixedDeltas.push_back(dt);
            },
            .update =
                [this](double dt)
            {
                m_calls.emplace_back("update");
                m_updateDeltas.push_back(dt);
            },
            .render =
                [this](double alpha)
            {
                m_calls.emplace_back("render");
                m_alphas.push_back(alpha);
            },
        };
    }

    const FrameClock::TimePoint m_start{};
    FrameClock m_clock{m_start};
    Meow2::Events::EventBus m_events{*std::pmr::new_delete_resource()};
    std::vector<std::string> m_calls;
    std::vector<double> m_fixedDeltas;
    std::vector<double> m_updateDeltas;
    std::vector<double> m_alphas;
};
} // namespace

TEST_F(GameLoopTest, CallsPhasesInOrder)
{
    GameLoop loop{m_clock, m_events, {.fixedStepSeconds = 0.010, .maxFixedStepsPerFrame = 8}, recordingCallbacks()};
    loop.tick(m_start + 25ms);
    const std::vector<std::string> expected{"pump", "fixed", "fixed", "update", "render"};
    EXPECT_EQ(m_calls, expected);
}

TEST_F(GameLoopTest, RunsFixedStepsForElapsedTime)
{
    constexpr double c_step = 1.0 / 60.0;
    GameLoop loop{m_clock, m_events, {.fixedStepSeconds = c_step, .maxFixedStepsPerFrame = 8}, recordingCallbacks()};
    loop.tick(m_start + 50ms);

    ASSERT_EQ(m_fixedDeltas.size(), 3U) << "50 ms hold three 16.7 ms steps";
    for (const double dt : m_fixedDeltas)
    {
        EXPECT_DOUBLE_EQ(dt, c_step) << "fixedUpdate always gets the same step";
    }
    ASSERT_EQ(m_updateDeltas.size(), 1U);
    EXPECT_NEAR(m_updateDeltas[0], 0.050, c_tolerance) << "update gets the frame's real delta";
    ASSERT_EQ(m_alphas.size(), 1U);
    EXPECT_NEAR(m_alphas[0], (0.050 - 3 * c_step) / c_step, 1e-6);
    EXPECT_EQ(loop.fixedStepCount(), 3U);
    EXPECT_EQ(m_clock.frameCount(), 1U);
}

TEST_F(GameLoopTest, DispatchesQueuedEvents)
{
    GameLoopCallbacks callbacks = recordingCallbacks();
    callbacks.pumpEvents = [this]
    {
        m_calls.emplace_back("pump");
        m_events.enqueue(Ping{1}); // like a window event from the platform
    };
    callbacks.update = [this](double)
    {
        m_calls.emplace_back("update");
        m_events.enqueue(Ping{2}); // like gameplay announcing something
    };
    const Meow2::Events::Subscription onPing =
        m_events.subscribe<Ping>([this](const Ping& ping) { m_calls.push_back(std::format("ping{}", ping.value)); });

    GameLoop loop{m_clock, m_events, {.fixedStepSeconds = 0.010, .maxFixedStepsPerFrame = 8}, callbacks};
    loop.tick(m_start + 10ms);
    const std::vector<std::string> expected{"pump", "ping1", "fixed", "update", "ping2", "render"};
    EXPECT_EQ(m_calls, expected);
}

TEST_F(GameLoopTest, LongFrameRunsBoundedSteps)
{
    GameLoop loop{
        m_clock, m_events, {.fixedStepSeconds = 1.0 / 60.0, .maxFixedStepsPerFrame = 8}, recordingCallbacks()};
    loop.tick(m_start + 10s); // a breakpoint or a stall
    EXPECT_EQ(m_fixedDeltas.size(), 8U) << "clamped to 0.25 s (15 steps), then capped at 8";
    EXPECT_TRUE(loop.droppedTime());
    ASSERT_EQ(m_updateDeltas.size(), 1U);
    EXPECT_NEAR(m_updateDeltas[0], FrameClock::c_maxFrameDeltaSeconds, c_tolerance);

    loop.tick(m_start + 10s + 16ms);
    EXPECT_FALSE(loop.droppedTime()) << "the next frame is normal again";
}

TEST_F(GameLoopTest, EmptyCallbacksAreSkipped)
{
    GameLoop loop{m_clock, m_events, {}, {}};
    loop.tick(m_start + 20ms);
    loop.tick(m_start + 40ms);
    EXPECT_EQ(m_clock.frameCount(), 2U);
    EXPECT_EQ(loop.fixedStepCount(), 2U) << "the default 60 Hz step still advances";
}

namespace
{
/// @brief A config read from a temporary file, plus a log whose text the tests check.
class GameLoopConfigTest : public ::testing::Test
{
protected:
    GameLoopConfigTest()
        : m_directory(directory())
        , m_logSystem({.appName = "Test", .directory = m_directory, .console = false})
        , m_log(m_logSystem.createLogger("Engine"))
        , m_config(Meow2::Config::Config::empty(m_directory / "Meow2Test.toml", m_log))
    {
    }

    /// @brief Applies `--set` overrides, as an app's command line would.
    void set(std::initializer_list<const char*> settings)
    {
        std::vector<const char*> argv{"app"};
        for (const char* setting : settings)
        {
            argv.push_back("--set");
            argv.push_back(setting);
        }
        ASSERT_TRUE(m_config.applyArguments(static_cast<int>(argv.size()), argv.data()).has_value());
    }

    /// @brief Everything logged so far.
    std::string logText()
    {
        m_logSystem.flush();
        std::ifstream file(m_logSystem.filePath(), std::ios::binary);
        return std::string{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    }

    fs::path m_directory;
    Meow2::Log::LogSystem m_logSystem;
    Meow2::Log::Logger m_log;
    Meow2::Config::Config m_config;

private:
    static fs::path directory()
    {
        const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
        const fs::path path =
            fs::temp_directory_path() / std::format("meow2-gameloop-{}-{}", Meow2::c_configName, info->name());
        std::error_code ignored;
        fs::remove_all(path, ignored);
        return path;
    }
};
} // namespace

TEST_F(GameLoopConfigTest, ReadsValues)
{
    const GameLoopDesc defaults = Meow2::Time::gameLoopDescFromConfig(m_config, m_log);
    EXPECT_DOUBLE_EQ(defaults.fixedStepSeconds, 1.0 / 60.0);
    EXPECT_EQ(defaults.maxFixedStepsPerFrame, 8U);

    set({"loop.fixedStepHz=120", "loop.maxFixedSteps=4"});
    const GameLoopDesc desc = Meow2::Time::gameLoopDescFromConfig(m_config, m_log);
    EXPECT_DOUBLE_EQ(desc.fixedStepSeconds, 1.0 / 120.0);
    EXPECT_EQ(desc.maxFixedStepsPerFrame, 4U);
}

TEST_F(GameLoopConfigTest, BadValuesUseDefaults)
{
    set({"loop.fixedStepHz=0", "loop.maxFixedSteps=1000"});
    const GameLoopDesc desc = Meow2::Time::gameLoopDescFromConfig(m_config, m_log);
    EXPECT_DOUBLE_EQ(desc.fixedStepSeconds, 1.0 / 60.0);
    EXPECT_EQ(desc.maxFixedStepsPerFrame, 8U);
    const std::string text = logText();
    EXPECT_NE(text.find("loop.fixedStepHz = 0 is outside 1..1000; using 60"), std::string::npos) << text;
    EXPECT_NE(text.find("loop.maxFixedSteps = 1000 is outside 1..64; using 8"), std::string::npos) << text;
}
