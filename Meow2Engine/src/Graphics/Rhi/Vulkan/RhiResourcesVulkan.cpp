/// @file
/// @brief Resources on Vulkan: buffers, textures, samplers, pipelines, the heaps and deferred destruction.

#include "Graphics/Rhi/Vulkan/RhiDeviceVulkan.h"
#include "Meow2/Core/Assert.h"
#include "Meow2/Core/Memory/MemTag.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <format>
#include <iterator>
#include <mutex>
#include <string>
#include <utility>

namespace Meow2::Rhi
{
namespace
{
// Slots of each heap. One million descriptors would cost tens of MB before anything uses them; these are enough for
// M7's sample scenes and can grow with the first scene that needs it.
constexpr std::uint32_t c_resourceHeapSize = 1U << 16U;
constexpr std::uint32_t c_samplerHeapSize = 256;

// Every descriptor type a resource heap slot can hold (VK_EXT_mutable_descriptor_type). DXC emits one view of the same
// binding per type a shader uses, which Vulkan only allows on a mutable binding.
constexpr std::array<VkDescriptorType, 3> c_heapTypes{
    VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER};

/// @brief Creates a shader module from bytecode.
/// @param device The device.
/// @param code   SPIR-V, aligned to 4 bytes.
/// @return The module, or null if Vulkan refused it.
VkShaderModule makeModule(VkDevice device, std::span<const std::byte> code)
{
    const VkShaderModuleCreateInfo info{.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
                                        .codeSize = code.size(),
                                        .pCode = reinterpret_cast<const std::uint32_t*>(code.data())};
    VkShaderModule module = VK_NULL_HANDLE;
    vkCreateShaderModule(device, &info, nullptr, &module);
    return module;
}

/// @brief Whether saved cache data comes from this GPU and driver. Vulkan says to ignore foreign data, but a bad header
///        has crashed drivers before, and it's only 32 bytes to check.
/// @param data       The saved bytes.
/// @param properties This GPU.
/// @return True if it can be passed to the driver.
bool cacheMatches(std::span<const std::byte> data, const VkPhysicalDeviceProperties& properties)
{
    VkPipelineCacheHeaderVersionOne header{};
    if (data.size() < sizeof(header))
    {
        return false;
    }
    std::memcpy(&header, data.data(), sizeof(header));
    return header.headerVersion == VK_PIPELINE_CACHE_HEADER_VERSION_ONE && header.vendorID == properties.vendorID &&
           header.deviceID == properties.deviceID &&
           std::memcmp(header.pipelineCacheUUID, properties.pipelineCacheUUID, VK_UUID_SIZE) == 0;
}
} // namespace

void HeapIndexAllocator::reset(std::uint32_t first, std::uint32_t count)
{
    m_next = first;
    m_end = count;
    m_free.clear();
}

std::uint32_t HeapIndexAllocator::allocate()
{
    if (!m_free.empty())
    {
        const std::uint32_t index = m_free.back();
        m_free.pop_back();
        return index;
    }
    return m_next < m_end ? m_next++ : 0;
}

void HeapIndexAllocator::release(std::uint32_t index)
{
    m_free.push_back(index);
}

VkFormat vulkanFormat(RhiFormat format)
{
    switch (format)
    {
        case RhiFormat::Unknown:
            return VK_FORMAT_UNDEFINED;
        case RhiFormat::Bgra8Srgb:
            return VK_FORMAT_B8G8R8A8_SRGB;
        case RhiFormat::Rgba8Srgb:
            return VK_FORMAT_R8G8B8A8_SRGB;
        case RhiFormat::Rgba8Unorm:
            return VK_FORMAT_R8G8B8A8_UNORM;
        case RhiFormat::Rgba16Float:
            return VK_FORMAT_R16G16B16A16_SFLOAT;
        case RhiFormat::R32Float:
            return VK_FORMAT_R32_SFLOAT;
        case RhiFormat::D32Float:
            return VK_FORMAT_D32_SFLOAT;
    }
    return VK_FORMAT_UNDEFINED;
}

Result<void> RhiDeviceVulkan::createBindless(const RhiDeviceDesc& desc)
{
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(m_physicalDevice, &properties);
    m_maxAnisotropy = properties.limits.maxSamplerAnisotropy;

    const VkMutableDescriptorTypeListEXT mutableList{
        .descriptorTypeCount = static_cast<std::uint32_t>(c_heapTypes.size()), .pDescriptorTypes = c_heapTypes.data()};
    const std::array<VkMutableDescriptorTypeListEXT, 2> mutableLists{VkMutableDescriptorTypeListEXT{}, mutableList};
    const VkMutableDescriptorTypeCreateInfoEXT mutableInfo{
        .sType = VK_STRUCTURE_TYPE_MUTABLE_DESCRIPTOR_TYPE_CREATE_INFO_EXT,
        .mutableDescriptorTypeListCount = static_cast<std::uint32_t>(mutableLists.size()),
        .pMutableDescriptorTypeLists = mutableLists.data()};

    const std::array<VkDescriptorSetLayoutBinding, 2> bindings{
        VkDescriptorSetLayoutBinding{.binding = 0,
                                     .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER,
                                     .descriptorCount = c_samplerHeapSize,
                                     .stageFlags = VK_SHADER_STAGE_ALL},
        VkDescriptorSetLayoutBinding{.binding = 1,
                                     .descriptorType = VK_DESCRIPTOR_TYPE_MUTABLE_EXT,
                                     .descriptorCount = c_resourceHeapSize,
                                     .stageFlags = VK_SHADER_STAGE_ALL}};
    // Partially bound: most slots are empty. Update after bind: new resources appear while frames are in flight.
    const VkDescriptorBindingFlags heapFlags = VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT |
                                               VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT |
                                               VK_DESCRIPTOR_BINDING_UPDATE_UNUSED_WHILE_PENDING_BIT;
    const std::array<VkDescriptorBindingFlags, 2> bindingFlags{heapFlags, heapFlags};
    const VkDescriptorSetLayoutBindingFlagsCreateInfo flagsInfo{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO,
        .pNext = &mutableInfo,
        .bindingCount = static_cast<std::uint32_t>(bindingFlags.size()),
        .pBindingFlags = bindingFlags.data()};
    const VkDescriptorSetLayoutCreateInfo layoutInfo{.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
                                                     .pNext = &flagsInfo,
                                                     .flags =
                                                         VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT,
                                                     .bindingCount = static_cast<std::uint32_t>(bindings.size()),
                                                     .pBindings = bindings.data()};
    if (const VkResult result = vkCreateDescriptorSetLayout(m_device, &layoutInfo, nullptr, &m_heapLayout);
        result != VK_SUCCESS)
    {
        return fail(ErrorCode::Unknown, std::format("Heap layout: {}", vulkanResultName(result)));
    }

    const std::array<VkDescriptorPoolSize, 2> sizes{
        VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_SAMPLER, c_samplerHeapSize},
        VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_MUTABLE_EXT, c_resourceHeapSize}};
    const VkDescriptorPoolCreateInfo poolInfo{.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
                                              .pNext = &mutableInfo,
                                              .flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT,
                                              .maxSets = 1,
                                              .poolSizeCount = static_cast<std::uint32_t>(sizes.size()),
                                              .pPoolSizes = sizes.data()};
    if (const VkResult result = vkCreateDescriptorPool(m_device, &poolInfo, nullptr, &m_heapPool); result != VK_SUCCESS)
    {
        return fail(ErrorCode::OutOfMemory, std::format("Heap pool: {}", vulkanResultName(result)));
    }
    const VkDescriptorSetAllocateInfo setInfo{.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
                                              .descriptorPool = m_heapPool,
                                              .descriptorSetCount = 1,
                                              .pSetLayouts = &m_heapLayout};
    if (const VkResult result = vkAllocateDescriptorSets(m_device, &setInfo, &m_heapSet); result != VK_SUCCESS)
    {
        return fail(ErrorCode::OutOfMemory, std::format("Heap set: {}", vulkanResultName(result)));
    }
    setObjectName(VK_OBJECT_TYPE_DESCRIPTOR_SET, reinterpret_cast<std::uint64_t>(m_heapSet), "Bindless heap");

    const VkPushConstantRange push{
        .stageFlags = VK_SHADER_STAGE_ALL, .offset = 0, .size = sizeof(Shader::PushConstants)};
    const VkPipelineLayoutCreateInfo pipelineLayout{.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
                                                    .setLayoutCount = 1,
                                                    .pSetLayouts = &m_heapLayout,
                                                    .pushConstantRangeCount = 1,
                                                    .pPushConstantRanges = &push};
    if (const VkResult result = vkCreatePipelineLayout(m_device, &pipelineLayout, nullptr, &m_pipelineLayout);
        result != VK_SUCCESS)
    {
        return fail(ErrorCode::Unknown, std::format("Pipeline layout: {}", vulkanResultName(result)));
    }

    VkPipelineCacheCreateInfo cacheInfo{.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO};
    if (cacheMatches(desc.pipelineCache, properties))
    {
        cacheInfo.initialDataSize = desc.pipelineCache.size();
        cacheInfo.pInitialData = desc.pipelineCache.data();
    }
    else if (!desc.pipelineCache.empty())
    {
        MEOW2_LOG_INFO(m_log, "Ignoring the saved pipeline cache: it's from another GPU or driver");
    }
    if (const VkResult result = vkCreatePipelineCache(m_device, &cacheInfo, nullptr, &m_pipelineCache);
        result != VK_SUCCESS)
    {
        return fail(ErrorCode::Unknown, std::format("Pipeline cache: {}", vulkanResultName(result)));
    }

    m_resourceSlots.reset(c_invalidBindlessIndex + 1, c_resourceHeapSize);
    m_samplerSlots.reset(c_defaultSamplerIndex, c_samplerHeapSize);
    Result<SamplerVulkan> sampler = makeSampler({.name = "Default sampler"});
    if (!sampler)
    {
        return Unexpected<Error>{sampler.error()};
    }
    MEOW2_ASSERT(sampler->heapIndex == c_defaultSamplerIndex, "The first sampler takes the default slot");
    m_defaultSampler = sampler->sampler;
    return {};
}

void RhiDeviceVulkan::deferDestroy(std::function<void()> destroyLater)
{
    // Work recorded so far is covered by the current value; a frame still recording ends with the next one.
    const std::uint64_t value = m_timelineValue.load() + (m_inFrame.load() ? 1 : 0);
    const std::scoped_lock lock{m_resourceMutex};
    m_pendingDestroys.push_back({.value = value, .destroy = std::move(destroyLater)});
}

void RhiDeviceVulkan::collectGarbage()
{
    std::uint64_t reached = 0;
    vkGetSemaphoreCounterValue(m_device, m_timeline, &reached);
    std::vector<PendingDestroy> ready;
    {
        const std::scoped_lock lock{m_resourceMutex};
        const auto firstLater = std::ranges::stable_partition(m_pendingDestroys, [reached](const PendingDestroy& item)
                                                              { return item.value <= reached; });
        ready.assign(std::make_move_iterator(m_pendingDestroys.begin()), std::make_move_iterator(firstLater.begin()));
        m_pendingDestroys.erase(m_pendingDestroys.begin(), firstLater.begin());
    }
    // Without the lock: the callbacks take it to give back heap slots.
    for (const PendingDestroy& item : ready)
    {
        item.destroy();
    }
}

void RhiDeviceVulkan::writeBufferDescriptor(std::uint32_t index, VkBuffer buffer)
{
    const VkDescriptorBufferInfo info{.buffer = buffer, .offset = 0, .range = VK_WHOLE_SIZE};
    const VkWriteDescriptorSet write{.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                                     .dstSet = m_heapSet,
                                     .dstBinding = 1,
                                     .dstArrayElement = index,
                                     .descriptorCount = 1,
                                     .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                                     .pBufferInfo = &info};
    vkUpdateDescriptorSets(m_device, 1, &write, 0, nullptr);
}

void RhiDeviceVulkan::writeImageDescriptor(std::uint32_t index, VkImageView view, VkDescriptorType type)
{
    const VkDescriptorImageInfo info{.imageView = view,
                                     .imageLayout = type == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE
                                                        ? VK_IMAGE_LAYOUT_GENERAL
                                                        : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    const VkWriteDescriptorSet write{.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                                     .dstSet = m_heapSet,
                                     .dstBinding = 1,
                                     .dstArrayElement = index,
                                     .descriptorCount = 1,
                                     .descriptorType = type,
                                     .pImageInfo = &info};
    vkUpdateDescriptorSets(m_device, 1, &write, 0, nullptr);
}

Result<BufferHandle> RhiDeviceVulkan::createBuffer(const BufferDesc& desc)
{
    MEOW2_ASSERT(desc.size > 0, "A buffer needs a size");
    const Memory::MemTagScope tag{Memory::MemTag::Rhi};
    const VkBufferCreateInfo info{.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                                  .size = desc.size,
                                  .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
                                           VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
                                           VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT |
                                           VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                                  .sharingMode = VK_SHARING_MODE_EXCLUSIVE};
    VmaAllocationCreateInfo allocation{};
    allocation.usage = VMA_MEMORY_USAGE_AUTO;
    if (desc.memory == RhiMemory::Upload)
    {
        allocation.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
    }
    else if (desc.memory == RhiMemory::Readback)
    {
        allocation.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
    }
    if (desc.memory != RhiMemory::GpuOnly)
    {
        allocation.requiredFlags = VK_MEMORY_PROPERTY_HOST_COHERENT_BIT; // no flush or invalidate calls to forget
    }

    BufferVulkan record;
    VmaAllocationInfo allocationInfo{};
    if (const VkResult result =
            vmaCreateBuffer(m_allocator, &info, &allocation, &record.buffer, &record.allocation, &allocationInfo);
        result != VK_SUCCESS)
    {
        return fail(ErrorCode::OutOfMemory, std::format("Buffer '{}': {}", desc.name, vulkanResultName(result)));
    }
    record.mapped = static_cast<std::byte*>(allocationInfo.pMappedData);
    record.tag = desc.tag;
    record.bytes = allocationInfo.size;
    m_bytesByTag[static_cast<std::size_t>(record.tag)] += record.bytes;
    record.size = desc.size;
    setObjectName(VK_OBJECT_TYPE_BUFFER, reinterpret_cast<std::uint64_t>(record.buffer), desc.name.c_str());

    {
        const std::scoped_lock lock{m_resourceMutex};
        record.heapIndex = m_resourceSlots.allocate();
    }
    if (record.heapIndex == c_invalidBindlessIndex)
    {
        vmaDestroyBuffer(m_allocator, record.buffer, record.allocation);
        return fail(ErrorCode::OutOfMemory, "The bindless heap is full");
    }
    writeBufferDescriptor(record.heapIndex, record.buffer);

    const std::scoped_lock lock{m_resourceMutex};
    return m_buffers.create(record);
}

void RhiDeviceVulkan::destroy(BufferHandle buffer)
{
    BufferVulkan record;
    {
        const std::scoped_lock lock{m_resourceMutex};
        const BufferVulkan* found = m_buffers.get(buffer);
        if (found == nullptr)
        {
            return;
        }
        record = *found;
        m_buffers.destroy(buffer);
    }
    deferDestroy(
        [this, record]
        {
            vmaDestroyBuffer(m_allocator, record.buffer, record.allocation);
            m_bytesByTag[static_cast<std::size_t>(record.tag)] -= record.bytes;
            const std::scoped_lock lock{m_resourceMutex};
            m_resourceSlots.release(record.heapIndex);
        });
}

std::span<std::byte> RhiDeviceVulkan::mappedMemory(BufferHandle buffer)
{
    const std::shared_lock lock{m_resourceMutex};
    const BufferVulkan* found = m_buffers.get(buffer);
    if (found == nullptr || found->mapped == nullptr)
    {
        return {};
    }
    return {found->mapped, static_cast<std::size_t>(found->size)};
}

std::uint32_t RhiDeviceVulkan::bindlessIndex(BufferHandle buffer)
{
    const std::shared_lock lock{m_resourceMutex};
    const BufferVulkan* found = m_buffers.get(buffer);
    return found != nullptr ? found->heapIndex : c_invalidBindlessIndex;
}

BufferVulkan RhiDeviceVulkan::buffer(BufferHandle handle)
{
    const std::shared_lock lock{m_resourceMutex};
    const BufferVulkan* found = m_buffers.get(handle);
    MEOW2_ASSERT(found != nullptr, "Stale buffer handle");
    return found != nullptr ? *found : BufferVulkan{};
}

Result<TextureHandle> RhiDeviceVulkan::createTexture(const TextureDesc& desc)
{
    MEOW2_ASSERT(desc.width > 0 && desc.height > 0 && desc.mipLevels > 0, "A texture needs a size");
    const Memory::MemTagScope tag{Memory::MemTag::Rhi};
    const bool depth = desc.format == RhiFormat::D32Float;
    TextureVulkan record;
    record.format = vulkanFormat(desc.format);
    record.extent = {desc.width, desc.height};
    record.mipLevels = desc.mipLevels;
    record.depth = depth;

    VkImageUsageFlags usage =
        VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    if (desc.renderTarget)
    {
        usage |= depth ? VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT : VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    }
    if (desc.storage)
    {
        usage |= VK_IMAGE_USAGE_STORAGE_BIT;
    }
    const VkImageCreateInfo info{.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
                                 .imageType = VK_IMAGE_TYPE_2D,
                                 .format = record.format,
                                 .extent = {desc.width, desc.height, 1},
                                 .mipLevels = desc.mipLevels,
                                 .arrayLayers = 1,
                                 .samples = VK_SAMPLE_COUNT_1_BIT,
                                 .tiling = VK_IMAGE_TILING_OPTIMAL,
                                 .usage = usage,
                                 .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
                                 .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED};
    VmaAllocationCreateInfo allocation{};
    allocation.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    VmaAllocationInfo allocationInfo{};
    if (const VkResult result =
            vmaCreateImage(m_allocator, &info, &allocation, &record.image, &record.allocation, &allocationInfo);
        result != VK_SUCCESS)
    {
        return fail(ErrorCode::OutOfMemory, std::format("Texture '{}': {}", desc.name, vulkanResultName(result)));
    }
    record.tag = desc.tag;
    record.bytes = allocationInfo.size;
    m_bytesByTag[static_cast<std::size_t>(record.tag)] += record.bytes;
    setObjectName(VK_OBJECT_TYPE_IMAGE, reinterpret_cast<std::uint64_t>(record.image), desc.name.c_str());

    const VkImageViewCreateInfo viewInfo{
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = record.image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = record.format,
        .subresourceRange = {.aspectMask = static_cast<VkImageAspectFlags>(depth ? VK_IMAGE_ASPECT_DEPTH_BIT
                                                                                 : VK_IMAGE_ASPECT_COLOR_BIT),
                             .baseMipLevel = 0,
                             .levelCount = desc.mipLevels,
                             .baseArrayLayer = 0,
                             .layerCount = 1}};
    if (const VkResult result = vkCreateImageView(m_device, &viewInfo, nullptr, &record.view); result != VK_SUCCESS)
    {
        vmaDestroyImage(m_allocator, record.image, record.allocation);
        return fail(ErrorCode::Unknown, std::format("Texture view '{}': {}", desc.name, vulkanResultName(result)));
    }

    {
        const std::scoped_lock lock{m_resourceMutex};
        record.heapIndex = m_resourceSlots.allocate();
        record.storageIndex = desc.storage ? m_resourceSlots.allocate() : c_invalidBindlessIndex;
    }
    if (record.heapIndex == c_invalidBindlessIndex || (desc.storage && record.storageIndex == c_invalidBindlessIndex))
    {
        vkDestroyImageView(m_device, record.view, nullptr);
        vmaDestroyImage(m_allocator, record.image, record.allocation);
        return fail(ErrorCode::OutOfMemory, "The bindless heap is full");
    }
    writeImageDescriptor(record.heapIndex, record.view, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE);
    if (desc.storage)
    {
        writeImageDescriptor(record.storageIndex, record.view, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
    }

    const std::scoped_lock lock{m_resourceMutex};
    return m_textures.create(record);
}

void RhiDeviceVulkan::destroy(TextureHandle texture)
{
    TextureVulkan record;
    {
        const std::scoped_lock lock{m_resourceMutex};
        const TextureVulkan* found = m_textures.get(texture);
        if (found == nullptr)
        {
            return;
        }
        record = *found;
        m_textures.destroy(texture);
    }
    deferDestroy(
        [this, record]
        {
            vkDestroyImageView(m_device, record.view, nullptr);
            vmaDestroyImage(m_allocator, record.image, record.allocation);
            m_bytesByTag[static_cast<std::size_t>(record.tag)] -= record.bytes;
            const std::scoped_lock lock{m_resourceMutex};
            m_resourceSlots.release(record.heapIndex);
            if (record.storageIndex != c_invalidBindlessIndex)
            {
                m_resourceSlots.release(record.storageIndex);
            }
        });
}

std::uint32_t RhiDeviceVulkan::bindlessIndex(TextureHandle texture)
{
    const std::shared_lock lock{m_resourceMutex};
    const TextureVulkan* found = m_textures.get(texture);
    return found != nullptr ? found->heapIndex : c_invalidBindlessIndex;
}

std::uint32_t RhiDeviceVulkan::bindlessStorageIndex(TextureHandle texture)
{
    const std::shared_lock lock{m_resourceMutex};
    const TextureVulkan* found = m_textures.get(texture);
    return found != nullptr ? found->storageIndex : c_invalidBindlessIndex;
}

TextureVulkan RhiDeviceVulkan::texture(TextureHandle handle)
{
    const std::shared_lock lock{m_resourceMutex};
    const TextureVulkan* found = m_textures.get(handle);
    MEOW2_ASSERT(found != nullptr, "Stale texture handle");
    return found != nullptr ? *found : TextureVulkan{};
}

Result<SamplerVulkan> RhiDeviceVulkan::makeSampler(const SamplerDesc& desc)
{
    const bool linear = desc.filter == SamplerFilter::Linear;
    const VkFilter filter = linear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
    const VkSamplerAddressMode address =
        desc.address == SamplerAddress::Repeat ? VK_SAMPLER_ADDRESS_MODE_REPEAT : VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    const float anisotropy = std::clamp(desc.maxAnisotropy, 1.0F, m_maxAnisotropy);
    const VkSamplerCreateInfo info{.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
                                   .magFilter = filter,
                                   .minFilter = filter,
                                   .mipmapMode =
                                       linear ? VK_SAMPLER_MIPMAP_MODE_LINEAR : VK_SAMPLER_MIPMAP_MODE_NEAREST,
                                   .addressModeU = address,
                                   .addressModeV = address,
                                   .addressModeW = address,
                                   .anisotropyEnable = anisotropy > 1.0F ? VK_TRUE : VK_FALSE,
                                   .maxAnisotropy = anisotropy,
                                   .maxLod = VK_LOD_CLAMP_NONE};
    SamplerVulkan record;
    if (const VkResult result = vkCreateSampler(m_device, &info, nullptr, &record.sampler); result != VK_SUCCESS)
    {
        return fail(ErrorCode::Unknown, std::format("Sampler '{}': {}", desc.name, vulkanResultName(result)));
    }
    setObjectName(VK_OBJECT_TYPE_SAMPLER, reinterpret_cast<std::uint64_t>(record.sampler), desc.name.c_str());

    {
        const std::scoped_lock lock{m_resourceMutex};
        // Slot 0 is a valid sampler, so "full" is checked by the first call succeeding and later ones not returning 0.
        record.heapIndex = m_samplerSlots.allocate();
    }
    if (record.heapIndex == 0 && m_defaultSampler != VK_NULL_HANDLE)
    {
        vkDestroySampler(m_device, record.sampler, nullptr);
        return fail(ErrorCode::OutOfMemory, "The sampler heap is full");
    }
    const VkDescriptorImageInfo image{.sampler = record.sampler};
    const VkWriteDescriptorSet write{.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                                     .dstSet = m_heapSet,
                                     .dstBinding = 0,
                                     .dstArrayElement = record.heapIndex,
                                     .descriptorCount = 1,
                                     .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER,
                                     .pImageInfo = &image};
    vkUpdateDescriptorSets(m_device, 1, &write, 0, nullptr);
    return record;
}

Result<SamplerHandle> RhiDeviceVulkan::createSampler(const SamplerDesc& desc)
{
    const Memory::MemTagScope tag{Memory::MemTag::Rhi};
    Result<SamplerVulkan> record = makeSampler(desc);
    if (!record)
    {
        return Unexpected<Error>{record.error()};
    }
    const std::scoped_lock lock{m_resourceMutex};
    return m_samplers.create(*record);
}

void RhiDeviceVulkan::destroy(SamplerHandle sampler)
{
    SamplerVulkan record;
    {
        const std::scoped_lock lock{m_resourceMutex};
        const SamplerVulkan* found = m_samplers.get(sampler);
        if (found == nullptr)
        {
            return;
        }
        record = *found;
        m_samplers.destroy(sampler);
    }
    deferDestroy(
        [this, record]
        {
            vkDestroySampler(m_device, record.sampler, nullptr);
            const std::scoped_lock lock{m_resourceMutex};
            m_samplerSlots.release(record.heapIndex);
        });
}

std::uint32_t RhiDeviceVulkan::bindlessIndex(SamplerHandle sampler)
{
    const std::shared_lock lock{m_resourceMutex};
    const SamplerVulkan* found = m_samplers.get(sampler);
    return found != nullptr ? found->heapIndex : c_defaultSamplerIndex;
}

Result<PipelineHandle> RhiDeviceVulkan::createGraphicsPipeline(const GraphicsPipelineDesc& desc)
{
    const Memory::MemTagScope tag{Memory::MemTag::Rhi};
    const VkShaderModule vertex = makeModule(m_device, desc.vertexCode);
    const VkShaderModule pixel = makeModule(m_device, desc.pixelCode);
    if (vertex == VK_NULL_HANDLE || pixel == VK_NULL_HANDLE)
    {
        vkDestroyShaderModule(m_device, vertex, nullptr);
        vkDestroyShaderModule(m_device, pixel, nullptr);
        return fail(ErrorCode::InvalidArgument, std::format("Pipeline '{}': bad shader bytecode", desc.name));
    }
    const std::array<VkPipelineShaderStageCreateInfo, 2> stages{
        VkPipelineShaderStageCreateInfo{.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                                        .stage = VK_SHADER_STAGE_VERTEX_BIT,
                                        .module = vertex,
                                        .pName = desc.vertexEntry.c_str()},
        VkPipelineShaderStageCreateInfo{.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                                        .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
                                        .module = pixel,
                                        .pName = desc.pixelEntry.c_str()}};

    const VkFormat colorFormat = vulkanFormat(desc.colorFormat);
    const VkPipelineRenderingCreateInfo rendering{.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
                                                  .colorAttachmentCount = 1,
                                                  .pColorAttachmentFormats = &colorFormat,
                                                  .depthAttachmentFormat = vulkanFormat(desc.depthFormat)};
    const VkPipelineVertexInputStateCreateInfo vertexInput{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO}; // shaders pull their own vertices
    const VkPipelineInputAssemblyStateCreateInfo assembly{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST};
    const VkPipelineViewportStateCreateInfo viewport{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO, .viewportCount = 1, .scissorCount = 1};
    // Clockwise, as in D3D: the flipped viewport keeps screen positions the same on both backends.
    const VkPipelineRasterizationStateCreateInfo rasterization{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .cullMode =
            static_cast<VkCullModeFlags>(desc.cull == CullMode::Back ? VK_CULL_MODE_BACK_BIT : VK_CULL_MODE_NONE),
        .frontFace = VK_FRONT_FACE_CLOCKWISE,
        .lineWidth = 1.0F};
    const VkPipelineMultisampleStateCreateInfo multisample{.sType =
                                                               VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
                                                           .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT};
    const bool hasDepth = desc.depthFormat != RhiFormat::Unknown;
    const VkPipelineDepthStencilStateCreateInfo depth{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable = hasDepth ? VK_TRUE : VK_FALSE,
        .depthWriteEnable = hasDepth && desc.depthWrite ? VK_TRUE : VK_FALSE,
        .depthCompareOp = VK_COMPARE_OP_GREATER_OR_EQUAL}; // reversed Z
    const VkPipelineColorBlendAttachmentState blendAttachment{.colorWriteMask =
                                                                  VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                                                  VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT};
    const VkPipelineColorBlendStateCreateInfo blend{.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
                                                    .attachmentCount = 1,
                                                    .pAttachments = &blendAttachment};
    const std::array<VkDynamicState, 2> dynamicStates{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    const VkPipelineDynamicStateCreateInfo dynamic{.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
                                                   .dynamicStateCount =
                                                       static_cast<std::uint32_t>(dynamicStates.size()),
                                                   .pDynamicStates = dynamicStates.data()};
    const VkGraphicsPipelineCreateInfo info{.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
                                            .pNext = &rendering,
                                            .stageCount = static_cast<std::uint32_t>(stages.size()),
                                            .pStages = stages.data(),
                                            .pVertexInputState = &vertexInput,
                                            .pInputAssemblyState = &assembly,
                                            .pViewportState = &viewport,
                                            .pRasterizationState = &rasterization,
                                            .pMultisampleState = &multisample,
                                            .pDepthStencilState = &depth,
                                            .pColorBlendState = &blend,
                                            .pDynamicState = &dynamic,
                                            .layout = m_pipelineLayout};
    PipelineVulkan record;
    const VkResult result = vkCreateGraphicsPipelines(m_device, m_pipelineCache, 1, &info, nullptr, &record.pipeline);
    vkDestroyShaderModule(m_device, vertex, nullptr); // the pipeline holds its own compiled code
    vkDestroyShaderModule(m_device, pixel, nullptr);
    if (result != VK_SUCCESS)
    {
        return fail(ErrorCode::Unknown, std::format("Pipeline '{}': {}", desc.name, vulkanResultName(result)));
    }
    setObjectName(VK_OBJECT_TYPE_PIPELINE, reinterpret_cast<std::uint64_t>(record.pipeline), desc.name.c_str());
    const std::scoped_lock lock{m_resourceMutex};
    return m_pipelines.create(record);
}

Result<PipelineHandle> RhiDeviceVulkan::createComputePipeline(const ComputePipelineDesc& desc)
{
    const Memory::MemTagScope tag{Memory::MemTag::Rhi};
    const VkShaderModule shader = makeModule(m_device, desc.code);
    if (shader == VK_NULL_HANDLE)
    {
        return fail(ErrorCode::InvalidArgument, std::format("Pipeline '{}': bad shader bytecode", desc.name));
    }
    const VkComputePipelineCreateInfo info{.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
                                           .stage = {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                                                     .stage = VK_SHADER_STAGE_COMPUTE_BIT,
                                                     .module = shader,
                                                     .pName = desc.entry.c_str()},
                                           .layout = m_pipelineLayout};
    PipelineVulkan record{.bindPoint = VK_PIPELINE_BIND_POINT_COMPUTE};
    const VkResult result = vkCreateComputePipelines(m_device, m_pipelineCache, 1, &info, nullptr, &record.pipeline);
    vkDestroyShaderModule(m_device, shader, nullptr);
    if (result != VK_SUCCESS)
    {
        return fail(ErrorCode::Unknown, std::format("Pipeline '{}': {}", desc.name, vulkanResultName(result)));
    }
    setObjectName(VK_OBJECT_TYPE_PIPELINE, reinterpret_cast<std::uint64_t>(record.pipeline), desc.name.c_str());
    const std::scoped_lock lock{m_resourceMutex};
    return m_pipelines.create(record);
}

void RhiDeviceVulkan::destroy(PipelineHandle pipeline)
{
    PipelineVulkan record;
    {
        const std::scoped_lock lock{m_resourceMutex};
        const PipelineVulkan* found = m_pipelines.get(pipeline);
        if (found == nullptr)
        {
            return;
        }
        record = *found;
        m_pipelines.destroy(pipeline);
    }
    deferDestroy([this, record] { vkDestroyPipeline(m_device, record.pipeline, nullptr); });
}

PipelineVulkan RhiDeviceVulkan::pipeline(PipelineHandle handle)
{
    const std::shared_lock lock{m_resourceMutex};
    const PipelineVulkan* found = m_pipelines.get(handle);
    MEOW2_ASSERT(found != nullptr, "Stale pipeline handle");
    return found != nullptr ? *found : PipelineVulkan{};
}

GpuMemoryStats RhiDeviceVulkan::memoryStats()
{
    GpuMemoryStats stats;
    std::array<VmaBudget, VK_MAX_MEMORY_HEAPS> budgets{};
    vmaGetHeapBudgets(m_allocator, budgets.data());
    const VkPhysicalDeviceMemoryProperties* memory = nullptr;
    vmaGetMemoryProperties(m_allocator, &memory);
    for (std::uint32_t heap = 0; heap < memory->memoryHeapCount; ++heap)
    {
        stats.usedBytes += budgets[heap].usage;
        stats.budgetBytes += budgets[heap].budget;
    }
    for (std::size_t tag = 0; tag < stats.bytesByTag.size(); ++tag)
    {
        stats.bytesByTag[tag] = m_bytesByTag[tag].load();
    }
    return stats;
}

std::vector<std::byte> RhiDeviceVulkan::pipelineCacheData()
{
    std::size_t size = 0;
    if (vkGetPipelineCacheData(m_device, m_pipelineCache, &size, nullptr) != VK_SUCCESS)
    {
        return {};
    }
    std::vector<std::byte> data(size);
    if (vkGetPipelineCacheData(m_device, m_pipelineCache, &size, data.data()) != VK_SUCCESS)
    {
        return {};
    }
    data.resize(size);
    return data;
}
} // namespace Meow2::Rhi
