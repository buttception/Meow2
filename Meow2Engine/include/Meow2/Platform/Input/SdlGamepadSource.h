#pragma once

/// @file
/// @brief SdlGamepadSource: feeds the InputSystem with gamepads (connect, buttons, axes) from SDL.
///
/// @code
/// Meow2::Result<Meow2::Input::SdlGamepadSource> gamepads =
///     Meow2::Input::SdlGamepadSource::create(*platform, input, engineLog);
/// if (!gamepads) { MEOW2_LOG_WARN(log, "No gamepads: {}", gamepads.error()); } // the app runs without them
/// @endcode
/// @see techstack.md §9, docs/wiki/Input.md

#include <Meow2/Core/Log/Logger.h>
#include <Meow2/Core/Result.h>
#include <Meow2/Platform/Input/InputSystem.h>

#include <memory>

namespace Meow2::Platform
{
class Platform;
} // namespace Meow2::Platform

namespace Meow2::Input
{
/// @brief Starts SDL's gamepad support and translates its gamepad events into InputSystem::submit() calls.
///
/// - A gamepad that appears (also one already plugged in when the source starts) is opened and submitted as a
///   GamepadConnection with its name; its GamepadId is SDL's joystick instance id. One that goes away is closed and
///   submitted as disconnected.
/// - Buttons and axes keep SDL's standard gamepad layout, which GamepadButton and GamepadAxis follow one to one.
///   Sticks are scaled to -1..1 and triggers to 0..1; no dead zone is applied.
/// @par Thread safety
/// Not thread-safe; main thread only, like the Platform.
/// @par Lifetime
/// The Platform, the InputSystem and the Logger must outlive it. Destroy it before the Platform (asserted there).
class SdlGamepadSource
{
public:
    /// @brief Starts SDL's gamepad subsystem and begins forwarding gamepad events to `input`.
    /// @param platform The windowing layer whose pumpEvents() delivers the events. Must outlive the source.
    /// @param input    Receives the translated input. Must outlive the source.
    /// @param log      Where connections and problems are logged. Must outlive the source.
    /// @return The source, or ErrorCode::Unsupported (with SDL's message) if gamepad support can't start; the app
    ///         can run without it.
    [[nodiscard]] static Result<SdlGamepadSource> create(Platform::Platform& platform, InputSystem& input,
                                                         Log::Logger& log);

    /// @brief Closes the open gamepads and stops SDL's gamepad subsystem.
    ~SdlGamepadSource();

    /// @brief Takes over another source; `other` must not be used afterwards, except to be destroyed.
    /// @param other Source to move from.
    SdlGamepadSource(SdlGamepadSource&& other) noexcept;

    SdlGamepadSource(const SdlGamepadSource&) = delete;            ///< Not copyable: owns the open gamepads.
    SdlGamepadSource& operator=(const SdlGamepadSource&) = delete; ///< Not copyable: owns the open gamepads.
    SdlGamepadSource& operator=(SdlGamepadSource&&) = delete;      ///< Not assignable: create a new one instead.

    /// @brief Backend state, defined in `src/Platform/Input/Sdl/`.
    struct Impl;

private:
    explicit SdlGamepadSource(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> m_impl;
};
} // namespace Meow2::Input
