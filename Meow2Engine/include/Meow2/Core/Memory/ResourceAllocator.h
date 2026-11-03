#pragma once

/// @file
/// @brief ResourceAllocator: a standard allocator that takes its memory from a `std::pmr::memory_resource`, for
///        libraries that don't work with `std::pmr::polymorphic_allocator`.
/// @see techstack.md §6, docs/wiki/Memory.md

#include <cstddef>
#include <memory_resource>

namespace Meow2::Memory
{
/// @brief Allocates `T`s from a memory resource, like `std::pmr::polymorphic_allocator<T>` without its
///        `construct()`.
///
/// `polymorphic_allocator::construct()` does *uses-allocator construction*: when it builds an object that itself
/// takes an allocator, it passes itself along. EnTT builds its component pools through the registry's allocator,
/// and that extra argument doesn't fit their constructors, so a pmr registry doesn't compile. This allocator only
/// allocates and frees; objects are constructed normally (`std::allocator_traits` falls back to placement new).
/// Containers stored *inside* the objects (e.g. a `std::string` in a component) keep their own default allocator.
/// @tparam T The element type.
/// @par Thread safety
/// As thread-safe as the resource.
/// @par Lifetime
/// The resource must outlive every allocator and every block allocated through it.
template <class T>
class ResourceAllocator
{
public:
    /// @brief The element type (allocator requirement).
    using value_type = T;

    /// @brief An allocator over `resource`.
    /// @param resource Where the memory comes from. Must outlive the allocator and its blocks.
    explicit ResourceAllocator(std::pmr::memory_resource* resource) noexcept
        : m_resource(resource)
    {
    }

    /// @brief The same resource, for another element type (containers rebind their allocator).
    /// @tparam U The other element type.
    /// @param other The allocator to copy the resource from.
    template <class U>
    ResourceAllocator(const ResourceAllocator<U>& other) noexcept // NOLINT: implicit by design (allocator rebinding)
        : m_resource(other.resource())
    {
    }

    /// @brief Allocates room for `count` objects; doesn't construct them.
    /// @param count Number of objects.
    /// @return Memory aligned for `T`.
    [[nodiscard]] T* allocate(std::size_t count)
    {
        return static_cast<T*>(m_resource->allocate(count * sizeof(T), alignof(T)));
    }

    /// @brief Frees memory from allocate().
    /// @param pointer What allocate() returned.
    /// @param count   The same count.
    void deallocate(T* pointer, std::size_t count) noexcept
    {
        m_resource->deallocate(pointer, count * sizeof(T), alignof(T));
    }

    /// @brief The resource behind the allocator.
    /// @return The resource.
    [[nodiscard]] std::pmr::memory_resource* resource() const noexcept
    {
        return m_resource;
    }

    /// @brief Allocators are interchangeable when their resources are: memory from one can be freed by the other.
    /// @tparam U The other element type.
    /// @param other The other allocator.
    /// @return `true` if both use equal resources.
    template <class U>
    [[nodiscard]] bool operator==(const ResourceAllocator<U>& other) const noexcept
    {
        return *m_resource == *other.resource();
    }

private:
    std::pmr::memory_resource* m_resource;
};
} // namespace Meow2::Memory
