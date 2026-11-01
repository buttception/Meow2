#pragma once

/// @file
/// @brief EditorMainWindow: the editor's top-level window. Docks, menus and commands come with M9.

#include "ViewportWidget.h"

#include <QMainWindow>

namespace Meow2::Editor
{
/// @brief The editor's main window: for now a title bar and the viewport filling it.
///
/// Later (M9) it gets the dock layout (scene hierarchy, inspector, log console). Every UI action will go through an
/// editor command (techstack.md §20), so this window will never change editor state directly.
/// @par Thread safety
/// Not thread-safe; GUI thread only, like every QWidget.
class EditorMainWindow final : public QMainWindow
{
public:
    /// @brief Creates the window with its viewport. It isn't shown until showWithDesc() (WindowPlacement.h).
    /// @param parent Owning widget; null for a top-level window.
    explicit EditorMainWindow(QWidget* parent = nullptr);

    /// @brief The viewport in the middle of the window.
    /// @return The viewport, owned by this window.
    [[nodiscard]] ViewportWidget& viewport();

private:
    ViewportWidget* m_viewport; // owned by Qt (parent–child), never null
};
} // namespace Meow2::Editor
