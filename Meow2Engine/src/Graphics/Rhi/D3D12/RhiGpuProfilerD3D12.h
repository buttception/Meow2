/// @file
/// @brief GPU zones in Tracy for D3D12, behind a header with no Tracy in it. Without TRACY_ENABLE everything here does
///        nothing.

#pragma once

#include <d3d12.h>

#include <memory>

namespace Meow2::Rhi
{
/// @brief Tracy's connection to one queue: it owns the query heap that timestamps go to.
/// @par Thread safety
/// collect() on the thread that submits. Zones may open on any thread.
class GpuProfilerD3D12
{
public:
    GpuProfilerD3D12();
    ~GpuProfilerD3D12();

    GpuProfilerD3D12(const GpuProfilerD3D12&) = delete;            ///< Not copyable: owns a Tracy context.
    GpuProfilerD3D12& operator=(const GpuProfilerD3D12&) = delete; ///< Not copyable: owns a Tracy context.

    /// @brief Connects to the queue.
    /// @param device The device.
    /// @param queue  The direct queue.
    void connect(ID3D12Device* device, ID3D12CommandQueue* queue);

    /// @brief Disconnects. Call before the device goes away, with the GPU idle.
    void disconnect();

    /// @brief Hands finished timestamps to Tracy.
    void collect();

    /// @brief Tracy's context, for GpuZoneStackD3D12.
    /// @return The context; null when Tracy is off.
    [[nodiscard]] void* context() const
    {
        return m_context;
    }

private:
    void* m_context = nullptr;
};

/// @brief The GPU zones open in one command list.
/// @par Thread safety
/// Not thread-safe; belongs to the thread recording its list.
class GpuZoneStackD3D12
{
public:
    GpuZoneStackD3D12();
    ~GpuZoneStackD3D12();

    GpuZoneStackD3D12(const GpuZoneStackD3D12&) = delete;            ///< Not copyable: holds open zones.
    GpuZoneStackD3D12& operator=(const GpuZoneStackD3D12&) = delete; ///< Not copyable: holds open zones.

    /// @brief Opens a zone.
    /// @param profiler The profiler.
    /// @param list     The command list.
    /// @param name     The name; copied.
    void push(const GpuProfilerD3D12& profiler, ID3D12GraphicsCommandList* list, const char* name);

    /// @brief Closes the zone opened last.
    void pop();

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
} // namespace Meow2::Rhi
