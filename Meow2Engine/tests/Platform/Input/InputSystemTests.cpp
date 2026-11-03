/// @file
/// @brief Tests for InputSystem: state, frame and fixed-step edges, mouse, gamepads, bus events, releaseAll.

#include <Meow2/Core/Events/EventBus.h>
#include <Meow2/Platform/Input/InputSystem.h>

#include <gtest/gtest.h>

#include <memory_resource>
#include <string>
#include <vector>

namespace
{
using namespace Meow2::Input;

/// @brief An InputSystem on its own bus, plus a record of the bus events it sends.
class InputSystemTest : public ::testing::Test
{
protected:
    /// @brief Applies what was submitted and delivers the bus events, like one frame of the game loop.
    void frame()
    {
        m_input.beginFrame();
        m_events.dispatchQueued();
    }

    Meow2::Events::EventBus m_events{*std::pmr::new_delete_resource()};
    InputSystem m_input{m_events};
    std::vector<std::string> m_log; ///< Bus events, as short strings.

    // Last members: destroyed first, before the log their handlers write to.
    Meow2::Events::Subscription m_onKeyPressed = m_events.subscribe<KeyPressed>(
        [this](const KeyPressed& e)
        { m_log.push_back(std::string{"pressed"} + std::to_string(static_cast<int>(e.key)) + (e.repeat ? "r" : "")); });
    Meow2::Events::Subscription m_onKeyReleased = m_events.subscribe<KeyReleased>(
        [this](const KeyReleased& e) { m_log.push_back("released" + std::to_string(static_cast<int>(e.key))); });
    Meow2::Events::Subscription m_onMouseButton = m_events.subscribe<MouseButtonChanged>(
        [this](const MouseButtonChanged& e) { m_log.push_back(e.down ? "mouseDown" : "mouseUp"); });
    Meow2::Events::Subscription m_onMouseMoved =
        m_events.subscribe<MouseMoved>([this](const MouseMoved&) { m_log.emplace_back("moved"); });
    Meow2::Events::Subscription m_onWheel =
        m_events.subscribe<MouseWheelScrolled>([this](const MouseWheelScrolled&) { m_log.emplace_back("wheel"); });
    Meow2::Events::Subscription m_onText =
        m_events.subscribe<TextEntered>([this](const TextEntered& e) { m_log.push_back("text:" + e.text); });
    Meow2::Events::Subscription m_onPadConnected = m_events.subscribe<GamepadConnected>(
        [this](const GamepadConnected& e) { m_log.push_back("padConnected:" + e.name); });
    Meow2::Events::Subscription m_onPadDisconnected = m_events.subscribe<GamepadDisconnected>(
        [this](const GamepadDisconnected&) { m_log.emplace_back("padDisconnected"); });
    Meow2::Events::Subscription m_onPadButton = m_events.subscribe<GamepadButtonChanged>(
        [this](const GamepadButtonChanged& e) { m_log.push_back(e.down ? "padDown" : "padUp"); });
};
} // namespace

TEST_F(InputSystemTest, KeyDownAndEdges)
{
    m_input.submit(KeyInput{.key = Key::W, .down = true});
    frame();
    EXPECT_TRUE(m_input.isDown(Key::W));
    EXPECT_TRUE(m_input.wasPressed(Key::W));
    EXPECT_FALSE(m_input.wasReleased(Key::W));

    frame();
    EXPECT_TRUE(m_input.isDown(Key::W)) << "still held";
    EXPECT_FALSE(m_input.wasPressed(Key::W)) << "the press edge lasts one frame";

    m_input.submit(KeyInput{.key = Key::W, .down = false});
    frame();
    EXPECT_FALSE(m_input.isDown(Key::W));
    EXPECT_TRUE(m_input.wasReleased(Key::W));
    EXPECT_FALSE(m_input.isDown(Key::A)) << "other keys untouched";
}

TEST_F(InputSystemTest, NothingBeforeBeginFrame)
{
    m_input.submit(KeyInput{.key = Key::Space, .down = true});
    EXPECT_FALSE(m_input.isDown(Key::Space)) << "input is applied at the frame boundary, not when submitted";
    frame();
    EXPECT_TRUE(m_input.isDown(Key::Space));
}

TEST_F(InputSystemTest, TapWithinOneFrame)
{
    m_input.submit(KeyInput{.key = Key::E, .down = true});
    m_input.submit(KeyInput{.key = Key::E, .down = false});
    frame();
    EXPECT_FALSE(m_input.isDown(Key::E));
    EXPECT_TRUE(m_input.wasPressed(Key::E)) << "a quick tap still counts as a press";
    EXPECT_TRUE(m_input.wasReleased(Key::E));
    EXPECT_TRUE(m_input.fixedWasPressed(Key::E));
}

TEST_F(InputSystemTest, FixedLatchSurvivesZeroSteps)
{
    m_input.submit(KeyInput{.key = Key::Space, .down = true});
    frame(); // a fast frame: no fixedUpdate ran, so no endFixedStep()
    m_input.submit(KeyInput{.key = Key::Space, .down = false});
    frame();
    EXPECT_FALSE(m_input.wasPressed(Key::Space)) << "the frame edge is gone";
    EXPECT_TRUE(m_input.fixedWasPressed(Key::Space)) << "the fixed-step edge waits for the next step";
    EXPECT_TRUE(m_input.fixedWasReleased(Key::Space));

    m_input.endFixedStep();
    EXPECT_FALSE(m_input.fixedWasPressed(Key::Space));
    EXPECT_FALSE(m_input.fixedWasReleased(Key::Space));
}

TEST_F(InputSystemTest, FixedLatchSeenOnce)
{
    m_input.submit(KeyInput{.key = Key::Space, .down = true});
    frame();
    int stepsThatSawIt = 0;
    for (int step = 0; step < 3; ++step) // a slow frame: three fixed steps
    {
        stepsThatSawIt += m_input.fixedWasPressed(Key::Space) ? 1 : 0;
        m_input.endFixedStep();
    }
    EXPECT_EQ(stepsThatSawIt, 1) << "one jump, not three";
    EXPECT_TRUE(m_input.isDown(Key::Space));
}

TEST_F(InputSystemTest, RepeatIsNotAnEdge)
{
    m_input.submit(KeyInput{.key = Key::Left, .down = true});
    frame();
    m_input.submit(KeyInput{.key = Key::Left, .down = true, .repeat = true});
    frame();
    EXPECT_FALSE(m_input.wasPressed(Key::Left)) << "auto-repeat isn't a new press";
    EXPECT_TRUE(m_input.isDown(Key::Left));
    const std::vector<std::string> expected{"pressed80", "pressed80r"};
    EXPECT_EQ(m_log, expected) << "but UI code sees the repeat on the bus";
}

TEST_F(InputSystemTest, MouseState)
{
    m_input.submit(MouseMotionInput{.x = 10.0F, .y = 20.0F, .dx = 3.0F, .dy = 4.0F});
    m_input.submit(MouseMotionInput{.x = 12.0F, .y = 21.0F, .dx = 2.0F, .dy = 1.0F});
    m_input.submit(MouseWheelInput{.x = 0.0F, .y = 1.0F});
    m_input.submit(MouseWheelInput{.x = 0.0F, .y = 0.5F});
    m_input.submit(MouseButtonInput{.button = MouseButton::Right, .down = true});
    frame();
    EXPECT_EQ(m_input.mousePosition(), (Vec2{12.0F, 21.0F}));
    EXPECT_EQ(m_input.mouseDelta(), (Vec2{5.0F, 5.0F})) << "deltas are summed over the frame";
    EXPECT_EQ(m_input.wheelDelta(), (Vec2{0.0F, 1.5F}));
    EXPECT_TRUE(m_input.isDown(MouseButton::Right));
    EXPECT_TRUE(m_input.wasPressed(MouseButton::Right));
    EXPECT_TRUE(m_input.fixedWasPressed(MouseButton::Right));

    frame();
    EXPECT_EQ(m_input.mouseDelta(), (Vec2{})) << "no motion this frame";
    EXPECT_EQ(m_input.wheelDelta(), (Vec2{}));
    EXPECT_EQ(m_input.mousePosition(), (Vec2{12.0F, 21.0F})) << "the position stays";
    m_input.submit(MouseButtonInput{.button = MouseButton::Right, .down = false});
    frame();
    EXPECT_TRUE(m_input.wasReleased(MouseButton::Right));
}

TEST_F(InputSystemTest, GamepadConnectAxesButtons)
{
    constexpr GamepadId c_pad = 7;
    m_input.submit(GamepadButtonInput{.gamepad = c_pad, .button = GamepadButton::South, .down = true});
    frame();
    EXPECT_FALSE(m_input.isDown(c_pad, GamepadButton::South)) << "input from a gamepad that isn't connected";

    m_input.submit(GamepadConnection{.gamepad = c_pad, .connected = true, .name = "Pad"});
    m_input.submit(GamepadButtonInput{.gamepad = c_pad, .button = GamepadButton::South, .down = true});
    m_input.submit(GamepadAxisInput{.gamepad = c_pad, .axis = GamepadAxis::LeftX, .value = -0.5F});
    frame();
    ASSERT_EQ(m_input.connectedGamepads().size(), 1U);
    EXPECT_EQ(m_input.connectedGamepads()[0], c_pad);
    EXPECT_TRUE(m_input.isDown(c_pad, GamepadButton::South));
    EXPECT_TRUE(m_input.wasPressed(c_pad, GamepadButton::South));
    EXPECT_TRUE(m_input.fixedWasPressed(c_pad, GamepadButton::South));
    EXPECT_FLOAT_EQ(m_input.axis(c_pad, GamepadAxis::LeftX), -0.5F);
    EXPECT_FLOAT_EQ(m_input.axis(c_pad, GamepadAxis::RightTrigger), 0.0F);

    m_input.submit(GamepadConnection{.gamepad = c_pad, .connected = false});
    frame();
    EXPECT_TRUE(m_input.connectedGamepads().empty());
    EXPECT_FALSE(m_input.isDown(c_pad, GamepadButton::South)) << "a disconnected gamepad has nothing held";
    EXPECT_FLOAT_EQ(m_input.axis(c_pad, GamepadAxis::LeftX), 0.0F);
}

TEST_F(InputSystemTest, PublishesEvents)
{
    m_input.submit(KeyInput{.key = Key::A, .down = true});
    m_input.submit(MouseMotionInput{.x = 1.0F, .y = 1.0F, .dx = 1.0F, .dy = 0.0F});
    m_input.submit(MouseButtonInput{.button = MouseButton::Left, .down = true});
    m_input.submit(MouseWheelInput{.x = 0.0F, .y = 1.0F});
    m_input.submit(TextInput{.text = "a"});
    m_input.submit(GamepadConnection{.gamepad = 1, .connected = true, .name = "Pad"});
    m_input.submit(GamepadButtonInput{.gamepad = 1, .button = GamepadButton::Start, .down = true});
    m_input.beginFrame();
    EXPECT_TRUE(m_log.empty()) << "beginFrame only enqueues";
    m_events.dispatchQueued();

    const std::vector<std::string> expected{"pressed4", "mouseDown", "text:a", "padConnected:Pad",
                                            "padDown",  "moved",     "wheel"};
    EXPECT_EQ(m_log, expected) << "in submission order; motion and wheel summed at the end of the frame";

    m_log.clear();
    m_input.submit(KeyInput{.key = Key::A, .down = false});
    m_input.submit(GamepadConnection{.gamepad = 1, .connected = false});
    frame();
    const std::vector<std::string> expectedAfter{"released4", "padDisconnected"};
    EXPECT_EQ(m_log, expectedAfter);
}

TEST_F(InputSystemTest, ReleaseAllReleasesHeld)
{
    m_input.submit(KeyInput{.key = Key::W, .down = true});
    m_input.submit(KeyInput{.key = Key::LeftShift, .down = true});
    m_input.submit(MouseButtonInput{.button = MouseButton::Left, .down = true});
    frame();
    m_log.clear();

    m_input.releaseAll(); // the window lost focus while they were held
    frame();
    EXPECT_FALSE(m_input.isDown(Key::W));
    EXPECT_FALSE(m_input.isDown(Key::LeftShift));
    EXPECT_FALSE(m_input.isDown(MouseButton::Left));
    EXPECT_TRUE(m_input.wasReleased(Key::W));
    const std::vector<std::string> expected{"released26", "released225", "mouseUp"};
    EXPECT_EQ(m_log, expected);
}

TEST(InputTypes, KeysAreHidUsageIds)
{
    // The values are the USB HID keyboard page, the same numbers as SDL's scancodes: sources rely on it.
    EXPECT_EQ(static_cast<int>(Key::A), 4);
    EXPECT_EQ(static_cast<int>(Key::Z), 29);
    EXPECT_EQ(static_cast<int>(Key::Num0), 39);
    EXPECT_EQ(static_cast<int>(Key::Space), 44);
    EXPECT_EQ(static_cast<int>(Key::F12), 69);
    EXPECT_EQ(static_cast<int>(Key::Up), 82);
    EXPECT_EQ(static_cast<int>(Key::LeftCtrl), 224);
    EXPECT_EQ(static_cast<int>(Key::RightGui), 231);
}
