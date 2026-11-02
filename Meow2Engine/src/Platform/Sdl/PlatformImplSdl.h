#pragma once

/// @file
/// @brief Private: Platform::Impl for the SDL3 backend, shared with the SDL Window implementation.

#include "Meow2/Platform/Platform.h"
#include "Meow2/Platform/Window/Window.h"

#include <string>
#include <vector>

namespace Meow2::Platform
{
/// @brief SDL3 state of the Platform: the initialised video subsystem and the windows to route events to.
///
/// Not thread-safe (main thread only). Lives on the heap behind Platform, so Window::Impls can keep a pointer to it
/// even when the Platform object itself is moved.
struct Platform::Impl
{
    /// @brief Shuts SDL down (SDL_Quit).
    /// @pre No window is registered any more (asserted).
    ~Impl();

    Log::Logger* log = nullptr;         ///< Never null.
    Events::EventBus* events = nullptr; ///< Never null; window and quit events are enqueued here.
    std::string videoDriver;            ///< Name of the driver SDL picked, e.g. "windows".
    bool quitRequested = false;         ///< An SDL_EVENT_QUIT arrived.
    std::vector<Window::Impl*> windows; ///< Live windows; each registers on creation, unregisters on destruction.
};
} // namespace Meow2::Platform
