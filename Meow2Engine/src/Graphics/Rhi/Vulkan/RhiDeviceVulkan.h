/// @file
/// @brief The Vulkan device.

#pragma once

#include "Meow2/Graphics/Rhi/IRhiDevice.h"

// volk must come first, or VMA leaves out its volk import function.
// clang-format off
#include <volk.h>
#include <vk_mem_alloc.h>
// clang-format on

#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string_view>
#include <vector>

namespace Meow2::Rhi
{
class RhiDeviceVulkan;
class RhiSwapchainVulkan;

/// @brief The name of a Vulkan result, for error messages.
/// @param result The result.
/// @return Its name, or `VkResult` for a value this function doesn't list.
[[nodiscard]] std::string_view vulkanResultName(VkResult result);

/// @brief The Vulkan format of an RHI format.
/// @param format The format.
/// @return The Vulkan format; VK_FORMAT_UNDEFINED for RhiFormat::Unknown.
[[nodiscard]] VkFormat vulkanFormat(RhiFormat format);

/// @brief A buffer's Vulkan objects. Plain data.
struct BufferVulkan
{
    VkBuffer buffer = VK_NULL_HANDLE;   ///< The buffer.
    VmaAllocation allocation = nullptr; ///< Its memory.
    std::byte* mapped = nullptr;        ///< Mapped address; null for GPU-only memory.
    std::uint64_t size = 0;             ///< Size in bytes.
    std::uint32_t heapIndex = 0;        ///< Slot in the resource heap.
};

/// @brief A texture's Vulkan objects. Plain data.
struct TextureVulkan
{
    VkImage image = VK_NULL_HANDLE;        ///< The image.
    VkImageView view = VK_NULL_HANDLE;     ///< A view of every mip level.
    VmaAllocation allocation = nullptr;    ///< Its memory.
    VkFormat format = VK_FORMAT_UNDEFINED; ///< Its format.
    VkExtent2D extent{};                   ///< Size in pixels.
    std::uint32_t mipLevels = 1;           ///< Mip levels.
    bool depth = false;                    ///< A depth format.
    std::uint32_t heapIndex = 0;           ///< Sampled slot in the resource heap.
    std::uint32_t storageIndex = 0;        ///< Writable slot in the resource heap; 0 if it has none.
};

/// @brief A sampler's Vulkan object. Plain data.
struct SamplerVulkan
{
    VkSampler sampler = VK_NULL_HANDLE; ///< The sampler.
    std::uint32_t heapIndex = 0;        ///< Slot in the sampler heap.
};

/// @brief A pipeline's Vulkan object. Plain data.
struct PipelineVulkan
{
    VkPipeline pipeline = VK_NULL_HANDLE;                            ///< The pipeline.
    VkPipelineBindPoint bindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS; ///< Graphics or compute.
};

/// @brief Hands out slots of a descriptor heap and takes them back. Plain bookkeeping; the device locks around it.
/// @par Thread safety
/// Not thread-safe.
class HeapIndexAllocator
{
public:
    /// @brief Sets how many slots there are.
    /// @param first The first slot to hand out.
    /// @param count One past the last slot.
    void reset(std::uint32_t first, std::uint32_t count);

    /// @brief Takes a slot.
    /// @return The slot; 0 if the heap is full.
    [[nodiscard]] std::uint32_t allocate();

    /// @brief Gives a slot back. Only once the GPU can no longer use it.
    /// @param index The slot.
    void release(std::uint32_t index);

private:
    std::uint32_t m_next = 0;
    std::uint32_t m_end = 0;
    std::vector<std::uint32_t> m_free;
};

/// @brief Records into the current frame's VkCommandBuffer.
/// @par Thread safety
/// Not thread-safe.
class RhiCommandListVulkan final : public IRhiCommandList
{
public:
    /// @brief A list with nothing to record into yet.
    /// @param device The device. Must outlive the list.
    explicit RhiCommandListVulkan(RhiDeviceVulkan& device);

    /// @brief Points it at this frame's command buffer.
    /// @param commandBuffer The command buffer, which is recording.
    void setCommandBuffer(VkCommandBuffer commandBuffer)
    {
        m_commandBuffer = commandBuffer;
    }

    void beginRendering(IRhiSwapchain& swapchain, std::optional<Math::Color> clear) override;
    void beginRendering(const RenderTargets& targets) override;
    void endRendering() override;
    void setPipeline(PipelineHandle pipeline) override;
    void setPushConstants(const Shader::PushConstants& constants) override;
    void draw(std::uint32_t vertexCount, std::uint32_t instanceCount) override;
    void dispatch(std::uint32_t x, std::uint32_t y, std::uint32_t z) override;
    void barrier(BufferHandle buffer, ResourceState from, ResourceState to) override;
    void barrier(TextureHandle texture, ResourceState from, ResourceState to) override;
    void copyBuffer(BufferHandle source, BufferHandle target, std::uint64_t size, std::uint64_t sourceOffset,
                    std::uint64_t targetOffset) override;
    void copyBufferToTexture(BufferHandle source, TextureHandle target) override;
    void copyTextureToBuffer(TextureHandle source, BufferHandle target) override;

private:
    /// @brief Starts rendering and sets the viewport and scissor to the target's size.
    /// @param color  The colour attachment.
    /// @param depth  The depth attachment, or null.
    /// @param extent The target's size.
    void startRendering(const VkRenderingAttachmentInfo& color, const VkRenderingAttachmentInfo* depth,
                        VkExtent2D extent);

    RhiDeviceVulkan& m_device;
    VkCommandBuffer m_commandBuffer = VK_NULL_HANDLE;
    bool m_rendering = false;
};

/// @brief An instance, a device, one queue for graphics and compute, the frame loop, and the resources.
///
/// Frames are paced by one timeline semaphore: every submit signals the next value, and a frame slot is free again once
/// the value it signalled has been reached. The same counter drives deferred destruction: a destroyed object waits in
/// a queue with the value that will prove the GPU is done with it.
///
/// There's one descriptor set for everything: binding 0 is the sampler heap, binding 1 the mutable resource heap, which
/// DXC is told about with `-fvk-bind-sampler-heap` and `-fvk-bind-resource-heap`.
/// @par Thread safety
/// The frame functions are not thread-safe. Creating and destroying resources, and looking them up, are, through one
/// mutex that's never held across a Vulkan creation call.
class RhiDeviceVulkan final : public IRhiDevice
{
public:
    /// @brief Use create().
    /// @param log Must outlive the device.
    explicit RhiDeviceVulkan(Log::Logger& log);

    /// @brief Connects to Vulkan and picks a GPU.
    /// @param desc Options.
    /// @param log  Must outlive the device.
    /// @return The device or an error.
    [[nodiscard]] static Result<std::unique_ptr<IRhiDevice>> create(const RhiDeviceDesc& desc, Log::Logger& log);

    ~RhiDeviceVulkan() override;

    [[nodiscard]] RhiBackend backend() const override
    {
        return RhiBackend::Vulkan;
    }
    [[nodiscard]] const RhiAdapterInfo& adapter() const override
    {
        return m_adapter;
    }
    [[nodiscard]] Result<std::unique_ptr<IRhiSwapchain>> createSwapchain(const RhiSwapchainDesc& desc) override;
    [[nodiscard]] Result<IRhiCommandList*> beginFrame() override;
    [[nodiscard]] bool acquire(IRhiSwapchain& swapchain) override;
    [[nodiscard]] Result<void> endFrame() override;
    void waitIdle() override;
    [[nodiscard]] std::uint64_t validationErrorCount() const override
    {
        return m_validationErrors.load();
    }
    [[nodiscard]] bool validationActive() const override
    {
        return m_messenger != VK_NULL_HANDLE;
    }

    [[nodiscard]] Result<BufferHandle> createBuffer(const BufferDesc& desc) override;
    void destroy(BufferHandle buffer) override;
    [[nodiscard]] std::span<std::byte> mappedMemory(BufferHandle buffer) override;
    [[nodiscard]] std::uint32_t bindlessIndex(BufferHandle buffer) override;
    [[nodiscard]] Result<TextureHandle> createTexture(const TextureDesc& desc) override;
    void destroy(TextureHandle texture) override;
    [[nodiscard]] std::uint32_t bindlessIndex(TextureHandle texture) override;
    [[nodiscard]] std::uint32_t bindlessStorageIndex(TextureHandle texture) override;
    [[nodiscard]] Result<SamplerHandle> createSampler(const SamplerDesc& desc) override;
    void destroy(SamplerHandle sampler) override;
    [[nodiscard]] std::uint32_t bindlessIndex(SamplerHandle sampler) override;
    [[nodiscard]] Result<PipelineHandle> createGraphicsPipeline(const GraphicsPipelineDesc& desc) override;
    [[nodiscard]] Result<PipelineHandle> createComputePipeline(const ComputePipelineDesc& desc) override;
    void destroy(PipelineHandle pipeline) override;
    [[nodiscard]] std::vector<std::byte> pipelineCacheData() override;

    /// @brief The instance.
    /// @return The handle.
    [[nodiscard]] VkInstance instance() const
    {
        return m_instance;
    }

    /// @brief The chosen GPU.
    /// @return The handle.
    [[nodiscard]] VkPhysicalDevice physicalDevice() const
    {
        return m_physicalDevice;
    }

    /// @brief The logical device.
    /// @return The handle.
    [[nodiscard]] VkDevice device() const
    {
        return m_device;
    }

    /// @brief The family of the only queue.
    /// @return The index.
    [[nodiscard]] std::uint32_t queueFamily() const
    {
        return m_queueFamily;
    }

    /// @brief The layout every pipeline shares.
    /// @return The handle.
    [[nodiscard]] VkPipelineLayout pipelineLayout() const
    {
        return m_pipelineLayout;
    }

    /// @brief The log.
    /// @return The logger.
    [[nodiscard]] Log::Logger& log()
    {
        return m_log;
    }

    /// @brief Whether a frame is being recorded.
    /// @return True between beginFrame() and endFrame().
    [[nodiscard]] bool inFrame() const
    {
        return m_inFrame;
    }

    /// @brief Names an object for validation messages and debuggers. Does nothing without VK_EXT_debug_utils.
    /// @param type   The object's type.
    /// @param handle The object, as an integer.
    /// @param name   Its name.
    void setObjectName(VkObjectType type, std::uint64_t handle, const char* name) const;

    /// @brief Counts a swapchain in, so the destructor can check they're all gone.
    void swapchainCreated()
    {
        ++m_swapchainCount;
    }

    /// @brief Counts a swapchain out.
    void swapchainDestroyed()
    {
        --m_swapchainCount;
    }

    /// @brief Forwards a validation message to the log. Called by the debug callback, on any thread.
    /// @param isError Whether it's an error rather than a warning.
    /// @param message The text.
    void onValidationMessage(bool isError, const char* message);

    /// @brief Looks up a buffer for the command list.
    /// @param handle The handle.
    /// @return A copy of its objects; asserts on a stale handle.
    [[nodiscard]] BufferVulkan buffer(BufferHandle handle);

    /// @brief Looks up a texture for the command list.
    /// @param handle The handle.
    /// @return A copy of its objects; asserts on a stale handle.
    [[nodiscard]] TextureVulkan texture(TextureHandle handle);

    /// @brief Looks up a pipeline for the command list.
    /// @param handle The handle.
    /// @return A copy of its objects; asserts on a stale handle.
    [[nodiscard]] PipelineVulkan pipeline(PipelineHandle handle);

private:
    /// @brief One slot of the frames in flight.
    struct Frame
    {
        VkCommandPool pool = VK_NULL_HANDLE;       ///< Reset as a whole when the slot is reused.
        VkCommandBuffer commands = VK_NULL_HANDLE; ///< The frame's command buffer.
        std::uint64_t submittedValue = 0;          ///< Timeline value that means the GPU is done with this slot.
    };

    /// @brief Something to destroy once the GPU has reached a timeline value.
    struct PendingDestroy
    {
        std::uint64_t value = 0;       ///< The value.
        std::function<void()> destroy; ///< Destroys the objects and gives back heap slots.
    };

    /// @brief Creates the instance and, with validation, the debug messenger.
    /// @param desc Options.
    /// @return An error if Vulkan is missing.
    [[nodiscard]] Result<void> createInstance(const RhiDeviceDesc& desc);

    /// @brief Chooses the best GPU that meets the requirements.
    /// @param desc Options.
    /// @return An error naming each GPU and what it's missing.
    [[nodiscard]] Result<void> pickPhysicalDevice(const RhiDeviceDesc& desc);

    /// @brief Creates the device, the queue, the allocator and the frame slots.
    /// @param desc Options.
    /// @return An error if Vulkan refuses.
    [[nodiscard]] Result<void> createDevice(const RhiDeviceDesc& desc);

    /// @brief Creates the heaps, the pipeline layout, the pipeline cache and the default sampler.
    /// @param desc Options.
    /// @return An error if Vulkan refuses.
    [[nodiscard]] Result<void> createBindless(const RhiDeviceDesc& desc);

    /// @brief Records an image layout change for a swapchain image.
    /// @param swapchain The swapchain.
    /// @param toPresent True for attachment → present, false for undefined → attachment.
    void recordSwapchainBarrier(const RhiSwapchainVulkan& swapchain, bool toPresent);

    /// @brief Queues objects for destruction after the frames that could use them.
    /// @param destroyLater Destroys them.
    void deferDestroy(std::function<void()> destroyLater);

    /// @brief Destroys everything the GPU has finished with.
    void collectGarbage();

    /// @brief Writes a buffer into the resource heap.
    /// @param index  The slot.
    /// @param buffer The buffer.
    void writeBufferDescriptor(std::uint32_t index, VkBuffer buffer);

    /// @brief Writes an image into the resource heap.
    /// @param index The slot.
    /// @param view  The image.
    /// @param type  VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE or VK_DESCRIPTOR_TYPE_STORAGE_IMAGE.
    void writeImageDescriptor(std::uint32_t index, VkImageView view, VkDescriptorType type);

    /// @brief Creates a sampler object and its heap slot.
    /// @param desc The settings.
    /// @return The record, or an error.
    [[nodiscard]] Result<SamplerVulkan> makeSampler(const SamplerDesc& desc);

    Log::Logger& m_log;
    VkInstance m_instance = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT m_messenger = VK_NULL_HANDLE;
    VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
    VkDevice m_device = VK_NULL_HANDLE;
    VkQueue m_queue = VK_NULL_HANDLE;
    std::uint32_t m_queueFamily = 0;
    VmaAllocator m_allocator = VK_NULL_HANDLE;
    VkSemaphore m_timeline = VK_NULL_HANDLE;
    std::array<Frame, c_maxFramesInFlight> m_frames{};
    std::uint32_t m_frameIndex = 0;
    std::atomic<std::uint64_t> m_timelineValue{0};
    std::atomic<bool> m_inFrame{false};
    bool m_debugUtils = false;
    float m_maxAnisotropy = 1.0F;
    RhiCommandListVulkan m_commandList;
    std::vector<RhiSwapchainVulkan*> m_acquired;
    std::uint32_t m_swapchainCount = 0;
    RhiAdapterInfo m_adapter;
    std::atomic<std::uint64_t> m_validationErrors{0};

    VkDescriptorSetLayout m_heapLayout = VK_NULL_HANDLE;
    VkDescriptorPool m_heapPool = VK_NULL_HANDLE;
    VkDescriptorSet m_heapSet = VK_NULL_HANDLE;
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
    VkPipelineCache m_pipelineCache = VK_NULL_HANDLE;
    VkSampler m_defaultSampler = VK_NULL_HANDLE;

    std::mutex m_resourceMutex; ///< Guards everything below.
    HandlePool<BufferVulkan, BufferTag> m_buffers;
    HandlePool<TextureVulkan, TextureTag> m_textures;
    HandlePool<SamplerVulkan, SamplerTag> m_samplers;
    HandlePool<PipelineVulkan, PipelineTag> m_pipelines;
    HeapIndexAllocator m_resourceSlots;
    HeapIndexAllocator m_samplerSlots;
    std::vector<PendingDestroy> m_pendingDestroys;
};
} // namespace Meow2::Rhi
