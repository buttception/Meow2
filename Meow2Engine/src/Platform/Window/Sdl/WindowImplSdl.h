#pragma once

/// @file
/// @brief Private: Window::Impl for the SDL3 backend, used by the SDL Platform to route events.

#include "Meow2/Platform/Window/Window.h"
#include "Platform/Sdl/PlatformImplSdl.h"

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_video.h>

#include <cstdint>

namespace Meow2::Platform
{
/// @brief SDL3 state of one Window: the SDL_Window and the state last read from it.
///
/// Not thread-safe (main thread only). Registered in its Platform::Impl for its whole life.
struct Window::Impl
{
    /// @brief Unregisters from the Platform and destroys the SDL window.
    ~Impl();

    /// @brief Handles one window event for this window (the close request). Everything else is re-read in refresh(),
    ///        after all events of a pump.
    /// @param event An event whose `windowID` is this window's.
    void handleEvent(const SDL_WindowEvent& event);

    /// @brief Starts a new pump: clears the per-pump flags.
    void beginPump();

    /// @brief Reads position, sizes, scale and flags from SDL, and updates restoredBounds. Enqueues nothing.
    void readState();

    /// @brief At the end of a pump: readState(), then sets the resized flag and enqueues one event per kind of change
    ///        since the previous pump (order in WindowEvents.h).
    void refresh();

    Platform::Impl* platform = nullptr; ///< Never null; outlives this window.
    Log::Logger* log = nullptr;         ///< Never null.
    SDL_Window* window = nullptr;       ///< Owned.
    SDL_WindowID id = 0;                ///< SDL's id, to match events.

    bool closeRequested = false;     ///< SDL_EVENT_WINDOW_CLOSE_REQUESTED arrived.
    bool closeEventEnqueued = false; ///< WindowCloseRequested was enqueued (it's sent once).
    bool resized = false;            ///< Size or pixel size changed in the current pump.

    WindowRect bounds;            ///< Current position and size.
    WindowRect restored;          ///< Last bounds while windowed and not maximized or minimized.
    std::int32_t pixelWidth = 0;  ///< Drawable width in pixels.
    std::int32_t pixelHeight = 0; ///< Drawable height in pixels.
    float displayScale = 1.0F;    ///< Pixels per desktop unit on the window's display.
    SDL_WindowFlags flags = 0;    ///< Last flags read from SDL.
};
} // namespace Meow2::Platform
