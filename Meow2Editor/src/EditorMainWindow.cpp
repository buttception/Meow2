/// @file
/// @brief EditorMainWindow implementation.

#include "EditorMainWindow.h"

namespace Meow2::Editor
{
EditorMainWindow::EditorMainWindow(QWidget* parent)
    : QMainWindow(parent)
    , m_viewport(new ViewportWidget(this)) // Qt parent–child: the window deletes it
{
    setCentralWidget(m_viewport);
}

ViewportWidget& EditorMainWindow::viewport()
{
    return *m_viewport;
}
} // namespace Meow2::Editor
