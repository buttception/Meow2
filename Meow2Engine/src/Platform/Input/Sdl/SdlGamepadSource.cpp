/// @file
/// @brief SdlGamepadSource (SdlGamepadSource.h): SDL gamepad events → InputSystem.

#include "Meow2/Platform/Input/SdlGamepadSource.h"

#include "Platform/Sdl/PlatformImplSdl.h"

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_gamepad.h>
#include <SDL3/SDL_init.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <format>
#include <string>
#include <utility>
#include <vector>

namespace Meow2::Input
{
// GamepadButton and GamepadAxis follow SDL's standard gamepad layout one to one, so a cast converts them.
static_assert(static_cast<int>(GamepadButton::South) == SDL_GAMEPAD_BUTTON_SOUTH &&
              static_cast<int>(GamepadButton::North) == SDL_GAMEPAD_BUTTON_NORTH &&
              static_cast<int>(GamepadButton::Start) == SDL_GAMEPAD_BUTTON_START &&
              static_cast<int>(GamepadButton::LeftShoulder) == SDL_GAMEPAD_BUTTON_LEFT_SHOULDER &&
              static_cast<int>(GamepadButton::DpadRight) == SDL_GAMEPAD_BUTTON_DPAD_RIGHT &&
              c_gamepadButtonCount == static_cast<std::size_t>(SDL_GAMEPAD_BUTTON_DPAD_RIGHT) + 1);
static_assert(static_cast<int>(GamepadAxis::LeftX) == SDL_GAMEPAD_AXIS_LEFTX &&
              static_cast<int>(GamepadAxis::RightY) == SDL_GAMEPAD_AXIS_RIGHTY &&
              static_cast<int>(GamepadAxis::LeftTrigger) == SDL_GAMEPAD_AXIS_LEFT_TRIGGER &&
              static_cast<int>(GamepadAxis::RightTrigger) == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER &&
              c_gamepadAxisCount == static_cast<std::size_t>(SDL_GAMEPAD_AXIS_COUNT));

/// @brief The gamepad subsystem, the open gamepads and the registration with the Platform.
struct SdlGamepadSource::Impl
{
    /// @brief Closes the gamepads, unregisters and stops SDL's gamepad subsystem.
    ~Impl();

    /// @brief Translates one SDL event, if it's a gamepad event.
    void handle(const SDL_Event& event);

    Platform::Platform::Impl* platform = nullptr; ///< Never null; outlives this.
    InputSystem* input = nullptr;                 ///< Never null; outlives this.
    Log::Logger* log = nullptr;                   ///< Never null.
    std::uint64_t handlerId = 0;                  ///< The Platform's token for removing the handler.
    std::vector<SDL_Gamepad*> gamepads;           ///< Open gamepads; owned.
};

namespace
{
/// @brief A stick value (-32768..32767) as -1..1, or a trigger (0..32767) as 0..1.
float axisValue(Sint16 value)
{
    return std::clamp(static_cast<float>(value) / 32767.0F, -1.0F, 1.0F);
}
} // namespace

SdlGamepadSource::Impl::~Impl()
{
    if (platform == nullptr)
    {
        return; // create() failed before anything was set up
    }
    platform->removeEventHandler(handlerId);
    for (SDL_Gamepad* gamepad : gamepads)
    {
        SDL_CloseGamepad(gamepad);
    }
    SDL_QuitSubSystem(SDL_INIT_GAMEPAD);
}

void SdlGamepadSource::Impl::handle(const SDL_Event& event)
{
    switch (event.type)
    {
        case SDL_EVENT_GAMEPAD_ADDED:
        {
            SDL_Gamepad* gamepad = SDL_OpenGamepad(event.gdevice.which);
            if (gamepad == nullptr)
            {
                MEOW2_LOG_WARN(*log, "Gamepad {} couldn't be opened: {}", event.gdevice.which, SDL_GetError());
                break;
            }
            gamepads.push_back(gamepad);
            const char* name = SDL_GetGamepadName(gamepad);
            const std::string gamepadName = name != nullptr ? name : "Gamepad";
            MEOW2_LOG_INFO(*log, "Gamepad connected: {} (id {})", gamepadName, event.gdevice.which);
            input->submit(GamepadConnection{.gamepad = event.gdevice.which, .connected = true, .name = gamepadName});
            break;
        }
        case SDL_EVENT_GAMEPAD_REMOVED:
        {
            const auto found = std::ranges::find_if(gamepads, [&event](SDL_Gamepad* gamepad)
                                                    { return SDL_GetGamepadID(gamepad) == event.gdevice.which; });
            if (found != gamepads.end())
            {
                SDL_CloseGamepad(*found);
                gamepads.erase(found);
            }
            MEOW2_LOG_INFO(*log, "Gamepad disconnected (id {})", event.gdevice.which);
            input->submit(GamepadConnection{.gamepad = event.gdevice.which, .connected = false});
            break;
        }
        case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
        case SDL_EVENT_GAMEPAD_BUTTON_UP:
            if (event.gbutton.button < c_gamepadButtonCount) // newer buttons (paddles, touchpad) aren't mapped yet
            {
                input->submit(GamepadButtonInput{.gamepad = event.gbutton.which,
                                                 .button = static_cast<GamepadButton>(event.gbutton.button),
                                                 .down = event.gbutton.down});
            }
            break;
        case SDL_EVENT_GAMEPAD_AXIS_MOTION:
            if (event.gaxis.axis < c_gamepadAxisCount)
            {
                input->submit(GamepadAxisInput{.gamepad = event.gaxis.which,
                                               .axis = static_cast<GamepadAxis>(event.gaxis.axis),
                                               .value = axisValue(event.gaxis.value)});
            }
            break;
        default:
            break; // not a gamepad event
    }
}

SdlGamepadSource::SdlGamepadSource(std::unique_ptr<Impl> impl)
    : m_impl(std::move(impl))
{
}

SdlGamepadSource::~SdlGamepadSource() = default;
SdlGamepadSource::SdlGamepadSource(SdlGamepadSource&& other) noexcept = default;

Result<SdlGamepadSource> SdlGamepadSource::create(Platform::Platform& platform, InputSystem& input, Log::Logger& log)
{
    if (!SDL_InitSubSystem(SDL_INIT_GAMEPAD))
    {
        return fail(ErrorCode::Unsupported, std::format("SDL gamepad support didn't start: {}", SDL_GetError()));
    }
    auto impl = std::make_unique<Impl>();
    impl->platform = &Platform::sdlPlatformImpl(platform);
    impl->input = &input;
    impl->log = &log;
    Impl* raw = impl.get(); // the Impl stays where it is for the source's whole life, even when the source moves
    impl->handlerId = impl->platform->addEventHandler([raw](const SDL_Event& event) { raw->handle(event); });
    // Gamepads already plugged in arrive as SDL_EVENT_GAMEPAD_ADDED at the next pump.
    return SdlGamepadSource{std::move(impl)};
}
} // namespace Meow2::Input
