#pragma once

/// @file
/// @brief Reading a WindowDesc from the app's config file, and writing the window's state back to it.
///
/// The keys, all optional (`Meow2<App>.toml`):
/// @code{.toml}
/// [window]
/// width = 1600          # windowed size, in desktop units
/// height = 900
/// x = 200               # left/top edge; centred when missing or off every display
/// y = 120
/// maximized = false
/// mode = "windowed"     # or "fullscreen"
/// @endcode
/// @see docs/wiki/Window.md, docs/wiki/Config.md

#include <Meow2/Core/Config/Config.h>
#include <Meow2/Core/Log/Logger.h>
#include <Meow2/Platform/Window/Window.h>

#include <cstdint>
#include <span>

namespace Meow2::Platform
{
/// @brief Smallest accepted window width or height from the config, in desktop units.
inline constexpr std::int32_t c_minConfigWindowSize = 160;

/// @brief Largest accepted window width or height from the config, in desktop units.
inline constexpr std::int32_t c_maxConfigWindowSize = 16384;

/// @brief Builds a WindowDesc from the `window.*` keys, with the app's defaults for missing or invalid values.
///
/// Invalid values (a size outside c_minConfigWindowSize..c_maxConfigWindowSize, an unknown mode) are logged as a
/// warning and replaced by the default, so a bad edit never stops the app from opening a window.
/// Title, resizable and hidden aren't read from the config; they come from `defaults`.
/// @param config   The app's settings.
/// @param defaults What the app uses when a key is missing or invalid.
/// @param log      Receives the warnings.
/// @return The window description.
[[nodiscard]] WindowDesc windowDescFromConfig(const Config::Config& config, const WindowDesc& defaults,
                                              Log::Logger& log);

/// @brief Stores the window's restored bounds, maximized state and mode in the `window.*` keys.
///
/// Call it before Config::save() at shutdown. The *restored* bounds are stored (see Window::restoredBounds()), so
/// a window closed while maximized or fullscreen reopens maximized/fullscreen and goes back to its normal size when
/// restored. Keys whose value didn't change don't make the file count as changed.
/// @param config The app's settings.
/// @param window The window to remember.
void storeWindowState(Config::Config& config, const Window& window);

/// @brief Stores a window's state given as plain values, for windows that aren't a Platform::Window (the Qt editor).
/// @param config    The app's settings.
/// @param restored  Position and size when windowed and not maximized (see Window::restoredBounds()).
/// @param maximized Whether the window is maximized.
/// @param mode      Windowed or fullscreen.
void storeWindowState(Config::Config& config, const WindowRect& restored, bool maximized, WindowMode mode);

/// @brief Minimum part of a window, in desktop units, that must be on a display for its position to be usable:
///        enough of the title bar to grab it with the mouse.
inline constexpr std::int32_t c_minReachableWidth = 100;

/// @brief Minimum height of the part of a window that must be on a display; see c_minReachableWidth.
inline constexpr std::int32_t c_minReachableHeight = 40;

/// @brief Whether a window at `rect` can be reached by the user: at least c_minReachableWidth ×
///        c_minReachableHeight of it (or all of it, if smaller) overlaps one of the displays.
///
/// Used before restoring a saved position: one on a monitor that has since been unplugged fails, and the window is
/// centred instead. Each windowing backend passes its own list of display rectangles.
/// @param rect     The window's client area, in desktop coordinates.
/// @param displays Every display's bounds, in the same coordinates.
/// @return `true` if the window would be reachable.
[[nodiscard]] bool isReachable(const WindowRect& rect, std::span<const WindowRect> displays);
} // namespace Meow2::Platform
