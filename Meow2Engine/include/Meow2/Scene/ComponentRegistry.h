#pragma once

/// @file
/// @brief ComponentRegistry: each component describes its fields once (name, type, how to read and write them), its
///        version and its migrations. Scene files use it today; the editor's inspector and MCP schemas will too.

#include <Meow2/Core/Assert.h>
#include <Meow2/Core/Id/Uuid.h>
#include <Meow2/Core/Result.h>
#include <Meow2/Math/Camera.h>
#include <Meow2/Math/Color.h>
#include <Meow2/Math/Types.h>
#include <Meow2/Scene/Scene.h>
#include <Meow2/Scene/TransformHierarchy.h>

#include <array>
#include <concepts>
#include <cstdint>
#include <format>
#include <functional>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace Meow2::Scene
{
/// @brief One field's value in a form every component shares: what a scene file holds, and what migrations edit.
///
/// Numbers are `double` (integers `std::int64_t`); vectors, quaternions and colours are number arrays; asset and
/// entity references are their IDs as text (empty for none); enums are their names.
using FieldValue = std::variant<bool, std::int64_t, double, std::string, std::vector<double>>;

/// @brief A component's fields by name, sorted, as read from or written to a file.
using FieldTable = std::map<std::string, FieldValue, std::less<>>;

/// @brief What kind of value a field holds; decides how the inspector edits it and how files store it.
enum class FieldType : std::uint8_t
{
    Bool,   ///< `bool`.
    Int,    ///< `std::int32_t` or `std::uint32_t`.
    Float,  ///< `float`.
    String, ///< `std::string` (UTF-8).
    Vec3,   ///< `Math::Vec3`: three numbers.
    Quat,   ///< `Math::Quat`: four numbers, x y z w.
    Color,  ///< `Math::Color`: four numbers, linear r g b a.
    Asset,  ///< `AssetId`: the ID as text.
    Entity, ///< `entt::entity` in the scene: saved as the entity's EntityId.
    Enum,   ///< An enum with EnumNames: saved as the value's name.
};

/// @brief The names of an enum's values, for files and the inspector: specialise it with a
///        `static constexpr std::array<std::string_view, N> c_names` whose index is the value.
/// @tparam E The enum; its values must run from 0 to N − 1.
template <class E>
struct EnumNames;

/// @brief An enum saved by name (EnumNames); `entt::entity` is an enum too, but saved as a reference.
/// @tparam E The type.
template <class E>
concept NamedEnum = std::is_enum_v<E> && !std::same_as<E, entt::entity>;

/// @brief Names of Math::ProjectionType's values.
template <>
struct EnumNames<Math::ProjectionType>
{
    /// @brief By value.
    static constexpr std::array<std::string_view, 2> c_names{"perspective", "orthographic"};
};

/// @brief Names of Mobility's values.
template <>
struct EnumNames<Mobility>
{
    /// @brief By value.
    static constexpr std::array<std::string_view, 3> c_names{"auto", "static", "movable"};
};

/// @brief One field of a component: its name in files, its type, and functions that read and write it in a
///        component of the type it was made for (make it with field()).
/// @par Thread safety
/// Immutable after construction.
struct FieldDesc
{
    std::string name;                   ///< The key in files and the label in the inspector (camelCase).
    FieldType type = FieldType::Bool;   ///< What it holds.
    std::vector<std::string> enumNames; ///< FieldType::Enum: the values' names, by value.
    /// @brief Reads the field from a component (type-erased), in the shared form.
    FieldValue (*get)(const void* component, const Scene& scene) = nullptr;
    /// @brief Writes the field into a component; fails when the value has the wrong shape or names no entity.
    Result<void> (*set)(void* component, const FieldValue& value, const Scene& scene) = nullptr;
};

/// @brief A step between two versions of a component's fields, as stored in files.
///
/// `up` turns version `from` into `from + 1` (reading an old file); `down` turns `from + 1` back into `from` (saving a
/// file for an older build). Each edits the table in place: rename keys, convert units, fill in new fields.
/// @par Thread safety
/// Immutable after construction; the functions must not touch shared state.
struct Migration
{
    std::uint32_t from = 1;                  ///< The version `up` starts from.
    std::function<void(FieldTable&)> up{};   ///< `from` → `from + 1`.
    std::function<void(FieldTable&)> down{}; ///< `from + 1` → `from`.
};

/// @brief One component type: its name and version in files, old names, fields, migrations, and type-erased access
///        to its storage in a Scene (make it with component()).
/// @par Thread safety
/// Immutable after construction.
struct ComponentDesc
{
    std::string name;                  ///< The key in files (PascalCase, the C++ type's name).
    std::uint32_t version = 1;         ///< The current version of its fields; raise it with a migration.
    std::vector<std::string> aliases;  ///< Names it was saved under before; read as this component, saved as `name`.
    std::vector<FieldDesc> fields;     ///< In the order files list them.
    std::vector<Migration> migrations; ///< One per version step, any order.
    /// @brief A scene setting, not a component: one value per scene (in the registry's context), saved as the scene
    ///        file's `[settings.<name>]` table and edited in Scene settings, never added to entities.
    bool setting = false;
    /// @brief Whether an entity has the component.
    bool (*has)(const Scene::Registry& registry, entt::entity entity) = nullptr;
    /// @brief The entity's component; it must have one.
    const void* (*get)(const Scene::Registry& registry, entt::entity entity) = nullptr;
    /// @brief The entity's component, added with default values when it has none.
    void* (*getOrEmplace)(Scene::Registry& registry, entt::entity entity) = nullptr;
    /// @brief Removes the component from an entity (nothing if it has none).
    void (*remove)(Scene::Registry& registry, entt::entity entity) = nullptr;
    /// @brief Creates the component's storage up front (Scene::registerComponent()).
    void (*registerStorage)(Scene& scene) = nullptr;
};

/// @brief A component an app doesn't know, read from a scene file: kept on its entity and written back unchanged, so
///        opening an editor scene in Meow2Renderer (no scripts) and saving it loses nothing. Plain data.
struct UnknownComponent
{
    std::string name;          ///< As in the file.
    std::uint32_t version = 1; ///< As in the file's `[components]` table.
    FieldTable fields;         ///< As in the file.
};

/// @brief The unknown components of one entity. Plain data.
struct UnknownComponents
{
    std::vector<UnknownComponent> components; ///< In the order they were read.
};

/// @cond
namespace Detail
{
[[nodiscard]] double floatToDouble(float value);
[[nodiscard]] Result<double> toNumber(const FieldValue& value);
[[nodiscard]] Result<std::int64_t> toInteger(const FieldValue& value);
[[nodiscard]] Result<std::span<const double>> toNumbers(const FieldValue& value, std::size_t count);
[[nodiscard]] Result<const std::string*> toText(const FieldValue& value);
[[nodiscard]] Result<std::size_t> enumIndex(const FieldValue& value, std::span<const std::string_view> names);
[[nodiscard]] FieldValue entityRef(entt::entity entity, const Scene& scene);
[[nodiscard]] Result<entt::entity> resolveEntity(const FieldValue& value, const Scene& scene);
[[nodiscard]] FieldValue assetRef(AssetId id);
[[nodiscard]] Result<AssetId> resolveAsset(const FieldValue& value);

template <class T>
struct FieldTraits;

template <>
struct FieldTraits<bool>
{
    static constexpr FieldType c_type = FieldType::Bool;
    static FieldValue get(const bool& member, const Scene&)
    {
        return member;
    }
    static Result<void> set(bool& member, const FieldValue& value, const Scene&)
    {
        if (const bool* flag = std::get_if<bool>(&value))
        {
            member = *flag;
            return {};
        }
        return fail(ErrorCode::InvalidArgument, "expected true or false");
    }
};

template <class T>
    requires std::same_as<T, std::int32_t> || std::same_as<T, std::uint32_t>
struct FieldTraits<T>
{
    static constexpr FieldType c_type = FieldType::Int;
    static FieldValue get(const T& member, const Scene&)
    {
        return static_cast<std::int64_t>(member);
    }
    static Result<void> set(T& member, const FieldValue& value, const Scene&)
    {
        Result<std::int64_t> number = toInteger(value);
        if (!number)
        {
            return Unexpected<Error>{number.error()};
        }
        if (!std::in_range<T>(*number))
        {
            return fail(ErrorCode::InvalidArgument, std::format("{} is out of range", *number));
        }
        member = static_cast<T>(*number);
        return {};
    }
};

template <>
struct FieldTraits<float>
{
    static constexpr FieldType c_type = FieldType::Float;
    static FieldValue get(const float& member, const Scene&)
    {
        return floatToDouble(member);
    }
    static Result<void> set(float& member, const FieldValue& value, const Scene&)
    {
        Result<double> number = toNumber(value);
        if (!number)
        {
            return Unexpected<Error>{number.error()};
        }
        member = static_cast<float>(*number);
        return {};
    }
};

template <>
struct FieldTraits<std::string>
{
    static constexpr FieldType c_type = FieldType::String;
    static FieldValue get(const std::string& member, const Scene&)
    {
        return member;
    }
    static Result<void> set(std::string& member, const FieldValue& value, const Scene&)
    {
        Result<const std::string*> text = toText(value);
        if (!text)
        {
            return Unexpected<Error>{text.error()};
        }
        member = **text;
        return {};
    }
};

/// Vectors, quaternions and colours: N floats, read and written through the type's own members (Members).
template <class T, FieldType Type, std::size_t N, class Members>
struct FloatArrayTraits
{
    static constexpr FieldType c_type = Type;
    static FieldValue get(const T& member, const Scene&)
    {
        T copy = member;
        const std::array<float*, N> values = Members{}(copy);
        std::vector<double> numbers(N);
        for (std::size_t i = 0; i < N; ++i)
        {
            numbers[i] = floatToDouble(*values[i]);
        }
        return numbers;
    }
    static Result<void> set(T& member, const FieldValue& value, const Scene&)
    {
        Result<std::span<const double>> numbers = toNumbers(value, N);
        if (!numbers)
        {
            return Unexpected<Error>{numbers.error()};
        }
        const std::array<float*, N> values = Members{}(member);
        for (std::size_t i = 0; i < N; ++i)
        {
            *values[i] = static_cast<float>((*numbers)[i]);
        }
        return {};
    }
};

struct Vec3Members
{
    std::array<float*, 3> operator()(Math::Vec3& v) const
    {
        return {&v.x, &v.y, &v.z};
    }
};

struct QuatMembers
{
    std::array<float*, 4> operator()(Math::Quat& q) const
    {
        return {&q.x, &q.y, &q.z, &q.w};
    }
};

struct ColorMembers
{
    std::array<float*, 4> operator()(Math::Color& c) const
    {
        return {&c.r, &c.g, &c.b, &c.a};
    }
};

template <>
struct FieldTraits<Math::Vec3> : FloatArrayTraits<Math::Vec3, FieldType::Vec3, 3, Vec3Members>
{
};

template <>
struct FieldTraits<Math::Quat> : FloatArrayTraits<Math::Quat, FieldType::Quat, 4, QuatMembers>
{
};

template <>
struct FieldTraits<Math::Color> : FloatArrayTraits<Math::Color, FieldType::Color, 4, ColorMembers>
{
};

template <>
struct FieldTraits<AssetId>
{
    static constexpr FieldType c_type = FieldType::Asset;
    static FieldValue get(const AssetId& member, const Scene&)
    {
        return assetRef(member);
    }
    static Result<void> set(AssetId& member, const FieldValue& value, const Scene&)
    {
        Result<AssetId> id = resolveAsset(value);
        if (!id)
        {
            return Unexpected<Error>{id.error()};
        }
        member = *id;
        return {};
    }
};

template <>
struct FieldTraits<entt::entity>
{
    static constexpr FieldType c_type = FieldType::Entity;
    static FieldValue get(const entt::entity& member, const Scene& scene)
    {
        return entityRef(member, scene);
    }
    static Result<void> set(entt::entity& member, const FieldValue& value, const Scene& scene)
    {
        Result<entt::entity> entity = resolveEntity(value, scene);
        if (!entity)
        {
            return Unexpected<Error>{entity.error()};
        }
        member = *entity;
        return {};
    }
};

template <NamedEnum E>
struct FieldTraits<E>
{
    static constexpr FieldType c_type = FieldType::Enum;
    static FieldValue get(const E& member, const Scene&)
    {
        const auto index = static_cast<std::size_t>(member);
        MEOW2_ASSERT(index < EnumNames<E>::c_names.size(), "an enum value without a name");
        return std::string{EnumNames<E>::c_names[index]};
    }
    static Result<void> set(E& member, const FieldValue& value, const Scene&)
    {
        Result<std::size_t> index = enumIndex(value, EnumNames<E>::c_names);
        if (!index)
        {
            return Unexpected<Error>{index.error()};
        }
        member = static_cast<E>(*index);
        return {};
    }
};
} // namespace Detail
/// @endcond

/// @brief Describes one field of component `C`.
///
/// ```cpp
/// field<LocalTransform>("position", [](LocalTransform& c) -> auto& { return c.value.position; })
/// ```
/// The field's type is the member's C++ type (see FieldType), so a descriptor can't disagree with the struct.
/// @tparam C      The component.
/// @tparam Access A lambda without captures returning a reference to the member, so nested members work too.
/// @param name   The key in files.
/// @param access The lambda (only its type is used).
/// @return The descriptor.
template <class C, class Access>
[[nodiscard]] FieldDesc field(std::string name, Access access)
{
    static_assert(std::is_empty_v<Access> && std::is_default_constructible_v<Access>,
                  "the accessor must be a lambda without captures");
    static_cast<void>(access);
    using Member = std::remove_cvref_t<decltype(Access{}(std::declval<C&>()))>;
    using Traits = Detail::FieldTraits<Member>;
    FieldDesc desc{.name = std::move(name), .type = Traits::c_type, .enumNames = {}, .get = nullptr, .set = nullptr};
    if constexpr (NamedEnum<Member>)
    {
        for (const std::string_view value : EnumNames<Member>::c_names)
        {
            desc.enumNames.emplace_back(value);
        }
    }
    desc.get = [](const void* component, const Scene& scene) -> FieldValue
    {
        // The accessor takes a non-const reference so one lambda serves both directions; it only returns the member.
        C& typed = *const_cast<C*>(static_cast<const C*>(component));
        return Traits::get(Access{}(typed), scene);
    };
    desc.set = [](void* component, const FieldValue& value, const Scene& scene) -> Result<void>
    {
        return Traits::set(Access{}(*static_cast<C*>(component)), value, scene);
    };
    return desc;
}

/// @brief Describes component type `C`: its storage access, plus what the caller gives.
/// @tparam C A plain data component, default-constructible and not empty.
/// @param name    The key in files.
/// @param version Its current version.
/// @param fields  Its fields (field<C>()).
/// @return The descriptor; add aliases and migrations to it before ComponentRegistry::add().
template <class C>
[[nodiscard]] ComponentDesc component(std::string name, std::uint32_t version, std::vector<FieldDesc> fields)
{
    static_assert(!std::is_empty_v<C>, "empty components (tags) have no fields to save");
    static_assert(std::is_default_constructible_v<C>);
    ComponentDesc desc{.name = std::move(name),
                       .version = version,
                       .aliases = {},
                       .fields = std::move(fields),
                       .migrations = {},
                       .has = nullptr,
                       .get = nullptr,
                       .getOrEmplace = nullptr,
                       .remove = nullptr,
                       .registerStorage = nullptr};
    desc.has = [](const Scene::Registry& registry, entt::entity entity)
    {
        return registry.all_of<C>(entity);
    };
    desc.get = [](const Scene::Registry& registry, entt::entity entity) -> const void*
    {
        return &registry.get<C>(entity);
    };
    desc.getOrEmplace = [](Scene::Registry& registry, entt::entity entity) -> void*
    {
        return &registry.get_or_emplace<C>(entity);
    };
    desc.remove = [](Scene::Registry& registry, entt::entity entity)
    {
        registry.remove<C>(entity);
    };
    desc.registerStorage = [](Scene& scene)
    {
        scene.registerComponent<C>();
    };
    return desc;
}

/// @brief Reads every field of a component into a table.
/// @param desc      Its descriptor.
/// @param component The component, of the descriptor's type.
/// @param scene     For entity references.
/// @return The fields by name.
[[nodiscard]] FieldTable toFieldTable(const ComponentDesc& desc, const void* component, const Scene& scene);

/// @brief Writes a table's fields into a component; fields the table doesn't have keep their values.
/// @param desc      Its descriptor.
/// @param component The component, of the descriptor's type.
/// @param table     The values.
/// @param scene     For entity references.
/// @param[out] warnings Keys the component has no field for (from a newer or older build), one line each.
/// @return Nothing, or the first field whose value has the wrong shape (named in the message).
[[nodiscard]] Result<void> fromFieldTable(const ComponentDesc& desc, void* component, const FieldTable& table,
                                          const Scene& scene, std::vector<std::string>& warnings);

/// @brief Moves a table between two versions of a component, one migration at a time (up or down).
/// @param desc  Its descriptor.
/// @param table The fields, edited in place.
/// @param from  The table's version.
/// @param to    The version wanted.
/// @return Nothing, or an error naming a missing step.
[[nodiscard]] Result<void> migrate(const ComponentDesc& desc, FieldTable& table, std::uint32_t from, std::uint32_t to);

/// @brief All the component types an app saves and loads, by name.
///
/// Each app registers what it knows (the engine's components with registerSceneComponents(), the renderer's with
/// Renderer::registerRenderComponents(), the editor's own); a scene file's other components are kept as unknown.
/// @par Thread safety
/// Not thread-safe while components are added; afterwards immutable, so any thread may read.
/// @par Lifetime
/// Pointers from find() stay valid until the next add(); add everything at startup.
class ComponentRegistry
{
public:
    /// @brief Adds a component type.
    /// @param desc Its descriptor (component<C>()).
    /// @pre No registered component has the same name or alias.
    void add(ComponentDesc desc);

    /// @brief The component saved under a name, or one of its old names.
    /// @param name As in a file.
    /// @return Its descriptor, or null.
    [[nodiscard]] const ComponentDesc* find(std::string_view name) const;

    /// @brief Every registered component, in the order added.
    /// @return The descriptors.
    [[nodiscard]] std::span<const ComponentDesc> components() const
    {
        return m_components;
    }

    /// @brief Creates the storage of every registered component, and of UnknownComponents, in a scene (techstack §25:
    ///        pools exist before any job could touch them).
    /// @param scene The scene.
    void registerAll(Scene& scene) const;

private:
    std::vector<ComponentDesc> m_components;
};

/// @brief Registers the engine's scene components: `LocalTransform`, `Parent`, `Camera` and `Environment`.
/// @param registry Where.
void registerSceneComponents(ComponentRegistry& registry);
} // namespace Meow2::Scene
