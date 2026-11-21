#pragma once

/// @file
/// @brief SceneEditor: the commands that edit a scene's entities and components, the selection, and the queries the
///        Hierarchy and Inspector docks (and MCP) read the scene through.

#include "Commands/EditorCommandRegistry.h"
#include "Viewports/EditorWorld.h"

#include <Meow2/Core/Id/Uuid.h>
#include <Meow2/Core/Log/Logger.h>
#include <Meow2/Core/Result.h>
#include <Meow2/Scene/ComponentRegistry.h>

#include <QJsonValue>
#include <QString>
#include <string>
#include <utility>
#include <vector>

namespace Meow2::Editor
{
/// @brief What a SceneEditor uses. Plain data; everything must outlive it.
struct SceneEditorDeps
{
    EditorCommandRegistry& commands;            ///< Where its commands are registered.
    EditorWorld& world;                         ///< The scene being edited.
    const Scene::ComponentRegistry& components; ///< What components exist and how their fields read and write.
    Log::Logger& log;                           ///< What went wrong.
};

/// @brief One saved entity as it was: enough to make it again with the same ID (undoing a delete) or with a new one
///        (a duplicate). Plain data.
struct EntitySnapshot
{
    EntityId id;                                                       ///< Its ID.
    std::string name;                                                  ///< Its name (may be empty).
    std::vector<std::pair<std::string, Scene::FieldTable>> components; ///< Registered components, by name.
    std::vector<Scene::UnknownComponent> unknown;                      ///< Components kept from a scene file.
};

/// @brief The type name a FieldType has in queries and schemas ("vec3", "enum", …).
/// @param type The type.
/// @return Its name.
[[nodiscard]] QString fieldTypeName(Scene::FieldType type);

/// @brief A field value as JSON: numbers, text, true/false, or an array of numbers.
/// @param value The value.
/// @return It as JSON.
[[nodiscard]] QJsonValue toJson(const Scene::FieldValue& value);

/// @brief JSON as a field value of a type (a whole number for Int, an array of 3 or 4 numbers for vectors, …).
/// @param json The value.
/// @param type The field's type.
/// @return The value, or an error saying what was expected.
[[nodiscard]] Result<Scene::FieldValue> fromJson(const QJsonValue& json, Scene::FieldType type);

/// @brief Edits the scene through commands, all undoable but the selection and the queries:
///
/// - `entity.create {name, parent}`: a new entity with a transform, under `parent` or at the root; returns its ID.
/// - `entity.delete {id}`: deletes an entity and everything under it.
/// - `entity.duplicate {id}`: copies an entity and everything under it, with new IDs, under the same parent.
/// - `entity.rename {id, name}`: renames an entity.
/// - `entity.setParent {id, parent}`: hangs an entity under another ("" for the root); its local transform is kept.
/// - `entity.setTransform {id, position, rotation, scale, preview, from}`: sets the local transform (the gizmos'
///   command); `preview` steps aren't recorded, and undo goes back to `from`.
/// - `component.add {id, component}`, `component.remove {id, component}`: adds (with default values) or removes one.
/// - `component.setField {id, component, field, value}`: sets one field.
/// - `selection.set {ids}`: selects entities (not recorded in the history).
/// - Queries: `selection.get`, `scene.hierarchy`, `entity.get`, `component.list`.
///
/// Entities are named by EntityId text. Undo brings back the same IDs, components and children (EntitySnapshot), so
/// steps further back in the history still find their entities.
/// @par Thread safety
/// Not thread-safe; the GUI thread.
class SceneEditor
{
public:
    /// @brief Registers the commands.
    /// @param deps What it uses.
    explicit SceneEditor(const SceneEditorDeps& deps);

    /// @brief Removes its commands.
    ~SceneEditor();

    SceneEditor(const SceneEditor&) = delete;            ///< Not copyable: registers commands.
    SceneEditor& operator=(const SceneEditor&) = delete; ///< Not copyable: registers commands.

    /// @brief The selected entities that still exist, in the order selected.
    /// @return Their IDs.
    [[nodiscard]] std::vector<EntityId> selection() const;

    /// @brief The entity an ID names in the current scene.
    /// @param id The ID.
    /// @return The entity, or null if there's none.
    [[nodiscard]] entt::entity find(EntityId id) const;

    /// @brief An entity's snapshot and every entity under it, parents before children.
    /// @param root The entity.
    /// @return The snapshots.
    [[nodiscard]] std::vector<EntitySnapshot> snapshot(entt::entity root) const;

    /// @brief Makes entities from snapshots: all of them first, then their components (parents last, so references
    ///        between them resolve), then attaches transforms to the hierarchy.
    /// @param snapshots From snapshot(), parents before children.
    void restore(const std::vector<EntitySnapshot>& snapshots);

private:
    [[nodiscard]] Result<entt::entity> entityParameter(const QJsonObject& parameters, const QString& key) const;
    [[nodiscard]] Result<const Scene::ComponentDesc*> componentParameter(const QJsonObject& parameters) const;
    [[nodiscard]] std::vector<entt::entity> childrenOf(entt::entity entity) const;
    [[nodiscard]] QJsonObject describeTree(entt::entity entity) const;
    [[nodiscard]] QJsonObject describeEntity(entt::entity entity) const;
    /// @brief Destroys entities by ID, last first (children before parents).
    void destroyAll(const std::vector<EntitySnapshot>& snapshots);
    /// @brief Adds a component with defaults; the transform goes through the hierarchy.
    void addComponent(const Scene::ComponentDesc& desc, entt::entity entity);
    /// @brief Removes a component; the transform takes the hierarchy's state with it.
    void removeComponent(const Scene::ComponentDesc& desc, entt::entity entity);
    void registerCommands();

    SceneEditorDeps m_deps;
    std::vector<EntityId> m_selection;   ///< selection.set's IDs.
    std::vector<QString> m_commandNames; ///< What it registered.
};
} // namespace Meow2::Editor
