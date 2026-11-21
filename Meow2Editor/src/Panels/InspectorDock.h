#pragma once

/// @file
/// @brief InspectorDock: the selected entity's name and components, with an editor per field generated from the
///        component registry; every edit runs a command.

#include "Commands/EditorCommandRegistry.h"
#include "Editing/SceneEditor.h"
#include "Viewports/EditorWorld.h"

#include <Meow2/Core/Id/Uuid.h>
#include <Meow2/Core/Log/Logger.h>
#include <Meow2/Math/Types.h>

#include <cstdint>
#include <map>
#include <optional>
#include <QDockWidget>
#include <QJsonObject>
#include <QSet>
#include <QString>

class QVBoxLayout;
class QWidget;

namespace Meow2::Editor
{
/// @brief A dock showing the selected entity: its name, then each component as a group of field editors made from
///        `component.list` (types) and `entity.get` (values).
///
/// | Field type | Editor |
/// |---|---|
/// | bool | check box |
/// | int, float | spin box |
/// | vec3, color | a row of 3 or 4 spin boxes |
/// | quat | Euler angles in degrees (X pitch, Y yaw, Z roll) |
/// | enum | drop-down |
/// | string, asset, entity | text |
///
/// A finished edit (Enter, or leaving the box) runs `component.setField`; the name runs `entity.rename`; each group's
/// Remove button `component.remove`; the Add component drop-down `component.add`. Nothing is changed directly
/// (techstack §20).
///
/// **Rotations** are stored as quaternions and shown as Euler angles. Several angle sets give the same rotation, so
/// angles computed back from the quaternion could jump (90, 0, 0 shown as 90, 0, -0.0001 or 180, …). The angles the
/// user typed are kept per entity and field while they still describe the stored rotation, so the inspector shows
/// what was typed (techstack §10).
/// @par Thread safety
/// Not thread-safe; GUI thread only, like every widget.
/// @par Lifetime
/// The registry, the scene editor, the world and the log must outlive it.
class InspectorDock final : public QDockWidget
{
public:
    /// @brief An empty dock; refresh() fills it.
    /// @param commands Runs edits and reads the entity.
    /// @param editor   The selection.
    /// @param world    The scene's revision.
    /// @param log      Commands that fail.
    /// @param parent   Qt owner, usually the main window.
    InspectorDock(EditorCommandRegistry& commands, const SceneEditor& editor, const EditorWorld& world,
                  Log::Logger& log, QWidget* parent = nullptr);

    /// @brief Rebuilds the editors if the selection or the scene changed.
    void refresh();

    /// @brief The entity shown.
    /// @return Its ID, or nothing (no selection, or several).
    [[nodiscard]] std::optional<EntityId> shown() const
    {
        return m_shown;
    }

protected:
    /// @brief Opens the Add component list on a click into its empty search field.
    /// @param watched The object the event is for.
    /// @param event   The event.
    /// @return Whether it was handled (never: the field still gets the click).
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    /// @brief Runs a command; a failure is logged.
    bool run(const QString& command, const QJsonObject& parameters);
    /// @brief Runs `component.setField` for the shown entity.
    void setField(const QString& component, const QString& field, const QJsonValue& value);
    /// @brief Makes the editors (the shown entity is set).
    void build();
    /// @brief The editor for one field.
    [[nodiscard]] QWidget* fieldEditor(const QString& component, const QJsonObject& field, const QJsonValue& value,
                                       QWidget* parent);

    EditorCommandRegistry& m_commands;
    const SceneEditor& m_editor;
    const EditorWorld& m_world;
    Log::Logger& m_log;
    QWidget* m_content = nullptr;                      ///< Rebuilt by build(); owned by the scroll area.
    QVBoxLayout* m_layout = nullptr;                   ///< m_content's layout.
    std::optional<EntityId> m_shown;                   ///< The entity the editors are for.
    std::uint64_t m_shownRevision = ~std::uint64_t{0}; ///< The world revision they show.
    bool m_built = false;                              ///< build() ran at least once.
    /// @brief Euler angles (degrees) the user typed, by "<entity> <component>.<field>", kept while they still give
    ///        the stored rotation.
    std::map<QString, Math::Vec3> m_typedAngles;
    QSet<QString> m_folded; ///< Component types folded shut, kept across rebuilds.
};
} // namespace Meow2::Editor
