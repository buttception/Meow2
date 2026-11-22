#pragma once

/// @file
/// @brief SceneSettingsDock: the open scene's settings (its Environment: the sun and the exposure).

#include "Commands/EditorCommandRegistry.h"
#include "Viewports/EditorWorld.h"

#include <Meow2/Core/Log/Logger.h>

#include <cstdint>
#include <QDockWidget>
#include <QJsonObject>
#include <QString>

class QVBoxLayout;

namespace Meow2::Editor
{
/// @brief A dock with the scene's settings, the ones that belong to the scene rather than to an entity: the
///        Environment's sun angles, colour and strength, and the exposure. Opened from File → Scene settings… or the
///        Window menu.
///
/// Built from `component.list`'s settings and `scene.settings`, and every edit runs `scene.setSetting`, so it can be
/// undone and an agent does the same. It rebuilds when the world's revision moves (another scene, an undo, an agent).
/// @par Thread safety
/// Not thread-safe; the GUI thread.
class SceneSettingsDock final : public QDockWidget
{
public:
    /// @brief An empty dock; refresh() fills it.
    /// @param commands Runs the queries and `scene.setSetting`.
    /// @param world    Its revision says when to rebuild.
    /// @param log      Receives failed commands.
    /// @param parent   The main window.
    SceneSettingsDock(EditorCommandRegistry& commands, const EditorWorld& world, Log::Logger& log,
                      QWidget* parent = nullptr);

    /// @brief Rebuilds the fields if the scene changed since the last time.
    void refresh();

private:
    /// @brief Makes the fields from the settings' descriptions and values.
    void build();
    /// @brief Runs a command; a failure is logged.
    void run(const QString& command, const QJsonObject& parameters);

    EditorCommandRegistry& m_commands;
    const EditorWorld& m_world;
    Log::Logger& m_log;
    std::uint64_t m_shownRevision = ~std::uint64_t{0}; ///< The world revision the fields show.
};
} // namespace Meow2::Editor
