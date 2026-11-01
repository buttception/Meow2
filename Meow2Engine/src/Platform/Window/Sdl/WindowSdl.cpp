/// @file
/// @brief Window over SDL3.

#include "Meow2/Core/Assert.h"
#include "Platform/Window/Sdl/WindowImplSdl.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <format>
#include <string_view>
#include <utility>

namespace Meow2::Platform
{
namespace
{
/// @brief How much of a window must be on a display for a saved position to count as visible, in desktop units.
/// Enough of the title bar to grab it with the mouse.
constexpr std::int32_t c_minVisibleWidth = 100;
constexpr std::int32_t c_minVisibleHeight = 40;

/// @brief Whether a window at `rect` would be reachable: enough of it overlaps at least one current display.
/// A position saved on a monitor that has since been unplugged fails this.
bool isOnAnyDisplay(const WindowRect& rect)
{
    int count = 0;
    SDL_DisplayID* displays = SDL_GetDisplays(&count);
    bool visible = false;
    const SDL_Rect window{rect.x, rect.y, rect.width, rect.height};
    for (int i = 0; i < count && !visible; ++i)
    {
        SDL_Rect display;
        SDL_Rect overlap;
        visible = SDL_GetDisplayBounds(displays[i], &display) && SDL_GetRectIntersection(&window, &display, &overlap) &&
                  overlap.w >= std::min(c_minVisibleWidth, rect.width) &&
                  overlap.h >= std::min(c_minVisibleHeight, rect.height);
    }
    SDL_free(displays);
    return visible;
}

/// @brief The top-left corner that centres a `width`×`height` window on the primary display.
std::pair<std::int32_t, std::int32_t> centredOnPrimary(std::int32_t width, std::int32_t height)
{
    SDL_Rect display{0, 0, width, height};
    SDL_GetDisplayBounds(SDL_GetPrimaryDisplay(), &display); // keeps the fallback on failure
    return {display.x + (display.w - width) / 2, display.y + (display.h - height) / 2};
}

constexpr std::string_view toString(WindowMode mode)
{
    return mode == WindowMode::Fullscreen ? "fullscreen" : "windowed";
}
} // namespace

Window::Impl::~Impl()
{
    std::erase(platform->windows, this);
    SDL_DestroyWindow(window);
}

void Window::Impl::handleEvent(const SDL_WindowEvent& event)
{
    if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED)
    {
        closeRequested = true;
    }
    // Everything else (moved, resized, maximized, …) is picked up by refresh() at the end of the pump, which reads
    // the final state instead of replaying each intermediate step.
}

void Window::Impl::beginPump()
{
    resized = false;
}

void Window::Impl::refresh()
{
    const WindowRect previous = bounds;
    const std::int32_t previousPixelWidth = pixelWidth;
    const std::int32_t previousPixelHeight = pixelHeight;

    SDL_GetWindowPosition(window, &bounds.x, &bounds.y);
    SDL_GetWindowSize(window, &bounds.width, &bounds.height);
    SDL_GetWindowSizeInPixels(window, &pixelWidth, &pixelHeight);
    flags = SDL_GetWindowFlags(window);

    // Only a plain window's bounds are worth restoring to: a maximized, minimized or fullscreen window reports the
    // display's size (or nonsense while minimized).
    if ((flags & (SDL_WINDOW_MAXIMIZED | SDL_WINDOW_MINIMIZED | SDL_WINDOW_FULLSCREEN)) == 0)
    {
        restored = bounds;
    }
    resized = resized || previous.width != bounds.width || previous.height != bounds.height ||
              previousPixelWidth != pixelWidth || previousPixelHeight != pixelHeight;
}

Window::Window(std::unique_ptr<Impl> impl)
    : m_impl(std::move(impl))
{
}

Window::~Window() = default;
Window::Window(Window&& other) noexcept = default;

Result<Window> Window::create(Platform& platform, Log::Logger& log, const WindowDesc& desc)
{
    if (desc.width <= 0 || desc.height <= 0)
    {
        return fail(ErrorCode::InvalidArgument, std::format("window size {}x{}", desc.width, desc.height));
    }

    // Where the window goes when windowed: the saved position if it's still on a display, else centred.
    const auto [centredX, centredY] = centredOnPrimary(desc.width, desc.height);
    WindowRect restored{desc.x.value_or(centredX), desc.y.value_or(centredY), desc.width, desc.height};
    if ((desc.x || desc.y) && !isOnAnyDisplay(restored))
    {
        MEOW2_LOG_INFO(log, "Window position ({}, {}) is off every display; centring it", restored.x, restored.y);
        restored.x = centredX;
        restored.y = centredY;
    }

    const SDL_PropertiesID properties = SDL_CreateProperties();
    SDL_SetStringProperty(properties, SDL_PROP_WINDOW_CREATE_TITLE_STRING, desc.title.c_str());
    SDL_SetNumberProperty(properties, SDL_PROP_WINDOW_CREATE_X_NUMBER, restored.x);
    SDL_SetNumberProperty(properties, SDL_PROP_WINDOW_CREATE_Y_NUMBER, restored.y);
    SDL_SetNumberProperty(properties, SDL_PROP_WINDOW_CREATE_WIDTH_NUMBER, restored.width);
    SDL_SetNumberProperty(properties, SDL_PROP_WINDOW_CREATE_HEIGHT_NUMBER, restored.height);
    SDL_SetBooleanProperty(properties, SDL_PROP_WINDOW_CREATE_RESIZABLE_BOOLEAN, desc.resizable);
    SDL_SetBooleanProperty(properties, SDL_PROP_WINDOW_CREATE_HIDDEN_BOOLEAN, desc.hidden);
    SDL_SetBooleanProperty(properties, SDL_PROP_WINDOW_CREATE_MAXIMIZED_BOOLEAN, desc.maximized);
    SDL_SetBooleanProperty(properties, SDL_PROP_WINDOW_CREATE_FULLSCREEN_BOOLEAN, desc.mode == WindowMode::Fullscreen);
    // Sizes stay in desktop units on high-DPI displays, and the drawable area gets the full pixel resolution.
    SDL_SetBooleanProperty(properties, SDL_PROP_WINDOW_CREATE_HIGH_PIXEL_DENSITY_BOOLEAN, true);
    SDL_Window* sdlWindow = SDL_CreateWindowWithProperties(properties);
    SDL_DestroyProperties(properties);
    if (sdlWindow == nullptr)
    {
        return fail(ErrorCode::Unknown, std::format("SDL_CreateWindow failed: {}", SDL_GetError()));
    }

    auto impl = std::make_unique<Impl>();
    impl->platform = platform.m_impl.get();
    impl->log = &log;
    impl->window = sdlWindow;
    impl->id = SDL_GetWindowID(sdlWindow);
    impl->restored = restored; // replaced by the real bounds whenever the window is plain windowed
    impl->platform->windows.push_back(impl.get());
    SDL_SyncWindow(sdlWindow); // let asynchronous window systems (X11, Wayland) apply the initial state
    impl->refresh();
    impl->resized = false;

    MEOW2_LOG_INFO(log, "Window '{}': {}x{} at ({}, {}), {}{}, {}x{} pixels", desc.title, impl->bounds.width,
                   impl->bounds.height, impl->bounds.x, impl->bounds.y, toString(desc.mode),
                   (impl->flags & SDL_WINDOW_MAXIMIZED) != 0 ? ", maximized" : "", impl->pixelWidth, impl->pixelHeight);
    return Window{std::move(impl)};
}

bool Window::closeRequested() const
{
    return m_impl->closeRequested;
}

bool Window::wasResized() const
{
    return m_impl->resized;
}

WindowRect Window::bounds() const
{
    return m_impl->bounds;
}

WindowRect Window::restoredBounds() const
{
    return m_impl->restored;
}

std::int32_t Window::pixelWidth() const
{
    return m_impl->pixelWidth;
}

std::int32_t Window::pixelHeight() const
{
    return m_impl->pixelHeight;
}

bool Window::maximized() const
{
    return (m_impl->flags & SDL_WINDOW_MAXIMIZED) != 0;
}

bool Window::minimized() const
{
    return (m_impl->flags & SDL_WINDOW_MINIMIZED) != 0;
}

bool Window::focused() const
{
    return (m_impl->flags & SDL_WINDOW_INPUT_FOCUS) != 0;
}

WindowMode Window::mode() const
{
    return (m_impl->flags & SDL_WINDOW_FULLSCREEN) != 0 ? WindowMode::Fullscreen : WindowMode::Windowed;
}

std::string Window::title() const
{
    return SDL_GetWindowTitle(m_impl->window);
}

NativeWindowHandle Window::nativeHandle() const
{
    const SDL_PropertiesID properties = SDL_GetWindowProperties(m_impl->window);
    const std::string_view driver = m_impl->platform->videoDriver;
    NativeWindowHandle handle;
    if (driver == "windows")
    {
        handle.type = NativeWindowType::Win32;
        handle.window = SDL_GetPointerProperty(properties, SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
    }
    else if (driver == "x11")
    {
        handle.type = NativeWindowType::X11;
        handle.display = SDL_GetPointerProperty(properties, SDL_PROP_WINDOW_X11_DISPLAY_POINTER, nullptr);
        handle.x11Window =
            static_cast<std::uint64_t>(SDL_GetNumberProperty(properties, SDL_PROP_WINDOW_X11_WINDOW_NUMBER, 0));
    }
    else if (driver == "wayland")
    {
        handle.type = NativeWindowType::Wayland;
        handle.display = SDL_GetPointerProperty(properties, SDL_PROP_WINDOW_WAYLAND_DISPLAY_POINTER, nullptr);
        handle.window = SDL_GetPointerProperty(properties, SDL_PROP_WINDOW_WAYLAND_SURFACE_POINTER, nullptr);
    }
    return handle;
}

void Window::setTitle(std::string_view title)
{
    SDL_SetWindowTitle(m_impl->window, std::string{title}.c_str());
}

void Window::setMode(WindowMode mode)
{
    SDL_SetWindowFullscreen(m_impl->window, mode == WindowMode::Fullscreen);
    SDL_SyncWindow(m_impl->window);
}

void Window::setSize(std::int32_t width, std::int32_t height)
{
    MEOW2_ASSERT(width > 0 && height > 0, "window sizes are positive");
    SDL_SetWindowSize(m_impl->window, width, height);
    SDL_SyncWindow(m_impl->window);
}

void Window::setPosition(std::int32_t x, std::int32_t y)
{
    SDL_SetWindowPosition(m_impl->window, x, y);
    SDL_SyncWindow(m_impl->window);
}

void Window::maximize()
{
    SDL_MaximizeWindow(m_impl->window);
    SDL_SyncWindow(m_impl->window);
}

void Window::restore()
{
    SDL_RestoreWindow(m_impl->window);
    SDL_SyncWindow(m_impl->window);
}

void Window::show()
{
    SDL_ShowWindow(m_impl->window);
    SDL_SyncWindow(m_impl->window);
}
} // namespace Meow2::Platform
