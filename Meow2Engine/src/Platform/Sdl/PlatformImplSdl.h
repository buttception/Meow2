#pragma once

/// @file
/// @brief Private: Platform::Impl for the SDL3 backend, shared with the SDL Window and input source implementations.

#include "Meow2/Platform/Platform.h"
#include "Meow2/Platform/Window/Window.h"

#include <SDL3/SDL_events.h>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace Meow2::Platform
{
/// @brief A function that receives the SDL events that aren't window events (keyboard, mouse, gamepads, …).
struct SdlEventHandler
{
    std::uint64_t id = 0;                              ///< Token for removing it again.
    std::function<void(const SDL_Event&)> handle = {}; ///< Called for each event, on the main thread.
};

/// @brief SDL3 state of the Platform: the initialised video subsystem, the windows and the input sources to route
///        events to.
///
/// Not thread-safe (main thread only). Lives on the heap behind Platform, so Window::Impls and input sources can keep
/// a pointer to it even when the Platform object itself is moved.
struct Platform::Impl
{
    /// @brief Shuts SDL down (SDL_Quit).
    /// @pre No window and no event handler is registered any more (asserted).
    ~Impl();

    /// @brief Registers a handler for the non-window events of every pumpEvents().
    /// @param handle Called for each event. Must not add or remove handlers.
    /// @return The token for removeEventHandler().
    std::uint64_t addEventHandler(std::function<void(const SDL_Event&)> handle);

    /// @brief Removes a handler added with addEventHandler(). Not from inside a handler.
    /// @param id The token addEventHandler() returned.
    void removeEventHandler(std::uint64_t id);

    Log::Logger* log = nullptr;         ///< Never null.
    Events::EventBus* events = nullptr; ///< Never null; window and quit events are enqueued here.
    std::string videoDriver;            ///< Name of the driver SDL picked, e.g. "windows".
    bool quitRequested = false;         ///< An SDL_EVENT_QUIT arrived.
    std::vector<Window::Impl*> windows; ///< Live windows; each registers on creation, unregisters on destruction.
    std::vector<SdlEventHandler> eventHandlers; ///< Input sources; each registers on creation, unregisters at the end.
    std::uint64_t nextEventHandlerId = 1;       ///< Next token to hand out.
};

/// @brief The SDL state behind a Platform, for the SDL input sources.
/// @param platform The Platform.
/// @return Its Impl; lives as long as the Platform.
Platform::Impl& sdlPlatformImpl(Platform& platform);
} // namespace Meow2::Platform
