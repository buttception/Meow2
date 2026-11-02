#pragma once

/// @file
/// @brief Events the Platform puts on the EventBus when a window changes or the app is asked to quit.
///
/// Platform::pumpEvents() enqueues them; they arrive at the next EventBus::dispatchQueued() (techstack.md §22, §23).
/// @code
/// Events::Subscription onResize = events.subscribe<Platform::WindowResized>(
///     [&](const Platform::WindowResized& e) { swapchain.resize(e.pixelWidth, e.pixelHeight); });
/// @endcode
///
/// - **Coalesced:** after each pump, every window compares its state with the previous pump's and enqueues at most
///   one event of each kind, with the final values. Dragging a window's frame across many OS events gives one
///   WindowResized per pump, not one per step.
/// - **Order within one pump:** per window WindowMoved, WindowResized, WindowStateChanged, WindowFocusChanged,
///   WindowDisplayScaleChanged, WindowCloseRequested; then QuitRequested.
/// - Creating a window enqueues nothing: its initial state is what Window::create() returned.
/// @see techstack.md §9, docs/wiki/Window.md

#include <Meow2/Platform/Window/Window.h>

#include <cstdint>

namespace Meow2::Platform
{
/// @brief A window's client area changed size, in desktop coordinates or in pixels. Resize the swapchain to the
///        pixel size. Plain data.
struct WindowResized
{
    WindowId window = 0;          ///< Which window (Window::id()).
    std::int32_t width = 0;       ///< New client-area width, in desktop coordinates.
    std::int32_t height = 0;      ///< New client-area height, in desktop coordinates.
    std::int32_t pixelWidth = 0;  ///< New drawable width in pixels (the swapchain width).
    std::int32_t pixelHeight = 0; ///< New drawable height in pixels (the swapchain height).
};

/// @brief A window moved on the desktop. Plain data.
struct WindowMoved
{
    WindowId window = 0; ///< Which window (Window::id()).
    std::int32_t x = 0;  ///< New left edge of the client area, in desktop coordinates.
    std::int32_t y = 0;  ///< New top edge of the client area, in desktop coordinates.
};

/// @brief A window was maximized, minimized, restored, or switched between windowed and fullscreen. Plain data.
///
/// Carries the whole new state, so a handler doesn't need to know what it was before. Stop rendering while
/// `minimized` is set.
struct WindowStateChanged
{
    WindowId window = 0;                    ///< Which window (Window::id()).
    bool maximized = false;                 ///< Now maximized (Window::maximized()).
    bool minimized = false;                 ///< Now minimized (Window::minimized()).
    WindowMode mode = WindowMode::Windowed; ///< Now windowed or fullscreen (Window::mode()).
};

/// @brief A window gained or lost keyboard focus. Plain data.
struct WindowFocusChanged
{
    WindowId window = 0;  ///< Which window (Window::id()).
    bool focused = false; ///< Now has keyboard focus (Window::focused()).
};

/// @brief A window's display scale changed, e.g. it moved to a monitor with a different DPI setting. Plain data.
///
/// The pixel size usually changes with it, so a WindowResized arrives in the same dispatch.
struct WindowDisplayScaleChanged
{
    WindowId window = 0; ///< Which window (Window::id()).
    float scale = 1.0F;  ///< New scale: 1.0 at 100 %, 1.5 at 150 % (Window::displayScale()).
};

/// @brief The user asked to close a window (close button, Alt+F4). Sent once per window. Plain data.
///
/// The window stays open until it's destroyed, so the app can save first (Window::closeRequested() stays `true`).
struct WindowCloseRequested
{
    WindowId window = 0; ///< Which window (Window::id()).
};

/// @brief The OS asked the app to quit: the last window was closed, Ctrl+C in the console, logoff. Sent once.
///        Plain data.
///
/// Platform::quitRequested() stays `true` afterwards.
struct QuitRequested
{
};
} // namespace Meow2::Platform
