/// @file
/// @brief GPU zones in Tracy, behind a header with no Tracy in it. Without TRACY_ENABLE everything here does nothing.

#pragma once

#include <volk.h>

#include <cstdint>
#include <memory>

namespace Meow2::Rhi
{
/// @brief Tracy's connection to one queue: it owns the query pool that timestamps go to.
/// @par Thread safety
/// collect() on one thread, with no frame recording in that list yet. Zones may open on any thread.
class GpuProfilerVulkan
{
public:
    GpuProfilerVulkan();
    ~GpuProfilerVulkan();

    GpuProfilerVulkan(const GpuProfilerVulkan&) = delete;            ///< Not copyable: owns a Tracy context.
    GpuProfilerVulkan& operator=(const GpuProfilerVulkan&) = delete; ///< Not copyable: owns a Tracy context.

    /// @brief Connects to the queue. Runs a short submit to line GPU time up with CPU time.
    /// @param physicalDevice The GPU.
    /// @param device         The device.
    /// @param queue          The queue. Idle.
    /// @param queueFamily    Its family.
    void connect(VkPhysicalDevice physicalDevice, VkDevice device, VkQueue queue, std::uint32_t queueFamily);

    /// @brief Disconnects. Call before the device goes away.
    void disconnect();

    /// @brief Hands finished timestamps to Tracy.
    /// @param commands A command buffer that's recording, outside any rendering.
    void collect(VkCommandBuffer commands);

    /// @brief Tracy's context, for GpuZoneStack.
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
class GpuZoneStack
{
public:
    GpuZoneStack();
    ~GpuZoneStack();

    GpuZoneStack(const GpuZoneStack&) = delete;            ///< Not copyable: holds open zones.
    GpuZoneStack& operator=(const GpuZoneStack&) = delete; ///< Not copyable: holds open zones.

    /// @brief Opens a zone.
    /// @param profiler The profiler.
    /// @param commands The command buffer.
    /// @param name     The name; copied.
    void push(const GpuProfilerVulkan& profiler, VkCommandBuffer commands, const char* name);

    /// @brief Closes the zone opened last.
    void pop();

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
} // namespace Meow2::Rhi
