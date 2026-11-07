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
#include <memory>
#include <string_view>
#include <vector>

namespace Meow2::Rhi
{
class RhiSwapchainVulkan;

/// @brief The name of a Vulkan result, for error messages.
/// @param result The result.
/// @return Its name, or `VkResult` for a value this function doesn't list.
[[nodiscard]] std::string_view vulkanResultName(VkResult result);

/// @brief Records into the current frame's VkCommandBuffer.
/// @par Thread safety
/// Not thread-safe.
class RhiCommandListVulkan final : public IRhiCommandList
{
public:
    RhiCommandListVulkan() = default;

    /// @brief Points it at this frame's command buffer.
    /// @param commandBuffer The command buffer, which is recording.
    void setCommandBuffer(VkCommandBuffer commandBuffer)
    {
        m_commandBuffer = commandBuffer;
    }

    void clearSwapchain(IRhiSwapchain& swapchain, const Math::Color& color) override;

private:
    VkCommandBuffer m_commandBuffer = VK_NULL_HANDLE;
};

/// @brief An instance, a device, one queue for graphics and compute, and the frame loop.
///
/// Frames are paced by one timeline semaphore: every submit signals the next value, and a frame slot is free again once
/// the value it signalled has been reached.
/// @par Thread safety
/// Not thread-safe, except validationErrorCount().
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

private:
    /// @brief One slot of the frames in flight.
    struct Frame
    {
        VkCommandPool pool = VK_NULL_HANDLE;       ///< Reset as a whole when the slot is reused.
        VkCommandBuffer commands = VK_NULL_HANDLE; ///< The frame's command buffer.
        std::uint64_t submittedValue = 0;          ///< Timeline value that means the GPU is done with this slot.
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

    /// @brief Records an image layout change for a swapchain image.
    /// @param swapchain The swapchain.
    /// @param toPresent True for attachment → present, false for undefined → attachment.
    void recordSwapchainBarrier(const RhiSwapchainVulkan& swapchain, bool toPresent);

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
    std::uint64_t m_timelineValue = 0;
    bool m_inFrame = false;
    bool m_debugUtils = false;
    RhiCommandListVulkan m_commandList;
    std::vector<RhiSwapchainVulkan*> m_acquired;
    std::uint32_t m_swapchainCount = 0;
    RhiAdapterInfo m_adapter;
    std::atomic<std::uint64_t> m_validationErrors{0};
};
} // namespace Meow2::Rhi
