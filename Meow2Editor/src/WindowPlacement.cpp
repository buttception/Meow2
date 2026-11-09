/// @file
/// @brief WindowDesc ⇄ Qt top-level window (WindowPlacement.h).

#include "WindowPlacement.h"

#include <Meow2/Platform/Window/WindowConfig.h>

#include <algorithm>
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
    const QScreen* screen = reachable ? QGuiApplication::screenAt(rect.center()) : nullptr;
    if (screen == nullptr)
    {
        screen = QGuiApplication::primaryScreen();
    }
    if (screen != nullptr)
    {
        // Qt sizes are device-independent: at 125 % or 150 % display scaling the 1920 x 1080 default is 2400 x 1350
        // or 2880 x 1620 real pixels, bigger than the screen. Shrink to the screen's work area (keeping the shape),
        // with room for the title bar and frame, which setGeometry() leaves out.
        const QRect area = screen->availableGeometry();
        const QSize room{std::max(area.width() - 16, 320), std::max(area.height() - 48, 240)};
        if (rect.width() > room.width() || rect.height() > room.height())
        {
            const double fit = std::min(static_cast<double>(room.width()) / rect.width(),
                                        static_cast<double>(room.height()) / rect.height());
            rect.setSize(QSize{static_cast<int>(rect.width() * fit), static_cast<int>(rect.height() * fit)});
            MEOW2_LOG_INFO(log, "Window {}x{} doesn't fit the screen ({}x{} usable); using {}x{}", desc.width,
                           desc.height, area.width(), area.height(), rect.width(), rect.height());
        }
        if (!reachable || !area.contains(rect))
        {
            rect.moveCenter(area.center());
            rect.moveTop(std::max(rect.top(), area.top() + 32)); // keep the title bar on screen
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
