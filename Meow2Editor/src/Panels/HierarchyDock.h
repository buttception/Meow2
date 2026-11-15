#pragma once

/// @file
/// @brief HierarchyDock: the scene's entities as a tree; selecting, renaming and re-parenting run commands.

#include "Commands/EditorCommandRegistry.h"
#include "Editing/SceneEditor.h"
#include "Viewports/EditorWorld.h"

#include <Meow2/Core/Log/Logger.h>

#include <cstdint>
#include <QDockWidget>
#include <QJsonObject>
#include <QString>

class QTreeWidget;
class QTreeWidgetItem;

namespace Meow2::Editor
{
/// @brief A dock showing the scene's saved entities as a tree (`scene.hierarchy`).
///
/// It changes nothing itself (techstack §20): selecting runs `selection.set`, editing a name (F2, or a double-click)
/// runs `entity.rename`, dropping entities onto another runs `entity.setParent` (onto empty space: to the root), and
/// Create runs `entity.create`. refresh() rebuilds the tree only when the world's revision changed, and keeps the
/// selection in step with the scene editor's.
/// @par Thread safety
/// Not thread-safe; GUI thread only, like every widget.
/// @par Lifetime
/// The registry, the scene editor, the world and the log must outlive it.
class HierarchyDock final : public QDockWidget
{
public:
    /// @brief An empty dock; refresh() fills it.
    /// @param commands Runs what the user does.
    /// @param editor   The selection.
    /// @param world    The scene's revision.
    /// @param log      Commands that fail.
    /// @param parent   Qt owner, usually the main window.
    HierarchyDock(EditorCommandRegistry& commands, const SceneEditor& editor, const EditorWorld& world,
                  Log::Logger& log, QWidget* parent = nullptr);

    /// @brief Rebuilds the tree if the scene changed, and shows the scene editor's selection.
    void refresh();

    /// @brief The tree (tests).
    /// @return It.
    [[nodiscard]] QTreeWidget& tree()
    {
        return *m_tree;
    }

    /// @brief Moves entities under another, as a drop does.
    /// @param ids    The dragged entities' IDs.
    /// @param parent The entity dropped on, or empty for the root.
    void reparent(const QStringList& ids, const QString& parent);

private:
    /// @brief Runs a command; a failure is logged.
    void run(const QString& command, const QJsonObject& parameters);
    /// @brief Adds an entity and its children (from scene.hierarchy) under `parent` (null: top level).
    void addItems(const QJsonObject& entity, QTreeWidgetItem* parent);
    /// @brief Selects the scene editor's selection in the tree, without echoing it back as a command.
    void showSelection();

    EditorCommandRegistry& m_commands;
    const SceneEditor& m_editor;
    const EditorWorld& m_world;
    Log::Logger& m_log;
    QTreeWidget* m_tree;                               ///< Owned by Qt (parent–child).
    std::uint64_t m_shownRevision = ~std::uint64_t{0}; ///< The world revision the tree shows.
    bool m_updating = false;                           ///< Set while the tree is changed from code, not the user.
};
} // namespace Meow2::Editor
