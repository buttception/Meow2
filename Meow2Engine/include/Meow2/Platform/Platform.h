#pragma once

/// @file
/// @brief Platform: owns the OS windowing layer (SDL3) for the whole process and delivers its events.
///
/// @code
/// Result<Platform::Platform> platform = Platform::Platform::create({}, engineLog, events);
/// Result<Platform::Window> window = Platform::Window::create(*platform, engineLog, {.title = "Meow2Player"});
/// bool running = true;
/// Events::Subscription onClose =
///     events.subscribe<Platform::WindowCloseRequested>([&](const Platform::WindowCloseRequested&) { running = false;
///     });
/// while (running)
/// {
///     platform->pumpEvents(std::chrono::milliseconds{16}); // sleeps until an event arrives, at most 16 ms
///     events.dispatchQueued();                             // window events (WindowEvents.h) arrive here
///     // ... update, render ...
/// }
/// @endcode
/// @see techstack.md §9, docs/wiki/Window.md

#include <Meow2/Core/Events/EventBus.h>
#include <Meow2/Core/Log/Logger.h>
#include <Meow2/Core/Result.h>

#include <chrono>
#include <memory>
#include <string>
#include <string_view>

namespace Meow2::Platform
{
class Window;

/// @brief How the Platform is set up. Plain data.
struct PlatformDesc
{
    /// @brief The windowing backend's video driver, or empty for the platform's default.
    ///
    /// For SDL3: `"windows"`, `"x11"`, `"wayland"`, … and `"offscreen"`, which creates windows that are never shown
    /// and needs no desktop. Tests and smoke runs use `"offscreen"`, so they also run on CI machines without a
    /// display. Config key `platform.videoDriver`.
    std::string videoDriver;
};

/// @brief The process-wide windowing layer: initialises it, pumps its events to the Windows, and shuts it down.
///
/// SDL3 keeps global state, so there is **one Platform per process** at a time (create() fails otherwise). The
/// composition root creates it before any Window and destroys it after the last one (techstack.md §18, "third-party
/// libraries with process-global state").
///
/// In Debug and RelWithDebInfo SDL's allocations go to Memory::globalTracker() under MemTag::Platform, so they
/// appear in the memory statistics and the leak check.
/// @par Thread safety
/// Not thread-safe. Create it, pump events and destroy it on the main thread (SDL requires the thread that
/// initialised video to handle its events).
/// @par Lifetime
/// Must outlive every Window created from it, and the Logger and the EventBus must outlive the Platform.
class Platform
{
public:
    /// @brief Initialises the windowing layer.
    /// @param desc How to set it up.
    /// @param log    Where problems and the chosen video driver are logged. Must outlive the Platform.
    /// @param events Where pumpEvents() enqueues window and quit events (WindowEvents.h). Must outlive the Platform.
    /// @return The Platform, or ErrorCode::AlreadyExists if another one is alive, or ErrorCode::Unsupported (with
    ///         SDL's message) if the video driver can't start, e.g. no display on a Linux machine.
    [[nodiscard]] static Result<Platform> create(const PlatformDesc& desc, Log::Logger& log, Events::EventBus& events);

    /// @brief Shuts the windowing layer down.
    /// @pre No Window created from this Platform is alive (asserted).
    ~Platform();

    /// @brief Takes over the windowing layer; `other` must not be used afterwards, except to be destroyed.
    /// @param other Platform to move from.
    Platform(Platform&& other) noexcept;

    Platform(const Platform&) = delete;            ///< Not copyable: one per process.
    Platform& operator=(const Platform&) = delete; ///< Not copyable: one per process.
    Platform& operator=(Platform&&) = delete;      ///< Not assignable: there is never a second one to assign.

    /// @brief Handles every pending OS event, updates the Windows' state and enqueues an event for each change.
    ///
    /// Call it once per frame, before reading window state, followed by EventBus::dispatchQueued() (techstack.md
    /// §23). The events are only enqueued here (WindowEvents.h says which, and in what order); no handler runs inside
    /// this call. Per-frame flags such as Window::wasResized() describe what happened during the last call.
    /// @param wait How long to sleep if no event is pending yet, so an idle app doesn't spin a CPU core. 0 (the
    ///             default) returns immediately.
    void pumpEvents(std::chrono::milliseconds wait = std::chrono::milliseconds{0});

    /// @brief Whether the OS asked the app to quit: the last window was closed, Ctrl+C in the console, logoff.
    /// @return `true` once requested; stays `true`.
    [[nodiscard]] bool quitRequested() const;

    /// @brief The video driver in use, e.g. `"windows"` or `"offscreen"`.
    /// @return The driver's name.
    [[nodiscard]] std::string_view videoDriver() const;

    /// @brief Backend state, defined by the backend (`src/Platform/Sdl/`).
    struct Impl;

private:
    friend class Window; // registers itself to receive its events
    /// @brief Backend access for the input sources (`src/Platform/Input/`), which register for input events.
    /// @param platform The Platform.
    /// @return Its backend state.
    friend Impl& sdlPlatformImpl(Platform& platform);

    explicit Platform(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> m_impl;
};

/// @brief Shows a modal message box, for errors the user must see even with no console (Release has none).
/// @param title   Title bar text (UTF-8).
/// @param message The message (UTF-8).
/// @note Blocks until it's closed, so don't use it in unattended runs.
void showErrorDialog(const std::string& title, const std::string& message);
} // namespace Meow2::Platform
