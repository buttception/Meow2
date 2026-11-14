/// @file
/// @brief WindowDesc ⇄ config (WindowConfig.h). Backend-independent: only engine types.

#include "Meow2/Platform/Window/WindowConfig.h"

#include <algorithm>
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
    if (value == 0)
    {
        return 0; // the default share of the display
    }
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
    // Read only, never written back: it's for runs set up on the command line (tests, agents), not a user choice.
    desc.focusable = config.getBool("window.focusable", defaults.focusable);

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
    // Sizes are saved in logical units (the size at 100 % scaling), positions in desktop coordinates.
    WindowRect restored = window.restoredBounds();
    const float scale = window.unitsPerLogical();
    restored.width = static_cast<std::int32_t>(static_cast<float>(restored.width) / scale + 0.5F);
    restored.height = static_cast<std::int32_t>(static_cast<float>(restored.height) / scale + 0.5F);
    storeWindowState(config, restored, window.maximized(), window.mode());
}

void storeWindowState(Config::Config& config, const WindowRect& restored, bool maximized, WindowMode mode)
{
    config.setInt("window.width", restored.width);
    config.setInt("window.height", restored.height);
    config.setInt("window.x", restored.x);
    config.setInt("window.y", restored.y);
    config.setBool("window.maximized", maximized);
    config.setString("window.mode", mode == WindowMode::Fullscreen ? c_fullscreenName : c_windowedName);
}

bool isReachable(const WindowRect& rect, std::span<const WindowRect> displays)
{
    // Overlap per axis in 64 bits: x + width of two int32 values can overflow 32 bits.
    const auto overlap = [](std::int64_t start, std::int64_t length, std::int64_t otherStart, std::int64_t otherLength)
    {
        return std::min(start + length, otherStart + otherLength) - std::max(start, otherStart);
    };
    const std::int64_t needWidth = std::min(c_minReachableWidth, rect.width);
    const std::int64_t needHeight = std::min(c_minReachableHeight, rect.height);
    return std::ranges::any_of(displays,
                               [&](const WindowRect& display)
                               {
                                   return overlap(rect.x, rect.width, display.x, display.width) >= needWidth &&
                                          overlap(rect.y, rect.height, display.y, display.height) >= needHeight;
                               });
}
} // namespace Meow2::Platform
