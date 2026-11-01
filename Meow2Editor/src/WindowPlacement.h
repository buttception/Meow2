#pragma once

/// @file
/// @brief Engine window settings (WindowDesc, `window.*` config keys) applied to and read from a Qt top-level window.
///
/// The editor uses the same `window.*` keys as Meow2Player and Meow2Renderer (docs/wiki/Window.md), with the same
/// meaning: client-area position and size in desktop units, the *restored* bounds when maximized or fullscreen.

#include <Meow2/Core/Config/Config.h>
#include <Meow2/Core/Log/Logger.h>
#include <Meow2/Platform/Window/Window.h>

#include <QWidget>

namespace Meow2::Editor
{
/// @brief Sizes, places and shows a top-level widget as `desc` says.
///
/// - The position is used if it's on a current screen (Platform::isReachable()); otherwise, or if it's unset, the
///   window is centred on the primary screen.
/// - Then the window is shown maximized, fullscreen or normal, unless WindowDesc::hidden is set (tests): a hidden
///   window gets its geometry and state but isn't shown.
/// - WindowDesc::resizable = false fixes the size.
/// @param window A top-level widget, usually the EditorMainWindow.
/// @param desc   Title, size, position and mode, usually from Platform::windowDescFromConfig().
/// @param log    Receives a note when a saved position is off every screen.
void showWithDesc(QWidget& window, const Platform::WindowDesc& desc, Log::Logger& log);

/// @brief The window's bounds when windowed and not maximized: what's saved, and what restoring returns to.
/// @param window A top-level widget.
/// @return Client-area position and size in desktop units (Qt's device-independent pixels).
[[nodiscard]] Platform::WindowRect restoredBounds(const QWidget& window);

/// @brief Stores the window's restored bounds, maximized state and mode in the `window.*` config keys.
/// @param config The app's settings.
/// @param window A top-level widget.
void storeWindowState(Config::Config& config, const QWidget& window);
} // namespace Meow2::Editor
