/// @file
/// @brief Tests for Config: loading TOML files, typed reads by dotted key, command-line overrides and write-back.

#include <Meow2/Core/Config/Config.h>
#include <Meow2/Core/Log/LogSystem.h>

#include <gtest/gtest.h>

#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <system_error>

namespace
{
namespace fs = std::filesystem;
using Meow2::ErrorCode;
using Meow2::Config::Config;

/// @brief Reads a whole file, or "" if it doesn't exist.
std::string readFile(const fs::path& path)
{
    std::ifstream file(path, std::ios::binary);
    return std::string{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

/// @brief Writes a whole file, creating its folder.
void writeFile(const fs::path& path, std::string_view text)
{
    fs::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file << text;
}

/// @brief A fresh folder per test with a logger writing into it, so warnings can be checked.
class ConfigTest : public ::testing::Test
{
protected:
    ConfigTest()
        : m_directory(makeDirectory())
        , m_logSystem({.appName = "Test", .directory = m_directory / "logs", .console = false})
        , m_log(m_logSystem.createLogger("Engine"))
    {
    }

    /// @brief The config file used by the test.
    fs::path configPath() const
    {
        return m_directory / "Meow2Test.toml";
    }

    /// @brief Loads configPath(), failing the test if that fails.
    Config load()
    {
        Meow2::Result<Config> config = Config::load(configPath(), m_log);
        EXPECT_TRUE(config.has_value()) << (config ? "" : std::format("{}", config.error()));
        return config ? std::move(*config) : Config::empty(configPath(), m_log);
    }

    /// @brief Everything logged so far.
    std::string logText()
    {
        m_logSystem.flush();
        return readFile(m_logSystem.filePath());
    }

    fs::path m_directory;
    Meow2::Log::LogSystem m_logSystem;
    Meow2::Log::Logger m_log;

private:
    static fs::path makeDirectory()
    {
        const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
        const fs::path directory =
            fs::temp_directory_path() / std::format("meow2-config-{}-{}", Meow2::c_configName, info->name());
        std::error_code ignored;
        fs::remove_all(directory, ignored);
        return directory;
    }
};
} // namespace

TEST_F(ConfigTest, MissingFileIsEmpty)
{
    Config config = load();
    EXPECT_FALSE(config.contains("window.width"));
    EXPECT_EQ(config.getInt("window.width", 1280), 1280);
    EXPECT_EQ(config.getString("window.mode", "windowed"), "windowed");
    EXPECT_FALSE(config.hasUnsavedChanges());
    EXPECT_TRUE(config.save().has_value());
    EXPECT_FALSE(fs::exists(configPath())) << "nothing changed, so nothing is written";
}

TEST_F(ConfigTest, ParseErrorIsReported)
{
    writeFile(configPath(), "[window]\nwidth = 1280\nheight = = 720\n");
    const Meow2::Result<Config> config = Config::load(configPath(), m_log);
    ASSERT_FALSE(config.has_value());
    EXPECT_EQ(config.error().code, ErrorCode::ParseError);
    EXPECT_NE(config.error().message.find("Meow2Test.toml(3:"), std::string::npos) << config.error().message;
}

TEST_F(ConfigTest, ReadsDottedKeys)
{
    writeFile(configPath(), "top = 7\n"
                            "[window]\n"
                            "width = 1600\n"
                            "fullscreen = true\n"
                            "title = \"Meow\"\n"
                            "[renderer.quality]\n"
                            "scale = 0.75\n");
    const Config config = load();
    EXPECT_EQ(config.getInt("top", 0), 7);
    EXPECT_EQ(config.getInt("window.width", 0), 1600);
    EXPECT_TRUE(config.getBool("window.fullscreen", false));
    EXPECT_EQ(config.getString("window.title", ""), "Meow");
    EXPECT_DOUBLE_EQ(config.getFloat("renderer.quality.scale", 1.0), 0.75);
    EXPECT_TRUE(config.contains("renderer.quality"));
    EXPECT_FALSE(config.contains("window.height"));
    EXPECT_EQ(config.getInt("window.width.deeper", 3), 3) << "a value isn't a table";
}

TEST_F(ConfigTest, WrongTypeReturnsDefault)
{
    writeFile(configPath(), "[window]\nwidth = \"wide\"\n");
    const Config config = load();
    EXPECT_EQ(config.getInt("window.width", 1280), 1280);
    EXPECT_NE(logText().find("<warn> Engine: Config "), std::string::npos);
    EXPECT_NE(logText().find("'window.width' should be an integer; using the default"), std::string::npos);
}

TEST_F(ConfigTest, IntReadsAsFloat)
{
    writeFile(configPath(), "scale = 2\n");
    const Config config = load();
    EXPECT_DOUBLE_EQ(config.getFloat("scale", 1.0), 2.0);
}

TEST_F(ConfigTest, SaveRoundTrips)
{
    {
        Config config = load();
        config.setInt("window.width", 1920);
        config.setBool("window.fullscreen", true);
        config.setFloat("renderer.scale", 1.5);
        config.setString("window.title", "Meow \"quoted\"");
        EXPECT_TRUE(config.hasUnsavedChanges());
        ASSERT_TRUE(config.save().has_value());
        EXPECT_FALSE(config.hasUnsavedChanges());
    }
    EXPECT_FALSE(fs::exists(fs::path{configPath()} += ".tmp"));

    const Config config = load();
    EXPECT_EQ(config.getInt("window.width", 0), 1920);
    EXPECT_TRUE(config.getBool("window.fullscreen", false));
    EXPECT_DOUBLE_EQ(config.getFloat("renderer.scale", 0.0), 1.5);
    EXPECT_EQ(config.getString("window.title", ""), "Meow \"quoted\"");
}

TEST_F(ConfigTest, SaveWithoutChangesKeepsFile)
{
    const std::string text = "# my settings\n[window]\nwidth   = 1600 # wide\n";
    writeFile(configPath(), text);
    Config config = load();
    config.setInt("window.width", 1600); // the same value: not a change
    EXPECT_FALSE(config.hasUnsavedChanges());
    ASSERT_TRUE(config.save().has_value());
    EXPECT_EQ(readFile(configPath()), text);
}

TEST_F(ConfigTest, SaveCreatesFolders)
{
    const fs::path path = m_directory / "a" / "b" / "Meow2Test.toml";
    Config config = Config::empty(path, m_log);
    config.setInt("value", 1);
    ASSERT_TRUE(config.save().has_value());
    EXPECT_TRUE(fs::exists(path));
}

TEST_F(ConfigTest, SetReplacesValueInTheWay)
{
    Config config = load();
    config.setInt("window", 3);
    config.setInt("window.width", 800);
    EXPECT_EQ(config.getInt("window.width", 0), 800);
    EXPECT_NE(logText().find("a value on the way to 'window.width' was replaced by a table"), std::string::npos);
}

TEST_F(ConfigTest, ArgumentsOverrideFile)
{
    writeFile(configPath(), "[window]\nwidth = 1600\nheight = 900\n");
    Config config = load();
    const char* const argv[] = {
        "app", "--set", "window.width=800", "--set=window.vsync=false", "--set", "window.scale=1.25"};
    ASSERT_TRUE(config.applyArguments(6, argv).has_value());
    EXPECT_EQ(config.getInt("window.width", 0), 800);
    EXPECT_EQ(config.getInt("window.height", 0), 900) << "keys without an override still come from the file";
    EXPECT_FALSE(config.getBool("window.vsync", true));
    EXPECT_DOUBLE_EQ(config.getFloat("window.scale", 0.0), 1.25);
}

TEST_F(ConfigTest, OverridesAreNotSaved)
{
    writeFile(configPath(), "[window]\nwidth = 1600\n");
    {
        Config config = load();
        const char* const argv[] = {"app", "--set", "window.width=800"};
        ASSERT_TRUE(config.applyArguments(3, argv).has_value());
        config.setInt("window.height", 900); // a real change, so save() writes
        ASSERT_TRUE(config.save().has_value());
    }
    const Config config = load();
    EXPECT_EQ(config.getInt("window.width", 0), 1600);
    EXPECT_EQ(config.getInt("window.height", 0), 900);
}

TEST_F(ConfigTest, BareArgumentIsString)
{
    Config config = load();
    const char* const argv[] = {
        "app", "--set", "window.mode=fullscreen", "--set", "window.title=My Game", "--set", "window.quoted=\"42\""};
    ASSERT_TRUE(config.applyArguments(7, argv).has_value());
    EXPECT_EQ(config.getString("window.mode", ""), "fullscreen");
    EXPECT_EQ(config.getString("window.title", ""), "My Game");
    EXPECT_EQ(config.getString("window.quoted", ""), "42") << "a quoted TOML string stays a string";
}

TEST_F(ConfigTest, BadArgumentFails)
{
    Config config = load();
    const char* const argv[] = {"app", "--set", "nonsense", "--set", "bad key=1", "--set", "good=5"};
    const Meow2::Result<void> applied = config.applyArguments(7, argv);
    ASSERT_FALSE(applied.has_value());
    EXPECT_EQ(applied.error().code, ErrorCode::InvalidArgument);
    EXPECT_NE(applied.error().message.find("nonsense, bad key=1"), std::string::npos) << applied.error().message;
    EXPECT_EQ(config.getInt("good", 0), 5) << "valid overrides are still applied";
}

TEST_F(ConfigTest, SetReplacesOverride)
{
    Config config = load();
    const char* const argv[] = {"app", "--set", "window.width=800"};
    ASSERT_TRUE(config.applyArguments(3, argv).has_value());
    config.setInt("window.width", 1024);
    EXPECT_EQ(config.getInt("window.width", 0), 1024);
}

TEST(Config, PathFromArguments)
{
    using Meow2::Config::configPathFromArguments;
    const char* const separate[] = {"app", "--other", "--config", "a.toml"};
    const char* const joined[] = {"app", "--config=b.toml"};
    const char* const repeated[] = {"app", "--config", "a.toml", "--config=b.toml"};
    const char* const missingValue[] = {"app", "--config"};
    const char* const none[] = {"app", "--configx=c.toml"};

    EXPECT_EQ(configPathFromArguments(4, separate), fs::path("a.toml"));
    EXPECT_EQ(configPathFromArguments(2, joined), fs::path("b.toml"));
    EXPECT_EQ(configPathFromArguments(4, repeated), fs::path("b.toml")) << "the last one wins";
    EXPECT_EQ(configPathFromArguments(2, missingValue), std::nullopt);
    EXPECT_EQ(configPathFromArguments(2, none), std::nullopt);
    EXPECT_EQ(configPathFromArguments(0, nullptr), std::nullopt);
}

TEST(Config, DefaultPathIsNextToExecutable)
{
    const fs::path path = Meow2::Config::defaultConfigPath("Test");
    EXPECT_EQ(path.filename(), "Meow2Test.toml");
    EXPECT_TRUE(fs::exists(path.parent_path() / "Meow2EngineCoreTests.exe") ||
                fs::exists(path.parent_path() / "Meow2EngineCoreTests"));
}

TEST_F(ConfigTest, AppConfigUsesArguments)
{
    writeFile(configPath(), "[window]\nwidth = 1600\n");
    const std::string path = configPath().string();
    const char* const argv[] = {"app", "--config", path.c_str(), "--set", "window.height=900", "--set", "oops"};
    const Config config = Meow2::Config::openAppConfig("Test", 7, argv, m_log);
    EXPECT_EQ(config.path(), configPath());
    EXPECT_EQ(config.getInt("window.width", 0), 1600);
    EXPECT_EQ(config.getInt("window.height", 0), 900);
    EXPECT_NE(logText().find("<warn> Engine: Ignored command-line settings: InvalidArgument"), std::string::npos);
    EXPECT_NE(logText().find("<info> Engine: Config file: "), std::string::npos);
}

TEST_F(ConfigTest, AppConfigKeepsBrokenFile)
{
    const std::string text = "[window\nwidth = 1600\n";
    writeFile(configPath(), text);
    const std::string path = configPath().string();
    const char* const argv[] = {"app", "--config", path.c_str()};
    Config config = Meow2::Config::openAppConfig("Test", 3, argv, m_log);
    EXPECT_EQ(config.getInt("window.width", 1280), 1280) << "runs with defaults";
    EXPECT_NE(logText().find("<error> Engine: Config not loaded"), std::string::npos);

    config.setInt("window.width", 800);
    const Meow2::Result<void> saved = config.save();
    ASSERT_FALSE(saved.has_value());
    EXPECT_EQ(saved.error().code, ErrorCode::Unsupported);
    EXPECT_EQ(readFile(configPath()), text) << "the user's file isn't overwritten";
}
