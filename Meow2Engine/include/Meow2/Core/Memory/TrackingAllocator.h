#pragma once

/// @file
/// @brief TrackingAllocator: counts every allocation per MemTag, reports leaks, and catches heap overruns.
///
/// @code
/// Meow2::Memory::MallocResource mallocResource;
/// Meow2::Memory::TrackingAllocator tracker{mallocResource};
///
/// {
///     const Meow2::Memory::MemTagScope scope{Meow2::Memory::MemTag::Assets};
///     std::pmr::vector<Mesh> meshes{&tracker};               // counted under Assets
/// }
/// Meow2::Memory::BlockArena arena{tracker, Meow2::Memory::MemTag::Renderer}; // its blocks: Renderer
///
/// const Meow2::Memory::MemTagStats renderer = tracker.stats(Meow2::Memory::MemTag::Renderer);
/// if (tracker.hasLeaks())
/// {
///     engineLog.warn("{}", tracker.leakReport());
/// }
/// @endcode
/// @see techstack.md §6, docs/wiki/Memory.md

#include <Meow2/Core/Memory/DebugFill.h>
#include <Meow2/Core/Memory/MemTag.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory_resource>
#include <string>

namespace Meow2::Memory
{
/// @brief Allocation statistics for one MemTag (or all of them). Plain data; a snapshot, not live values.
struct MemTagStats
{
    std::size_t currentBytes = 0;       ///< Bytes allocated and not freed yet (requested sizes, no overhead).
    std::size_t peakBytes = 0;          ///< Highest currentBytes seen since the allocator was created.
    std::size_t liveAllocations = 0;    ///< Allocations not freed yet.
    std::uint64_t totalAllocations = 0; ///< Allocations made since the allocator was created.
};

/// @brief `std::pmr::memory_resource` that wraps an upstream resource and counts every allocation per MemTag.
///
/// Each allocation gets a small header in front (magic value, size, tag, alignment) and 16 guard bytes behind it,
/// both taken from the upstream request; the caller only sees its own bytes.
/// - **Tags:** `allocate()` (and every `std::pmr` container using the tracker) is counted under the calling
///   thread's currentMemTag() (see MemTagScope). allocateTagged() takes an explicit tag, for library allocator hooks.
///   Freeing reads the tag from the header, so a block can be freed on any thread, under any scope.
/// - **Statistics:** stats() per tag and totalStats(): current bytes, peak, live and total allocations.
/// - **Leaks:** hasLeaks() and leakReport() list what is still allocated, per tag.
/// - **Checks** (asserts, Debug and RelWithDebInfo): freeing a pointer this allocator didn't hand out (wrong magic),
///   freeing with a different size than was allocated, and, with debug fill on, overwritten guard bytes (a heap
///   overrun: something wrote past the end of its allocation).
/// - **Debug fill** (DebugFill.h): new memory is filled with `0xCD`, freed memory with `0xDD`, guards with `0xFD`.
///   setDebugFill(false) turns this off, e.g. while profiling.
///
/// Meant for Debug and RelWithDebInfo; Release builds use MallocResource directly (techstack.md §5).
/// @par Thread safety
/// Thread-safe. Allocations and frees from any thread; the counters are `std::atomic`, so there is no lock. stats()
/// may run concurrently with allocations: each value is exact, but values of different fields may be from slightly
/// different moments. setDebugFill() must not race with allocations.
/// @par Lifetime
/// The upstream resource must outlive the tracker, and every allocation must be freed through the tracker before it
/// is destroyed (whatever isn't is a leak; it's never returned upstream).
class TrackingAllocator final : public std::pmr::memory_resource
{
public:
    /// @brief Number of guard bytes behind every allocation (checked on free when debug fill is on).
    static constexpr std::size_t c_guardBytes = 16;

    /// @brief Creates a tracker with all counters at zero.
    /// @param upstream Resource the memory (plus header and guard) comes from, usually a MallocResource. Must outlive
    ///                 the tracker.
    explicit TrackingAllocator(std::pmr::memory_resource& upstream);

    TrackingAllocator(const TrackingAllocator&) = delete;            ///< Not copyable: owns the counters.
    TrackingAllocator& operator=(const TrackingAllocator&) = delete; ///< Not copyable: owns the counters.
    TrackingAllocator(TrackingAllocator&&) = delete;                 ///< Not movable: containers point at it.
    TrackingAllocator& operator=(TrackingAllocator&&) = delete;      ///< Not movable: containers point at it.

    /// @brief Allocates memory counted under an explicit tag, ignoring the current MemTagScope.
    ///
    /// For callers that know their tag, such as library allocator hooks (SDL, Jolt, ImGui). Free it with the normal
    /// `deallocate()`.
    /// @param bytes     Size in bytes (0 is allowed and returns a distinct pointer).
    /// @param alignment Alignment in bytes; a power of two.
    /// @param tag       Tag the allocation is counted under.
    /// @return The memory. Throws `std::bad_alloc` if the upstream resource does.
    [[nodiscard]] void* allocateTagged(std::size_t bytes, std::size_t alignment, MemTag tag);

    /// @brief Statistics for one tag.
    /// @param tag Tag to report; MemTag::Count is not a tag.
    /// @return A snapshot of the tag's counters.
    [[nodiscard]] MemTagStats stats(MemTag tag) const;

    /// @brief Statistics summed over all tags.
    /// @return Total current bytes, live and total allocations; `peakBytes` is the sum of the per-tag peaks (an
    ///         upper bound of the real combined peak, since the tags didn't necessarily peak at the same time).
    [[nodiscard]] MemTagStats totalStats() const;

    /// @brief Whether any allocation is still live.
    /// @return `true` if some tag has live allocations.
    [[nodiscard]] bool hasLeaks() const;

    /// @brief Human-readable list of live allocations per tag, for the log at shutdown.
    /// @return One line per tag with live allocations, e.g. `"Physics: 3 allocations, 1536 bytes"`, after a summary
    ///         line; an empty string when nothing is live.
    [[nodiscard]] std::string leakReport() const;

    /// @brief Turns the debug fill patterns and the guard check on or off for allocations made from now on.
    /// @param enabled `false` skips the fills and guard checks (profiling). Ignored in builds without debug fill.
    void setDebugFill(bool enabled)
    {
        m_debugFill = enabled && c_debugFillAvailable;
    }

    /// @brief Whether new allocations get debug fills and guard checks.
    /// @return `true` if available in this build and not turned off.
    [[nodiscard]] bool debugFill() const
    {
        return m_debugFill;
    }

private:
    /// @brief Live counters of one tag.
    struct TagCounters
    {
        std::atomic<std::size_t> currentBytes{0};       ///< See MemTagStats::currentBytes.
        std::atomic<std::size_t> peakBytes{0};          ///< See MemTagStats::peakBytes.
        std::atomic<std::size_t> liveAllocations{0};    ///< See MemTagStats::liveAllocations.
        std::atomic<std::uint64_t> totalAllocations{0}; ///< See MemTagStats::totalAllocations.
    };

    void* do_allocate(std::size_t bytes, std::size_t alignment) override;
    void do_deallocate(void* pointer, std::size_t bytes, std::size_t alignment) override;
    [[nodiscard]] bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override;

    std::pmr::memory_resource& m_upstream;
    std::array<TagCounters, c_memTagCount> m_counters;
    bool m_debugFill = c_debugFillAvailable;
};
} // namespace Meow2::Memory
