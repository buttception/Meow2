#pragma once

/// @file
/// @brief AssetsDock: the project folder's scenes and models as a file tree.

#include <functional>
#include <QDockWidget>
#include <QString>

class QFileSystemModel;
class QTreeView;

namespace Meow2::Editor
{
/// @brief A dock listing the project's scene files (`.m2scene`) and models (`.gltf`, `.glb`) in their folders.
///
/// Activating an entry (double-click, or Enter) hands its native path to onActivate(); the window turns that into a
/// command (`scene.open` for a scene, `asset.importModel` for a model), so the dock changes nothing itself. The tree
/// follows the folder on disk as files come and go (QFileSystemModel watches it).
/// @par Thread safety
/// Not thread-safe; GUI thread only, like every widget.
class AssetsDock final : public QDockWidget
{
public:
    /// @brief An empty dock; setProject() fills it.
    /// @param parent Qt owner, usually the main window.
    explicit AssetsDock(QWidget* parent = nullptr);

    /// @brief Shows a project folder.
    /// @param folder The folder, a native path.
    void setProject(const QString& folder);

    /// @brief The folder shown.
    /// @return A native path, or empty.
    [[nodiscard]] const QString& project() const
    {
        return m_folder;
    }

    /// @brief What to do with an activated entry: called with its native path.
    /// @param handler The handler; replaces any earlier one.
    void onActivate(std::function<void(const QString&)> handler)
    {
        m_onActivate = std::move(handler);
    }

    /// @brief Activates an entry as a double-click would (tests, and the tree's own signal).
    /// @param path The file, a native path; folders are ignored.
    void activate(const QString& path);

private:
    QFileSystemModel* m_model;                        ///< Owned by Qt (parent–child).
    QTreeView* m_tree;                                ///< Owned by Qt.
    QString m_folder;                                 ///< setProject().
    std::function<void(const QString&)> m_onActivate; ///< onActivate().
};
} // namespace Meow2::Editor
