/// @file
/// @brief GPU zones in Tracy. The only file that includes TracyVulkan.hpp.

#include "Graphics/Rhi/Vulkan/RhiGpuProfilerVulkan.h"

#include <tracy/TracyVulkan.hpp>

#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

namespace Meow2::Rhi
{
struct GpuZoneStack::Impl
{
    std::vector<std::unique_ptr<tracy::VkCtxScope>> zones; ///< A stack: zones close in reverse order.
};

GpuProfilerVulkan::GpuProfilerVulkan() = default;

GpuProfilerVulkan::~GpuProfilerVulkan()
{
    disconnect();
}

void GpuProfilerVulkan::connect([[maybe_unused]] VkPhysicalDevice physicalDevice, [[maybe_unused]] VkDevice device,
                                [[maybe_unused]] VkQueue queue, [[maybe_unused]] std::uint32_t queueFamily)
{
#if defined(TRACY_ENABLE)
    // A pool of its own: Tracy begins this buffer repeatedly, which needs the reset flag no frame pool has.
    const VkCommandPoolCreateInfo poolInfo{.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
                                           .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
                                           .queueFamilyIndex = queueFamily};
    VkCommandPool pool = VK_NULL_HANDLE;
    if (vkCreateCommandPool(device, &poolInfo, nullptr, &pool) != VK_SUCCESS)
    {
        return;
    }
    const VkCommandBufferAllocateInfo bufferInfo{.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
                                                 .commandPool = pool,
                                                 .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
                                                 .commandBufferCount = 1};
    VkCommandBuffer commands = VK_NULL_HANDLE;
    if (vkAllocateCommandBuffers(device, &bufferInfo, &commands) == VK_SUCCESS)
    {
        m_context = tracy::CreateVkContext(physicalDevice, device, queue, commands, nullptr, nullptr);
        static_cast<tracy::VkCtx*>(m_context)->Name("Graphics queue", 14);
    }
    vkDestroyCommandPool(device, pool, nullptr);
#endif
}

void GpuProfilerVulkan::disconnect()
{
#if defined(TRACY_ENABLE)
    if (m_context != nullptr)
    {
        tracy::DestroyVkContext(static_cast<tracy::VkCtx*>(m_context));
        m_context = nullptr;
    }
#endif
}

void GpuProfilerVulkan::collect([[maybe_unused]] VkCommandBuffer commands)
{
#if defined(TRACY_ENABLE)
    if (m_context != nullptr)
    {
        static_cast<tracy::VkCtx*>(m_context)->Collect(commands);
    }
#endif
}

GpuZoneStack::GpuZoneStack()
    : m_impl(std::make_unique<Impl>())
{
}

GpuZoneStack::~GpuZoneStack() = default;

void GpuZoneStack::push([[maybe_unused]] const GpuProfilerVulkan& profiler, [[maybe_unused]] VkCommandBuffer commands,
                        [[maybe_unused]] const char* name)
{
#if defined(TRACY_ENABLE)
    if (profiler.context() != nullptr)
    {
        constexpr char c_source[] = "IRhiCommandList";
        constexpr char c_function[] = "beginMarker";
        m_impl->zones.push_back(std::make_unique<tracy::VkCtxScope>(
            static_cast<tracy::VkCtx*>(profiler.context()), std::uint32_t{0}, c_source, sizeof(c_source) - 1,
            c_function, sizeof(c_function) - 1, name, std::strlen(name), commands, true));
    }
#endif
}

void GpuZoneStack::pop()
{
    if (!m_impl->zones.empty())
    {
        m_impl->zones.pop_back();
    }
}
} // namespace Meow2::Rhi
