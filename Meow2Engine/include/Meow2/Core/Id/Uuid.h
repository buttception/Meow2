#pragma once

/// @file
/// @brief 128-bit IDs for things that are saved and referenced across runs: Uuid, IdKind and the typed wrappers.
///
/// Layout, an RFC 9562 UUIDv8 (v8 = custom fields, so these are valid UUIDs any tool accepts):
/// @code
///  bit 127                                                                                     bit 0
/// | unix time, ms (48) | version = 8 (4) | sequence (12) | variant = 0b10 (2) | kind (8) | node (54) |
/// @endcode
/// IDs sort by creation time, say what kind of thing they name, and need no coordination between machines (the node
/// is random). Runtime-only objects use handles instead (techstack.md §11, §25).
/// @see techstack.md §24, docs/wiki/Ids.md, UuidGenerator

#include <Meow2/Core/Assert.h>
#include <Meow2/Core/Result.h>

#include <compare>
#include <cstddef>
#include <cstdint>
#include <format>
#include <functional>
#include <string>
#include <string_view>

namespace Meow2
{
/// @brief What an ID names. Stored in the ID itself (8 bits), so any ID found in a file or a log says what it is.
enum class IdKind : std::uint8_t
{
    Invalid = 0, ///< Not an ID; the kind of the all-zero Uuid.
    Entity = 1,  ///< A scene entity (techstack.md §25).
    Asset = 2,   ///< An imported asset (mesh, texture, material, …); kept in its `.meta` file.
    Scene = 3,   ///< A scene file.
    Prefab = 4,  ///< A prefab.
};

/// @brief A 128-bit identifier in the engine's UUIDv8 layout (see the file comment).
///
/// Value type: cheap to copy and compare (ordered by creation time, then sequence, kind and node). `std::hash` is
/// specialised for Uuid and TypedId (unordered containers), and `std::format("{}", id)` writes toString().
/// Create them with UuidGenerator; read them back from text with parse().
/// @par Thread safety
/// Immutable value; any thread may read.
class Uuid
{
public:
    /// @brief The all-zero ID, which is invalid.
    constexpr Uuid() = default;

    /// @brief Builds an ID from its two 64-bit halves, e.g. as stored from high() and low().
    /// @param high Bits 127..64: time, version, sequence.
    /// @param low  Bits 63..0: variant, kind, node.
    /// @return The ID; not checked, see isValid().
    [[nodiscard]] static constexpr Uuid fromParts(std::uint64_t high, std::uint64_t low)
    {
        Uuid id;
        id.m_high = high;
        id.m_low = low;
        return id;
    }

    /// @brief Reads an ID from its text form `xxxxxxxx-xxxx-8xxx-xxxx-xxxxxxxxxxxx` (either case).
    /// @param text The text, exactly 36 characters.
    /// @return The ID, or ErrorCode::InvalidArgument for a wrong length, a misplaced dash, a non-hex digit, a version
    ///         other than 8, a variant other than 0b10, or kind Invalid.
    [[nodiscard]] static Result<Uuid> parse(std::string_view text);

    /// @brief The text form: lowercase `8-4-4-4-12` hex digits.
    /// @return 36 characters.
    [[nodiscard]] std::string toString() const;

    /// @brief Whether this is an engine ID: version 8, variant 0b10 and a kind other than Invalid.
    /// @return `false` for the all-zero ID and for UUIDs from elsewhere.
    [[nodiscard]] constexpr bool isValid() const
    {
        return ((m_high >> 12) & 0xFU) == 8U && (m_low >> 62) == 0b10U && kind() != IdKind::Invalid;
    }

    /// @brief What the ID names.
    /// @return The kind bits; IdKind::Invalid for the all-zero ID.
    [[nodiscard]] constexpr IdKind kind() const
    {
        return static_cast<IdKind>((m_low >> 54) & 0xFFU);
    }

    /// @brief When the ID was made.
    /// @return Milliseconds since 1970-01-01 UTC.
    [[nodiscard]] constexpr std::uint64_t unixMilliseconds() const
    {
        return m_high >> 16;
    }

    /// @brief The upper half, for storing the ID as two integers.
    /// @return Bits 127..64.
    [[nodiscard]] constexpr std::uint64_t high() const
    {
        return m_high;
    }

    /// @brief The lower half, for storing the ID as two integers.
    /// @return Bits 63..0.
    [[nodiscard]] constexpr std::uint64_t low() const
    {
        return m_low;
    }

    /// @brief IDs are ordered by their 128 bits, i.e. by creation time first.
    friend constexpr std::strong_ordering operator<=>(const Uuid&, const Uuid&) = default;

private:
    std::uint64_t m_high = 0;
    std::uint64_t m_low = 0;
};

/// @brief A Uuid that is known to be of kind `K`, so IDs of different kinds can't be mixed up at compile time.
/// @tparam K The kind every value has.
/// @par Thread safety
/// Immutable value; any thread may read.
template <IdKind K>
class TypedId
{
public:
    /// @brief The kind of every value of this type.
    static constexpr IdKind c_kind = K;

    /// @brief The invalid ID (all zero), e.g. for "no entity".
    constexpr TypedId() = default;

    /// @brief Wraps an ID that is known to be of kind `K`.
    /// @param uuid The ID.
    /// @pre `uuid.kind() == K` (asserted). Use fromUuid() for IDs read from outside.
    explicit TypedId(const Uuid& uuid)
        : m_uuid(uuid)
    {
        MEOW2_ASSERT(uuid.kind() == K, "an ID of another kind");
    }

    /// @brief Wraps an ID read from outside (a file, a command), checking its kind.
    /// @param uuid The ID.
    /// @return The typed ID, or ErrorCode::InvalidArgument if it isn't a valid ID of kind `K`.
    [[nodiscard]] static Result<TypedId> fromUuid(const Uuid& uuid)
    {
        if (!uuid.isValid() || uuid.kind() != K)
        {
            return fail(ErrorCode::InvalidArgument,
                        std::format("{} isn't an ID of the expected kind", uuid.toString()));
        }
        return TypedId{uuid};
    }

    /// @brief The plain ID.
    /// @return The wrapped Uuid.
    [[nodiscard]] constexpr const Uuid& uuid() const
    {
        return m_uuid;
    }

    /// @brief Whether it holds an ID (not the default, all-zero one).
    /// @return `true` for a real ID.
    [[nodiscard]] constexpr bool isValid() const
    {
        return m_uuid.isValid();
    }

    /// @brief Ordered like the wrapped Uuid.
    friend constexpr std::strong_ordering operator<=>(const TypedId&, const TypedId&) = default;

private:
    Uuid m_uuid;
};

using EntityId = TypedId<IdKind::Entity>; ///< ID of a scene entity.
using AssetId = TypedId<IdKind::Asset>;   ///< ID of an asset.
using SceneId = TypedId<IdKind::Scene>;   ///< ID of a scene.
using PrefabId = TypedId<IdKind::Prefab>; ///< ID of a prefab.
} // namespace Meow2

// Doxygen can't place specializations of std templates (it has no model of namespace std), so these are hidden from
// the API reference; Uuid's class comment says it is hashable and formattable.
/// @cond
/// @brief Hashing for unordered containers: the halves mixed (they are already well distributed by time and node).
template <>
struct std::hash<Meow2::Uuid>
{
    /// @brief Hashes an ID.
    /// @param id The ID.
    /// @return The hash.
    std::size_t operator()(const Meow2::Uuid& id) const noexcept
    {
        return static_cast<std::size_t>(id.high() ^ (id.low() * 0x9E3779B97F4A7C15ULL));
    }
};

/// @brief Hashing for typed IDs, the same as their Uuid.
/// @tparam K The kind.
template <Meow2::IdKind K>
struct std::hash<Meow2::TypedId<K>>
{
    /// @brief Hashes an ID.
    /// @param id The ID.
    /// @return The hash.
    std::size_t operator()(const Meow2::TypedId<K>& id) const noexcept
    {
        return std::hash<Meow2::Uuid>{}(id.uuid());
    }
};

/// @brief `std::format("{}", id)` writes the text form (Uuid::toString()).
template <>
struct std::formatter<Meow2::Uuid> : std::formatter<std::string_view>
{
    /// @brief Formats an ID.
    /// @param id      The ID.
    /// @param context The format context.
    /// @return The output iterator after the text.
    auto format(const Meow2::Uuid& id, std::format_context& context) const
    {
        return std::formatter<std::string_view>::format(id.toString(), context);
    }
};
/// @endcond
