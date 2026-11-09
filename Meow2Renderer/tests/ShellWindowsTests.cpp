/// @file
/// @brief The Console and Settings windows' logic, and the ImGui layout's trip through the config.

#include "ConsoleWindow.h"
#include "SettingsWindow.h"

#include <Meow2/Core/Config/Config.h>
#include <Meow2/Core/Log/ConsoleBuffer.h>
#include <Meow2/Core/Log/LogSystem.h>
#include <Meow2/Core/Platform.h>

#include <gtest/gtest.h>
#include <imgui.h>

#include <filesystem>
#include <format>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace Meow2::RendererApp
{
namespace
{
namespace fs = std::filesystem;

/// @brief A fresh folder for one test.
fs::path freshFolder(std::string_view test)
{
    const fs::path folder = fs::temp_directory_path() / std::format("meow2-shell-{}-{}", c_configName, test);
    std::error_code error;
    fs::remove_all(folder, error);
    fs::create_directories(folder, error);
    return folder;
}

Log::LogLine line(Log::LogLevel level, std::string logger, std::string message)
{
    return {.sequence = 0,
            .time = "12:00:00.000",
            .level = level,
            .logger = std::move(logger),
            .message = std::move(message)};
}

/// @brief A logger for Config, writing into a test folder only.
class TestLog
{
public:
    explicit TestLog(const fs::path& folder)
        : m_system({.appName = "Test", .directory = folder / "logs", .console = false})
        , m_log(m_system.createLogger("Test"))
    {
    }

    Log::Logger& log()
    {
        return m_log;
    }

private:
    Log::LogSystem m_system;
    Log::Logger m_log;
};
} // namespace

TEST(ConsoleWindow, FiltersByLevelAndText)
{
    const Log::LogLine debug = line(Log::LogLevel::Debug, "Engine", "Jobs: 16 threads");
    const Log::LogLine warn = line(Log::LogLevel::Warn, "RHI", "Swapchain recreated");
    const Log::LogLine error = line(Log::LogLevel::Error, "Renderer", "Image 3 not uploaded");

    ConsoleFilter filter;
    EXPECT_TRUE(consoleLineMatches(debug, filter)) << "everything shows by default";
    filter.levels[static_cast<std::size_t>(Log::LogLevel::Debug)] = false;
    EXPECT_FALSE(consoleLineMatches(debug, filter));
    EXPECT_TRUE(consoleLineMatches(warn, filter));

    filter.text = "swapchain";
    EXPECT_TRUE(consoleLineMatches(warn, filter)) << "any case";
    EXPECT_FALSE(consoleLineMatches(error, filter));
    filter.text = "render";
    EXPECT_TRUE(consoleLineMatches(error, filter)) << "the logger's name counts too";
}

TEST(ConsoleWindow, CopiesNewLinesAndKeepsAtMostTheCapacity)
{
    Log::ConsoleBuffer buffer{4};
    ConsoleWindow window{buffer};
    buffer.add(line(Log::LogLevel::Info, "Test", "first"));
    window.update();
    ASSERT_EQ(window.lines().size(), 1U);

    for (int i = 0; i < 6; ++i)
    {
        buffer.add(line(Log::LogLevel::Info, "Test", std::format("more {}", i)));
    }
    window.update();
    EXPECT_EQ(window.lines().size(), 4U) << "no more than the buffer keeps";
    EXPECT_EQ(window.lines().back().message, "more 5");
    EXPECT_EQ(window.missedLines(), 2U) << "6 lines arrived in a ring of 4 between two frames";

    window.clear();
    EXPECT_TRUE(window.lines().empty());
    buffer.add(line(Log::LogLevel::Info, "Test", "after clear"));
    window.update();
    ASSERT_EQ(window.lines().size(), 1U) << "clearing forgets old lines, not new ones";
    EXPECT_EQ(window.lines().front().message, "after clear");
}

TEST(SettingsWindow, WritesChangesToTheConfig)
{
    const fs::path folder = freshFolder("Settings");
    TestLog log{folder};
    Config::Config config = Config::Config::empty(folder / "Meow2Test.toml", log.log());
    SettingsWindow window{config};
    EXPECT_FALSE(window.restartNeeded());

    EXPECT_TRUE(window.set(c_vsyncSetting, !c_vsyncSetting.defaultValue));
    EXPECT_EQ(readSetting(config, c_vsyncSetting), !c_vsyncSetting.defaultValue);
    EXPECT_TRUE(window.restartNeeded()) << "vsync is read when the swapchain is made";
    EXPECT_TRUE(window.takeNextSceneChanges().empty());

    EXPECT_TRUE(window.set(c_compressTexturesSetting, !c_compressTexturesSetting.defaultValue));
    const std::vector<std::string_view> nextScene = window.takeNextSceneChanges();
    ASSERT_EQ(nextScene.size(), 1U);
    EXPECT_EQ(nextScene[0], c_compressTexturesSetting.key);
    EXPECT_TRUE(window.takeNextSceneChanges().empty()) << "taken once";

    EXPECT_TRUE(window.set(c_vsyncSetting, c_vsyncSetting.defaultValue));
    EXPECT_FALSE(window.restartNeeded()) << "back to how the app started";
    EXPECT_TRUE(window.setStartBackend("d3d12"));
    EXPECT_TRUE(window.restartNeeded());

    // Saved at once: a new Config reads the changes from the file.
    const Result<Config::Config> reloaded = Config::Config::load(folder / "Meow2Test.toml", log.log());
    ASSERT_TRUE(reloaded) << reloaded.error().message;
    EXPECT_EQ(readSetting(*reloaded, c_compressTexturesSetting), !c_compressTexturesSetting.defaultValue);
    EXPECT_EQ(reloaded->getString(c_backendKey, ""), "d3d12");
}

TEST(ImGuiLayout, RoundTripsThroughTheConfig)
{
    const fs::path folder = freshFolder("Layout");
    TestLog log{folder};
    // ImGui without a renderer: the atlas is never drawn, so it only has to accept the texture-request protocol.
    const auto frame = []
    {
        ImGuiIO& io = ImGui::GetIO();
        io.DisplaySize = ImVec2{1280.0F, 720.0F};
        io.DeltaTime = 1.0F / 60.0F;
        ImGui::NewFrame();
    };
    const auto makeContext = []
    {
        ImGuiContext* context = ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
        return context;
    };

    std::string saved;
    {
        ImGuiContext* context = makeContext();
        frame();
        ImGui::SetNextWindowPos(ImVec2{123.0F, 45.0F});
        ImGui::SetNextWindowSize(ImVec2{300.0F, 200.0F});
        ImGui::Begin("Probe");
        ImGui::End();
        ImGui::EndFrame();
        std::size_t size = 0;
        const char* text = ImGui::SaveIniSettingsToMemory(&size);
        saved.assign(text, size);
        ImGui::DestroyContext(context);
    }
    ASSERT_NE(saved.find("[Window][Probe]"), std::string::npos) << saved;

    // Through the TOML file, as the app keeps it: a multi-line string with brackets and '='.
    {
        Config::Config config = Config::Config::empty(folder / "Meow2Test.toml", log.log());
        config.setString(c_uiLayoutKey, saved);
        ASSERT_TRUE(config.save());
    }
    const Result<Config::Config> reloaded = Config::Config::load(folder / "Meow2Test.toml", log.log());
    ASSERT_TRUE(reloaded) << reloaded.error().message;
    const std::string loaded = reloaded->getString(c_uiLayoutKey, "");
    EXPECT_EQ(loaded, saved);

    ImGuiContext* context = makeContext();
    ImGui::LoadIniSettingsFromMemory(loaded.data(), loaded.size());
    frame();
    ImGui::Begin("Probe");
    const ImVec2 position = ImGui::GetWindowPos();
    ImGui::End();
    ImGui::EndFrame();
    ImGui::DestroyContext(context);
    EXPECT_FLOAT_EQ(position.x, 123.0F);
    EXPECT_FLOAT_EQ(position.y, 45.0F);
}
} // namespace Meow2::RendererApp
