#pragma once

/// @file
/// @brief ViewportWidget: the editor's 3D view, a native child window the RHI renders into (M5).

#include <Meow2/Platform/Window/Window.h>

#include <QWidget>

namespace Meow2::Editor
{
/// @brief A widget with its own native OS window, which Qt never paints, for the RHI's swapchain.
///
/// Qt normally draws all widgets of a window into one backing store. A viewport rendered by Vulkan or D3D12 needs a
/// real OS window of its own (an `HWND`, an X11 window) that only the swapchain draws to. This widget asks Qt for
/// one (`Qt::WA_NativeWindow`) and tells Qt not to paint it (`WA_PaintOnScreen` with no paint engine). Until the RHI
/// exists (M5) it shows whatever the OS leaves there.
/// @par Thread safety
/// Not thread-safe; GUI thread only, like every QWidget.
class ViewportWidget final : public QWidget
{
public:
    /// @brief Creates the viewport.
    /// @param parent Owning widget (Qt parent–child ownership), usually the main window.
    explicit ViewportWidget(QWidget* parent = nullptr);

    /// @brief The OS window behind the widget, for creating a swapchain. Creates the native window if needed.
    /// @return `Win32` with the `HWND` on Windows, `X11` with display and window id on X11, and
    ///         NativeWindowType::None on platforms the RHI can't present to (offscreen, Wayland for now).
    /// @warning Query it when creating the swapchain; don't keep it (techstack.md §11).
    [[nodiscard]] Platform::NativeWindowHandle nativeHandle();

    /// @brief No paint engine: Qt must not draw over what the swapchain presents.
    /// @return Always null.
    [[nodiscard]] QPaintEngine* paintEngine() const override;
};
} // namespace Meow2::Editor
