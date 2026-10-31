/// @file
/// @brief TrackingAllocator implementation: a header in front of every allocation, guard bytes behind it, and
///        lock-free per-tag counters.

#include "Meow2/Core/Memory/TrackingAllocator.h"

#include "Meow2/Core/Assert.h"

#include <algorithm>
#include <cstring>
#include <format>
#include <iterator>
#include <limits>
#include <new>

namespace Meow2::Memory
{
namespace
{
/// Marks a live allocation of a TrackingAllocator; anything else in front of a freed pointer is a bug.
constexpr std::uint64_t c_liveMagic = 0x4D656F7732416C6CULL; // "Meow2All"
/// Written over the magic on free, so a second free of the same pointer is caught (while the memory isn't reused).
constexpr std::uint64_t c_freedMagic = 0x4D656F7732467265ULL; // "Meow2Fre"

/// @brief Bookkeeping stored directly in front of the pointer handed to the caller.
struct AllocationHeader
{
    std::uint64_t magic;           ///< c_liveMagic while allocated.
    std::size_t size;              ///< Bytes requested by the caller.
    std::size_t offset;            ///< Distance from the upstream block start to the caller's pointer.
    std::size_t upstreamAlignment; ///< Alignment the upstream block was requested with.
    MemTag tag;                    ///< Tag the allocation is counted under.
    bool guarded;                  ///< Guard bytes were written (debug fill was on) and must be checked.
};

constexpr std::size_t alignUp(std::size_t value, std::size_t alignment)
{
    return (value + alignment - 1) & ~(alignment - 1);
}

AllocationHeader* headerOf(void* pointer)
{
    return reinterpret_cast<AllocationHeader*>(static_cast<std::byte*>(pointer) - sizeof(AllocationHeader));
}

std::size_t tagIndex(MemTag tag)
{
    MEOW2_ASSERT(static_cast<std::size_t>(tag) < c_memTagCount, "MemTag::Count is not a tag");
    return std::min(static_cast<std::size_t>(tag), c_memTagCount - 1);
}
} // namespace

TrackingAllocator::TrackingAllocator(std::pmr::memory_resource& upstream)
    : m_upstream(upstream)
{
}

void* TrackingAllocator::allocateTagged(std::size_t bytes, std::size_t alignment, MemTag tag)
{
    MEOW2_ASSERT(alignment != 0 && (alignment & (alignment - 1)) == 0, "alignment must be a power of two");

    // Layout of the upstream block: [padding][header][caller's bytes][guard]. The header sits right in front of the
    // caller's pointer, so deallocate finds it without a lookup.
    const std::size_t upstreamAlignment = std::max(alignment, alignof(AllocationHeader));
    const std::size_t offset = alignUp(sizeof(AllocationHeader), upstreamAlignment);
    if (bytes > std::numeric_limits<std::size_t>::max() - offset - c_guardBytes)
    {
        throw std::bad_alloc{};
    }
    auto* block = static_cast<std::byte*>(m_upstream.allocate(offset + bytes + c_guardBytes, upstreamAlignment));

    std::byte* pointer = block + offset;
    // Placement new only constructs the header inside memory that came from upstream.
    ::new (headerOf(pointer)) AllocationHeader{.magic = c_liveMagic,
                                               .size = bytes,
                                               .offset = offset,
                                               .upstreamAlignment = upstreamAlignment,
                                               .tag = tag,
                                               .guarded = m_debugFill};
    if (m_debugFill)
    {
        std::memset(pointer, std::to_integer<int>(c_allocatedFill), bytes);
        std::memset(pointer + bytes, std::to_integer<int>(c_guardFill), c_guardBytes);
    }

    TagCounters& counters = m_counters[tagIndex(tag)];
    const std::size_t current = counters.currentBytes.fetch_add(bytes) + bytes;
    std::size_t peak = counters.peakBytes.load();
    while (current > peak && !counters.peakBytes.compare_exchange_weak(peak, current))
    {
        // compare_exchange_weak reloaded `peak`; retry until ours is not higher or it's stored.
    }
    ++counters.liveAllocations;
    ++counters.totalAllocations;
    return pointer;
}

void* TrackingAllocator::do_allocate(std::size_t bytes, std::size_t alignment)
{
    return allocateTagged(bytes, alignment, currentMemTag());
}

void TrackingAllocator::do_deallocate(void* pointer, std::size_t bytes, std::size_t /*alignment*/)
{
    AllocationHeader* header = headerOf(pointer);
    if (header->magic != c_liveMagic)
    {
        // Not ours, or freed twice. Returning it upstream would corrupt the heap, so it's leaked instead.
        MEOW2_ASSERT(false, "freeing a pointer this TrackingAllocator didn't allocate, or freeing it twice");
        return;
    }
    MEOW2_ASSERT(header->size == bytes, "deallocate size differs from the allocated size");

    auto* data = static_cast<std::byte*>(pointer);
    const std::size_t size = header->size;
    if (header->guarded)
    {
        const bool intact =
            std::all_of(data + size, data + size + c_guardBytes, [](std::byte value) { return value == c_guardFill; });
        MEOW2_ASSERT(intact, "heap overrun: the guard bytes behind this allocation were overwritten");
    }

    TagCounters& counters = m_counters[tagIndex(header->tag)];
    counters.currentBytes -= size;
    --counters.liveAllocations;

    const std::size_t offset = header->offset;
    const std::size_t upstreamAlignment = header->upstreamAlignment;
    header->magic = c_freedMagic;
    if (m_debugFill)
    {
        std::memset(data, std::to_integer<int>(c_freedFill), size);
    }
    m_upstream.deallocate(data - offset, offset + size + c_guardBytes, upstreamAlignment);
}

bool TrackingAllocator::do_is_equal(const std::pmr::memory_resource& other) const noexcept
{
    return this == &other;
}

MemTagStats TrackingAllocator::stats(MemTag tag) const
{
    const TagCounters& counters = m_counters[tagIndex(tag)];
    return MemTagStats{.currentBytes = counters.currentBytes.load(),
                       .peakBytes = counters.peakBytes.load(),
                       .liveAllocations = counters.liveAllocations.load(),
                       .totalAllocations = counters.totalAllocations.load()};
}

MemTagStats TrackingAllocator::totalStats() const
{
    MemTagStats total;
    for (std::size_t i = 0; i < c_memTagCount; ++i)
    {
        const MemTagStats tag = stats(static_cast<MemTag>(i));
        total.currentBytes += tag.currentBytes;
        total.peakBytes += tag.peakBytes;
        total.liveAllocations += tag.liveAllocations;
        total.totalAllocations += tag.totalAllocations;
    }
    return total;
}

bool TrackingAllocator::hasLeaks() const
{
    return totalStats().liveAllocations != 0;
}

std::string TrackingAllocator::leakReport() const
{
    const MemTagStats total = totalStats();
    if (total.liveAllocations == 0)
    {
        return {};
    }
    std::string report = std::format("Memory leaks: {} allocations, {} bytes still allocated", total.liveAllocations,
                                     total.currentBytes);
    for (std::size_t i = 0; i < c_memTagCount; ++i)
    {
        const auto tag = static_cast<MemTag>(i);
        const MemTagStats tagStats = stats(tag);
        if (tagStats.liveAllocations != 0)
        {
            std::format_to(std::back_inserter(report), "\n  {}: {} allocations, {} bytes", memTagName(tag),
                           tagStats.liveAllocations, tagStats.currentBytes);
        }
    }
    return report;
}
} // namespace Meow2::Memory
