/// @file
/// @brief The Vulkan swapchain.

#pragma once

#include "Meow2/Graphics/Rhi/IRhiSwapchain.h"

#include <volk.h>

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace Meow2::Rhi
{
class RhiDeviceVulkan;

/// @brief A VkSwapchainKHR, its image views and its semaphores.
///
/// Each frame in flight has its own acquire semaphore, because a semaphore can't be reused until the GPU has waited on
/// it. Each image has its own present semaphore, because the display holds it until that image comes round again,
/// which isn't tied to our frame order.
/// @par Thread safety
/// Not thread-safe.
class RhiSwapchainVulkan final : public IRhiSwapchain
{
public:
    /// @brief Use create().
    /// @param device The device. Must outlive the swapchain.
    /// @param desc   Name and vsync.
    RhiSwapchainVulkan(RhiDeviceVulkan& device, const RhiSwapchainDesc& desc);

    /// @brief Creates the surface and, unless the size is 0, the images.
    /// @param device The device. Must outlive the swapchain.
    /// @param desc   Window and size.
    /// @return The swapchain or an error.
    [[nodiscard]] static Result<std::unique_ptr<IRhiSwapchain>> create(RhiDeviceVulkan& device,
                                                                       const RhiSwapchainDesc& desc);

    ~RhiSwapchainVulkan() override;

    [[nodiscard]] std::uint32_t width() const override
    {
        return m_extent.width;
    }
    [[nodiscard]] std::uint32_t height() const override
    {
        return m_extent.height;
    }
    [[nodiscard]] RhiFormat format() const override
    {
        return m_format;
    }
    [[nodiscard]] bool needsResize() const override
    {
        return m_outOfDate;
    }
    [[nodiscard]] Result<void> resize(std::uint32_t width, std::uint32_t height) override;

    /// @brief Takes the next image.
    /// @param frameIndex The frame in flight, which picks the acquire semaphore.
    /// @return True if an image was taken.
    [[nodiscard]] bool acquire(std::uint32_t frameIndex);

    /// @brief Marks the image as handed back to the display. Called after present.
    void released()
    {
        m_acquired = false;
    }

    /// @brief Remembers that the display says the images no longer fit.
    void markOutOfDate()
    {
        m_outOfDate = true;
    }

    /// @brief Whether acquire() succeeded this frame.
    /// @return True until released().
    [[nodiscard]] bool isAcquired() const
    {
        return m_acquired;
    }

    /// @brief The swapchain object.
    /// @return The handle; null with no images.
    [[nodiscard]] VkSwapchainKHR handle() const
    {
        return m_swapchain;
    }

    /// @brief The image taken by acquire().
    /// @return Its index.
    [[nodiscard]] std::uint32_t imageIndex() const
    {
        return m_imageIndex;
    }

    /// @brief The current image.
    /// @return The image.
    [[nodiscard]] VkImage image() const
    {
        return m_images[m_imageIndex];
    }

    /// @brief A view of the current image.
    /// @return The view.
    [[nodiscard]] VkImageView imageView() const
    {
        return m_views[m_imageIndex];
    }

    /// @brief The size of the images.
    /// @return The extent.
    [[nodiscard]] VkExtent2D extent() const
    {
        return m_extent;
    }

    /// @brief Signalled when the image is ready to draw to.
    /// @param frameIndex The frame in flight.
    /// @return The semaphore.
    [[nodiscard]] VkSemaphore acquireSemaphore(std::uint32_t frameIndex) const
    {
        return m_acquireSemaphores[frameIndex];
    }

    /// @brief Signalled when the current image may be shown.
    /// @return The semaphore.
    [[nodiscard]] VkSemaphore presentSemaphore() const
    {
        return m_presentSemaphores[m_imageIndex];
    }

private:
    /// @brief (Re)creates the swapchain and everything per image. The old swapchain goes once the new one exists.
    /// @param width  Drawable width in pixels.
    /// @param height Drawable height in pixels.
    /// @return An error if Vulkan refuses.
    [[nodiscard]] Result<void> build(std::uint32_t width, std::uint32_t height);

    /// @brief Destroys the views and present semaphores.
    void destroyImageObjects();

    RhiDeviceVulkan& m_device;
    std::string m_name;
    bool m_vsync = true;
    VkSurfaceKHR m_surface = VK_NULL_HANDLE;
    VkSwapchainKHR m_swapchain = VK_NULL_HANDLE;
    VkSurfaceFormatKHR m_surfaceFormat{};
    RhiFormat m_format = RhiFormat::Unknown;
    VkExtent2D m_extent{};
    std::vector<VkImage> m_images;
    std::vector<VkImageView> m_views;
    std::vector<VkSemaphore> m_presentSemaphores;
    std::array<VkSemaphore, c_maxFramesInFlight> m_acquireSemaphores{};
    std::uint32_t m_imageIndex = 0;
    bool m_acquired = false;
    bool m_outOfDate = false;
};
} // namespace Meow2::Rhi
