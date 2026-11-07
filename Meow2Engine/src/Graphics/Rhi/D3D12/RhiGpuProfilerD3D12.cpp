/// @file
/// @brief GPU zones in Tracy for D3D12. The only file that includes TracyD3D12.hpp.

#include "Graphics/Rhi/D3D12/RhiGpuProfilerD3D12.h"

#include <tracy/TracyD3D12.hpp>

#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

namespace Meow2::Rhi
{
struct GpuZoneStackD3D12::Impl
{
#if defined(TRACY_ENABLE)
    std::vector<std::unique_ptr<tracy::D3D12ZoneScope>> zones; ///< A stack: zones close in reverse order.
#endif
};

GpuProfilerD3D12::GpuProfilerD3D12() = default;

GpuProfilerD3D12::~GpuProfilerD3D12()
{
    disconnect();
}

void GpuProfilerD3D12::connect([[maybe_unused]] ID3D12Device* device, [[maybe_unused]] ID3D12CommandQueue* queue)
{
#if defined(TRACY_ENABLE)
    auto* context = tracy::CreateD3D12Context(device, queue);
    constexpr char c_name[] = "Graphics queue";
    context->Name(c_name, sizeof(c_name) - 1);
    m_context = context;
#endif
}

void GpuProfilerD3D12::disconnect()
{
#if defined(TRACY_ENABLE)
    if (m_context != nullptr)
    {
        tracy::DestroyD3D12Context(static_cast<tracy::D3D12QueueCtx*>(m_context));
        m_context = nullptr;
    }
#endif
}

void GpuProfilerD3D12::collect()
{
#if defined(TRACY_ENABLE)
    if (m_context != nullptr)
    {
        static_cast<tracy::D3D12QueueCtx*>(m_context)->Collect();
    }
#endif
}

GpuZoneStackD3D12::GpuZoneStackD3D12()
    : m_impl(std::make_unique<Impl>())
{
}

GpuZoneStackD3D12::~GpuZoneStackD3D12() = default;

void GpuZoneStackD3D12::push([[maybe_unused]] const GpuProfilerD3D12& profiler,
                             [[maybe_unused]] ID3D12GraphicsCommandList* list, [[maybe_unused]] const char* name)
{
#if defined(TRACY_ENABLE)
    if (profiler.context() != nullptr)
    {
        constexpr char c_source[] = "IRhiCommandList";
        constexpr char c_function[] = "beginMarker";
        m_impl->zones.push_back(std::make_unique<tracy::D3D12ZoneScope>(
            static_cast<tracy::D3D12QueueCtx*>(profiler.context()), std::uint32_t{0}, c_source, sizeof(c_source) - 1,
            c_function, sizeof(c_function) - 1, name, std::strlen(name), list, true));
    }
#endif
}

void GpuZoneStackD3D12::pop()
{
#if defined(TRACY_ENABLE)
    if (!m_impl->zones.empty())
    {
        m_impl->zones.pop_back();
    }
#endif
}
} // namespace Meow2::Rhi
