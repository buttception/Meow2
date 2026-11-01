/// @file
/// @brief Tests for WindowDesc ⇄ config (WindowConfig.h).

#include <Meow2/Core/Config/Config.h>
#include <Meow2/Core/Log/LogSystem.h>
#include <Meow2/Platform/Platform.h>
#include <Meow2/Platform/Window/WindowConfig.h>

#include <gtest/gtest.h>

#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>

namespace
{
namespace fs = std::filesystem;
using Meow2::Config::Config;
using Meow2::Platform::WindowDesc;
using Meow2::Platform::WindowMode;

/// @brief A log and an empty config in a fresh folder per test.
class WindowConfigTest : public ::testing::Test
{
protected:
    WindowConfigTest()
        : m_directory(makeDirectory())
        , m_logSystem({.appName = "Test", .directory = m_directory, .console = false})
        , m_log(m_logSystem.createLogger("Engine"))
        , m_config(Config::empty(m_directory / "Meow2Test.toml", m_log))
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
    Config m_config;

private:
    static fs::path makeDirectory()
    {
        const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
        const fs::path directory =
            fs::temp_directory_path() / std::format("meow2-windowconfig-{}-{}", Meow2::c_configName, info->name());
        std::error_code ignored;
        fs::remove_all(directory, ignored);
        return directory;
    }
};
} // namespace

TEST_F(WindowConfigTest, MissingKeysUseDefaults)
{
    const WindowDesc defaults{.title = "App", .width = 1024, .height = 576};
    const WindowDesc desc = Meow2::Platform::windowDescFromConfig(m_config, defaults, m_log);
    EXPECT_EQ(desc.title, "App");
    EXPECT_EQ(desc.width, 1024);
    EXPECT_EQ(desc.height, 576);
    EXPECT_FALSE(desc.x.has_value());
    EXPECT_FALSE(desc.maximized);
    EXPECT_EQ(desc.mode, WindowMode::Windowed);
}

TEST_F(WindowConfigTest, ReadsDesc)
{
    set({"window.width=1600", "window.height=900", "window.x=-200", "window.y=100", "window.maximized=true",
         "window.mode=fullscreen"});
    const WindowDesc desc = Meow2::Platform::windowDescFromConfig(m_config, {}, m_log);
    EXPECT_EQ(desc.width, 1600);
    EXPECT_EQ(desc.height, 900);
    EXPECT_EQ(desc.x, -200) << "negative: a monitor left of the primary one";
    EXPECT_EQ(desc.y, 100);
    EXPECT_TRUE(desc.maximized);
    EXPECT_EQ(desc.mode, WindowMode::Fullscreen);
}

TEST_F(WindowConfigTest, BadValuesUseDefaults)
{
    set({"window.width=5", "window.height=100000", "window.mode=huge", "window.x=99999999999"});
    const WindowDesc desc = Meow2::Platform::windowDescFromConfig(m_config, {.width = 1280, .height = 720}, m_log);
    EXPECT_EQ(desc.width, 1280);
    EXPECT_EQ(desc.height, 720);
    EXPECT_EQ(desc.mode, WindowMode::Windowed);
    EXPECT_FALSE(desc.x.has_value());
    const std::string text = logText();
    EXPECT_NE(text.find("window.width = 5 is outside 160..16384; using 1280"), std::string::npos) << text;
    EXPECT_NE(text.find("window.height = 100000 is outside"), std::string::npos) << text;
    EXPECT_NE(text.find("window.mode = \"huge\" isn't"), std::string::npos) << text;
    EXPECT_NE(text.find("window.x = 99999999999 is out of range"), std::string::npos) << text;
}

TEST_F(WindowConfigTest, StoresState)
{
    Meow2::Result<Meow2::Platform::Platform> platform =
        Meow2::Platform::Platform::create({.videoDriver = "offscreen"}, m_log);
    ASSERT_TRUE(platform.has_value()) << std::format("{}", platform.error());
    Meow2::Result<Meow2::Platform::Window> window = Meow2::Platform::Window::create(
        *platform, m_log, {.width = 700, .height = 500, .x = 30, .y = 40, .hidden = true});
    ASSERT_TRUE(window.has_value()) << std::format("{}", window.error());
    window->setMode(WindowMode::Fullscreen);
    platform->pumpEvents();

    Meow2::Platform::storeWindowState(m_config, *window);
    EXPECT_TRUE(m_config.hasUnsavedChanges());
    EXPECT_EQ(m_config.getInt("window.width", 0), 700) << "the restored size, not the fullscreen one";
    EXPECT_EQ(m_config.getInt("window.height", 0), 500);
    EXPECT_EQ(m_config.getInt("window.x", 0), 30);
    EXPECT_EQ(m_config.getInt("window.y", 0), 40);
    EXPECT_FALSE(m_config.getBool("window.maximized", true));
    EXPECT_EQ(m_config.getString("window.mode", ""), "fullscreen");

    // Round trip: the stored keys give back the same desc.
    const WindowDesc desc = Meow2::Platform::windowDescFromConfig(m_config, {}, m_log);
    EXPECT_EQ(desc.width, 700);
    EXPECT_EQ(desc.x, 30);
    EXPECT_EQ(desc.mode, WindowMode::Fullscreen);
}
