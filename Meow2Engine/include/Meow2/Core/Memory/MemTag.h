#pragma once

/// @file
/// @brief MemTag: which subsystem a piece of memory belongs to.
///
/// Every engine allocation is attributed to a tag, so memory statistics and leak reports can say *who* uses the
/// memory ("Renderer: 212 MiB, Physics: 40 MiB") instead of only a total. Arenas carry a tag for their blocks; the
/// tracking allocator (later in M1) keeps its counters per tag.
/// @see techstack.md §6

#include <cstddef>
#include <cstdint>
#include <string_view>

/// @brief Engine memory management: allocators, arenas and memory tags.
namespace Meow2::Memory
{
/// @brief The subsystem an allocation is counted under. One value per subsystem.
enum class MemTag : std::uint8_t
{
    General,  ///< Anything without a more specific tag (the default).
    Logging,  ///< Logging (spdlog, sinks, message buffers).
    Config,   ///< Configuration files and values.
    Jobs,     ///< Job system and its task data.
    Events,   ///< Event bus queues.
    Platform, ///< Window, input and OS layer.
    Renderer, ///< Renderer and render graph (CPU side).
    Rhi,      ///< Graphics API backends (CPU side).
    Physics,  ///< Physics engine.
    Assets,   ///< Asset import and registry.
    Editor,   ///< Editor-only data.
    ImGui,    ///< Dear ImGui.
    Profiler, ///< Profiler (Tracy).
    Count     ///< Number of tags; not a tag.
};

/// @brief Number of real tags (MemTag::Count), for arrays indexed by tag.
inline constexpr std::size_t c_memTagCount = static_cast<std::size_t>(MemTag::Count);

/// @brief Human-readable name of a tag, e.g. `"Physics"`, for statistics and leak reports.
/// @param tag Any MemTag value.
/// @return The enumerator's name; `"Unknown"` for values outside the enum.
[[nodiscard]] std::string_view memTagName(MemTag tag);

/// @brief The calling thread's current tag: what an allocation without an explicit tag is counted under.
/// @return The tag of the innermost MemTagScope on this thread, or MemTag::General outside any scope.
[[nodiscard]] MemTag currentMemTag();

/// @brief Sets the calling thread's current MemTag for a scope, and restores the previous one when it ends.
///
/// Allocations that can't pass a tag themselves (`std::pmr` containers over a TrackingAllocator, and later every
/// `new`) are counted under the current tag:
/// @code
/// {
///     const Meow2::Memory::MemTagScope scope{Meow2::Memory::MemTag::Physics};
///     auto world = std::make_unique<PhysicsWorld>(); // counted under Physics (once operator new is tracked)
/// }                                                   // back to the previous tag
/// @endcode
/// @par Thread safety
/// The tag is per thread: a scope affects only the thread that created it. Create and destroy a scope on the same
/// thread, in nested (stack) order.
class MemTagScope
{
public:
    /// @brief Makes @p tag the calling thread's current tag.
    /// @param tag Tag for allocations on this thread until the scope ends.
    explicit MemTagScope(MemTag tag);

    /// @brief Restores the tag that was current before this scope.
    ~MemTagScope();

    MemTagScope(const MemTagScope&) = delete;            ///< Not copyable: one scope, one restore.
    MemTagScope& operator=(const MemTagScope&) = delete; ///< Not copyable: one scope, one restore.

private:
    MemTag m_previous;
};
} // namespace Meow2::Memory
