#pragma once

/// @file
/// @brief Window: one OS window, created API-neutral; the RHI renders into it through its native handle.
/// @see techstack.md §9, docs/wiki/Window.md

#include <Meow2/Core/Log/Logger.h>
#include <Meow2/Core/Result.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace Meow2::Platform
{
class Platform;

/// @brief Identifies a window in window events (WindowEvents.h). Unique among the windows alive at the same time;
///        0 is never a window.
using WindowId = std::uint32_t;

/// @brief Whether a window has a frame or covers its display.
enum class WindowMode : std::uint8_t
{
    Windowed,  ///< A normal window with a title bar, resizable when WindowDesc::resizable is set.
    Fullscreen ///< Borderless, covering the whole display at the desktop resolution (no display-mode change).
};

/// @brief A window's position and size on the desktop. Plain data.
///
/// In desktop coordinates (logical units): on a display scaled to 150 %, a 1000-unit-wide window is 1500 pixels wide.
/// See Window::pixelWidth() for the size of the drawable area in pixels.
struct WindowRect
{
    std::int32_t x = 0;      ///< Left edge of the client area, in desktop coordinates.
    std::int32_t y = 0;      ///< Top edge of the client area, in desktop coordinates.
    std::int32_t width = 0;  ///< Client-area width (without the frame), in desktop coordinates.
    std::int32_t height = 0; ///< Client-area height (without the frame), in desktop coordinates.

    /// @brief Rects are equal when all four values are.
    friend bool operator==(const WindowRect&, const WindowRect&) = default;
};

/// @brief How a window is created. Plain data; usually filled from the config (windowDescFromConfig()).
struct WindowDesc
{
    std::string title = "Meow2";     ///< Text in the title bar and taskbar (UTF-8).
    std::int32_t width = 1920;       ///< Client-area width when windowed, in desktop coordinates (1080p by default).
    std::int32_t height = 1080;      ///< Client-area height when windowed, in desktop coordinates.
    std::optional<std::int32_t> x{}; ///< Left edge; centred on the primary display if unset or off every display.
    std::optional<std::int32_t> y{}; ///< Top edge; centred on the primary display if unset or off every display.
    bool maximized = false;          ///< Open maximized (windowed mode); width/height are the size when restored.
    WindowMode mode = WindowMode::Windowed; ///< Windowed or fullscreen.
    bool resizable = true;                  ///< The user can resize the window by its frame.
    bool hidden = false;                    ///< Create it without showing it (tests); show() makes it visible.
};

/// @brief What a native window handle refers to, so the RHI knows how to create a surface for it.
enum class NativeWindowType : std::uint8_t
{
    None,   ///< No native window (the `offscreen` video driver): nothing can be presented to it.
    Win32,  ///< `window` is an `HWND`.
    X11,    ///< `display` is a `Display*`, `x11Window` the `Window` id.
    Wayland ///< `display` is a `wl_display*`, `window` a `wl_surface*`.
};

/// @brief The OS objects behind a Window, for creating a Vulkan or D3D12 swapchain. Plain data.
/// @warning Query it when creating the swapchain and don't keep it above the RHI (techstack.md §11): the RHI may
///          recreate its surface (backend hot swap), and the window can be recreated.
struct NativeWindowHandle
{
    NativeWindowType type = NativeWindowType::None; ///< Which fields are set.
    void* display = nullptr;                        ///< X11 `Display*` or Wayland `wl_display*`; null on Win32.
    void* window = nullptr;                         ///< Win32 `HWND` or Wayland `wl_surface*`; null on X11.
    std::uint64_t x11Window = 0;                    ///< X11 `Window` id; 0 otherwise.
};

/// @brief One OS window. Its state is updated by Platform::pumpEvents(), which also enqueues an event on the
///        EventBus for every change (WindowEvents.h).
///
/// React to changes by subscribing to the events; read the current values (e.g. the size when creating a
/// swapchain) with the getters.
///
/// Two sets of bounds are kept:
/// - bounds(): where the window is right now (maximized or fullscreen included);
/// - restoredBounds(): where it is when *windowed and not maximized*. That's what an app saves in its config, so the
///   window comes back with its normal size after being closed while maximized.
///
/// The setters ask the OS for a change and wait briefly for it to apply; the state (and wasResized()) reflects it
/// after the next Platform::pumpEvents().
/// @par Thread safety
/// Not thread-safe; main thread only, like the Platform.
/// @par Lifetime
/// The Platform and the Logger must outlive the Window.
class Window
{
public:
    /// @brief Creates and (unless WindowDesc::hidden) shows a window.
    /// @param platform The windowing layer. Must outlive the Window.
    /// @param log      Where problems are logged. Must outlive the Window.
    /// @param desc     Title, size, position and mode.
    /// @return The window, or ErrorCode::InvalidArgument for a non-positive size, or ErrorCode::Unknown (with the
    ///         backend's message) if the OS refused to create it.
    [[nodiscard]] static Result<Window> create(Platform& platform, Log::Logger& log, const WindowDesc& desc);

    /// @brief Destroys the OS window.
    ~Window();

    /// @brief Takes over the OS window; `other` must not be used afterwards, except to be destroyed.
    /// @param other Window to move from.
    Window(Window&& other) noexcept;

    Window(const Window&) = delete;            ///< Not copyable: one object per OS window.
    Window& operator=(const Window&) = delete; ///< Not copyable: one object per OS window.
    Window& operator=(Window&&) = delete;      ///< Not assignable: destroy one window and create the next instead.

    /// @brief The id that window events carry for this window.
    /// @return A non-zero id, fixed for the window's life.
    [[nodiscard]] WindowId id() const;

    /// @brief Ratio of pixels to desktop coordinates for the display the window is on: 1.0 at 100 %, 1.5 at 150 %.
    /// @return The display scale.
    [[nodiscard]] float displayScale() const;

    /// @brief Whether the user asked to close the window (close button, Alt+F4). The window stays open until it's
    ///        destroyed, so the app can save first.
    /// @return `true` once requested; stays `true`.
    [[nodiscard]] bool closeRequested() const;

    /// @brief Whether the window's size or pixel size changed during the last Platform::pumpEvents().
    /// @return `true` for exactly one pump after a change (the swapchain must be resized then).
    [[nodiscard]] bool wasResized() const;

    /// @brief Current position and client-area size, in desktop coordinates.
    /// @return The current bounds.
    [[nodiscard]] WindowRect bounds() const;

    /// @brief Position and size the last time the window was windowed and not maximized or minimized.
    /// @return The bounds to save and to restore to.
    [[nodiscard]] WindowRect restoredBounds() const;

    /// @brief Width of the drawable area in pixels (bounds().width × the display scale).
    /// @return Width in pixels; this is the swapchain width.
    [[nodiscard]] std::int32_t pixelWidth() const;

    /// @brief Height of the drawable area in pixels (bounds().height × the display scale).
    /// @return Height in pixels; this is the swapchain height.
    [[nodiscard]] std::int32_t pixelHeight() const;

    /// @brief Whether the window is maximized.
    /// @return `true` when maximized (windowed mode).
    [[nodiscard]] bool maximized() const;

    /// @brief Whether the window is minimized. Nothing should be rendered while it is.
    /// @return `true` when minimized.
    [[nodiscard]] bool minimized() const;

    /// @brief Whether the window has keyboard focus.
    /// @return `true` when focused.
    [[nodiscard]] bool focused() const;

    /// @brief Windowed or fullscreen.
    /// @return The current mode.
    [[nodiscard]] WindowMode mode() const;

    /// @brief The title.
    /// @return The title text (UTF-8).
    [[nodiscard]] std::string title() const;

    /// @brief The OS objects behind the window, for the RHI.
    /// @return The native handle; type NativeWindowType::None with the `offscreen` driver.
    [[nodiscard]] NativeWindowHandle nativeHandle() const;

    /// @brief Changes the title.
    /// @param title New title (UTF-8).
    void setTitle(std::string_view title);

    /// @brief Switches between windowed and fullscreen. Leaving fullscreen returns to the previous windowed bounds.
    /// @param mode New mode.
    void setMode(WindowMode mode);

    /// @brief Resizes the client area (windowed, not maximized).
    /// @param width  New width in desktop coordinates; > 0.
    /// @param height New height in desktop coordinates; > 0.
    /// @pre `width > 0 && height > 0` (asserted).
    void setSize(std::int32_t width, std::int32_t height);

    /// @brief Moves the window (windowed, not maximized).
    /// @param x New left edge in desktop coordinates.
    /// @param y New top edge in desktop coordinates.
    void setPosition(std::int32_t x, std::int32_t y);

    /// @brief Maximizes the window (windowed mode).
    void maximize();

    /// @brief Restores a maximized or minimized window to its restored bounds.
    void restore();

    /// @brief Shows a window created with WindowDesc::hidden.
    void show();

    /// @brief Backend state, defined by the backend (`src/Platform/Window/Sdl/`).
    struct Impl;

private:
    explicit Window(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> m_impl;
};
} // namespace Meow2::Platform
