/// @file
/// @brief Profiling over the Tracy client's C API (the only file that includes Tracy). No-ops without profiling.

#include "Meow2/Core/Memory/MemTag.h"
#include "Meow2/Core/Memory/TrackingAllocator.h"
#include "Meow2/Core/Profiling/Profile.h"

#if MEOW2_PROFILING
#include <tracy/TracyC.h>

#include <array>
#include <cstring>
#include <type_traits>
#endif

namespace Meow2::Profiling
{
#if MEOW2_PROFILING
namespace
{
// The zone macros hand Tracy our SourceLocation as its own source-location struct, so the two must stay identical.
static_assert(std::is_layout_compatible_v<SourceLocation, ___tracy_source_location_data>,
              "SourceLocation must match Tracy's ___tracy_source_location_data");

/// @brief One plot per memory tag. Tracy identifies a plot by the name's address, so these are fixed strings.
constexpr std::array<const char*, Memory::c_memTagCount> c_memoryPlotNames{
    "Memory/General",  "Memory/Logging",  "Memory/Config",  "Memory/Jobs",    "Memory/Events",
    "Memory/Platform", "Memory/Renderer", "Memory/Rhi",     "Memory/Physics", "Memory/Assets",
    "Memory/Editor",   "Memory/ImGui",    "Memory/Profiler"};
static_assert(c_memoryPlotNames.back() != nullptr, "one plot name per MemTag");
} // namespace

ScopedZone::ScopedZone(const SourceLocation& location) noexcept
{
    static_assert(sizeof(TracyCZoneCtx) <= sizeof(m_context) && alignof(TracyCZoneCtx) <= 8,
                  "Tracy's zone context doesn't fit ScopedZone's storage");
    const TracyCZoneCtx context =
        ___tracy_emit_zone_begin(reinterpret_cast<const ___tracy_source_location_data*>(&location), 1);
    std::memcpy(m_context, &context, sizeof context);
}

ScopedZone::~ScopedZone()
{
    TracyCZoneCtx context;
    std::memcpy(&context, m_context, sizeof context);
    ___tracy_emit_zone_end(context);
}

void frameMark() noexcept
{
    ___tracy_emit_frame_mark(nullptr);
}

void setThreadName(const char* name) noexcept
{
    ___tracy_set_thread_name(name);
}

void plotMemory(const Memory::TrackingAllocator& tracker) noexcept
{
    for (std::size_t i = 0; i < Memory::c_memTagCount; ++i)
    {
        const Memory::MemTagStats stats = tracker.stats(static_cast<Memory::MemTag>(i));
        ___tracy_emit_plot(c_memoryPlotNames[i], static_cast<double>(stats.currentBytes));
    }
}
#else
ScopedZone::ScopedZone(const SourceLocation& /*location*/) noexcept
    : m_context{}
{
}

ScopedZone::~ScopedZone() = default;

void frameMark() noexcept {}

void setThreadName(const char* /*name*/) noexcept {}

void plotMemory(const Memory::TrackingAllocator& /*tracker*/) noexcept {}
#endif
} // namespace Meow2::Profiling
