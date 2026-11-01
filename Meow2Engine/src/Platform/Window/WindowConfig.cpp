/// @file
/// @brief WindowDesc ⇄ config (WindowConfig.h). Backend-independent: only engine types.

#include "Meow2/Platform/Window/WindowConfig.h"

#include <limits>
#include <optional>
#include <string>
#include <string_view>

namespace Meow2::Platform
{
namespace
{
constexpr std::string_view c_windowedName = "windowed";
constexpr std::string_view c_fullscreenName = "fullscreen";

/// @brief A window size from the config, or the default (with a warning) if it's out of range.
std::int32_t readSize(const Config::Config& config, std::string_view key, std::int32_t defaultValue, Log::Logger& log)
{
    const std::int64_t value = config.getInt(key, defaultValue);
    if (value < c_minConfigWindowSize || value > c_maxConfigWindowSize)
    {
        MEOW2_LOG_WARN(log, "Config: {} = {} is outside {}..{}; using {}", key, value, c_minConfigWindowSize,
                       c_maxConfigWindowSize, defaultValue);
        return defaultValue;
    }
    return static_cast<std::int32_t>(value);
}

/// @brief A window position from the config, unset if missing or (with a warning) out of the int32 range.
std::optional<std::int32_t> readPosition(const Config::Config& config, std::string_view key,
                                         std::optional<std::int32_t> defaultValue, Log::Logger& log)
{
    if (!config.contains(key))
    {
        return defaultValue;
    }
    const std::int64_t value = config.getInt(key, defaultValue.value_or(0));
    if (value < std::numeric_limits<std::int32_t>::min() || value > std::numeric_limits<std::int32_t>::max())
    {
        MEOW2_LOG_WARN(log, "Config: {} = {} is out of range; centring the window", key, value);
        return std::nullopt;
    }
    return static_cast<std::int32_t>(value);
}
} // namespace

WindowDesc windowDescFromConfig(const Config::Config& config, const WindowDesc& defaults, Log::Logger& log)
{
    WindowDesc desc = defaults;
    desc.width = readSize(config, "window.width", defaults.width, log);
    desc.height = readSize(config, "window.height", defaults.height, log);
    desc.x = readPosition(config, "window.x", defaults.x, log);
    desc.y = readPosition(config, "window.y", defaults.y, log);
    desc.maximized = config.getBool("window.maximized", defaults.maximized);

    const std::string_view defaultMode = defaults.mode == WindowMode::Fullscreen ? c_fullscreenName : c_windowedName;
    const std::string mode = config.getString("window.mode", defaultMode);
    if (mode == c_fullscreenName)
    {
        desc.mode = WindowMode::Fullscreen;
    }
    else if (mode == c_windowedName)
    {
        desc.mode = WindowMode::Windowed;
    }
    else
    {
        MEOW2_LOG_WARN(log, "Config: window.mode = \"{}\" isn't \"{}\" or \"{}\"; using \"{}\"", mode, c_windowedName,
                       c_fullscreenName, defaultMode);
    }
    return desc;
}

void storeWindowState(Config::Config& config, const Window& window)
{
    const WindowRect restored = window.restoredBounds();
    config.setInt("window.width", restored.width);
    config.setInt("window.height", restored.height);
    config.setInt("window.x", restored.x);
    config.setInt("window.y", restored.y);
    config.setBool("window.maximized", window.maximized());
    config.setString("window.mode", window.mode() == WindowMode::Fullscreen ? c_fullscreenName : c_windowedName);
}
} // namespace Meow2::Platform
