/// @file
/// @brief FrameArena: scratch memory that lives for one frame, per thread and per frame in flight.

#pragma once

#include <Meow2/Core/Memory/BlockArena.h>
#include <Meow2/Core/Memory/MemTag.h>

#include <cstdint>
#include <memory>
#include <memory_resource>
#include <vector>

namespace Meow2::Memory
{
/// @brief One BlockArena per frame slot per thread, so a frame's data is allocated with no locks and freed all at once.
///
/// While the GPU works through frame N, the CPU is already filling frame N+1, and frame N's data must stay valid. So
/// there's one set of arenas per frame in flight, and a set is reset only when its slot comes round again, after the
/// GPU has finished that frame.
/// @warning A container must not outlive the reset of the arena it uses. Nothing here runs destructors.
/// @par Thread safety
/// beginFrame() on one thread, with no job using the arena. current() from any thread, each with its own index.
/// @see techstack.md §6
class FrameArena
{
public:
    /// @brief Creates the arenas; no memory is taken until the first allocation.
    /// @param upstream   Where blocks come from. Must outlive the arena.
    /// @param frameSlots Frames in flight; > 0.
    /// @param threads    Threads that allocate, JobSystem::threadCount(); > 0.
    /// @param tag        What the blocks count under.
    FrameArena(std::pmr::memory_resource& upstream, std::uint32_t frameSlots, std::uint32_t threads,
               MemTag tag = MemTag::Renderer);

    /// @brief Starts a frame: resets the slot's arenas, keeping their blocks.
    /// @param frameSlot IRhiDevice::frameSlot(), after IRhiDevice::beginFrame() has waited for the GPU.
    /// @pre `frameSlot` is in range (asserted).
    void beginFrame(std::uint32_t frameSlot);

    /// @brief The calling thread's arena for this frame.
    /// @param threadIndex JobSystem::threadIndex().
    /// @return The arena, a `std::pmr::memory_resource`.
    /// @pre `threadIndex` is in range (asserted).
    [[nodiscard]] BlockArena& current(std::uint32_t threadIndex);

private:
    std::uint32_t m_frameSlots;
    std::uint32_t m_threads;
    std::uint32_t m_slot = 0;
    std::vector<std::unique_ptr<BlockArena>> m_arenas; ///< frameSlots × threads. BlockArena can't move.
};
} // namespace Meow2::Memory
