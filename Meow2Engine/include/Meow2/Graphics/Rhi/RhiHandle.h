/// @file
/// @brief Typed handles to objects owned by the RHI, and the pool that hands them out.

#pragma once

#include <Meow2/Core/Assert.h>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <utility>
#include <vector>

namespace Meow2::Rhi
{
/// @brief A reference to an object in a HandlePool: a slot index plus the generation of that slot. Plain data.
///
/// A slot's generation changes every time its object is destroyed, so an old handle stops working instead of silently
/// naming whatever took the slot next. Handles live only as long as their pool and are never saved to disk
/// (techstack.md §11).
/// @tparam Tag Empty type that names what the handle refers to, so a buffer handle can't be passed as a texture one.
template <class Tag>
struct Handle
{
    /// @brief The index of a handle that refers to nothing.
    static constexpr std::uint32_t c_invalidIndex = std::numeric_limits<std::uint32_t>::max();

    std::uint32_t index = c_invalidIndex; ///< Slot in the pool.
    std::uint32_t generation = 0;         ///< Generation the slot had when the handle was made.

    /// @brief Whether the handle was ever set. Says nothing about whether its object is still alive.
    /// @return False for a default-constructed handle.
    [[nodiscard]] constexpr bool isSet() const
    {
        return index != c_invalidIndex;
    }

    /// @brief Handles compare by slot and generation.
    friend constexpr bool operator==(const Handle&, const Handle&) = default;
};

/// @brief Stores objects in reusable slots and refers to them by Handle.
///
/// Lookup is an array index plus one comparison, which is why GPU resources use handles rather than UUIDs (techstack.md
/// §11). Pointers returned by get() are invalidated by the next create().
/// @par Thread safety
/// Not thread-safe.
/// @tparam T   Stored type; movable.
/// @tparam Tag Handle tag.
template <class T, class Tag>
class HandlePool
{
public:
    /// @brief Stores an object, reusing a free slot if there is one.
    /// @param value The object.
    /// @return Its handle.
    [[nodiscard]] Handle<Tag> create(T value)
    {
        if (!m_free.empty())
        {
            const std::uint32_t index = m_free.back();
            m_free.pop_back();
            m_slots[index].value = std::move(value);
            return {.index = index, .generation = m_slots[index].generation};
        }
        MEOW2_ASSERT(m_slots.size() < Handle<Tag>::c_invalidIndex, "HandlePool is full");
        m_slots.push_back({.value = std::move(value), .generation = 0});
        return {.index = static_cast<std::uint32_t>(m_slots.size() - 1), .generation = 0};
    }

    /// @brief Looks up an object.
    /// @param handle The handle.
    /// @return The object, or null if the handle was never set or its object is gone.
    [[nodiscard]] T* get(Handle<Tag> handle)
    {
        if (handle.index >= m_slots.size() || m_slots[handle.index].generation != handle.generation ||
            !m_slots[handle.index].value)
        {
            return nullptr;
        }
        return &*m_slots[handle.index].value;
    }

    /// @brief Destroys an object and frees its slot.
    /// @param handle The handle.
    /// @return False if the handle was stale, in which case nothing happened.
    bool destroy(Handle<Tag> handle)
    {
        if (get(handle) == nullptr)
        {
            return false;
        }
        Slot& slot = m_slots[handle.index];
        slot.value.reset();
        ++slot.generation;
        m_free.push_back(handle.index);
        return true;
    }

    /// @brief How many objects are alive.
    /// @return The count.
    [[nodiscard]] std::size_t size() const
    {
        return m_slots.size() - m_free.size();
    }

private:
    /// @brief One entry: the object, if alive, and how many objects the slot has outlived.
    struct Slot
    {
        std::optional<T> value{};     ///< The object; empty while the slot is free.
        std::uint32_t generation = 0; ///< Bumped each time the object is destroyed.
    };

    std::vector<Slot> m_slots;
    std::vector<std::uint32_t> m_free;
};
} // namespace Meow2::Rhi
