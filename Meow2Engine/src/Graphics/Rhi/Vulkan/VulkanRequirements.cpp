/// @file
/// @brief The GPU requirements check.

#include "Graphics/Rhi/Vulkan/VulkanRequirements.h"

#include <algorithm>
#include <format>

namespace Meow2::Rhi
{
std::vector<std::string> missingVulkanRequirements(const VulkanGpuFacts& facts, bool presentation)
{
    std::vector<std::string> missing;
    if (facts.apiMajor < c_requiredVulkanVersion[0] ||
        (facts.apiMajor == c_requiredVulkanVersion[0] && facts.apiMinor < c_requiredVulkanVersion[1]))
    {
        missing.push_back(std::format("Vulkan {}.{} (it has {}.{}; a driver update may help)",
                                      c_requiredVulkanVersion[0], c_requiredVulkanVersion[1], facts.apiMajor,
                                      facts.apiMinor));
    }
    const auto requireExtension = [&](std::string_view name)
    {
        if (std::ranges::find(facts.extensions, name) == facts.extensions.end())
        {
            missing.emplace_back(name);
        }
    };
    std::ranges::for_each(c_requiredVulkanExtensions, requireExtension);
    if (presentation)
    {
        requireExtension("VK_KHR_swapchain");
    }
    const auto require = [&missing](bool present, std::string_view name)
    {
        if (!present)
        {
            missing.emplace_back(name);
        }
    };
    require(facts.graphicsAndComputeQueue, "a queue for both graphics and compute");
    require(facts.dynamicRendering, "dynamic rendering");
    require(facts.synchronization2, "synchronization2");
    require(facts.demoteToHelperInvocation, "shader demote to helper invocation (discard)");
    require(facts.textureCompressionBc, "BC texture compression");
    require(facts.storageImageWithoutFormat, "storage image writes without a format (RWTexture2D)");
    require(facts.timelineSemaphore, "timeline semaphores");
    require(facts.bufferDeviceAddress, "buffer device address");
    require(facts.bindlessDescriptors, "bindless descriptor indexing");
    require(facts.mutableDescriptorType, "mutable descriptors");
    require(facts.multiDrawIndirect, "multi-draw indirect");
    require(facts.drawIndirectCount, "indirect draw count");
    require(facts.shaderInt64, "64-bit integers in shaders");
    return missing;
}
} // namespace Meow2::Rhi
