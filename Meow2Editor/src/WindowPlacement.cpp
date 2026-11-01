/// @file
/// @brief WindowDesc ⇄ Qt top-level window (WindowPlacement.h).

#include "WindowPlacement.h"

#include <Meow2/Platform/Window/WindowConfig.h>

#include <QGuiApplication>
#include <QScreen>
#include <vector>

namespace Meow2::Editor
{
namespace
{
Platform::WindowRect toWindowRect(const QRect& rect)
{
    return {rect.x(), rect.y(), rect.width(), rect.height()};
}

/// @brief Every screen's bounds, for Platform::isReachable().
std::vector<Platform::WindowRect> screenBounds()
{
    std::vector<Platform::WindowRect> bounds;
    for (const QScreen* screen : QGuiApplication::screens())
    {
        bounds.push_back(toWindowRect(screen->geometry()));
    }
    return bounds;
}
} // namespace

void showWithDesc(QWidget& window, const Platform::WindowDesc& desc, Log::Logger& log)
{
    window.setWindowTitle(QString::fromStdString(desc.title));

    // setGeometry() places the client area (without the frame), which is what the window.* keys describe.
    QRect rect{desc.x.value_or(0), desc.y.value_or(0), desc.width, desc.height};
    const bool hasPosition = desc.x.has_value() || desc.y.has_value();
    const bool reachable = hasPosition && Platform::isReachable(toWindowRect(rect), screenBounds());
    if (hasPosition && !reachable)
    {
        MEOW2_LOG_INFO(log, "Window position ({}, {}) is off every screen; centring it", rect.x(), rect.y());
    }
    if (!reachable)
    {
        if (const QScreen* primary = QGuiApplication::primaryScreen())
        {
            rect.moveCenter(primary->availableGeometry().center());
        }
    }
    window.setGeometry(rect);
    if (!desc.resizable)
    {
        window.setFixedSize(rect.size());
    }

    // setWindowState() before show(): the normal geometry set above is kept as the one to restore to.
    Qt::WindowStates state = Qt::WindowNoState;
    if (desc.mode == Platform::WindowMode::Fullscreen)
    {
        state |= Qt::WindowFullScreen;
    }
    if (desc.maximized)
    {
        state |= Qt::WindowMaximized;
    }
    window.setWindowState(state);
    if (!desc.hidden)
    {
        window.show();
    }
}

Platform::WindowRect restoredBounds(const QWidget& window)
{
    // normalGeometry() is the geometry when shown normally: the current one while windowed, the remembered one
    // while maximized or fullscreen.
    return toWindowRect(window.normalGeometry());
}

void storeWindowState(Config::Config& config, const QWidget& window)
{
    const Qt::WindowStates state = window.windowState();
    Platform::storeWindowState(config, restoredBounds(window), state.testFlag(Qt::WindowMaximized),
                               state.testFlag(Qt::WindowFullScreen) ? Platform::WindowMode::Fullscreen
                                                                    : Platform::WindowMode::Windowed);
}
} // namespace Meow2::Editor
