/// @file
/// @brief ViewportWidget implementation.

#include "ViewportWidget.h"

#include <QGuiApplication>

#if defined(Q_OS_LINUX) && __has_include(<QtGui/qguiapplication_platform.h>)
#include <QtGui/qguiapplication_platform.h>
#endif

namespace Meow2::Editor
{
ViewportWidget::ViewportWidget(QWidget* parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_NativeWindow);       // its own OS window, for the swapchain
    setAttribute(Qt::WA_PaintOnScreen);      // Qt doesn't paint it through the backing store...
    setAttribute(Qt::WA_NoSystemBackground); // ...nor clear it before the swapchain presents
    setFocusPolicy(Qt::StrongFocus);         // keyboard input for the viewport camera (QtInputSource, M7)
    setMinimumSize(64, 64);
}

Platform::NativeWindowHandle ViewportWidget::nativeHandle()
{
    Platform::NativeWindowHandle handle;
    const QString platform = QGuiApplication::platformName();
    if (platform == QStringLiteral("windows"))
    {
        handle.type = Platform::NativeWindowType::Win32;
        handle.window = reinterpret_cast<void*>(winId()); // WId is the HWND
    }
#if defined(Q_OS_LINUX) && QT_CONFIG(xcb)
    else if (platform == QStringLiteral("xcb"))
    {
        handle.type = Platform::NativeWindowType::X11;
        handle.x11Window = static_cast<std::uint64_t>(winId()); // WId is the X11 window id
        if (const auto* x11 = qGuiApp->nativeInterface<QNativeInterface::QX11Application>())
        {
            handle.display = x11->display();
        }
    }
#endif
    return handle;
}

QPaintEngine* ViewportWidget::paintEngine() const
{
    return nullptr;
}
} // namespace Meow2::Editor
