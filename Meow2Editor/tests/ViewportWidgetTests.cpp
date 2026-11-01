/// @file
/// @brief Tests for ViewportWidget, the editor's native viewport.

#include "EditorMainWindow.h"
#include "QtTestApplication.h"

#include <array>
#include <QApplication>
#include <QGuiApplication>

namespace
{
using ViewportTest = Meow2::Tests::QtTest;
} // namespace

TEST_F(ViewportTest, IsTheCentralWidget)
{
    Meow2::Editor::EditorMainWindow window;
    EXPECT_EQ(window.centralWidget(), &window.viewport());
    EXPECT_TRUE(window.viewport().testAttribute(Qt::WA_NativeWindow));
    EXPECT_EQ(window.viewport().paintEngine(), nullptr) << "Qt never paints over the swapchain";
}

TEST_F(ViewportTest, NativeHandleOffscreen)
{
    Meow2::Editor::EditorMainWindow window;
    window.show();
    EXPECT_EQ(window.viewport().nativeHandle().type, Meow2::Platform::NativeWindowType::None)
        << "the offscreen platform has no OS window to present to";
}

#if defined(_WIN32)
TEST(ViewportWin32, NativeHandle)
{
    // The real Windows platform (no -platform argument), with a window that's never shown.
    std::array<char, 5> program{"test"};
    std::array<char*, 2> arguments{program.data(), nullptr};
    int argumentCount = 1;
    QApplication application{argumentCount, arguments.data()};
    ASSERT_EQ(QGuiApplication::platformName().toStdString(), "windows");

    Meow2::Editor::EditorMainWindow window;
    const Meow2::Platform::NativeWindowHandle handle = window.viewport().nativeHandle();
    EXPECT_EQ(handle.type, Meow2::Platform::NativeWindowType::Win32);
    EXPECT_NE(handle.window, nullptr);
    EXPECT_NE(handle.window, reinterpret_cast<void*>(window.winId())) << "the viewport has its own OS window";
}
#endif
