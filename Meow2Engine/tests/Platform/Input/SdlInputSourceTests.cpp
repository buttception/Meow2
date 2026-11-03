/// @file
/// @brief Tests for SdlInputSource and SdlGamepadSource: SDL events pushed by hand, and an SDL virtual gamepad, go
///        through Platform::pumpEvents() into the InputSystem. Offscreen driver, so no desktop is needed.

#include <Meow2/Core/Events/EventBus.h>
#include <Meow2/Core/Log/LogSystem.h>
#include <Meow2/Core/Platform.h>
#include <Meow2/Platform/Input/InputSystem.h>
#include <Meow2/Platform/Input/SdlGamepadSource.h>
#include <Meow2/Platform/Input/SdlInputSource.h>
#include <Meow2/Platform/Platform.h>

#include <gtest/gtest.h>
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_gamepad.h>
#include <SDL3/SDL_joystick.h>
#include <SDL3/SDL_mouse.h>

#include <algorithm>
#include <filesystem>
#include <format>
#include <memory_resource>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

namespace
{
namespace fs = std::filesystem;
using namespace Meow2::Input;

/// @brief A log, a bus, an InputSystem and an offscreen Platform per test.
class SdlInputSourceTest : public ::testing::Test
{
protected:
    SdlInputSourceTest()
        : m_logSystem({.appName = "Test", .directory = logDirectory(), .console = false})
        , m_log(m_logSystem.createLogger("Engine"))
    {
    }

    void SetUp() override
    {
        Meow2::Result<Meow2::Platform::Platform> platform =
            Meow2::Platform::Platform::create({.videoDriver = "offscreen"}, m_log, m_events);
        ASSERT_TRUE(platform.has_value()) << std::format("{}", platform.error());
        m_platform.emplace(std::move(*platform));
    }

    /// @brief Pushes an SDL event as if the OS had sent it.
    static void push(SDL_Event event)
    {
        ASSERT_TRUE(SDL_PushEvent(&event)) << SDL_GetError();
    }

    /// @brief One frame of the game loop's input part: pump, apply, dispatch.
    void frame()
    {
        m_platform->pumpEvents();
        m_input.beginFrame();
        m_events.dispatchQueued();
    }

    Meow2::Log::LogSystem m_logSystem;
    Meow2::Log::Logger m_log;
    Meow2::Events::EventBus m_events{*std::pmr::new_delete_resource()};
    InputSystem m_input{m_events};
    std::optional<Meow2::Platform::Platform> m_platform; // sources are destroyed in each test, before this

private:
    static fs::path logDirectory()
    {
        const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
        const fs::path directory =
            fs::temp_directory_path() / std::format("meow2-sdlinput-{}-{}", Meow2::c_configName, info->name());
        std::error_code ignored;
        fs::remove_all(directory, ignored);
        return directory;
    }
};

SDL_Event keyEvent(SDL_Scancode scancode, bool down, bool repeat = false)
{
    SDL_Event event{};
    event.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
    event.key.scancode = scancode;
    event.key.down = down;
    event.key.repeat = repeat;
    return event;
}
} // namespace

TEST_F(SdlInputSourceTest, KeyDownUp)
{
    const SdlInputSource source{*m_platform, m_input};
    push(keyEvent(SDL_SCANCODE_W, true));
    frame();
    EXPECT_TRUE(m_input.isDown(Key::W));
    EXPECT_TRUE(m_input.wasPressed(Key::W));

    push(keyEvent(SDL_SCANCODE_W, false));
    frame();
    EXPECT_FALSE(m_input.isDown(Key::W));
    EXPECT_TRUE(m_input.wasReleased(Key::W));
}

TEST_F(SdlInputSourceTest, KeyRepeat)
{
    const SdlInputSource source{*m_platform, m_input};
    push(keyEvent(SDL_SCANCODE_LEFT, true));
    frame();
    push(keyEvent(SDL_SCANCODE_LEFT, true, true));
    frame();
    EXPECT_TRUE(m_input.isDown(Key::Left));
    EXPECT_FALSE(m_input.wasPressed(Key::Left)) << "SDL's repeat flag reaches the InputSystem";
}

TEST_F(SdlInputSourceTest, MouseMotionButtonsWheel)
{
    const SdlInputSource source{*m_platform, m_input};
    SDL_Event motion{};
    motion.type = SDL_EVENT_MOUSE_MOTION;
    motion.motion.x = 100.0F;
    motion.motion.y = 50.0F;
    motion.motion.xrel = 4.0F;
    motion.motion.yrel = -2.0F;
    push(motion);
    SDL_Event button{};
    button.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    button.button.button = SDL_BUTTON_RIGHT;
    button.button.down = true;
    push(button);
    SDL_Event wheel{};
    wheel.type = SDL_EVENT_MOUSE_WHEEL;
    wheel.wheel.y = 2.0F;
    wheel.wheel.direction = SDL_MOUSEWHEEL_NORMAL;
    push(wheel);
    frame();

    EXPECT_EQ(m_input.mousePosition(), (Vec2{100.0F, 50.0F}));
    EXPECT_EQ(m_input.mouseDelta(), (Vec2{4.0F, -2.0F}));
    EXPECT_TRUE(m_input.isDown(MouseButton::Right)) << "SDL button 3 is the right button";
    EXPECT_FALSE(m_input.isDown(MouseButton::Left));
    EXPECT_EQ(m_input.wheelDelta(), (Vec2{0.0F, 2.0F}));
}

TEST_F(SdlInputSourceTest, FlippedWheelIsNormalised)
{
    const SdlInputSource source{*m_platform, m_input};
    SDL_Event wheel{};
    wheel.type = SDL_EVENT_MOUSE_WHEEL;
    wheel.wheel.y = 1.0F;
    wheel.wheel.direction = SDL_MOUSEWHEEL_FLIPPED; // "natural scrolling"
    push(wheel);
    frame();
    EXPECT_EQ(m_input.wheelDelta(), (Vec2{0.0F, -1.0F}));
}

TEST_F(SdlInputSourceTest, TextInput)
{
    const SdlInputSource source{*m_platform, m_input};
    std::vector<std::string> texts;
    const Meow2::Events::Subscription onText =
        m_events.subscribe<TextEntered>([&texts](const TextEntered& e) { texts.push_back(e.text); });
    static const char c_text[] = "\xC3\xA9"; // "e" with an acute accent, in UTF-8: a character from the IME
    SDL_Event text{};
    text.type = SDL_EVENT_TEXT_INPUT;
    text.text.text = c_text;
    push(text);
    frame();
    ASSERT_EQ(texts.size(), 1U);
    EXPECT_EQ(texts[0], c_text);
}

TEST_F(SdlInputSourceTest, StopsAfterDestruction)
{
    {
        const SdlInputSource source{*m_platform, m_input};
    }
    push(keyEvent(SDL_SCANCODE_A, true));
    frame();
    EXPECT_FALSE(m_input.isDown(Key::A)) << "no source, no input";
}

TEST_F(SdlInputSourceTest, VirtualGamepad)
{
    Meow2::Result<SdlGamepadSource> gamepads = SdlGamepadSource::create(*m_platform, m_input, m_log);
    ASSERT_TRUE(gamepads.has_value()) << std::format("{}", gamepads.error());

    // An SDL virtual gamepad with every standard button and axis, in SDL's order.
    SDL_VirtualJoystickDesc desc;
    SDL_INIT_INTERFACE(&desc);
    desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
    desc.naxes = SDL_GAMEPAD_AXIS_COUNT;
    desc.nbuttons = SDL_GAMEPAD_BUTTON_DPAD_RIGHT + 1;
    desc.axis_mask = (1U << SDL_GAMEPAD_AXIS_COUNT) - 1U;
    desc.button_mask = (1U << (SDL_GAMEPAD_BUTTON_DPAD_RIGHT + 1)) - 1U;
    desc.name = "Meow2 Test Pad";
    const SDL_JoystickID id = SDL_AttachVirtualJoystick(&desc);
    ASSERT_NE(id, 0U) << SDL_GetError();

    frame();
    // Real gamepads plugged into the test machine show up too, so look for this one by its id.
    const auto isConnected = [this, id]
    {
        const auto gamepads = m_input.connectedGamepads();
        return std::ranges::find(gamepads, id) != gamepads.end();
    };
    ASSERT_TRUE(isConnected()) << "the virtual pad arrives as GAMEPAD_ADDED";

    SDL_Joystick* joystick = SDL_GetJoystickFromID(id); // opened by the source
    ASSERT_NE(joystick, nullptr);
    ASSERT_TRUE(SDL_SetJoystickVirtualButton(joystick, SDL_GAMEPAD_BUTTON_SOUTH, true));
    ASSERT_TRUE(SDL_SetJoystickVirtualAxis(joystick, SDL_GAMEPAD_AXIS_LEFTX, SDL_JOYSTICK_AXIS_MAX));
    frame(); // the virtual device is updated during the pump; its events can land in this pump or the next
    frame();
    EXPECT_TRUE(m_input.isDown(id, GamepadButton::South));
    EXPECT_FLOAT_EQ(m_input.axis(id, GamepadAxis::LeftX), 1.0F);

    ASSERT_TRUE(SDL_DetachVirtualJoystick(id));
    frame();
    EXPECT_FALSE(isConnected());
    EXPECT_FALSE(m_input.isDown(id, GamepadButton::South));
}
