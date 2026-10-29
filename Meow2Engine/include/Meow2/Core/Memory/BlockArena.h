#pragma once

/// @file
/// @brief BlockArena: a bump allocator for short-lived data, usable by every `std::pmr` container.
///
/// An **arena** hands out memory by moving a pointer forward through large blocks it got from its upstream resource.
/// Individual frees do nothing; all memory is reclaimed at once by reset(), which keeps the blocks, so a warmed-up
/// arena never calls its upstream again. That makes allocation a few instructions, and it can't fragment.
///
/// @code
/// Meow2::Memory::BlockArena arena{*std::pmr::new_delete_resource(), Meow2::Memory::MemTag::Renderer};
///
/// {
///     std::pmr::vector<DrawItem> drawItems{&arena}; // any std::pmr container can use the arena
///     drawItems.reserve(itemCount);                 // reserve: a growing vector leaves its old buffers behind
///     // ... fill and use drawItems ...
/// }                                                 // containers gone: nothing may use the memory after reset
/// arena.reset();                                    // all memory back at once, blocks kept for the next frame
/// @endcode
/// @see techstack.md §6, docs/wiki/Memory.md

#include <Meow2/Core/Memory/MemTag.h>

#include <array>
#include <cstddef>
#include <memory_resource>

namespace Meow2::Memory
{
/// @brief Arena that hands out memory by bumping a pointer through size-class block lists.
///
/// Requests are sorted into size classes, so the unused tail of a block stays small:
/// | Request (bytes, alignment)        | Served from                                   |
/// |-----------------------------------|-----------------------------------------------|
/// | ≤ 8 KiB, alignment ≤ 4 KiB        | 64 KiB blocks                                 |
/// | ≤ 128 KiB, alignment ≤ 4 KiB      | 1 MiB blocks                                  |
/// | anything larger                   | a dedicated block of exactly that size        |
///
/// - `deallocate` is a no-op. Memory comes back only through reset() (or the destructor).
/// - reset() rewinds every list to its first block and keeps all blocks. Dedicated blocks are kept too and reused
///   for later large requests that fit (smallest fitting block first).
/// - trim() gives unused blocks back to the upstream resource, e.g. after a one-off spike such as a level load.
/// - Upstream failures (`std::bad_alloc`) propagate to the caller; the arena stays usable.
///
/// The blocks are labelled with a MemTag. Once the tracking allocator exists (later in M1), it counts them under
/// that tag.
/// @par Thread safety
/// Not thread-safe. Use one arena per thread (see FrameArena, M5), or synchronise externally.
/// @par Lifetime
/// The upstream resource must outlive the arena. Every container using the arena must be destroyed, or never touch
/// its memory again, before reset(), trim() frees what it used, or the arena is destroyed.
class BlockArena final : public std::pmr::memory_resource
{
public:
    /// @brief Size of the blocks for small requests, in bytes (64 KiB).
    static constexpr std::size_t c_smallBlockSize = std::size_t{64} * 1024;
    /// @brief Largest request served from small blocks, in bytes (8 KiB).
    static constexpr std::size_t c_smallRequestLimit = std::size_t{8} * 1024;
    /// @brief Size of the blocks for medium requests, in bytes (1 MiB).
    static constexpr std::size_t c_mediumBlockSize = std::size_t{1024} * 1024;
    /// @brief Largest request served from medium blocks, in bytes (128 KiB).
    static constexpr std::size_t c_mediumRequestLimit = std::size_t{128} * 1024;
    /// @brief Largest alignment served from the shared blocks, in bytes; larger alignments get a dedicated block.
    static constexpr std::size_t c_maxSharedAlignment = 4096;

    /// @brief Creates an empty arena. No memory is allocated until the first request.
    /// @param upstream Resource the blocks are allocated from. Must outlive the arena.
    /// @param tag      Memory tag the blocks are counted under.
    BlockArena(std::pmr::memory_resource& upstream, MemTag tag);

    /// @brief Returns every block to the upstream resource.
    ~BlockArena() override;

    BlockArena(const BlockArena&) = delete;            ///< Not copyable: owns its blocks.
    BlockArena& operator=(const BlockArena&) = delete; ///< Not copyable: owns its blocks.
    BlockArena(BlockArena&&) = delete;                 ///< Not movable: containers point at it.
    BlockArena& operator=(BlockArena&&) = delete;      ///< Not movable: containers point at it.

    /// @brief Makes all memory available again. Keeps every block for reuse, so no upstream calls are made.
    /// @warning Every container using this arena must be gone, or never touch its memory again, afterwards.
    void reset();

    /// @brief Returns unused blocks to the upstream resource until at most @p keepBytes stay reserved.
    ///
    /// Only blocks that hold no allocation since the last reset() are freed: unused dedicated blocks first, then the
    /// blocks behind the current one in each size class. Called right after reset(), every block is unused, so
    /// `trim(0)` then frees everything.
    /// @param keepBytes Upper bound on reservedBytes() afterwards, in bytes, as far as unused blocks allow. A good
    ///                  value is what a typical cycle needs, e.g. a recent maximum of usedBytes().
    void trim(std::size_t keepBytes);

    /// @brief Memory tag the blocks are counted under.
    /// @return The tag given to the constructor.
    [[nodiscard]] MemTag tag() const
    {
        return m_tag;
    }

    /// @brief Total size of all blocks currently owned, in bytes (block headers included).
    /// @return What the arena holds from upstream right now.
    [[nodiscard]] std::size_t reservedBytes() const
    {
        return m_reservedBytes;
    }

    /// @brief Bytes handed out since the last reset(), in bytes (requested sizes, without alignment padding).
    /// @return The sum of the sizes of all allocations since the last reset().
    [[nodiscard]] std::size_t usedBytes() const
    {
        return m_usedBytes;
    }

    /// @brief Number of blocks currently owned (all size classes and dedicated blocks).
    /// @return Block count.
    [[nodiscard]] std::size_t blockCount() const
    {
        return m_blockCount;
    }

private:
    struct Block;

    /// @brief One size class: a singly linked list of equal-sized blocks and a bump cursor in the current one.
    struct SizeClass
    {
        std::size_t blockSize;       ///< Bytes per block, header included.
        Block* first = nullptr;      ///< First block of the list, or null.
        Block* current = nullptr;    ///< Block being bumped through; null right after reset().
        std::byte* cursor = nullptr; ///< Next free byte in `current`.
        std::byte* end = nullptr;    ///< One past the last usable byte of `current`.
    };

    void* do_allocate(std::size_t bytes, std::size_t alignment) override;
    void do_deallocate(void* pointer, std::size_t bytes, std::size_t alignment) override;
    [[nodiscard]] bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override;

    void* allocateFrom(SizeClass& sizeClass, std::size_t bytes, std::size_t alignment);
    void* allocateDedicated(std::size_t bytes, std::size_t alignment);
    Block* createBlock(std::size_t totalSize, std::size_t alignment);
    void destroyBlock(Block* block);

    std::pmr::memory_resource& m_upstream;
    MemTag m_tag;
    std::array<SizeClass, 2> m_sizeClasses; ///< [0] small, [1] medium.
    Block* m_dedicated = nullptr;           ///< Dedicated blocks, in use or free.
    std::size_t m_reservedBytes = 0;
    std::size_t m_usedBytes = 0;
    std::size_t m_blockCount = 0;
};
} // namespace Meow2::Memory
