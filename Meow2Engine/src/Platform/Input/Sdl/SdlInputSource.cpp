/// @file
/// @brief SdlInputSource (SdlInputSource.h): SDL keyboard, mouse and text events → InputSystem.

#include "Meow2/Platform/Input/SdlInputSource.h"

#include "Platform/Sdl/PlatformImplSdl.h"

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_mouse.h>

#include <cstdint>

namespace Meow2::Input
{
// The engine's Key values are the USB HID usage ids, and so are SDL's scancodes: a cast converts them.
static_assert(static_cast<int>(Key::A) == SDL_SCANCODE_A && static_cast<int>(Key::Space) == SDL_SCANCODE_SPACE &&
              static_cast<int>(Key::LeftCtrl) == SDL_SCANCODE_LCTRL &&
              static_cast<int>(Key::RightGui) == SDL_SCANCODE_RGUI);
// SDL's buttons 1..5 are left, middle, right, X1, X2: MouseButton's order, one lower.
static_assert(SDL_BUTTON_LEFT == 1 && SDL_BUTTON_MIDDLE == 2 && SDL_BUTTON_RIGHT == 3 && SDL_BUTTON_X1 == 4 &&
              SDL_BUTTON_X2 == 5);

/// @brief The source's registration with the Platform.
struct SdlInputSource::Impl
{
    /// @brief Translates one SDL event, if it's keyboard, mouse or text input.
    void handle(const SDL_Event& event);

    Platform::Platform::Impl* platform = nullptr; ///< Never null; outlives this.
    InputSystem* input = nullptr;                 ///< Never null; outlives this.
    std::uint64_t handlerId = 0;                  ///< The Platform's token for removing the handler.
};

void SdlInputSource::Impl::handle(const SDL_Event& event)
{
    switch (event.type)
    {
        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP:
            input->submit(KeyInput{.key = static_cast<Key>(event.key.scancode),
                                   .down = event.key.down,
                                   .repeat = event.key.down && event.key.repeat});
            break;
        case SDL_EVENT_MOUSE_MOTION:
            input->submit(MouseMotionInput{
                .x = event.motion.x, .y = event.motion.y, .dx = event.motion.xrel, .dy = event.motion.yrel});
            break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP:
            if (event.button.button >= SDL_BUTTON_LEFT && event.button.button <= SDL_BUTTON_X2)
            {
                input->submit(
                    MouseButtonInput{.button = static_cast<MouseButton>(event.button.button - SDL_BUTTON_LEFT),
                                     .down = event.button.down});
            }
            break;
        case SDL_EVENT_MOUSE_WHEEL:
        {
            // With "natural scrolling" SDL reports the values flipped; turn them back so +y is always away from the
            // user.
            const float sign = event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -1.0F : 1.0F;
            input->submit(MouseWheelInput{.x = event.wheel.x * sign, .y = event.wheel.y * sign});
            break;
        }
        case SDL_EVENT_TEXT_INPUT:
            if (event.text.text != nullptr)
            {
                input->submit(TextInput{.text = event.text.text});
            }
            break;
        default:
            break; // not keyboard, mouse or text (e.g. gamepads, handled by SdlGamepadSource)
    }
}

SdlInputSource::SdlInputSource(Platform::Platform& platform, InputSystem& input)
    : m_impl(std::make_unique<Impl>())
{
    m_impl->platform = &Platform::sdlPlatformImpl(platform);
    m_impl->input = &input;
    Impl* impl = m_impl.get(); // the Impl stays where it is for the source's whole life
    m_impl->handlerId = m_impl->platform->addEventHandler([impl](const SDL_Event& event) { impl->handle(event); });
}

SdlInputSource::~SdlInputSource()
{
    m_impl->platform->removeEventHandler(m_impl->handlerId);
}
} // namespace Meow2::Input
