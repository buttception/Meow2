/// @file
/// @brief SceneEditor (SceneEditor.h).

#include "Editing/SceneEditor.h"

#include "Commands/CommandSchema.h"

#include <Meow2/Core/Assert.h>
#include <Meow2/Scene/TransformHierarchy.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <map>
#include <memory>
#include <QJsonArray>
#include <QJsonObject>
#include <set>
#include <string_view>
#include <unordered_map>

namespace Meow2::Editor
{
using namespace Qt::StringLiterals;

namespace
{
constexpr std::string_view c_transformName = "LocalTransform";
constexpr std::string_view c_parentName = "Parent";

QString text(const EntityId id)
{
    return QString::fromStdString(id.uuid().toString());
}

Result<EntityId> parseId(const QString& value)
{
    Result<Uuid> uuid = Uuid::parse(value.toStdString());
    if (!uuid)
    {
        return Unexpected<Error>{uuid.error()};
    }
    return EntityId::fromUuid(*uuid);
}

QJsonObject idParameter(const QString& description)
{
    return propertySchema(u"string"_s, description);
}

std::string nameOf(const Scene::Scene& scene, entt::entity entity)
{
    const auto* name = scene.registry().try_get<Scene::NameComponent>(entity);
    return name != nullptr ? name->name : std::string{};
}

/// @brief `count` numbers from a JSON array, or an error naming the key.
Result<std::array<float, 4>> numbersFrom(const QJsonValue& value, std::size_t count, std::string_view key)
{
    const QJsonArray array = value.toArray();
    if (!value.isArray() || static_cast<std::size_t>(array.size()) != count)
    {
        return fail(ErrorCode::InvalidArgument, std::format("{} needs an array of {} numbers", key, count));
    }
    std::array<float, 4> out{};
    for (std::size_t i = 0; i < count; ++i)
    {
        const QJsonValue element = array.at(static_cast<qsizetype>(i));
        if (!element.isDouble() || !std::isfinite(element.toDouble()))
        {
            return fail(ErrorCode::InvalidArgument, std::format("{}[{}] isn't a number", key, i));
        }
        out[i] = static_cast<float>(element.toDouble());
    }
    return out;
}

/// @brief A transform from `position`, `rotation` (a quaternion) and `scale` keys; missing ones come from `base`.
Result<Math::Transform> transformFromJson(const QJsonObject& object, const Math::Transform& base)
{
    Math::Transform out = base;
    if (object.contains(u"position"))
    {
        Result<std::array<float, 4>> v = numbersFrom(object.value(u"position"), 3, "position");
        if (!v)
        {
            return Unexpected<Error>{v.error()};
        }
        out.position = {(*v)[0], (*v)[1], (*v)[2]};
    }
    if (object.contains(u"rotation"))
    {
        Result<std::array<float, 4>> q = numbersFrom(object.value(u"rotation"), 4, "rotation");
        if (!q)
        {
            return Unexpected<Error>{q.error()};
        }
        const float length = std::sqrt((*q)[0] * (*q)[0] + (*q)[1] * (*q)[1] + (*q)[2] * (*q)[2] + (*q)[3] * (*q)[3]);
        if (length < 1e-6F)
        {
            return fail(ErrorCode::InvalidArgument, "rotation must not be all zeros");
        }
        out.rotation = Math::Quat{(*q)[0] / length, (*q)[1] / length, (*q)[2] / length, (*q)[3] / length};
    }
    if (object.contains(u"scale"))
    {
        Result<std::array<float, 4>> s = numbersFrom(object.value(u"scale"), 3, "scale");
        if (!s)
        {
            return Unexpected<Error>{s.error()};
        }
        out.scale = {(*s)[0], (*s)[1], (*s)[2]};
    }
    return out;
}
} // namespace

QString fieldTypeName(Scene::FieldType type)
{
    switch (type)
    {
        case Scene::FieldType::Bool:
            return u"bool"_s;
        case Scene::FieldType::Int:
            return u"int"_s;
        case Scene::FieldType::Float:
            return u"float"_s;
        case Scene::FieldType::String:
            return u"string"_s;
        case Scene::FieldType::Vec3:
            return u"vec3"_s;
        case Scene::FieldType::Quat:
            return u"quat"_s;
        case Scene::FieldType::Color:
            return u"color"_s;
        case Scene::FieldType::Asset:
            return u"asset"_s;
        case Scene::FieldType::Entity:
            return u"entity"_s;
        case Scene::FieldType::Enum:
            return u"enum"_s;
    }
    return u"?"_s;
}

QJsonValue toJson(const Scene::FieldValue& value)
{
    return std::visit(
        []<class T>(const T& v) -> QJsonValue
        {
            if constexpr (std::is_same_v<T, bool>)
            {
                return v;
            }
            else if constexpr (std::is_same_v<T, std::int64_t>)
            {
                return static_cast<qint64>(v);
            }
            else if constexpr (std::is_same_v<T, double>)
            {
                return v;
            }
            else if constexpr (std::is_same_v<T, std::string>)
            {
                return QString::fromStdString(v);
            }
            else
            {
                QJsonArray numbers;
                for (const double number : v)
                {
                    numbers.append(number);
                }
                return numbers;
            }
        },
        value);
}

Result<Scene::FieldValue> fromJson(const QJsonValue& json, Scene::FieldType type)
{
    using Scene::FieldType;
    switch (type)
    {
        case FieldType::Bool:
            if (json.isBool())
            {
                return Scene::FieldValue{json.toBool()};
            }
            return fail(ErrorCode::InvalidArgument, "expected true or false");
        case FieldType::Int:
        {
            const double number = json.toDouble(std::nan(""));
            if (!json.isDouble() || std::floor(number) != number)
            {
                return fail(ErrorCode::InvalidArgument, "expected a whole number");
            }
            return Scene::FieldValue{static_cast<std::int64_t>(number)};
        }
        case FieldType::Float:
            if (json.isDouble())
            {
                return Scene::FieldValue{json.toDouble()};
            }
            return fail(ErrorCode::InvalidArgument, "expected a number");
        case FieldType::Vec3:
        case FieldType::Quat:
        case FieldType::Color:
        {
            const std::size_t count = type == FieldType::Vec3 ? 3 : 4;
            const QJsonArray array = json.toArray();
            if (!json.isArray() || static_cast<std::size_t>(array.size()) != count)
            {
                return fail(ErrorCode::InvalidArgument, std::format("expected an array of {} numbers", count));
            }
            std::vector<double> numbers;
            for (const QJsonValue& element : array)
            {
                if (!element.isDouble())
                {
                    return fail(ErrorCode::InvalidArgument, std::format("expected an array of {} numbers", count));
                }
                numbers.push_back(element.toDouble());
            }
            return Scene::FieldValue{std::move(numbers)};
        }
        case FieldType::String:
        case FieldType::Asset:
        case FieldType::Entity:
        case FieldType::Enum:
            if (json.isString())
            {
                return Scene::FieldValue{json.toString().toStdString()};
            }
            return fail(ErrorCode::InvalidArgument, "expected text");
    }
    return fail(ErrorCode::InvalidArgument, "unknown field type");
}

SceneEditor::SceneEditor(const SceneEditorDeps& deps)
    : m_deps(deps)
{
    registerCommands();
}

SceneEditor::~SceneEditor()
{
    for (const QString& name : m_commandNames)
    {
        m_deps.commands.remove(name);
    }
}

std::vector<EntityId> SceneEditor::selection() const
{
    std::vector<EntityId> alive;
    for (const EntityId id : m_selection)
    {
        if (find(id) != entt::null)
        {
            alive.push_back(id);
        }
    }
    return alive;
}

entt::entity SceneEditor::find(EntityId id) const
{
    return m_deps.world.scene().find(id).value_or(entt::null);
}

std::vector<entt::entity> SceneEditor::childrenOf(entt::entity entity) const
{
    std::vector<std::pair<EntityId, entt::entity>> children;
    Scene::Scene& scene = m_deps.world.scene();
    for (const auto [child, parent] : scene.registry().view<Scene::Parent>().each())
    {
        if (parent.entity == entity)
        {
            children.emplace_back(scene.idOf(child), child);
        }
    }
    // In ID order (creation order): a stable order for the tree and for snapshots.
    std::ranges::sort(children, {}, &std::pair<EntityId, entt::entity>::first);
    std::vector<entt::entity> result;
    for (const auto& [id, child] : children)
    {
        result.push_back(child);
    }
    return result;
}

std::vector<EntitySnapshot> SceneEditor::snapshot(entt::entity root) const
{
    const Scene::Scene& scene = m_deps.world.scene();
    std::vector<EntitySnapshot> snapshots;
    std::vector<entt::entity> pending{root};
    while (!pending.empty())
    {
        const entt::entity entity = pending.front();
        pending.erase(pending.begin());
        EntitySnapshot shot{.id = scene.idOf(entity), .name = nameOf(scene, entity), .components = {}, .unknown = {}};
        for (const Scene::ComponentDesc& desc : m_deps.components.components())
        {
            if (desc.has(scene.registry(), entity))
            {
                shot.components.emplace_back(desc.name,
                                             Scene::toFieldTable(desc, desc.get(scene.registry(), entity), scene));
            }
        }
        if (const auto* unknown = scene.registry().try_get<Scene::UnknownComponents>(entity))
        {
            shot.unknown = unknown->components;
        }
        snapshots.push_back(std::move(shot));
        // Parents before children (breadth first), so restore() and destroyAll() can rely on the order.
        for (const entt::entity child : childrenOf(entity))
        {
            pending.push_back(child);
        }
    }
    return snapshots;
}

void SceneEditor::restore(const std::vector<EntitySnapshot>& snapshots)
{
    Scene::Scene& scene = m_deps.world.scene();
    Scene::Scene::Registry& registry = scene.registry();
    std::vector<entt::entity> made;
    for (const EntitySnapshot& shot : snapshots)
    {
        Result<entt::entity> entity = scene.createWithId(shot.id, shot.name);
        MEOW2_ASSERT(entity.has_value(), "a snapshot's entity is gone before it's restored");
        made.push_back(*entity);
    }
    // Components, then parents: a Parent names another entity of the snapshot, which must exist with its transform.
    for (const bool parents : {false, true})
    {
        for (std::size_t i = 0; i < snapshots.size(); ++i)
        {
            for (const auto& [name, table] : snapshots[i].components)
            {
                if ((name == c_parentName) != parents)
                {
                    continue;
                }
                const Scene::ComponentDesc* desc = m_deps.components.find(name);
                if (desc == nullptr)
                {
                    continue;
                }
                std::vector<std::string> warnings;
                if (Result<void> set =
                        Scene::fromFieldTable(*desc, desc->getOrEmplace(registry, made[i]), table, scene, warnings);
                    !set)
                {
                    MEOW2_LOG_WARN(m_deps.log, "{}", set.error());
                }
            }
            if (!parents && !snapshots[i].unknown.empty())
            {
                registry.emplace_or_replace<Scene::UnknownComponents>(made[i],
                                                                      Scene::UnknownComponents{snapshots[i].unknown});
            }
        }
    }
    for (const entt::entity entity : made)
    {
        if (registry.all_of<Scene::LocalTransform>(entity))
        {
            m_deps.world.hierarchy().attach(entity);
        }
    }
    m_deps.world.markChanged();
}

void SceneEditor::destroyAll(const std::vector<EntitySnapshot>& snapshots)
{
    // Children first: the hierarchy would otherwise turn them into roots for a moment.
    for (auto shot = snapshots.rbegin(); shot != snapshots.rend(); ++shot)
    {
        if (const entt::entity entity = find(shot->id); entity != entt::null)
        {
            m_deps.world.scene().destroy(entity);
        }
    }
    m_deps.world.markChanged();
}

void SceneEditor::addComponent(const Scene::ComponentDesc& desc, entt::entity entity)
{
    if (desc.name == c_transformName)
    {
        m_deps.world.hierarchy().add(entity); // with the world transform and state the hierarchy keeps
    }
    else
    {
        static_cast<void>(desc.getOrEmplace(m_deps.world.scene().registry(), entity));
    }
    m_deps.world.markChanged();
}

void SceneEditor::removeComponent(const Scene::ComponentDesc& desc, entt::entity entity)
{
    Scene::Scene::Registry& registry = m_deps.world.scene().registry();
    desc.remove(registry, entity);
    if (desc.name == c_transformName)
    {
        registry.remove<Scene::WorldTransform, Scene::PreviousWorldTransform, Scene::TransformState>(entity);
    }
    m_deps.world.markChanged();
}

Result<entt::entity> SceneEditor::entityParameter(const QJsonObject& parameters, const QString& key) const
{
    Result<EntityId> id = parseId(parameters.value(key).toString());
    if (!id)
    {
        return fail(ErrorCode::InvalidArgument,
                    std::format("{}: not an entity ID ({})", key.toStdString(), id.error().message));
    }
    const entt::entity entity = find(*id);
    if (entity == entt::null)
    {
        return fail(ErrorCode::NotFound, std::format("no entity {} in the scene", id->uuid().toString()));
    }
    return entity;
}

Result<const Scene::ComponentDesc*> SceneEditor::componentParameter(const QJsonObject& parameters) const
{
    const std::string name = parameters.value(u"component").toString().toStdString();
    const Scene::ComponentDesc* desc = m_deps.components.find(name);
    if (desc == nullptr)
    {
        return fail(ErrorCode::NotFound, std::format("no component '{}' (component.list lists them)", name));
    }
    return desc;
}

QJsonObject SceneEditor::describeTree(entt::entity entity) const
{
    QJsonArray children;
    for (const entt::entity child : childrenOf(entity))
    {
        children.append(describeTree(child));
    }
    const Scene::Scene& scene = m_deps.world.scene();
    return {{u"id"_s, text(scene.idOf(entity))},
            {u"name"_s, QString::fromStdString(nameOf(scene, entity))},
            {u"children"_s, children}};
}

QJsonObject SceneEditor::describeEntity(entt::entity entity) const
{
    const Scene::Scene& scene = m_deps.world.scene();
    QJsonArray components;
    for (const Scene::ComponentDesc& desc : m_deps.components.components())
    {
        if (!desc.has(scene.registry(), entity))
        {
            continue;
        }
        QJsonObject fields;
        for (const Scene::FieldDesc& field : desc.fields)
        {
            fields.insert(QString::fromStdString(field.name),
                          toJson(field.get(desc.get(scene.registry(), entity), scene)));
        }
        components.append(QJsonObject{{u"name"_s, QString::fromStdString(desc.name)}, {u"fields"_s, fields}});
    }
    QJsonArray unknown;
    if (const auto* kept = scene.registry().try_get<Scene::UnknownComponents>(entity))
    {
        for (const Scene::UnknownComponent& component : kept->components)
        {
            unknown.append(QString::fromStdString(component.name));
        }
    }
    const auto* parent = scene.registry().try_get<Scene::Parent>(entity);
    return {{u"id"_s, text(scene.idOf(entity))},
            {u"name"_s, QString::fromStdString(nameOf(scene, entity))},
            {u"parent"_s,
             parent != nullptr && scene.contains(parent->entity) ? text(scene.idOf(parent->entity)) : QString{}},
            {u"components"_s, components},
            {u"unknown"_s, unknown}};
}

void SceneEditor::registerCommands()
{
    const auto add = [this](CommandInfo info, CommandHandler handler)
    {
        m_commandNames.push_back(info.name);
        [[maybe_unused]] const Result<void> added = m_deps.commands.add(std::move(info), std::move(handler));
        MEOW2_ASSERT(added, "edit commands are registered once, by the one scene editor");
    };
    const auto idSchema = [](const QString& description, bool required)
    {
        return SchemaProperty{.name = u"id"_s, .schema = idParameter(description), .required = required};
    };

    add({.name = u"entity.create"_s,
         .description = u"Creates an entity with a transform, at the root or under a parent. Returns its ID."_s,
         .parameters = objectSchema(
             {{.name = u"name"_s, .schema = propertySchema(u"string"_s, u"Its name."_s), .required = false},
              {.name = u"parent"_s,
               .schema = idParameter(u"The parent's ID; empty or left out for the root."_s),
               .required = false}})},
        [this](const QJsonObject& parameters) -> Result<CommandOutcome>
        {
            entt::entity parent = entt::null;
            if (!parameters.value(u"parent").toString().isEmpty())
            {
                Result<entt::entity> found = entityParameter(parameters, u"parent"_s);
                if (!found)
                {
                    return Unexpected<Error>{found.error()};
                }
                if (!m_deps.world.scene().registry().all_of<Scene::LocalTransform>(*found))
                {
                    return fail(ErrorCode::InvalidArgument, "the parent has no transform");
                }
                parent = *found;
            }
            Scene::Scene& scene = m_deps.world.scene();
            const std::string name = parameters.value(u"name").toString(u"Entity"_s).toStdString();
            const entt::entity entity = scene.create(name);
            m_deps.world.hierarchy().add(entity, {}, parent);
            m_deps.world.markChanged();
            auto made = std::make_shared<std::vector<EntitySnapshot>>(snapshot(entity));
            const EntityId id = scene.idOf(entity);
            return CommandOutcome{.result = QJsonObject{{u"id"_s, text(id)}},
                                  .undoStep = UndoStep{.label = u"Create "_s + QString::fromStdString(name),
                                                       .undo = [this, made] { destroyAll(*made); },
                                                       .redo =
                                                           [this, made]
                                                       {
                                                           restore(*made);
                                                       }}};
        });

    add({.name = u"entity.delete"_s,
         .description = u"Deletes an entity and every entity under it."_s,
         .parameters = objectSchema({idSchema(u"The entity's ID."_s, true)})},
        [this](const QJsonObject& parameters) -> Result<CommandOutcome>
        {
            Result<entt::entity> entity = entityParameter(parameters, u"id"_s);
            if (!entity)
            {
                return Unexpected<Error>{entity.error()};
            }
            auto removed = std::make_shared<std::vector<EntitySnapshot>>(snapshot(*entity));
            const QString name = QString::fromStdString(removed->front().name);
            destroyAll(*removed);
            return CommandOutcome{.result = {},
                                  .undoStep = UndoStep{.label = u"Delete "_s + name,
                                                       .undo = [this, removed] { restore(*removed); },
                                                       .redo =
                                                           [this, removed]
                                                       {
                                                           destroyAll(*removed);
                                                       }}};
        });

    add({.name = u"entity.duplicate"_s,
         .description = u"Copies an entity and every entity under it, with new IDs, under the same parent. Returns "
                        u"the copy's ID."_s,
         .parameters = objectSchema({idSchema(u"The entity's ID."_s, true)})},
        [this](const QJsonObject& parameters) -> Result<CommandOutcome>
        {
            Result<entt::entity> entity = entityParameter(parameters, u"id"_s);
            if (!entity)
            {
                return Unexpected<Error>{entity.error()};
            }
            auto copy = std::make_shared<std::vector<EntitySnapshot>>(snapshot(*entity));
            // New IDs, and every reference to an entity of the copy (its children's parents) pointed at the new one.
            std::unordered_map<std::string, std::string> renamed;
            for (EntitySnapshot& shot : *copy)
            {
                const EntityId id = m_deps.world.newEntityId();
                renamed.emplace(shot.id.uuid().toString(), id.uuid().toString());
                shot.id = id;
            }
            for (EntitySnapshot& shot : *copy)
            {
                for (auto& [name, table] : shot.components)
                {
                    for (auto& [key, value] : table)
                    {
                        if (const std::string* reference = std::get_if<std::string>(&value))
                        {
                            if (const auto found = renamed.find(*reference); found != renamed.end())
                            {
                                value = found->second;
                            }
                        }
                    }
                }
            }
            restore(*copy);
            return CommandOutcome{.result = QJsonObject{{u"id"_s, text(copy->front().id)}},
                                  .undoStep =
                                      UndoStep{.label = u"Duplicate "_s + QString::fromStdString(copy->front().name),
                                               .undo = [this, copy] { destroyAll(*copy); },
                                               .redo =
                                                   [this, copy]
                                               {
                                                   restore(*copy);
                                               }}};
        });

    add({.name = u"entity.rename"_s,
         .description = u"Renames an entity."_s,
         .parameters = objectSchema(
             {idSchema(u"The entity's ID."_s, true),
              {.name = u"name"_s, .schema = propertySchema(u"string"_s, u"The new name."_s), .required = true}})},
        [this](const QJsonObject& parameters) -> Result<CommandOutcome>
        {
            Result<entt::entity> entity = entityParameter(parameters, u"id"_s);
            if (!entity)
            {
                return Unexpected<Error>{entity.error()};
            }
            Scene::Scene& scene = m_deps.world.scene();
            const EntityId id = scene.idOf(*entity);
            const std::string before = nameOf(scene, *entity);
            const std::string after = parameters.value(u"name").toString().toStdString();
            const auto setName = [this, id](const std::string& name)
            {
                if (const entt::entity target = find(id); target != entt::null)
                {
                    m_deps.world.scene().registry().emplace_or_replace<Scene::NameComponent>(
                        target, Scene::NameComponent{name});
                    m_deps.world.markChanged();
                }
            };
            setName(after);
            return CommandOutcome{.result = {},
                                  .undoStep = UndoStep{.label = u"Rename "_s + QString::fromStdString(before),
                                                       .undo = [setName, before] { setName(before); },
                                                       .redo =
                                                           [setName, after]
                                                       {
                                                           setName(after);
                                                       }}};
        });

    // The gizmos' command: one undo step per drag. While dragging, `preview` steps move the entity without touching
    // the history; the last step names the transform the drag began from in `from`, so undo goes back there at once.
    const auto transformSchema = [](const QString& description)
    {
        QJsonObject schema = objectSchema(
            {{.name = u"position"_s, .schema = propertySchema(u"array"_s, u"Local position: 3 numbers (metres)."_s)},
             {.name = u"rotation"_s,
              .schema = propertySchema(u"array"_s, u"Local rotation: a quaternion, 4 numbers (x, y, z, w)."_s)},
             {.name = u"scale"_s, .schema = propertySchema(u"array"_s, u"Local scale: 3 numbers."_s)}});
        schema.insert(u"description", description);
        return schema;
    };
    add({.name = u"entity.setTransform"_s,
         .description = u"Sets an entity's local transform (any of position, rotation, scale; the rest is kept). "
                        u"preview: apply without recording an undo step (while dragging). from: the transform undo "
                        u"goes back to (where a drag began); by default the one before this call."_s,
         .parameters = objectSchema(
             {idSchema(u"The entity's ID."_s, true),
              {.name = u"position"_s, .schema = propertySchema(u"array"_s, u"Local position: 3 numbers (metres)."_s)},
              {.name = u"rotation"_s,
               .schema = propertySchema(u"array"_s, u"Local rotation: a quaternion, 4 numbers (x, y, z, w)."_s)},
              {.name = u"scale"_s, .schema = propertySchema(u"array"_s, u"Local scale: 3 numbers."_s)},
              {.name = u"preview"_s, .schema = propertySchema(u"boolean"_s, u"Apply without an undo step."_s)},
              {.name = u"from"_s, .schema = transformSchema(u"What undo restores."_s)}})},
        [this](const QJsonObject& parameters) -> Result<CommandOutcome>
        {
            Result<entt::entity> entity = entityParameter(parameters, u"id"_s);
            if (!entity)
            {
                return Unexpected<Error>{entity.error()};
            }
            Scene::Scene& scene = m_deps.world.scene();
            const auto* local = scene.registry().try_get<Scene::LocalTransform>(*entity);
            if (local == nullptr)
            {
                return fail(ErrorCode::NotFound, "the entity has no LocalTransform");
            }
            const Math::Transform before = local->value;
            Result<Math::Transform> after = transformFromJson(parameters, before);
            if (!after)
            {
                return Unexpected<Error>{after.error()};
            }
            Result<Math::Transform> from = parameters.contains(u"from")
                                               ? transformFromJson(parameters.value(u"from").toObject(), before)
                                               : Result<Math::Transform>{before};
            if (!from)
            {
                return fail(ErrorCode::InvalidArgument, "from: " + from.error().message);
            }
            const EntityId id = scene.idOf(*entity);
            const auto setTransform = [this, id](const Math::Transform& to)
            {
                const entt::entity target = find(id);
                if (target == entt::null)
                {
                    return;
                }
                if (auto* transform = m_deps.world.scene().registry().try_get<Scene::LocalTransform>(target))
                {
                    transform->value = to;
                    m_deps.world.hierarchy().markDirty(target); // the world transform follows at the next update
                    m_deps.world.markChanged();
                }
            };
            setTransform(*after);
            if (parameters.value(u"preview").toBool())
            {
                return CommandOutcome{};
            }
            return CommandOutcome{.result = {},
                                  .undoStep =
                                      UndoStep{.label = u"Move "_s + QString::fromStdString(nameOf(scene, *entity)),
                                               .undo = [setTransform, start = *from] { setTransform(start); },
                                               .redo =
                                                   [setTransform, end = *after]
                                               {
                                                   setTransform(end);
                                               }}};
        });

    add({.name = u"entity.setParent"_s,
         .description = u"Hangs an entity under another, or at the root; its local transform is kept."_s,
         .parameters = objectSchema({idSchema(u"The entity's ID."_s, true),
                                     {.name = u"parent"_s,
                                      .schema = idParameter(u"The new parent's ID; empty for the root."_s),
                                      .required = true}})},
        [this](const QJsonObject& parameters) -> Result<CommandOutcome>
        {
            Result<entt::entity> entity = entityParameter(parameters, u"id"_s);
            if (!entity)
            {
                return Unexpected<Error>{entity.error()};
            }
            Scene::Scene& scene = m_deps.world.scene();
            Scene::Scene::Registry& registry = scene.registry();
            if (!registry.all_of<Scene::LocalTransform>(*entity))
            {
                return fail(ErrorCode::InvalidArgument, "the entity has no transform");
            }
            entt::entity parent = entt::null;
            if (!parameters.value(u"parent").toString().isEmpty())
            {
                Result<entt::entity> found = entityParameter(parameters, u"parent"_s);
                if (!found)
                {
                    return Unexpected<Error>{found.error()};
                }
                if (!registry.all_of<Scene::LocalTransform>(*found))
                {
                    return fail(ErrorCode::InvalidArgument, "the new parent has no transform");
                }
                // The hierarchy asserts on cycles; a command reports them instead.
                for (entt::entity up = *found; up != entt::null;)
                {
                    if (up == *entity)
                    {
                        return fail(ErrorCode::InvalidArgument, "an entity can't hang under itself or its children");
                    }
                    const auto* next = registry.try_get<Scene::Parent>(up);
                    up = next != nullptr ? next->entity : entt::null;
                }
                parent = *found;
            }
            const auto* old = registry.try_get<Scene::Parent>(*entity);
            const EntityId id = scene.idOf(*entity);
            const EntityId before = old != nullptr ? scene.idOf(old->entity) : EntityId{};
            const EntityId after = parent != entt::null ? scene.idOf(parent) : EntityId{};
            const auto setParent = [this, id](EntityId target)
            {
                const entt::entity moved = find(id);
                if (moved != entt::null)
                {
                    m_deps.world.hierarchy().setParent(moved, target.isValid() ? find(target) : entt::null);
                    m_deps.world.markChanged();
                }
            };
            setParent(after);
            return CommandOutcome{.result = {},
                                  .undoStep =
                                      UndoStep{.label = u"Move "_s + QString::fromStdString(nameOf(scene, *entity)),
                                               .undo = [setParent, before] { setParent(before); },
                                               .redo =
                                                   [setParent, after]
                                               {
                                                   setParent(after);
                                               }}};
        });

    const SchemaProperty componentSchema{
        .name = u"component"_s,
        .schema = propertySchema(u"string"_s, u"The component's name, e.g. LocalTransform (component.list)."_s),
        .required = true};

    add({.name = u"component.add"_s,
         .description = u"Adds a component with its default values to an entity."_s,
         .parameters = objectSchema({idSchema(u"The entity's ID."_s, true), componentSchema})},
        [this](const QJsonObject& parameters) -> Result<CommandOutcome>
        {
            Result<entt::entity> entity = entityParameter(parameters, u"id"_s);
            Result<const Scene::ComponentDesc*> desc = componentParameter(parameters);
            if (!entity || !desc)
            {
                return Unexpected<Error>{!entity ? entity.error() : desc.error()};
            }
            if ((*desc)->name == c_parentName)
            {
                return fail(ErrorCode::InvalidArgument, "use entity.setParent to give an entity a parent");
            }
            if ((*desc)->has(m_deps.world.scene().registry(), *entity))
            {
                return fail(ErrorCode::AlreadyExists, std::format("the entity has a {} already", (*desc)->name));
            }
            const EntityId id = m_deps.world.scene().idOf(*entity);
            const Scene::ComponentDesc* component = *desc;
            addComponent(*component, *entity);
            return CommandOutcome{.result = {},
                                  .undoStep =
                                      UndoStep{.label = u"Add "_s + QString::fromStdString(component->name),
                                               .undo =
                                                   [this, id, component]
                                               {
                                                   if (const entt::entity target = find(id); target != entt::null)
                                                   {
                                                       removeComponent(*component, target);
                                                   }
                                               },
                                               .redo =
                                                   [this, id, component]
                                               {
                                                   if (const entt::entity target = find(id); target != entt::null)
                                                   {
                                                       addComponent(*component, target);
                                                   }
                                               }}};
        });

    add({.name = u"component.remove"_s,
         .description = u"Removes a component from an entity."_s,
         .parameters = objectSchema({idSchema(u"The entity's ID."_s, true), componentSchema})},
        [this](const QJsonObject& parameters) -> Result<CommandOutcome>
        {
            Result<entt::entity> entity = entityParameter(parameters, u"id"_s);
            Result<const Scene::ComponentDesc*> desc = componentParameter(parameters);
            if (!entity || !desc)
            {
                return Unexpected<Error>{!entity ? entity.error() : desc.error()};
            }
            Scene::Scene& scene = m_deps.world.scene();
            const Scene::ComponentDesc* component = *desc;
            if (!component->has(scene.registry(), *entity))
            {
                return fail(ErrorCode::NotFound, std::format("the entity has no {}", component->name));
            }
            if (component->name == c_transformName &&
                (scene.registry().all_of<Scene::Parent>(*entity) || !childrenOf(*entity).empty()))
            {
                return fail(
                    ErrorCode::InvalidArgument,
                    "a transform with a parent or children can't be removed; move the entity to the root first");
            }
            const EntityId id = scene.idOf(*entity);
            const Scene::FieldTable fields =
                Scene::toFieldTable(*component, component->get(scene.registry(), *entity), scene);
            removeComponent(*component, *entity);
            return CommandOutcome{.result = {},
                                  .undoStep =
                                      UndoStep{.label = u"Remove "_s + QString::fromStdString(component->name),
                                               .undo =
                                                   [this, id, component, fields]
                                               {
                                                   const entt::entity target = find(id);
                                                   if (target == entt::null)
                                                   {
                                                       return;
                                                   }
                                                   addComponent(*component, target);
                                                   std::vector<std::string> warnings;
                                                   Scene::Scene& current = m_deps.world.scene();
                                                   static_cast<void>(Scene::fromFieldTable(
                                                       *component, component->getOrEmplace(current.registry(), target),
                                                       fields, current, warnings));
                                                   if (component->name == c_transformName)
                                                   {
                                                       m_deps.world.hierarchy().markDirty(target);
                                                   }
                                               },
                                               .redo =
                                                   [this, id, component]
                                               {
                                                   if (const entt::entity target = find(id); target != entt::null)
                                                   {
                                                       removeComponent(*component, target);
                                                   }
                                               }}};
        });

    add({.name = u"component.setField"_s,
         .description = u"Sets one field of an entity's component; the value's shape follows the field's type "
                        u"(component.list): a number, true/false, text, or an array of 3 or 4 numbers."_s,
         .parameters = objectSchema(
             {idSchema(u"The entity's ID."_s, true),
              componentSchema,
              {.name = u"field"_s, .schema = propertySchema(u"string"_s, u"The field's name."_s), .required = true},
              {.name = u"value"_s, .schema = propertySchema(QString{}, u"The new value."_s), .required = true}})},
        [this](const QJsonObject& parameters) -> Result<CommandOutcome>
        {
            Result<entt::entity> entity = entityParameter(parameters, u"id"_s);
            Result<const Scene::ComponentDesc*> desc = componentParameter(parameters);
            if (!entity || !desc)
            {
                return Unexpected<Error>{!entity ? entity.error() : desc.error()};
            }
            const Scene::ComponentDesc* component = *desc;
            const std::string fieldName = parameters.value(u"field").toString().toStdString();
            const auto field = std::ranges::find_if(component->fields, [&fieldName](const Scene::FieldDesc& candidate)
                                                    { return candidate.name == fieldName; });
            if (field == component->fields.end())
            {
                return fail(ErrorCode::NotFound, std::format("{} has no field '{}'", component->name, fieldName));
            }
            Scene::Scene& scene = m_deps.world.scene();
            if (!component->has(scene.registry(), *entity))
            {
                return fail(ErrorCode::NotFound, std::format("the entity has no {}", component->name));
            }
            Result<Scene::FieldValue> value = fromJson(parameters.value(u"value"), field->type);
            if (!value)
            {
                return fail(ErrorCode::InvalidArgument,
                            std::format("{}.{}: {}", component->name, fieldName, value.error().message));
            }
            const Scene::FieldDesc* fieldDesc = &*field;
            const Scene::FieldValue before = fieldDesc->get(component->get(scene.registry(), *entity), scene);
            const EntityId id = scene.idOf(*entity);
            const auto setValue = [this, id, component, fieldDesc](const Scene::FieldValue& to) -> Result<void>
            {
                const entt::entity target = find(id);
                if (target == entt::null || !component->has(m_deps.world.scene().registry(), target))
                {
                    return fail(ErrorCode::NotFound, "the entity or its component is gone");
                }
                Result<void> set = fieldDesc->set(component->getOrEmplace(m_deps.world.scene().registry(), target), to,
                                                  m_deps.world.scene());
                if (set && component->name == c_transformName)
                {
                    m_deps.world.hierarchy().markDirty(target); // the world transform follows at the next update
                }
                m_deps.world.markChanged();
                return set;
            };
            if (Result<void> set = setValue(*value); !set)
            {
                return fail(ErrorCode::InvalidArgument,
                            std::format("{}.{}: {}", component->name, fieldName, set.error().message));
            }
            const Scene::FieldValue after = *value;
            return CommandOutcome{
                .result = {},
                .undoStep =
                    UndoStep{.label = QString::fromStdString(std::format("Set {}.{}", component->name, fieldName)),
                             .undo = [setValue, before] { static_cast<void>(setValue(before)); },
                             .redo =
                                 [setValue, after]
                             {
                                 static_cast<void>(setValue(after));
                             }}};
        });

    add({.name = u"selection.set"_s,
         .description = u"Selects entities by ID (an empty list selects nothing). Not recorded in the history."_s,
         .parameters = objectSchema(
             {{.name = u"ids"_s, .schema = propertySchema(u"array"_s, u"The entities' IDs."_s), .required = true}})},
        [this](const QJsonObject& parameters) -> Result<CommandOutcome>
        {
            std::vector<EntityId> ids;
            for (const QJsonValue& value : parameters.value(u"ids").toArray())
            {
                Result<EntityId> id = parseId(value.toString());
                if (!id || find(*id) == entt::null)
                {
                    return fail(ErrorCode::NotFound,
                                std::format("no entity {} in the scene", value.toString().toStdString()));
                }
                ids.push_back(*id);
            }
            m_selection = std::move(ids);
            return CommandOutcome{};
        });

    add({.name = u"selection.get"_s, .description = u"The selected entities' IDs."_s, .kind = CommandKind::Query},
        [this](const QJsonObject&) -> Result<CommandOutcome>
        {
            QJsonArray ids;
            for (const EntityId id : selection())
            {
                ids.append(text(id));
            }
            return CommandOutcome{.result = QJsonObject{{u"ids"_s, ids}}};
        });

    add({.name = u"scene.hierarchy"_s,
         .description = u"The scene's saved entities as a tree: each with its ID, name and children."_s,
         .kind = CommandKind::Query},
        [this](const QJsonObject&) -> Result<CommandOutcome>
        {
            Scene::Scene& scene = m_deps.world.scene();
            std::vector<std::pair<EntityId, entt::entity>> roots;
            for (const auto [entity, id] : scene.registry().view<Scene::IdComponent>().each())
            {
                const auto* parent = scene.registry().try_get<Scene::Parent>(entity);
                if (parent == nullptr || !scene.contains(parent->entity))
                {
                    roots.emplace_back(id.id, entity);
                }
            }
            std::ranges::sort(roots, {}, &std::pair<EntityId, entt::entity>::first);
            QJsonArray tree;
            for (const auto& [id, entity] : roots)
            {
                tree.append(describeTree(entity));
            }
            return CommandOutcome{.result = QJsonObject{{u"entities"_s, tree}}};
        });

    add({.name = u"entity.get"_s,
         .description = u"An entity's name, parent and components with every field's value."_s,
         .kind = CommandKind::Query,
         .parameters = objectSchema({idSchema(u"The entity's ID."_s, true)})},
        [this](const QJsonObject& parameters) -> Result<CommandOutcome>
        {
            Result<entt::entity> entity = entityParameter(parameters, u"id"_s);
            if (!entity)
            {
                return Unexpected<Error>{entity.error()};
            }
            return CommandOutcome{.result = describeEntity(*entity)};
        });

    add({.name = u"component.list"_s,
         .description = u"Every component the editor knows: its fields with their types (and an enum's values)."_s,
         .kind = CommandKind::Query},
        [this](const QJsonObject&) -> Result<CommandOutcome>
        {
            QJsonArray list;
            for (const Scene::ComponentDesc& desc : m_deps.components.components())
            {
                QJsonArray fields;
                for (const Scene::FieldDesc& field : desc.fields)
                {
                    QJsonObject entry{{u"name"_s, QString::fromStdString(field.name)},
                                      {u"type"_s, fieldTypeName(field.type)}};
                    if (!field.enumNames.empty())
                    {
                        QJsonArray values;
                        for (const std::string& value : field.enumNames)
                        {
                            values.append(QString::fromStdString(value));
                        }
                        entry.insert(u"values"_s, values);
                    }
                    fields.append(entry);
                }
                list.append(QJsonObject{{u"name"_s, QString::fromStdString(desc.name)},
                                        {u"version"_s, static_cast<qint64>(desc.version)},
                                        {u"fields"_s, fields}});
            }
            return CommandOutcome{.result = QJsonObject{{u"components"_s, list}}};
        });
}
} // namespace Meow2::Editor
