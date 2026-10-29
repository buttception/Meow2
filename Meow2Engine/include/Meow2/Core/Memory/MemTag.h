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
} // namespace Meow2::Memory
