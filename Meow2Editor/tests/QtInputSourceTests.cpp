/// @file
/// @brief Tests for QtInputSource: Qt events on a viewport → the engine's InputSystem, and the scan code tables.

#include "QtTestApplication.h"
#include "Viewports/QtInputSource.h"

#include <Meow2/Core/Events/EventBus.h>
#include <Meow2/Core/Memory/GlobalTracking.h>
#include <Meow2/Platform/Input/InputSystem.h>

#include <QApplication>
#include <QCoreApplication>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMouseEvent>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QWidget>

namespace
{
using Meow2::Input::Key;

/// @brief A window with a viewport (watched) and a line edit beside it (not watched).
class QtInputSourceTest : public Meow2::Tests::QtTest
{
protected:
    QtInputSourceTest()
    {
        auto* layout = new QVBoxLayout(&m_window); // Qt parent–child ownership
        m_viewport = new QWidget(&m_window);
        m_viewport->setMinimumSize(100, 100);
        m_field = new QLineEdit(&m_window);
        layout->addWidget(m_viewport);
        layout->addWidget(m_field);
        m_source.watch(m_viewport);
        m_window.show();
        m_window.activateWindow(); // focus needs an active window, offscreen too
        QCoreApplication::processEvents();
    }

    /// @brief Sends the viewport a W key event with the platform's native scan code (0x11 on Windows, evdev 17 + 8
    ///        on X11). Qt events can't be copied, so it's built and sent here.
    /// @param type       Press or release.
    /// @param autoRepeat One of the release + press pairs a held key repeats as.
    void sendKeyW(QEvent::Type type, bool autoRepeat = false)
    {
#if defined(_WIN32)
        constexpr quint32 c_nativeW = 0x11;
#else
        constexpr quint32 c_nativeW = 17 + 8;
#endif
        QKeyEvent key{type,
                      Qt::Key_W,
                      Qt::NoModifier,
                      c_nativeW,
                      0,
                      0,
                      type == QEvent::KeyPress ? QStringLiteral("w") : QString{},
                      autoRepeat};
        QApplication::sendEvent(m_viewport, &key);
    }

    Meow2::Events::EventBus m_events{Meow2::Memory::globalTracker()};
    Meow2::Input::InputSystem m_input{m_events};
    Meow2::Editor::QtInputSource m_source{m_input};
    QWidget m_window;
    QWidget* m_viewport = nullptr;
    QLineEdit* m_field = nullptr;
};
} // namespace

TEST_F(QtInputSourceTest, KeysAndMouseReachTheInputSystem)
{
    m_viewport->setFocus();
    ASSERT_TRUE(m_viewport->hasFocus());

    sendKeyW(QEvent::KeyPress);
    QMouseEvent down{QEvent::MouseButtonPress, QPointF{10, 10}, QPointF{10, 10},
                     Qt::RightButton,          Qt::RightButton, Qt::NoModifier};
    QApplication::sendEvent(m_viewport, &down);
    QMouseEvent move1{QEvent::MouseMove, QPointF{10, 10}, QPointF{10, 10},
                      Qt::NoButton,      Qt::RightButton, Qt::NoModifier};
    QMouseEvent move2{QEvent::MouseMove, QPointF{25, 4}, QPointF{25, 4}, Qt::NoButton, Qt::RightButton, Qt::NoModifier};
    QApplication::sendEvent(m_viewport, &move1);
    QApplication::sendEvent(m_viewport, &move2);
    QWheelEvent wheel{QPointF{10, 10}, QPointF{10, 10}, QPoint{},          QPoint{0, 240},
                      Qt::NoButton,    Qt::NoModifier,  Qt::NoScrollPhase, false};
    QApplication::sendEvent(m_viewport, &wheel);
    m_input.beginFrame();

    EXPECT_TRUE(m_input.isDown(Key::W));
    EXPECT_TRUE(m_input.isDown(Meow2::Input::MouseButton::Right));
    EXPECT_FLOAT_EQ(m_input.mouseDelta().x, 15.0F) << "the motion between the two moves";
    EXPECT_FLOAT_EQ(m_input.mouseDelta().y, -6.0F);
    EXPECT_FLOAT_EQ(m_input.wheelDelta().y, 2.0F) << "two notches of 120";
    EXPECT_EQ(m_source.activeViewport(), m_viewport);

    // Losing focus releases what was held, so nothing stays stuck.
    m_field->setFocus();
    QFocusEvent out{QEvent::FocusOut, Qt::TabFocusReason};
    QApplication::sendEvent(m_viewport, &out);
    m_input.beginFrame();
    EXPECT_FALSE(m_input.isDown(Key::W));
    EXPECT_FALSE(m_input.isDown(Meow2::Input::MouseButton::Right));
    EXPECT_EQ(m_source.activeViewport(), nullptr);
}

TEST_F(QtInputSourceTest, UnfocusedViewportIsIgnored)
{
    m_field->setFocus();
    ASSERT_FALSE(m_viewport->hasFocus());
    sendKeyW(QEvent::KeyPress);
    m_input.beginFrame();
    EXPECT_FALSE(m_input.isDown(Key::W)) << "typing elsewhere never reaches the game or the camera";

    // A click on the viewport gives it focus, and the click itself counts.
    QMouseEvent click{QEvent::MouseButtonPress, QPointF{5, 5},  QPointF{5, 5},
                      Qt::LeftButton,           Qt::LeftButton, Qt::NoModifier};
    QApplication::sendEvent(m_viewport, &click);
    m_input.beginFrame();
    EXPECT_TRUE(m_viewport->hasFocus());
    EXPECT_TRUE(m_input.isDown(Meow2::Input::MouseButton::Left));
    // And where it is, though no move reached the viewport before it (owner report, #229: the first click didn't pick).
    EXPECT_FLOAT_EQ(m_input.mousePosition().x, 5.0F);
    EXPECT_FLOAT_EQ(m_input.mousePosition().y, 5.0F);
}

TEST(QtInputSourceKeys, MapsNativeScanCodes)
{
    using Meow2::Editor::keyFromEvdevCode;
    using Meow2::Editor::keyFromWindowsScanCode;
    // Windows scan code set 1 (extended keys carry 0x100).
    EXPECT_EQ(keyFromWindowsScanCode(0x11), Key::W);
    EXPECT_EQ(keyFromWindowsScanCode(0x39), Key::Space);
    EXPECT_EQ(keyFromWindowsScanCode(0x2A), Key::LeftShift);
    EXPECT_EQ(keyFromWindowsScanCode(0x3B), Key::F1);
    EXPECT_EQ(keyFromWindowsScanCode(0x148), Key::Up);
    EXPECT_EQ(keyFromWindowsScanCode(0x14B), Key::Left);
    EXPECT_EQ(keyFromWindowsScanCode(0x11D), Key::RightCtrl);
    EXPECT_EQ(keyFromWindowsScanCode(0x45), Key::Pause);
    EXPECT_EQ(keyFromWindowsScanCode(0x145), Key::NumLock);
    EXPECT_EQ(keyFromWindowsScanCode(0x7F), Key::Unknown);
    // Linux evdev (X11 key codes minus 8).
    EXPECT_EQ(keyFromEvdevCode(17), Key::W);
    EXPECT_EQ(keyFromEvdevCode(57), Key::Space);
    EXPECT_EQ(keyFromEvdevCode(42), Key::LeftShift);
    EXPECT_EQ(keyFromEvdevCode(59), Key::F1);
    EXPECT_EQ(keyFromEvdevCode(103), Key::Up);
    EXPECT_EQ(keyFromEvdevCode(105), Key::Left);
    EXPECT_EQ(keyFromEvdevCode(69), Key::NumLock);
    EXPECT_EQ(keyFromEvdevCode(119), Key::Pause);
    EXPECT_EQ(keyFromEvdevCode(500), Key::Unknown);
    // No native code (offscreen): the Qt key, layout and all.
    EXPECT_EQ(Meow2::Editor::keyFromQt(0, Qt::Key_D), Key::D);
    EXPECT_EQ(Meow2::Editor::keyFromQt(0, Qt::Key_Shift), Key::LeftShift);
    EXPECT_EQ(Meow2::Editor::keyFromQt(0, Qt::Key_Question), Key::Unknown);
}

// Owner report (#229): the fly camera stopped a second or two into holding W. A held key repeats as release + press
// pairs marked auto-repeat; the release was taken as a real one.
TEST_F(QtInputSourceTest, HeldKeyStaysDownThroughAutoRepeat)
{
    m_viewport->setFocus();
    ASSERT_TRUE(m_viewport->hasFocus());
    sendKeyW(QEvent::KeyPress);
    m_input.beginFrame();
    ASSERT_TRUE(m_input.isDown(Key::W));
    for (int i = 0; i < 3; ++i)
    {
        sendKeyW(QEvent::KeyRelease, true);
        sendKeyW(QEvent::KeyPress, true);
        m_input.beginFrame();
        EXPECT_TRUE(m_input.isDown(Key::W)) << "repeat " << i;
        EXPECT_FALSE(m_input.wasPressed(Key::W)) << "a repeat isn't a new press";
    }
    sendKeyW(QEvent::KeyRelease);
    m_input.beginFrame();
    EXPECT_FALSE(m_input.isDown(Key::W)) << "the real release lets go";
}
