/// @file
/// @brief Scene files (SceneFile.h): our own deterministic TOML writer, and reading through toml++.

#include <Meow2/Assets/SceneFile.h>
#include <Meow2/Platform/FileSystem/FileSystem.h>

#include <toml++/toml.h>

#include <algorithm>
#include <cmath>
#include <format>
#include <limits>
#include <optional>
#include <ranges>
#include <set>
#include <unordered_map>
#include <unordered_set>

namespace Meow2::Assets
{
namespace
{
constexpr std::string_view c_formatName = "meow2.scene";

// ---- Writing ----------------------------------------------------------------------------------------------------

/// @brief A TOML basic string: quotes, backslashes and control characters escaped; other UTF-8 kept as it is.
void appendString(std::string& out, std::string_view text)
{
    out += '"';
    for (const char c : text)
    {
        switch (c)
        {
            case '"':
                out += "\\\"";
                break;
            case '\\':
                out += "\\\\";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            default:
                if (static_cast<unsigned char>(c) < 0x20 || c == 0x7F)
                {
                    out += std::format("\\u{:04X}", static_cast<unsigned>(static_cast<unsigned char>(c)));
                }
                else
                {
                    out += c;
                }
        }
    }
    out += '"';
}

/// @brief A TOML float: the shortest text that reads back as the same double, always with a point or exponent so it
///        stays a float ("1.0", not "1").
void appendNumber(std::string& out, double value)
{
    if (std::isnan(value))
    {
        out += "nan";
        return;
    }
    if (std::isinf(value))
    {
        out += value < 0.0 ? "-inf" : "inf";
        return;
    }
    const std::string text = std::format("{}", value);
    out += text;
    if (text.find_first_of(".eE") == std::string::npos)
    {
        out += ".0";
    }
}

void appendValue(std::string& out, const Scene::FieldValue& value)
{
    std::visit(
        [&out]<class T>(const T& v)
        {
            if constexpr (std::is_same_v<T, bool>)
            {
                out += v ? "true" : "false";
            }
            else if constexpr (std::is_same_v<T, std::int64_t>)
            {
                out += std::format("{}", v);
            }
            else if constexpr (std::is_same_v<T, double>)
            {
                appendNumber(out, v);
            }
            else if constexpr (std::is_same_v<T, std::string>)
            {
                appendString(out, v);
            }
            else
            {
                out += '[';
                for (std::size_t i = 0; i < v.size(); ++i)
                {
                    out += i == 0 ? "" : ", ";
                    appendNumber(out, v[i]);
                }
                out += ']';
            }
        },
        value);
}

/// @brief A bare TOML key when it can be one (letters, digits, '_' and '-'), else a quoted one.
void appendKey(std::string& out, std::string_view key)
{
    const bool bare = !key.empty() && std::ranges::all_of(key,
                                                          [](char c)
                                                          {
                                                              return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                                                                     (c >= '0' && c <= '9') || c == '_' || c == '-';
                                                          });
    if (bare)
    {
        out += key;
    }
    else
    {
        appendString(out, key);
    }
}

/// @brief One component of one entity, ready to write: its fields in writing order.
struct WrittenComponent
{
    std::string name;
    std::vector<std::pair<std::string, Scene::FieldValue>> fields;
};

// ---- Reading ----------------------------------------------------------------------------------------------------

/// @brief A TOML value as a field value; nothing for shapes components never have (tables, mixed arrays, dates).
std::optional<Scene::FieldValue> toFieldValue(const toml::node& node)
{
    if (const auto* flag = node.as_boolean())
    {
        return Scene::FieldValue{flag->get()};
    }
    if (const auto* integer = node.as_integer())
    {
        return Scene::FieldValue{integer->get()};
    }
    if (const auto* number = node.as_floating_point())
    {
        return Scene::FieldValue{number->get()};
    }
    if (const auto* text = node.as_string())
    {
        return Scene::FieldValue{text->get()};
    }
    if (const auto* array = node.as_array())
    {
        std::vector<double> numbers;
        numbers.reserve(array->size());
        for (const toml::node& element : *array)
        {
            if (const auto* number = element.as_floating_point())
            {
                numbers.push_back(number->get());
            }
            else if (const auto* integer = element.as_integer())
            {
                numbers.push_back(static_cast<double>(integer->get()));
            }
            else
            {
                return std::nullopt;
            }
        }
        return Scene::FieldValue{std::move(numbers)};
    }
    return std::nullopt;
}

/// @brief One entity as read, before anything is created.
struct ReadEntity
{
    EntityId id;
    std::string name;
    std::vector<std::pair<std::string, Scene::FieldTable>> components; ///< In file order.
};
} // namespace

Result<std::string> writeScene(const Scene::Scene& scene, const Scene::ComponentRegistry& registry,
                               const SceneWriteOptions& options)
{
    const Scene::Scene::Registry& entities = scene.registry();

    // Saved entities only (transient ones have no IdComponent), in ID order: IDs grow with creation time, so new
    // entities go to the end and existing ones never move.
    std::vector<std::pair<EntityId, entt::entity>> ordered;
    // The storage directly: EnTT views over a const registry need an allocator with a default constructor, which the
    // scene's hasn't.
    if (const auto* ids = entities.storage<Scene::IdComponent>())
    {
        // A storage iterates its components; each() gives the entity with each.
        for (const auto [entity, id] : ids->each())
        {
            ordered.emplace_back(id.id, entity);
        }
    }
    std::ranges::sort(ordered, {}, &std::pair<EntityId, entt::entity>::first);

    std::map<std::string, std::uint32_t, std::less<>> versions; // what [components] lists
    std::vector<std::vector<WrittenComponent>> perEntity;
    perEntity.reserve(ordered.size());
    for (const auto& [id, entity] : ordered)
    {
        std::vector<WrittenComponent> components;
        for (const Scene::ComponentDesc& desc : registry.components())
        {
            if (!desc.has(entities, entity))
            {
                continue;
            }
            WrittenComponent written{.name = desc.name, .fields = {}};
            const auto target = options.componentVersions.find(desc.name);
            const std::uint32_t version = target != options.componentVersions.end() ? target->second : desc.version;
            if (version == desc.version)
            {
                // Known fields in their declared order, which reads better than alphabetical (position, rotation,
                // scale) and is just as deterministic.
                for (const Scene::FieldDesc& field : desc.fields)
                {
                    written.fields.emplace_back(field.name, field.get(desc.get(entities, entity), scene));
                }
            }
            else
            {
                Scene::FieldTable table = Scene::toFieldTable(desc, desc.get(entities, entity), scene);
                if (Result<void> migrated = Scene::migrate(desc, table, desc.version, version); !migrated)
                {
                    return Unexpected<Error>{migrated.error()};
                }
                for (auto& [key, value] : table)
                {
                    written.fields.emplace_back(key, std::move(value));
                }
            }
            versions[desc.name] = version;
            components.push_back(std::move(written));
        }
        if (const auto* unknown = entities.try_get<Scene::UnknownComponents>(entity))
        {
            for (const Scene::UnknownComponent& kept : unknown->components)
            {
                // A component this build does know wins over a kept copy of the same name.
                if (std::ranges::any_of(components, [&kept](const WrittenComponent& c) { return c.name == kept.name; }))
                {
                    continue;
                }
                WrittenComponent written{.name = kept.name, .fields = {}};
                for (const auto& [key, value] : kept.fields)
                {
                    written.fields.emplace_back(key, value);
                }
                versions.try_emplace(kept.name, kept.version);
                components.push_back(std::move(written));
            }
        }
        std::ranges::sort(components, {}, &WrittenComponent::name);
        perEntity.push_back(std::move(components));
    }

    std::string out;
    out += "# Meow2 scene. Entities are sorted by ID and each is one block, so edits to different entities merge.\n";
    out += std::format("format = \"{}\"\nversion = {}\n", c_formatName, c_sceneFormatVersion);
    out += "\n[components]\n";
    for (const auto& [name, version] : versions)
    {
        appendKey(out, name);
        out += std::format(" = {}\n", version);
    }
    for (std::size_t i = 0; i < ordered.size(); ++i)
    {
        const entt::entity entity = ordered[i].second;
        out += "\n[[entity]]\n";
        out += std::format("id = \"{}\"\n", ordered[i].first.uuid().toString());
        if (const auto* name = entities.try_get<Scene::NameComponent>(entity); name != nullptr && !name->name.empty())
        {
            out += "name = ";
            appendString(out, name->name);
            out += '\n';
        }
        for (const WrittenComponent& component : perEntity[i])
        {
            out += "\n[entity.";
            appendKey(out, component.name);
            out += "]\n";
            for (const auto& [key, value] : component.fields)
            {
                appendKey(out, key);
                out += " = ";
                appendValue(out, value);
                out += '\n';
            }
        }
    }
    return out;
}

Result<SceneReadReport> readScene(std::string_view text, Scene::Scene& scene, Scene::TransformHierarchy& hierarchy,
                                  const Scene::ComponentRegistry& registry, std::string_view source)
{
    const std::string where = source.empty() ? std::string{"scene"} : std::string{source};
    toml::parse_result parsed = toml::parse(text, source);
    if (!parsed)
    {
        const toml::parse_error& error = parsed.error();
        return fail(ErrorCode::ParseError, std::format("{}({}:{}): {}", where, error.source().begin.line,
                                                       error.source().begin.column, error.description()));
    }
    const toml::table& root = parsed.table();
    if (root["format"].value_or(std::string_view{}) != c_formatName)
    {
        return fail(ErrorCode::ParseError,
                    std::format("{}: not a Meow2 scene (format = \"{}\" missing)", where, c_formatName));
    }
    const std::int64_t formatVersion = root["version"].value_or(std::int64_t{0});
    if (formatVersion < 1)
    {
        return fail(ErrorCode::ParseError, std::format("{}: no valid format version", where));
    }
    if (formatVersion > static_cast<std::int64_t>(c_sceneFormatVersion))
    {
        return fail(ErrorCode::Unsupported,
                    std::format("{}: saved by a newer Meow2 (scene format {}, this build reads up to {})", where,
                                formatVersion, c_sceneFormatVersion));
    }

    SceneReadReport report;
    std::unordered_map<std::string, std::uint32_t> versions;
    if (const toml::table* table = root["components"].as_table())
    {
        for (const auto& [key, node] : *table)
        {
            const std::int64_t version = node.value_or(std::int64_t{1});
            versions.emplace(std::string{key.str()}, static_cast<std::uint32_t>(std::clamp<std::int64_t>(
                                                         version, 1, std::numeric_limits<std::uint32_t>::max())));
        }
    }

    // Pass 1: read and check everything, so a file that can't be read adds nothing to the scene.
    std::vector<ReadEntity> read;
    std::unordered_set<EntityId> seen;
    if (const toml::node_view entityNodes = root["entity"]; entityNodes)
    {
        const toml::array* array = entityNodes.as_array();
        if (array == nullptr)
        {
            return fail(ErrorCode::ParseError, std::format("{}: 'entity' must be an array of tables", where));
        }
        for (std::size_t index = 0; index < array->size(); ++index)
        {
            const toml::table* table = (*array)[index].as_table();
            if (table == nullptr)
            {
                return fail(ErrorCode::ParseError, std::format("{}: entity {} isn't a table", where, index + 1));
            }
            const std::string idText{(*table)["id"].value_or(std::string_view{})};
            Result<Uuid> uuid = Uuid::parse(idText);
            Result<EntityId> id = fail(ErrorCode::ParseError, "");
            if (uuid)
            {
                id = EntityId::fromUuid(*uuid);
            }
            else
            {
                id = Unexpected<Error>{uuid.error()};
            }
            if (!id)
            {
                return fail(ErrorCode::ParseError,
                            std::format("{}: entity {} has no valid id ({})", where, index + 1, id.error().message));
            }
            if (!seen.insert(*id).second || scene.find(*id))
            {
                return fail(ErrorCode::AlreadyExists, std::format("{}: entity {} is there twice", where, idText));
            }
            ReadEntity entity{
                .id = *id, .name = std::string{(*table)["name"].value_or(std::string_view{})}, .components = {}};
            for (const auto& [key, node] : *table)
            {
                if (key.str() == "id" || key.str() == "name")
                {
                    continue;
                }
                const toml::table* fields = node.as_table();
                if (fields == nullptr)
                {
                    report.warnings.push_back(
                        std::format("{}: entity {}: '{}' isn't a component table; skipped", where, idText, key.str()));
                    continue;
                }
                Scene::FieldTable values;
                for (const auto& [fieldKey, fieldNode] : *fields)
                {
                    if (std::optional<Scene::FieldValue> value = toFieldValue(fieldNode))
                    {
                        values.emplace(std::string{fieldKey.str()}, std::move(*value));
                    }
                    else
                    {
                        report.warnings.push_back(
                            std::format("{}: entity {}: {}.{} has a shape no component uses; skipped", where, idText,
                                        key.str(), fieldKey.str()));
                    }
                }
                entity.components.emplace_back(std::string{key.str()}, std::move(values));
            }
            read.push_back(std::move(entity));
        }
    }

    // Pass 2: every entity first, so references between them resolve whatever their order.
    Scene::Scene::Registry& entities = scene.registry();
    std::vector<entt::entity> created;
    created.reserve(read.size());
    for (const ReadEntity& entity : read)
    {
        Result<entt::entity> made = scene.createWithId(entity.id, entity.name);
        MEOW2_ASSERT(made.has_value(), "IDs were checked in pass 1");
        created.push_back(*made);
    }
    report.entities = created.size();

    // Pass 3: components.
    for (std::size_t i = 0; i < read.size(); ++i)
    {
        const entt::entity entity = created[i];
        const std::string idText = read[i].id.uuid().toString();
        for (auto& [name, table] : read[i].components)
        {
            const auto listed = versions.find(name);
            const std::uint32_t version = listed != versions.end() ? listed->second : 1;
            const Scene::ComponentDesc* desc = registry.find(name);
            if (desc == nullptr || version > desc->version)
            {
                if (desc != nullptr)
                {
                    report.warnings.push_back(std::format(
                        "{}: entity {}: {} is version {}, newer than this build's {}; kept unchanged but not used",
                        where, idText, name, version, desc->version));
                }
                entities.get_or_emplace<Scene::UnknownComponents>(entity).components.push_back(
                    {.name = name, .version = version, .fields = std::move(table)});
                ++report.unknown;
                continue;
            }
            if (version < desc->version)
            {
                if (Result<void> migrated = Scene::migrate(*desc, table, version, desc->version); !migrated)
                {
                    report.warnings.push_back(std::format("{}: entity {}: {}; kept unchanged but not used", where,
                                                          idText, migrated.error().message));
                    entities.get_or_emplace<Scene::UnknownComponents>(entity).components.push_back(
                        {.name = name, .version = version, .fields = std::move(table)});
                    ++report.unknown;
                    continue;
                }
                ++report.migrated;
            }
            void* component = desc->getOrEmplace(entities, entity);
            std::vector<std::string> fieldWarnings;
            if (Result<void> set = Scene::fromFieldTable(*desc, component, table, scene, fieldWarnings); !set)
            {
                fieldWarnings.push_back(set.error().message + "; the rest of the component keeps its defaults");
            }
            for (std::string& warning : fieldWarnings)
            {
                report.warnings.push_back(std::format("{}: entity {}: {}", where, idText, std::move(warning)));
            }
        }
    }

    // Pass 4: parents. A parent must exist and have a transform, and following parents must never come back to the
    // entity (a cycle would hang the hierarchy's depth walk): anything else is dropped with a warning.
    for (const entt::entity entity : created)
    {
        Scene::Parent* parent = entities.try_get<Scene::Parent>(entity);
        if (parent == nullptr)
        {
            continue;
        }
        std::string problem;
        if (!entities.all_of<Scene::LocalTransform>(entity))
        {
            problem = "has a Parent but no LocalTransform";
        }
        else if (parent->entity == entt::null)
        {
            problem = "has a Parent that names no entity";
        }
        else if (!entities.all_of<Scene::LocalTransform>(parent->entity))
        {
            problem = "has a parent without a LocalTransform";
        }
        else
        {
            std::set<entt::entity> visited{entity};
            for (entt::entity up = parent->entity; up != entt::null;)
            {
                if (!visited.insert(up).second)
                {
                    problem = "is its own ancestor";
                    break;
                }
                const Scene::Parent* next = entities.try_get<Scene::Parent>(up);
                up = next != nullptr ? next->entity : entt::null;
            }
        }
        if (!problem.empty())
        {
            report.warnings.push_back(std::format("{}: entity {} {}; its parent is dropped", where,
                                                  scene.idOf(entity).uuid().toString(), problem));
            entities.remove<Scene::Parent>(entity);
        }
    }
    for (const entt::entity entity : created)
    {
        if (entities.all_of<Scene::LocalTransform>(entity))
        {
            hierarchy.attach(entity);
        }
    }
    return report;
}

Result<void> saveScene(FileSystem::FileSystem& files, std::string_view path, const Scene::Scene& scene,
                       const Scene::ComponentRegistry& registry, const SceneWriteOptions& options)
{
    Result<std::string> text = writeScene(scene, registry, options);
    if (!text)
    {
        return Unexpected<Error>{text.error()};
    }
    return files.writeText(path, *text);
}

Result<SceneReadReport> loadScene(FileSystem::FileSystem& files, std::string_view path, Scene::Scene& scene,
                                  Scene::TransformHierarchy& hierarchy, const Scene::ComponentRegistry& registry)
{
    Result<std::string> text = files.readText(path);
    if (!text)
    {
        return Unexpected<Error>{text.error()};
    }
    return readScene(*text, scene, hierarchy, registry, path);
}
} // namespace Meow2::Assets
