/// @file
/// @brief Tests for WindowPlacement: engine window settings applied to and read from a Qt main window.

#include "EditorMainWindow.h"
#include "QtTestApplication.h"
#include "WindowPlacement.h"

#include <Meow2/Core/Config/Config.h>

#include <QGuiApplication>
#include <QScreen>
#include <string>

namespace
{
using Meow2::Platform::WindowDesc;
using Meow2::Platform::WindowMode;

/// @brief A Qt fixture plus an empty config that's never saved.
class WindowPlacementTest : public Meow2::Tests::QtTest
{
protected:
    WindowPlacementTest()
        : m_config(Meow2::Config::Config::empty("unused.toml", m_log))
    {
    }

    Meow2::Config::Config m_config;
    Meow2::Editor::EditorMainWindow m_window;
};
} // namespace

TEST_F(WindowPlacementTest, AppliesDesc)
{
    Meow2::Editor::showWithDesc(m_window, {.title = "Editor", .width = 600, .height = 400, .x = 40, .y = 50}, m_log);
    EXPECT_EQ(m_window.windowTitle().toStdString(), "Editor");
    EXPECT_TRUE(m_window.isVisible());
    const Meow2::Platform::WindowRect bounds = Meow2::Editor::restoredBounds(m_window);
    EXPECT_EQ(bounds.x, 40);
    EXPECT_EQ(bounds.y, 50);
    EXPECT_EQ(bounds.width, 600);
    EXPECT_EQ(bounds.height, 400);
    EXPECT_FALSE(m_window.isMaximized());
}

TEST_F(WindowPlacementTest, StoresNormalGeometryWhenMaximized)
{
    Meow2::Editor::showWithDesc(m_window, {.width = 640, .height = 480, .x = 10, .y = 20, .maximized = true}, m_log);
    EXPECT_TRUE(m_window.isMaximized());

    Meow2::Editor::storeWindowState(m_config, m_window);
    EXPECT_EQ(m_config.getInt("window.width", 0), 640) << "the restored size, not the maximized one";
    EXPECT_EQ(m_config.getInt("window.height", 0), 480);
    EXPECT_EQ(m_config.getInt("window.x", 0), 10);
    EXPECT_TRUE(m_config.getBool("window.maximized", false));
    EXPECT_EQ(m_config.getString("window.mode", ""), "windowed");
}

TEST_F(WindowPlacementTest, StoresFullscreen)
{
    Meow2::Editor::showWithDesc(m_window, {.width = 640, .height = 480, .mode = WindowMode::Fullscreen}, m_log);
    EXPECT_TRUE(m_window.isFullScreen());

    Meow2::Editor::storeWindowState(m_config, m_window);
    EXPECT_EQ(m_config.getString("window.mode", ""), "fullscreen");
    EXPECT_EQ(m_config.getInt("window.width", 0), 640);
}

TEST_F(WindowPlacementTest, OffDisplayIsCentred)
{
    Meow2::Editor::showWithDesc(m_window, {.width = 400, .height = 300, .x = -100000, .y = -100000}, m_log);
    const QRect screen = QGuiApplication::primaryScreen()->availableGeometry();
    const Meow2::Platform::WindowRect bounds = Meow2::Editor::restoredBounds(m_window);
    // QRect::center() rounds down (its right edge is x + width - 1), so compare centres the same way.
    EXPECT_EQ(QRect(bounds.x, bounds.y, bounds.width, bounds.height).center(), screen.center());
    EXPECT_NE(logText().find("is off every screen; centring it"), std::string::npos);
}

TEST_F(WindowPlacementTest, HiddenStaysHidden)
{
    Meow2::Editor::showWithDesc(m_window, {.width = 400, .height = 300, .hidden = true}, m_log);
    EXPECT_FALSE(m_window.isVisible());
    EXPECT_EQ(Meow2::Editor::restoredBounds(m_window).width, 400);
}

TEST_F(WindowPlacementTest, TooBigShrinksToTheScreen)
{
    // A size in Qt's device-independent pixels can be bigger than the screen (1920 x 1080 at 150 % scaling).
    const QRect screen = QGuiApplication::primaryScreen()->availableGeometry();
    Meow2::Editor::showWithDesc(m_window, {.width = screen.width() * 3, .height = screen.width() * 3 * 9 / 16}, m_log);
    const Meow2::Platform::WindowRect bounds = Meow2::Editor::restoredBounds(m_window);
    EXPECT_LE(bounds.width, screen.width());
    EXPECT_LE(bounds.height, screen.height());
    EXPECT_NEAR(static_cast<double>(bounds.width) / bounds.height, 16.0 / 9.0, 0.02) << "the shape is kept";
    EXPECT_TRUE(screen.contains(QRect(bounds.x, bounds.y, bounds.width, bounds.height)));
    EXPECT_NE(logText().find("doesn't fit the screen"), std::string::npos);
}
