/// @file
/// @brief FrameArena: one set of arenas per frame slot.

#include "Meow2/Core/Memory/FrameArena.h"

#include "Meow2/Core/Assert.h"

namespace Meow2::Memory
{
FrameArena::FrameArena(std::pmr::memory_resource& upstream, std::uint32_t frameSlots, std::uint32_t threads, MemTag tag)
    : m_frameSlots(frameSlots)
    , m_threads(threads)
{
    MEOW2_ASSERT(frameSlots > 0 && threads > 0, "A frame arena needs a slot and a thread");
    m_arenas.reserve(static_cast<std::size_t>(frameSlots) * threads);
    for (std::uint32_t i = 0; i < frameSlots * threads; ++i)
    {
        m_arenas.push_back(std::make_unique<BlockArena>(upstream, tag));
    }
}

void FrameArena::beginFrame(std::uint32_t frameSlot)
{
    MEOW2_ASSERT(frameSlot < m_frameSlots, "Frame slot out of range");
    m_slot = frameSlot;
    for (std::uint32_t thread = 0; thread < m_threads; ++thread)
    {
        m_arenas[static_cast<std::size_t>(frameSlot) * m_threads + thread]->reset();
    }
}

BlockArena& FrameArena::current(std::uint32_t threadIndex)
{
    MEOW2_ASSERT(threadIndex < m_threads, "Thread index out of range");
    return *m_arenas[static_cast<std::size_t>(m_slot) * m_threads + threadIndex];
}
} // namespace Meow2::Memory
