/// @file
/// @brief The Vulkan swapchain: surface, images, and the choices of format and present mode.

#include "Meow2/Core/Platform.h"

// Before volk: creating a Win32 surface needs the module handle. On Windows volk only declares the types.
#if MEOW2_PLATFORM_WINDOWS
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include "Graphics/Rhi/Vulkan/RhiDeviceVulkan.h"
#include "Graphics/Rhi/Vulkan/RhiSwapchainVulkan.h"
#include "Meow2/Core/Assert.h"

#include <algorithm>
#include <cstdint>
#include <format>
#include <limits>
#include <utility>

namespace Meow2::Rhi
{
namespace
{
/// @brief Creates a surface for a native window.
/// @param instance The instance.
/// @param window   The window.
/// @return The surface, or an error for a window type this platform can't show.
Result<VkSurfaceKHR> createSurface(VkInstance instance, const Platform::NativeWindowHandle& window)
{
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkResult result = VK_ERROR_INITIALIZATION_FAILED;
    switch (window.type)
    {
#if MEOW2_PLATFORM_WINDOWS
        case Platform::NativeWindowType::Win32:
        {
            const VkWin32SurfaceCreateInfoKHR info{.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR,
                                                   .hinstance = GetModuleHandleW(nullptr),
                                                   .hwnd = static_cast<HWND>(window.window)};
            result = vkCreateWin32SurfaceKHR(instance, &info, nullptr, &surface);
            break;
        }
#else
        case Platform::NativeWindowType::X11:
        {
            const VkXlibSurfaceCreateInfoKHR info{.sType = VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR,
                                                  .dpy = static_cast<Display*>(window.display),
                                                  .window = static_cast<Window>(window.x11Window)};
            result = vkCreateXlibSurfaceKHR(instance, &info, nullptr, &surface);
            break;
        }
        case Platform::NativeWindowType::Wayland:
        {
            const VkWaylandSurfaceCreateInfoKHR info{.sType = VK_STRUCTURE_TYPE_WAYLAND_SURFACE_CREATE_INFO_KHR,
                                                     .display = static_cast<wl_display*>(window.display),
                                                     .surface = static_cast<wl_surface*>(window.window)};
            result = vkCreateWaylandSurfaceKHR(instance, &info, nullptr, &surface);
            break;
        }
#endif
        default:
            return fail(ErrorCode::Unsupported, "This window has no native surface (the offscreen driver?)");
    }
    if (result != VK_SUCCESS)
    {
        return fail(ErrorCode::Unsupported, std::format("Creating the surface: {}", vulkanResultName(result)));
    }
    return surface;
}

/// @brief Picks a format: an sRGB 8-bit one, so linear colours are encoded for the display by the hardware.
/// @param gpu     The GPU.
/// @param surface The surface.
/// @return The format.
VkSurfaceFormatKHR chooseFormat(VkPhysicalDevice gpu, VkSurfaceKHR surface)
{
    std::uint32_t count = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(gpu, surface, &count, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(count);
    vkGetPhysicalDeviceSurfaceFormatsKHR(gpu, surface, &count, formats.data());
    for (const VkFormat wanted : {VK_FORMAT_B8G8R8A8_SRGB, VK_FORMAT_R8G8B8A8_SRGB})
    {
        const auto found = std::ranges::find_if(
            formats, [wanted](const VkSurfaceFormatKHR& format)
            { return format.format == wanted && format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR; });
        if (found != formats.end())
        {
            return *found;
        }
    }
    return formats.empty() ? VkSurfaceFormatKHR{} : formats.front();
}

/// @brief Picks how images reach the display. FIFO is the only mode every driver has, and it's vsync.
/// @param gpu     The GPU.
/// @param surface The surface.
/// @param vsync   Whether to wait for the display.
/// @return The mode.
VkPresentModeKHR choosePresentMode(VkPhysicalDevice gpu, VkSurfaceKHR surface, bool vsync)
{
    if (vsync)
    {
        return VK_PRESENT_MODE_FIFO_KHR;
    }
    std::uint32_t count = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(gpu, surface, &count, nullptr);
    std::vector<VkPresentModeKHR> modes(count);
    vkGetPhysicalDeviceSurfacePresentModesKHR(gpu, surface, &count, modes.data());
    for (const VkPresentModeKHR wanted : {VK_PRESENT_MODE_MAILBOX_KHR, VK_PRESENT_MODE_IMMEDIATE_KHR})
    {
        if (std::ranges::find(modes, wanted) != modes.end())
        {
            return wanted;
        }
    }
    return VK_PRESENT_MODE_FIFO_KHR;
}
} // namespace

RhiSwapchainVulkan::RhiSwapchainVulkan(RhiDeviceVulkan& device, const RhiSwapchainDesc& desc)
    : m_device(device)
    , m_name(desc.name)
    , m_vsync(desc.vsync)
{
    m_device.swapchainCreated();
}

Result<std::unique_ptr<IRhiSwapchain>> RhiSwapchainVulkan::create(RhiDeviceVulkan& device, const RhiSwapchainDesc& desc)
{
    auto swapchain = std::make_unique<RhiSwapchainVulkan>(device, desc);
    Result<VkSurfaceKHR> surface = createSurface(device.instance(), desc.window);
    if (!surface)
    {
        return Unexpected<Error>{surface.error()};
    }
    swapchain->m_surface = *surface;

    VkBool32 supported = VK_FALSE;
    vkGetPhysicalDeviceSurfaceSupportKHR(device.physicalDevice(), device.queueFamily(), *surface, &supported);
    if (supported == VK_FALSE)
    {
        return fail(ErrorCode::Unsupported, "This GPU can't show images in that window");
    }
    swapchain->m_surfaceFormat = chooseFormat(device.physicalDevice(), *surface);
    swapchain->m_format = swapchain->m_surfaceFormat.format == VK_FORMAT_B8G8R8A8_SRGB   ? RhiFormat::Bgra8Srgb
                          : swapchain->m_surfaceFormat.format == VK_FORMAT_R8G8B8A8_SRGB ? RhiFormat::Rgba8Srgb
                                                                                         : RhiFormat::Unknown;
    if (swapchain->m_format == RhiFormat::Unknown)
    {
        return fail(ErrorCode::Unsupported, "This window has no sRGB 8-bit format");
    }

    const VkSemaphoreCreateInfo semaphore{.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    for (VkSemaphore& slot : swapchain->m_acquireSemaphores)
    {
        if (const VkResult result = vkCreateSemaphore(device.device(), &semaphore, nullptr, &slot);
            result != VK_SUCCESS)
        {
            return fail(ErrorCode::Unknown, std::format("Semaphore: {}", vulkanResultName(result)));
        }
    }
    if (Result<void> built = swapchain->build(desc.width, desc.height); !built)
    {
        return Unexpected<Error>{built.error()};
    }
    return std::unique_ptr<IRhiSwapchain>{std::move(swapchain)};
}

RhiSwapchainVulkan::~RhiSwapchainVulkan()
{
    MEOW2_ASSERT(!m_device.inFrame(), "A swapchain can't be destroyed while a frame is recording");
    m_device.waitIdle();
    const VkDevice device = m_device.device();
    destroyImageObjects();
    if (m_swapchain != VK_NULL_HANDLE)
    {
        vkDestroySwapchainKHR(device, m_swapchain, nullptr);
    }
    for (const VkSemaphore semaphore : m_acquireSemaphores)
    {
        if (semaphore != VK_NULL_HANDLE)
        {
            vkDestroySemaphore(device, semaphore, nullptr);
        }
    }
    if (m_surface != VK_NULL_HANDLE)
    {
        vkDestroySurfaceKHR(m_device.instance(), m_surface, nullptr);
    }
    m_device.swapchainDestroyed();
}

void RhiSwapchainVulkan::destroyImageObjects()
{
    const VkDevice device = m_device.device();
    for (const VkImageView view : m_views)
    {
        vkDestroyImageView(device, view, nullptr);
    }
    for (const VkSemaphore semaphore : m_presentSemaphores)
    {
        vkDestroySemaphore(device, semaphore, nullptr);
    }
    m_views.clear();
    m_presentSemaphores.clear();
    m_images.clear();
}

Result<void> RhiSwapchainVulkan::resize(std::uint32_t width, std::uint32_t height)
{
    MEOW2_ASSERT(!m_device.inFrame(), "A swapchain can't be resized while a frame is recording");
    m_device.waitIdle();
    return build(width, height);
}

Result<void> RhiSwapchainVulkan::build(std::uint32_t width, std::uint32_t height)
{
    const VkDevice device = m_device.device();
    VkSurfaceCapabilitiesKHR capabilities{};
    if (const VkResult result =
            vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_device.physicalDevice(), m_surface, &capabilities);
        result != VK_SUCCESS)
    {
        return fail(ErrorCode::Unknown, std::format("Surface capabilities: {}", vulkanResultName(result)));
    }
    // Most platforms dictate the size. The special value means the window takes the swapchain's size instead.
    VkExtent2D extent = capabilities.currentExtent;
    if (extent.width == std::numeric_limits<std::uint32_t>::max())
    {
        extent.width = std::clamp(width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
        extent.height = std::clamp(height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);
    }

    destroyImageObjects();
    m_outOfDate = false;
    const VkSwapchainKHR oldSwapchain = m_swapchain;
    m_swapchain = VK_NULL_HANDLE;
    if (width == 0 || height == 0 || extent.width == 0 || extent.height == 0)
    {
        // Vulkan has no zero-sized swapchains: a minimised window simply has no images until it's restored.
        if (oldSwapchain != VK_NULL_HANDLE)
        {
            vkDestroySwapchainKHR(device, oldSwapchain, nullptr);
        }
        m_extent = {};
        return {};
    }

    std::uint32_t imageCount = capabilities.minImageCount + 1; // one spare, so we rarely wait for the driver
    if (capabilities.maxImageCount != 0)                       // 0: no limit
    {
        imageCount = std::min(imageCount, capabilities.maxImageCount);
    }
    const VkSwapchainCreateInfoKHR info{
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface = m_surface,
        .minImageCount = imageCount,
        .imageFormat = m_surfaceFormat.format,
        .imageColorSpace = m_surfaceFormat.colorSpace,
        .imageExtent = extent,
        .imageArrayLayers = 1,
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                      (capabilities.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT), // for screenshots and tools
        .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .preTransform = capabilities.currentTransform,
        .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode = choosePresentMode(m_device.physicalDevice(), m_surface, m_vsync),
        .clipped = VK_TRUE,
        .oldSwapchain = oldSwapchain};
    const VkResult created = vkCreateSwapchainKHR(device, &info, nullptr, &m_swapchain);
    if (oldSwapchain != VK_NULL_HANDLE)
    {
        vkDestroySwapchainKHR(device, oldSwapchain, nullptr);
    }
    if (created != VK_SUCCESS)
    {
        m_swapchain = VK_NULL_HANDLE;
        m_extent = {};
        return fail(ErrorCode::Unknown, std::format("vkCreateSwapchainKHR: {}", vulkanResultName(created)));
    }
    m_extent = extent;
    m_device.setObjectName(VK_OBJECT_TYPE_SWAPCHAIN_KHR, reinterpret_cast<std::uint64_t>(m_swapchain), m_name.c_str());

    std::uint32_t count = 0;
    vkGetSwapchainImagesKHR(device, m_swapchain, &count, nullptr);
    m_images.resize(count);
    vkGetSwapchainImagesKHR(device, m_swapchain, &count, m_images.data());
    const VkSemaphoreCreateInfo semaphoreInfo{.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    for (const VkImage image : m_images)
    {
        const VkImageViewCreateInfo viewInfo{
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = image,
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = m_surfaceFormat.format,
            .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .levelCount = 1, .layerCount = 1}};
        VkImageView view = VK_NULL_HANDLE;
        VkSemaphore semaphore = VK_NULL_HANDLE;
        if (const VkResult result = vkCreateImageView(device, &viewInfo, nullptr, &view); result != VK_SUCCESS)
        {
            return fail(ErrorCode::Unknown, std::format("Image view: {}", vulkanResultName(result)));
        }
        m_views.push_back(view);
        if (const VkResult result = vkCreateSemaphore(device, &semaphoreInfo, nullptr, &semaphore);
            result != VK_SUCCESS)
        {
            return fail(ErrorCode::Unknown, std::format("Semaphore: {}", vulkanResultName(result)));
        }
        m_presentSemaphores.push_back(semaphore);
    }
    return {};
}

bool RhiSwapchainVulkan::acquire(std::uint32_t frameIndex)
{
    MEOW2_ASSERT(!m_acquired, "acquire() was already called this frame");
    if (m_swapchain == VK_NULL_HANDLE || m_outOfDate)
    {
        return false;
    }
    const VkResult result =
        vkAcquireNextImageKHR(m_device.device(), m_swapchain, std::numeric_limits<std::uint64_t>::max(),
                              m_acquireSemaphores[frameIndex], VK_NULL_HANDLE, &m_imageIndex);
    if (result == VK_ERROR_OUT_OF_DATE_KHR)
    {
        m_outOfDate = true;
        return false;
    }
    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
    {
        MEOW2_LOG_ERROR(m_device.log(), "vkAcquireNextImageKHR: {}", vulkanResultName(result));
        return false;
    }
    if (result == VK_SUBOPTIMAL_KHR)
    {
        m_outOfDate = true; // still usable this frame; it gets resized before the next one
    }
    m_acquired = true;
    return true;
}
} // namespace Meow2::Rhi
