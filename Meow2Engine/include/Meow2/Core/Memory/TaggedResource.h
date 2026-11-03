#pragma once

/// @file
/// @brief TaggedResource: a memory resource that counts everything allocated through it under one MemTag.
///
/// @code
/// Meow2::Memory::TaggedResource sceneMemory{Meow2::Memory::globalTracker(), Meow2::Memory::MemTag::Scene};
/// std::pmr::vector<int> values{&sceneMemory}; // its buffer shows up under "Scene" in the statistics
/// @endcode
/// @see techstack.md §6, docs/wiki/Memory.md

#include <Meow2/Core/Memory/MemTag.h>

#include <cstddef>
#include <memory_resource>

namespace Meow2::Memory
{
/// @brief Forwards every allocation to an upstream resource inside a MemTagScope of a fixed tag.
///
/// With a TrackingAllocator upstream (e.g. globalTracker()), the blocks are counted under that tag, whichever tag the
/// calling thread has. That's how a long-lived container, such as a Scene's registry, which grows from many places in
/// the code, keeps all its memory under one tag. With any other upstream it just forwards.
/// @par Thread safety
/// As thread-safe as the upstream resource; the tag scope is per thread.
/// @par Lifetime
/// The upstream must outlive the TaggedResource, and the TaggedResource must outlive every block it handed out.
class TaggedResource final : public std::pmr::memory_resource
{
public:
    /// @brief Wraps `upstream`.
    /// @param upstream Where the memory comes from. Must outlive this.
    /// @param tag      The tag every allocation is counted under.
    TaggedResource(std::pmr::memory_resource& upstream, MemTag tag);

    /// @brief The tag allocations are counted under.
    /// @return The tag given at construction.
    [[nodiscard]] MemTag tag() const;

private:
    void* do_allocate(std::size_t bytes, std::size_t alignment) override;
    void do_deallocate(void* pointer, std::size_t bytes, std::size_t alignment) override;
    [[nodiscard]] bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override;

    std::pmr::memory_resource& m_upstream;
    MemTag m_tag;
};
} // namespace Meow2::Memory
