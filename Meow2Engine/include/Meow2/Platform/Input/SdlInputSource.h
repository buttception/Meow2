#pragma once

/// @file
/// @brief SdlInputSource: feeds the InputSystem with the window's keyboard, mouse and text input from SDL.
///
/// @code
/// Meow2::Input::InputSystem input{events};
/// Meow2::Input::SdlInputSource keyboardAndMouse{*platform, input}; // after the Platform, destroyed before it
/// // per frame: platform->pumpEvents(); input.beginFrame();
/// @endcode
/// @see techstack.md §9, docs/wiki/Input.md

#include <Meow2/Platform/Input/InputSystem.h>

#include <memory>

namespace Meow2::Platform
{
class Platform;
} // namespace Meow2::Platform

namespace Meow2::Input
{
/// @brief Translates SDL keyboard, mouse and text events into InputSystem::submit() calls.
///
/// While it exists, every Platform::pumpEvents() hands it the SDL input events, and it submits:
/// - key down/up as KeyInput (SDL's scancodes are the HID usage ids `Key` uses; auto-repeat sets `repeat`);
/// - motion, buttons 1..5 and the wheel as MouseMotionInput, MouseButtonInput and MouseWheelInput. A wheel the OS
///   reports as "flipped" (natural scrolling) is turned back, so positive y always means away from the user;
/// - typed text as TextInput. SDL only sends text while text input is on (comes with the UI).
///
/// It's a translator, not a virtual interface (techstack.md §9): another backend has its own source class.
/// @par Thread safety
/// Not thread-safe; main thread only, like the Platform.
/// @par Lifetime
/// The Platform and the InputSystem must outlive it. Destroy it before the Platform (asserted there).
class SdlInputSource
{
public:
    /// @brief Starts forwarding SDL's keyboard, mouse and text events to `input`.
    /// @param platform The windowing layer whose pumpEvents() delivers the events. Must outlive the source.
    /// @param input    Receives the translated input. Must outlive the source.
    SdlInputSource(Platform::Platform& platform, InputSystem& input);

    /// @brief Stops forwarding.
    ~SdlInputSource();

    SdlInputSource(const SdlInputSource&) = delete;            ///< Not copyable: registered by address.
    SdlInputSource& operator=(const SdlInputSource&) = delete; ///< Not copyable: registered by address.
    SdlInputSource(SdlInputSource&&) = delete;                 ///< Not movable: registered by address.
    SdlInputSource& operator=(SdlInputSource&&) = delete;      ///< Not movable: registered by address.

    /// @brief Backend state, defined in `src/Platform/Input/Sdl/`.
    struct Impl;

private:
    std::unique_ptr<Impl> m_impl;
};
} // namespace Meow2::Input
