/// @file
/// @brief Platform over SDL3: init/quit, the SDL memory hook and event routing.

#include "Meow2/Core/Assert.h"
#include "Meow2/Core/Memory/GlobalTracking.h"
#include "Meow2/Core/Profiling/Profile.h"
#include "Platform/Sdl/PlatformImplSdl.h"
#include "Platform/Window/Sdl/WindowImplSdl.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <format>
#include <limits>
#include <string>
#include <utility>

namespace Meow2::Platform
{
namespace
{
using Memory::globalTracker;
using Memory::MemTag;

// SDL's memory hooks are plain C function pointers without a user-data argument, so they reach the process-wide
// tracker directly (techstack.md §6/§18: globalTracker() is the documented exception for library hooks).
// malloc's guarantee: suitable for any fundamental type.
constexpr std::size_t c_sdlAlignment = alignof(std::max_align_t);

void* SDLCALL sdlMalloc(std::size_t size)
{
    try
    {
        return globalTracker().allocateTagged(size, c_sdlAlignment, MemTag::Platform);
    }
    catch (...) // C callers expect null on failure, never an exception
    {
        return nullptr;
    }
}

void* SDLCALL sdlCalloc(std::size_t count, std::size_t size)
{
    if (size != 0 && count > std::numeric_limits<std::size_t>::max() / size)
    {
        return nullptr;
    }
    void* memory = sdlMalloc(count * size);
    if (memory != nullptr)
    {
        std::memset(memory, 0, count * size);
    }
    return memory;
}

void SDLCALL sdlFree(void* memory)
{
    if (memory != nullptr)
    {
        globalTracker().deallocateUnsized(memory);
    }
}

void* SDLCALL sdlRealloc(void* memory, std::size_t size)
{
    if (memory == nullptr)
    {
        return sdlMalloc(size);
    }
    // The tracker has no in-place resize: allocate, copy, free. SDL reallocates rarely (growing arrays).
    void* resized = sdlMalloc(size);
    if (resized != nullptr)
    {
        std::memcpy(resized, memory, std::min(size, globalTracker().allocationSize(memory)));
        sdlFree(memory);
    }
    return resized; // on failure the old block stays valid, as realloc requires
}
} // namespace

Platform::Impl::~Impl()
{
    MEOW2_ASSERT(windows.empty(), "destroy every Window before its Platform");
    SDL_Quit();
    MEOW2_LOG_DEBUG(*log, "Platform shut down");
}

Platform::Platform(std::unique_ptr<Impl> impl)
    : m_impl(std::move(impl))
{
}

Platform::~Platform() = default;
Platform::Platform(Platform&& other) noexcept = default;

Result<Platform> Platform::create(const PlatformDesc& desc, Log::Logger& log)
{
    if (SDL_WasInit(0) != 0)
    {
        return fail(ErrorCode::AlreadyExists, "a Platform already exists; there can be only one per process");
    }
    if constexpr (Memory::c_memoryTrackingAvailable)
    {
        // Before any other SDL call: a block must be freed by the allocator that made it. Setting the same
        // functions again (a second Platform after the first was destroyed) is harmless.
        SDL_SetMemoryFunctions(sdlMalloc, sdlCalloc, sdlRealloc, sdlFree);
    }
    if (desc.videoDriver.empty())
    {
        SDL_ResetHint(SDL_HINT_VIDEO_DRIVER); // in case an earlier Platform in this process set one
    }
    else
    {
        SDL_SetHint(SDL_HINT_VIDEO_DRIVER, desc.videoDriver.c_str());
    }
    // Ctrl+C in the console arrives as SDL_EVENT_QUIT, so the app shuts down cleanly (and saves its settings).
    if (!SDL_Init(SDL_INIT_VIDEO))
    {
        const std::string error = SDL_GetError();
        SDL_Quit();
        return fail(ErrorCode::Unsupported,
                    std::format("video driver '{}' didn't start: {}",
                                desc.videoDriver.empty() ? "default" : desc.videoDriver, error));
    }

    auto impl = std::make_unique<Impl>();
    impl->log = &log;
    impl->videoDriver = SDL_GetCurrentVideoDriver();
    const int c_version = SDL_GetVersion();
    MEOW2_LOG_INFO(log, "Platform: SDL {}.{}.{}, video driver '{}'", SDL_VERSIONNUM_MAJOR(c_version),
                   SDL_VERSIONNUM_MINOR(c_version), SDL_VERSIONNUM_MICRO(c_version), impl->videoDriver);
    return Platform{std::move(impl)};
}

void Platform::pumpEvents(std::chrono::milliseconds wait)
{
    MEOW2_PROFILE_ZONE();
    for (Window::Impl* window : m_impl->windows)
    {
        window->beginPump();
    }

    SDL_Event event;
    // Sleep until the first event (or the timeout), then take everything that is pending without waiting again.
    bool hasEvent =
        wait.count() > 0
            ? SDL_WaitEventTimeout(
                  &event, static_cast<Sint32>(std::min<std::int64_t>(wait.count(), std::numeric_limits<Sint32>::max())))
            : SDL_PollEvent(&event);
    while (hasEvent)
    {
        if (event.type == SDL_EVENT_QUIT)
        {
            m_impl->quitRequested = true;
        }
        else if (event.type >= SDL_EVENT_WINDOW_FIRST && event.type <= SDL_EVENT_WINDOW_LAST)
        {
            const auto window = std::ranges::find_if(m_impl->windows, [&event](const Window::Impl* candidate)
                                                     { return candidate->id == event.window.windowID; });
            if (window != m_impl->windows.end())
            {
                (*window)->handleEvent(event.window);
            }
        }
        hasEvent = SDL_PollEvent(&event);
    }

    for (Window::Impl* window : m_impl->windows)
    {
        window->refresh();
    }
}

bool Platform::quitRequested() const
{
    return m_impl->quitRequested;
}

std::string_view Platform::videoDriver() const
{
    return m_impl->videoDriver;
}
} // namespace Meow2::Platform
