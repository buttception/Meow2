/// @file
/// @brief Recording GPU work on Vulkan: rendering, draws, dispatches, barriers and copies.

#include "Graphics/Rhi/Vulkan/RhiDeviceVulkan.h"
#include "Graphics/Rhi/Vulkan/RhiSwapchainVulkan.h"
#include "Meow2/Core/Assert.h"

namespace Meow2::Rhi
{
namespace
{
/// @brief What a resource state means to Vulkan. Plain data.
struct VulkanState
{
    VkPipelineStageFlags2 stages = VK_PIPELINE_STAGE_2_NONE; ///< Which work uses the resource in this state.
    VkAccessFlags2 access = VK_ACCESS_2_NONE;                ///< How it's used.
    VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;        ///< The layout, for textures.
};

/// @brief Translates a state.
/// @param state The state.
/// @return Its Vulkan stages, access and layout.
VulkanState vulkanState(ResourceState state)
{
    constexpr VkPipelineStageFlags2 c_shaders = VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT |
                                                VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT |
                                                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    switch (state)
    {
        case ResourceState::Undefined:
            return {};
        case ResourceState::ColorTarget:
            return {VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                    VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                    VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        case ResourceState::DepthTarget:
            return {VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                    VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                    VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL};
        case ResourceState::ShaderRead:
            return {c_shaders, VK_ACCESS_2_SHADER_READ_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        case ResourceState::ShaderWrite:
            return {c_shaders, VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL};
        case ResourceState::CopySource:
            return {VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_READ_BIT, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL};
        case ResourceState::CopyTarget:
            return {VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL};
        case ResourceState::IndirectArgs:
            return {VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT, VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT,
                    VK_IMAGE_LAYOUT_UNDEFINED};
        case ResourceState::HostRead:
            return {VK_PIPELINE_STAGE_2_HOST_BIT, VK_ACCESS_2_HOST_READ_BIT, VK_IMAGE_LAYOUT_UNDEFINED};
    }
    return {};
}

/// @brief Describes clearing or keeping an attachment.
/// @param clear Whether to clear.
/// @return The load operation.
VkAttachmentLoadOp loadOp(bool clear)
{
    return clear ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
}

/// @brief Builds the region of a buffer ↔ texture copy: the whole first mip level, tightly packed.
/// @param texture The texture.
/// @return The region.
VkBufferImageCopy2 wholeTextureCopy(const TextureVulkan& texture)
{
    return {.sType = VK_STRUCTURE_TYPE_BUFFER_IMAGE_COPY_2,
            .imageSubresource = {.aspectMask = static_cast<VkImageAspectFlags>(
                                     texture.depth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT),
                                 .mipLevel = 0,
                                 .baseArrayLayer = 0,
                                 .layerCount = 1},
            .imageExtent = {texture.extent.width, texture.extent.height, 1}};
}
} // namespace

RhiCommandListVulkan::RhiCommandListVulkan(RhiDeviceVulkan& device)
    : m_device(device)
{
}

void RhiCommandListVulkan::startRendering(const VkRenderingAttachmentInfo& color,
                                          const VkRenderingAttachmentInfo* depth, VkExtent2D extent)
{
    MEOW2_ASSERT(!m_rendering, "endRendering() wasn't called");
    const VkRenderingInfo rendering{.sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
                                    .renderArea = {.offset = {0, 0}, .extent = extent},
                                    .layerCount = 1,
                                    .colorAttachmentCount = 1,
                                    .pColorAttachments = &color,
                                    .pDepthAttachment = depth};
    vkCmdBeginRendering(m_commandBuffer, &rendering);
    m_rendering = true;

    // A negative height flips Y, so both backends share one set of matrices (techstack.md §10, Math::flipForVulkan).
    const VkViewport viewport{.x = 0.0F,
                              .y = static_cast<float>(extent.height),
                              .width = static_cast<float>(extent.width),
                              .height = -static_cast<float>(extent.height),
                              .minDepth = 0.0F,
                              .maxDepth = 1.0F};
    const VkRect2D scissor{.offset = {0, 0}, .extent = extent};
    vkCmdSetViewport(m_commandBuffer, 0, 1, &viewport);
    vkCmdSetScissor(m_commandBuffer, 0, 1, &scissor);
}

void RhiCommandListVulkan::beginRendering(IRhiSwapchain& swapchain, std::optional<Math::Color> clear)
{
    beginRendering(swapchain, clear, TextureHandle{}, std::nullopt);
}

void RhiCommandListVulkan::beginRendering(IRhiSwapchain& swapchain, std::optional<Math::Color> clear,
                                          TextureHandle depth, std::optional<float> clearDepth)
{
    auto& target = static_cast<RhiSwapchainVulkan&>(swapchain);
    MEOW2_ASSERT(target.isAcquired(), "acquire() must return true before drawing to a swapchain");
    VkRenderingAttachmentInfo attachment{.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
                                         .imageView = target.imageView(),
                                         .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                         .loadOp = loadOp(clear.has_value()),
                                         .storeOp = VK_ATTACHMENT_STORE_OP_STORE};
    if (clear)
    {
        attachment.clearValue.color = {{clear->r, clear->g, clear->b, clear->a}};
    }
    VkRenderingAttachmentInfo depthAttachment{};
    if (depth.isSet())
    {
        depthAttachment = {.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
                           .imageView = m_device.texture(depth).view,
                           .imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
                           .loadOp = loadOp(clearDepth.has_value()),
                           .storeOp = VK_ATTACHMENT_STORE_OP_STORE};
        depthAttachment.clearValue.depthStencil = {.depth = clearDepth.value_or(0.0F), .stencil = 0};
    }
    startRendering(attachment, depth.isSet() ? &depthAttachment : nullptr, target.extent());
}

void RhiCommandListVulkan::beginRendering(const RenderTargets& targets)
{
    const TextureVulkan color = m_device.texture(targets.color);
    VkRenderingAttachmentInfo attachment{.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
                                         .imageView = color.view,
                                         .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                         .loadOp = loadOp(targets.clearColor.has_value()),
                                         .storeOp = VK_ATTACHMENT_STORE_OP_STORE};
    if (targets.clearColor)
    {
        const Math::Color& clear = *targets.clearColor;
        attachment.clearValue.color = {{clear.r, clear.g, clear.b, clear.a}};
    }
    VkRenderingAttachmentInfo depth{};
    if (targets.depth.isSet())
    {
        depth = {.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
                 .imageView = m_device.texture(targets.depth).view,
                 .imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
                 .loadOp = loadOp(targets.clearDepth.has_value()),
                 .storeOp = VK_ATTACHMENT_STORE_OP_STORE};
        depth.clearValue.depthStencil = {.depth = targets.clearDepth.value_or(0.0F), .stencil = 0};
    }
    startRendering(attachment, targets.depth.isSet() ? &depth : nullptr, color.extent);
}

void RhiCommandListVulkan::endRendering()
{
    MEOW2_ASSERT(m_rendering, "beginRendering() wasn't called");
    vkCmdEndRendering(m_commandBuffer);
    m_rendering = false;
}

void RhiCommandListVulkan::setPipeline(PipelineHandle pipeline)
{
    const PipelineVulkan target = m_device.pipeline(pipeline);
    vkCmdBindPipeline(m_commandBuffer, target.bindPoint, target.pipeline);
}

void RhiCommandListVulkan::beginMarker(const char* name)
{
    if (m_device.hasDebugUtils())
    {
        const VkDebugUtilsLabelEXT label{.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT, .pLabelName = name};
        vkCmdBeginDebugUtilsLabelEXT(m_commandBuffer, &label);
    }
    m_gpuZones.push(m_device.gpuProfiler(), m_commandBuffer, name);
    ++m_openMarkers;
}

void RhiCommandListVulkan::endMarker()
{
    MEOW2_ASSERT(m_openMarkers > 0, "endMarker() without beginMarker()");
    m_gpuZones.pop();
    if (m_device.hasDebugUtils())
    {
        vkCmdEndDebugUtilsLabelEXT(m_commandBuffer);
    }
    --m_openMarkers;
}

void RhiCommandListVulkan::setPushConstants(const Shader::PushConstants& constants)
{
    vkCmdPushConstants(m_commandBuffer, m_device.pipelineLayout(), VK_SHADER_STAGE_ALL, 0, sizeof(constants),
                       &constants);
}

void RhiCommandListVulkan::draw(std::uint32_t vertexCount, std::uint32_t instanceCount)
{
    MEOW2_ASSERT(m_rendering, "draw() outside beginRendering()");
    vkCmdDraw(m_commandBuffer, vertexCount, instanceCount, 0, 0);
}

void RhiCommandListVulkan::drawIndirectCount(BufferHandle draws, BufferHandle count, std::uint32_t maxDraws)
{
    MEOW2_ASSERT(m_rendering, "drawIndirectCount() outside beginRendering()");
    vkCmdDrawIndirectCount(m_commandBuffer, m_device.buffer(draws).buffer, 0, m_device.buffer(count).buffer, 0,
                           maxDraws, sizeof(Shader::IndirectDraw));
}

void RhiCommandListVulkan::fillBuffer(BufferHandle buffer, std::uint32_t value)
{
    MEOW2_ASSERT(!m_rendering, "fillBuffer() inside beginRendering()");
    vkCmdFillBuffer(m_commandBuffer, m_device.buffer(buffer).buffer, 0, VK_WHOLE_SIZE, value);
}

void RhiCommandListVulkan::dispatch(std::uint32_t x, std::uint32_t y, std::uint32_t z)
{
    MEOW2_ASSERT(!m_rendering, "dispatch() inside beginRendering()");
    vkCmdDispatch(m_commandBuffer, x, y, z);
}

void RhiCommandListVulkan::barrier(BufferHandle buffer, ResourceState from, ResourceState to)
{
    MEOW2_ASSERT(!m_rendering, "Barriers go between passes");
    const VulkanState source = vulkanState(from);
    const VulkanState target = vulkanState(to);
    const VkBufferMemoryBarrier2 barrier{.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
                                         .srcStageMask = source.stages,
                                         .srcAccessMask = source.access,
                                         .dstStageMask = target.stages,
                                         .dstAccessMask = target.access,
                                         .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                         .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                         .buffer = m_device.buffer(buffer).buffer,
                                         .offset = 0,
                                         .size = VK_WHOLE_SIZE};
    const VkDependencyInfo dependency{
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO, .bufferMemoryBarrierCount = 1, .pBufferMemoryBarriers = &barrier};
    vkCmdPipelineBarrier2(m_commandBuffer, &dependency);
}

void RhiCommandListVulkan::barrier(TextureHandle texture, ResourceState from, ResourceState to)
{
    MEOW2_ASSERT(!m_rendering, "Barriers go between passes");
    const TextureVulkan image = m_device.texture(texture);
    const VulkanState source = vulkanState(from);
    const VulkanState target = vulkanState(to);
    const VkImageMemoryBarrier2 barrier{
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcStageMask = source.stages,
        .srcAccessMask = source.access,
        .dstStageMask = target.stages,
        .dstAccessMask = target.access,
        .oldLayout = source.layout,
        .newLayout = target.layout,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = image.image,
        .subresourceRange = {.aspectMask = static_cast<VkImageAspectFlags>(image.depth ? VK_IMAGE_ASPECT_DEPTH_BIT
                                                                                       : VK_IMAGE_ASPECT_COLOR_BIT),
                             .baseMipLevel = 0,
                             .levelCount = image.mipLevels,
                             .baseArrayLayer = 0,
                             .layerCount = 1}};
    const VkDependencyInfo dependency{
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO, .imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &barrier};
    vkCmdPipelineBarrier2(m_commandBuffer, &dependency);
}

void RhiCommandListVulkan::copyBuffer(BufferHandle source, BufferHandle target, std::uint64_t size,
                                      std::uint64_t sourceOffset, std::uint64_t targetOffset)
{
    const VkBufferCopy region{.srcOffset = sourceOffset, .dstOffset = targetOffset, .size = size};
    vkCmdCopyBuffer(m_commandBuffer, m_device.buffer(source).buffer, m_device.buffer(target).buffer, 1, &region);
}

void RhiCommandListVulkan::copyBufferToTexture(BufferHandle source, TextureHandle target)
{
    const TextureVulkan texture = m_device.texture(target);
    const VkBufferImageCopy2 region = wholeTextureCopy(texture);
    const VkCopyBufferToImageInfo2 copy{.sType = VK_STRUCTURE_TYPE_COPY_BUFFER_TO_IMAGE_INFO_2,
                                        .srcBuffer = m_device.buffer(source).buffer,
                                        .dstImage = texture.image,
                                        .dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                        .regionCount = 1,
                                        .pRegions = &region};
    vkCmdCopyBufferToImage2(m_commandBuffer, &copy);
}

void RhiCommandListVulkan::copyTextureToBuffer(TextureHandle source, BufferHandle target)
{
    const TextureVulkan texture = m_device.texture(source);
    const VkBufferImageCopy2 region = wholeTextureCopy(texture);
    const VkCopyImageToBufferInfo2 copy{.sType = VK_STRUCTURE_TYPE_COPY_IMAGE_TO_BUFFER_INFO_2,
                                        .srcImage = texture.image,
                                        .srcImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                        .dstBuffer = m_device.buffer(target).buffer,
                                        .regionCount = 1,
                                        .pRegions = &region};
    vkCmdCopyImageToBuffer2(m_commandBuffer, &copy);
}
} // namespace Meow2::Rhi
