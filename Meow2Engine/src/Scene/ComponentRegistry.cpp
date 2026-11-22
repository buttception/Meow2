/// @file
/// @brief ComponentRegistry and the field conversions (ComponentRegistry.h).

#include <Meow2/Scene/Camera.h>
#include <Meow2/Scene/ComponentRegistry.h>
#include <Meow2/Scene/Environment.h>
#include <Meow2/Scene/TransformHierarchy.h>

#include <algorithm>
#include <charconv>
#include <cmath>

namespace Meow2::Scene
{
namespace Detail
{
double floatToDouble(float value)
{
    // A float's shortest decimal form (0.1 for 0.1F) read back as a double: files show "0.1" rather than
    // 0.10000000149011612, and converting the double back to float gives the same float bit for bit.
    if (!std::isfinite(value))
    {
        return static_cast<double>(value);
    }
    std::array<char, 32> text{};
    const std::to_chars_result written = std::to_chars(text.data(), text.data() + text.size(), value);
    double result = 0.0;
    std::from_chars(text.data(), written.ptr, result);
    return result;
}

Result<double> toNumber(const FieldValue& value)
{
    if (const double* number = std::get_if<double>(&value))
    {
        return *number;
    }
    if (const std::int64_t* integer = std::get_if<std::int64_t>(&value))
    {
        return static_cast<double>(*integer); // "1" for a float field is fine
    }
    return fail(ErrorCode::InvalidArgument, "expected a number");
}

Result<std::int64_t> toInteger(const FieldValue& value)
{
    if (const std::int64_t* integer = std::get_if<std::int64_t>(&value))
    {
        return *integer;
    }
    return fail(ErrorCode::InvalidArgument, "expected a whole number");
}

Result<std::span<const double>> toNumbers(const FieldValue& value, std::size_t count)
{
    const std::vector<double>* numbers = std::get_if<std::vector<double>>(&value);
    if (numbers == nullptr || numbers->size() != count)
    {
        return fail(ErrorCode::InvalidArgument, std::format("expected {} numbers", count));
    }
    return std::span<const double>{*numbers};
}

Result<const std::string*> toText(const FieldValue& value)
{
    if (const std::string* text = std::get_if<std::string>(&value))
    {
        return text;
    }
    return fail(ErrorCode::InvalidArgument, "expected text");
}

Result<std::size_t> enumIndex(const FieldValue& value, std::span<const std::string_view> names)
{
    Result<const std::string*> text = toText(value);
    if (!text)
    {
        return Unexpected<Error>{text.error()};
    }
    const auto found = std::ranges::find(names, std::string_view{**text});
    if (found == names.end())
    {
        return fail(ErrorCode::InvalidArgument, std::format("'{}' isn't one of its values", **text));
    }
    return static_cast<std::size_t>(found - names.begin());
}

FieldValue entityRef(entt::entity entity, const Scene& scene)
{
    // A reference to nothing, or to an entity that isn't saved (transient), is stored as "": it can't be found again.
    if (entity == entt::null || !scene.contains(entity))
    {
        return std::string{};
    }
    const EntityId id = scene.idOf(entity);
    return id.isValid() ? id.uuid().toString() : std::string{};
}

Result<entt::entity> resolveEntity(const FieldValue& value, const Scene& scene)
{
    Result<const std::string*> text = toText(value);
    if (!text)
    {
        return Unexpected<Error>{text.error()};
    }
    if ((*text)->empty())
    {
        return entt::entity{entt::null};
    }
    Result<Uuid> uuid = Uuid::parse(**text);
    if (!uuid)
    {
        return Unexpected<Error>{uuid.error()};
    }
    Result<EntityId> id = EntityId::fromUuid(*uuid);
    if (!id)
    {
        return Unexpected<Error>{id.error()};
    }
    const std::optional<entt::entity> entity = scene.find(*id);
    if (!entity)
    {
        return fail(ErrorCode::NotFound, std::format("no entity {} in the scene", **text));
    }
    return *entity;
}

FieldValue assetRef(AssetId id)
{
    return id.isValid() ? id.uuid().toString() : std::string{};
}

Result<AssetId> resolveAsset(const FieldValue& value)
{
    Result<const std::string*> text = toText(value);
    if (!text)
    {
        return Unexpected<Error>{text.error()};
    }
    if ((*text)->empty())
    {
        return AssetId{};
    }
    Result<Uuid> uuid = Uuid::parse(**text);
    if (!uuid)
    {
        return Unexpected<Error>{uuid.error()};
    }
    return AssetId::fromUuid(*uuid);
}
} // namespace Detail

FieldTable toFieldTable(const ComponentDesc& desc, const void* component, const Scene& scene)
{
    FieldTable table;
    for (const FieldDesc& field : desc.fields)
    {
        table.emplace(field.name, field.get(component, scene));
    }
    return table;
}

Result<void> fromFieldTable(const ComponentDesc& desc, void* component, const FieldTable& table, const Scene& scene,
                            std::vector<std::string>& warnings)
{
    for (const auto& [key, value] : table)
    {
        const auto field =
            std::ranges::find_if(desc.fields, [&key](const FieldDesc& candidate) { return candidate.name == key; });
        if (field == desc.fields.end())
        {
            warnings.push_back(std::format("{} has no field '{}'; it's dropped", desc.name, key));
            continue;
        }
        if (Result<void> set = field->set(component, value, scene); !set)
        {
            return fail(set.error().code, std::format("{}.{}: {}", desc.name, key, set.error().message));
        }
    }
    return {};
}

Result<void> migrate(const ComponentDesc& desc, FieldTable& table, std::uint32_t from, std::uint32_t to)
{
    const auto step = [&desc](std::uint32_t version) -> const Migration*
    {
        const auto found = std::ranges::find_if(desc.migrations, [version](const Migration& migration)
                                                { return migration.from == version; });
        return found != desc.migrations.end() ? &*found : nullptr;
    };
    for (std::uint32_t version = from; version < to; ++version)
    {
        const Migration* migration = step(version);
        if (migration == nullptr || !migration->up)
        {
            return fail(ErrorCode::Unsupported,
                        std::format("{} can't be upgraded from version {} to {}", desc.name, version, version + 1));
        }
        migration->up(table);
    }
    for (std::uint32_t version = from; version > to; --version)
    {
        const Migration* migration = step(version - 1);
        if (migration == nullptr || !migration->down)
        {
            return fail(ErrorCode::Unsupported,
                        std::format("{} can't be downgraded from version {} to {}", desc.name, version, version - 1));
        }
        migration->down(table);
    }
    return {};
}

void ComponentRegistry::add(ComponentDesc desc)
{
    MEOW2_ASSERT(find(desc.name) == nullptr, "a component with this name is registered already");
    for ([[maybe_unused]] const std::string& alias : desc.aliases)
    {
        MEOW2_ASSERT(find(alias) == nullptr, "a component alias is taken already");
    }
    m_components.push_back(std::move(desc));
}

const ComponentDesc* ComponentRegistry::find(std::string_view name) const
{
    for (const ComponentDesc& desc : m_components)
    {
        if (desc.name == name || std::ranges::find(desc.aliases, name) != desc.aliases.end())
        {
            return &desc;
        }
    }
    return nullptr;
}

void ComponentRegistry::registerAll(Scene& scene) const
{
    for (const ComponentDesc& desc : m_components)
    {
        desc.registerStorage(scene);
    }
    scene.registerComponent<UnknownComponents>();
}

void registerSceneComponents(ComponentRegistry& registry)
{
    registry.add(component<LocalTransform>(
        "LocalTransform", 1,
        {field<LocalTransform>("position", [](LocalTransform& c) -> auto& { return c.value.position; }),
         field<LocalTransform>("rotation", [](LocalTransform& c) -> auto& { return c.value.rotation; }),
         field<LocalTransform>("scale", [](LocalTransform& c) -> auto& { return c.value.scale; }),
         field<LocalTransform>("mobility", [](LocalTransform& c) -> auto& { return c.mobility; })}));
    registry.add(
        component<Parent>("Parent", 1, {field<Parent>("entity", [](Parent& c) -> auto& { return c.entity; })}));
    registry.add(component<Camera>(
        "Camera", 1,
        {field<Camera>("projection", [](Camera& c) -> auto& { return c.lens.type; }),
         field<Camera>("verticalFov", [](Camera& c) -> auto& { return c.lens.verticalFov; }),
         field<Camera>("orthographicHeight", [](Camera& c) -> auto& { return c.lens.orthographicHeight; }),
         field<Camera>("nearZ", [](Camera& c) -> auto& { return c.lens.nearZ; }),
         field<Camera>("farZ", [](Camera& c) -> auto& { return c.lens.farZ; }),
         field<Camera>("main", [](Camera& c) -> auto& { return c.main; })}));
    // A scene setting (in the registry's context, Environment.h), described here like a component so files, the
    // Scene settings dock and agents read and write its fields the same way.
    ComponentDesc environmentDesc = component<Environment>(
        "Environment", 1,
        {field<Environment>("sunElevation", [](Environment& c) -> auto& { return c.sunElevation; }),
         field<Environment>("sunAzimuth", [](Environment& c) -> auto& { return c.sunAzimuth; }),
         field<Environment>("sunColor", [](Environment& c) -> auto& { return c.sunColor; }),
         field<Environment>("sunIntensity", [](Environment& c) -> auto& { return c.sunIntensity; }),
         field<Environment>("exposure", [](Environment& c) -> auto& { return c.exposure; })});
    environmentDesc.setting = true;
    registry.add(std::move(environmentDesc));
}
} // namespace Meow2::Scene
