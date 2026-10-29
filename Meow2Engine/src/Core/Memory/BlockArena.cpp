/// @file
/// @brief BlockArena implementation: intrusive block lists with the block header at the start of each block.

#include "Meow2/Core/Memory/BlockArena.h"

#include "Meow2/Core/Assert.h"

#include <algorithm>
#include <limits>
#include <memory>
#include <new>

namespace Meow2::Memory
{
namespace
{
/// Alignment of the size-class blocks: what `malloc`/`operator new` guarantee, so any upstream serves it cheaply.
constexpr std::size_t c_blockAlignment = alignof(std::max_align_t);

constexpr std::size_t alignUp(std::size_t value, std::size_t alignment)
{
    return (value + alignment - 1) & ~(alignment - 1);
}
} // namespace

/// @brief Header at the start of every block, so the bookkeeping needs no allocations of its own.
struct BlockArena::Block
{
    Block* next;           ///< Next block in the same list, or null.
    std::size_t size;      ///< Total bytes allocated from upstream, header included.
    std::size_t alignment; ///< Alignment the block was allocated with; also the alignment of its data.
    bool inUse;            ///< Dedicated blocks only: holds an allocation since the last reset().

    /// @brief First usable byte, right after the header (rounded up to the block's alignment).
    std::byte* data()
    {
        return reinterpret_cast<std::byte*>(this) + alignUp(sizeof(Block), alignment);
    }

    /// @brief Usable bytes after the header.
    [[nodiscard]] std::size_t capacity() const
    {
        return size - alignUp(sizeof(Block), alignment);
    }
};

BlockArena::BlockArena(std::pmr::memory_resource& upstream, MemTag tag)
    : m_upstream(upstream)
    , m_tag(tag)
    , m_sizeClasses{SizeClass{.blockSize = c_smallBlockSize}, SizeClass{.blockSize = c_mediumBlockSize}}
{
}

BlockArena::~BlockArena()
{
    for (SizeClass& sizeClass : m_sizeClasses)
    {
        while (Block* block = sizeClass.first)
        {
            sizeClass.first = block->next;
            destroyBlock(block);
        }
    }
    while (Block* block = m_dedicated)
    {
        m_dedicated = block->next;
        destroyBlock(block);
    }
}

void BlockArena::reset()
{
    for (SizeClass& sizeClass : m_sizeClasses)
    {
        // A null `current` means "before the first block": the next request starts at `first`.
        sizeClass.current = nullptr;
        sizeClass.cursor = nullptr;
        sizeClass.end = nullptr;
    }
    for (Block* block = m_dedicated; block != nullptr; block = block->next)
    {
        block->inUse = false;
    }
    m_usedBytes = 0;
}

void BlockArena::trim(std::size_t keepBytes)
{
    // Unused dedicated blocks first: they are the largest and the least likely to fit the next request.
    for (Block** link = &m_dedicated; *link != nullptr && m_reservedBytes > keepBytes;)
    {
        Block* block = *link;
        if (block->inUse)
        {
            link = &block->next;
            continue;
        }
        *link = block->next;
        destroyBlock(block);
    }

    // Then the blocks behind the current one of each list (medium before small, larger first). Blocks up to and
    // including `current` may hold live allocations, so they are never freed here.
    for (auto sizeClass = m_sizeClasses.rbegin(); sizeClass != m_sizeClasses.rend(); ++sizeClass)
    {
        Block** link = sizeClass->current != nullptr ? &sizeClass->current->next : &sizeClass->first;
        while (*link != nullptr && m_reservedBytes > keepBytes)
        {
            Block* block = *link;
            *link = block->next;
            destroyBlock(block);
        }
    }
}

void* BlockArena::do_allocate(std::size_t bytes, std::size_t alignment)
{
    MEOW2_ASSERT(alignment != 0 && (alignment & (alignment - 1)) == 0, "alignment must be a power of two");
    const std::size_t requested = std::max<std::size_t>(bytes, 1); // distinct pointers even for 0 bytes

    void* pointer = nullptr;
    if (alignment <= c_maxSharedAlignment && requested <= c_smallRequestLimit)
    {
        pointer = allocateFrom(m_sizeClasses[0], requested, alignment);
    }
    else if (alignment <= c_maxSharedAlignment && requested <= c_mediumRequestLimit)
    {
        pointer = allocateFrom(m_sizeClasses[1], requested, alignment);
    }
    else
    {
        pointer = allocateDedicated(requested, alignment);
    }
    m_usedBytes += bytes;
    return pointer;
}

void BlockArena::do_deallocate(void* /*pointer*/, std::size_t /*bytes*/, std::size_t /*alignment*/)
{
    // Intentionally empty: arena memory comes back all at once in reset().
}

bool BlockArena::do_is_equal(const std::pmr::memory_resource& other) const noexcept
{
    // Memory from one arena can only be "freed" by that arena.
    return this == &other;
}

void* BlockArena::allocateFrom(SizeClass& sizeClass, std::size_t bytes, std::size_t alignment)
{
    // Terminates: a fresh block always fits, because the size-class limits leave room for the header and up to
    // c_maxSharedAlignment - 1 bytes of padding (8 KiB + 4 KiB < 64 KiB, 128 KiB + 4 KiB < 1 MiB).
    for (;;)
    {
        if (sizeClass.current != nullptr)
        {
            void* pointer = sizeClass.cursor;
            std::size_t space = static_cast<std::size_t>(sizeClass.end - sizeClass.cursor);
            if (std::align(alignment, bytes, pointer, space) != nullptr)
            {
                sizeClass.cursor = static_cast<std::byte*>(pointer) + bytes;
                return pointer;
            }
        }

        // Move on to the next block of the list: one kept from before a reset(), or a new one appended at the end.
        Block* next = sizeClass.current != nullptr ? sizeClass.current->next : sizeClass.first;
        if (next == nullptr)
        {
            next = createBlock(sizeClass.blockSize, c_blockAlignment); // may throw; nothing is changed yet
            (sizeClass.current != nullptr ? sizeClass.current->next : sizeClass.first) = next;
        }
        sizeClass.current = next;
        sizeClass.cursor = next->data();
        sizeClass.end = reinterpret_cast<std::byte*>(next) + next->size;
    }
}

void* BlockArena::allocateDedicated(std::size_t bytes, std::size_t alignment)
{
    // Reuse the smallest free dedicated block that fits, so big blocks stay available for big requests.
    Block* best = nullptr;
    for (Block* block = m_dedicated; block != nullptr; block = block->next)
    {
        if (!block->inUse && block->alignment >= alignment && block->capacity() >= bytes &&
            (best == nullptr || block->capacity() < best->capacity()))
        {
            best = block;
        }
    }

    if (best == nullptr)
    {
        const std::size_t blockAlignment = std::max(alignment, c_blockAlignment);
        const std::size_t headerSize = alignUp(sizeof(Block), blockAlignment);
        if (bytes > std::numeric_limits<std::size_t>::max() - headerSize)
        {
            throw std::bad_alloc{};
        }
        best = createBlock(headerSize + bytes, blockAlignment);
        best->next = m_dedicated;
        m_dedicated = best;
    }
    best->inUse = true;
    return best->data();
}

BlockArena::Block* BlockArena::createBlock(std::size_t totalSize, std::size_t alignment)
{
    void* memory = m_upstream.allocate(totalSize, alignment);
    // Placement new only constructs the header in memory that came from upstream; no heap allocation.
    Block* block = ::new (memory) Block{.next = nullptr, .size = totalSize, .alignment = alignment, .inUse = false};
    m_reservedBytes += totalSize;
    ++m_blockCount;
    return block;
}

void BlockArena::destroyBlock(Block* block)
{
    const std::size_t size = block->size;
    const std::size_t alignment = block->alignment;
    block->~Block();
    m_upstream.deallocate(block, size, alignment);
    m_reservedBytes -= size;
    --m_blockCount;
}

} // namespace Meow2::Memory
