/// @file
/// @brief TaggedResource (TaggedResource.h).

#include "Meow2/Core/Memory/TaggedResource.h"

namespace Meow2::Memory
{
TaggedResource::TaggedResource(std::pmr::memory_resource& upstream, MemTag tag)
    : m_upstream(upstream)
    , m_tag(tag)
{
}

MemTag TaggedResource::tag() const
{
    return m_tag;
}

void* TaggedResource::do_allocate(std::size_t bytes, std::size_t alignment)
{
    const MemTagScope scope{m_tag}; // a TrackingAllocator upstream counts the block under the current tag
    return m_upstream.allocate(bytes, alignment);
}

void TaggedResource::do_deallocate(void* pointer, std::size_t bytes, std::size_t alignment)
{
    m_upstream.deallocate(pointer, bytes, alignment); // the tracker remembers each block's tag in its header
}

bool TaggedResource::do_is_equal(const std::pmr::memory_resource& other) const noexcept
{
    return this == &other;
}
} // namespace Meow2::Memory
