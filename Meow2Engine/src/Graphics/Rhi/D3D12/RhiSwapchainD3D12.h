/// @file
/// @brief The D3D12 swapchain.

#pragma once

#include "Meow2/Graphics/Rhi/IRhiSwapchain.h"

#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace Meow2::Rhi
{
class RhiDeviceD3D12;

/// @brief An IDXGISwapChain3 in the flip model, and a render target view per image.
///
/// DXGI picks the next image itself, so acquiring is only reading its index; the frame pacing of the device (two frames
/// in flight, three images) means that image's last frame has already finished. The images are plain 8-bit BGRA, and
/// the views are sRGB, since flip-model swapchains can't be created in an sRGB format.
/// @par Thread safety
/// Not thread-safe.
class RhiSwapchainD3D12 final : public IRhiSwapchain
{
public:
    /// @brief Use create().
    /// @param device The device. Must outlive the swapchain.
    /// @param desc   Window, name and vsync.
    RhiSwapchainD3D12(RhiDeviceD3D12& device, const RhiSwapchainDesc& desc);

    /// @brief Creates the swapchain for a window, unless the size is 0.
    /// @param device The device. Must outlive the swapchain.
    /// @param desc   Window and size.
    /// @return The swapchain or an error.
    [[nodiscard]] static Result<std::unique_ptr<IRhiSwapchain>> create(RhiDeviceD3D12& device,
                                                                       const RhiSwapchainDesc& desc);

    ~RhiSwapchainD3D12() override;

    RhiSwapchainD3D12(const RhiSwapchainD3D12&) = delete;            ///< Not copyable.
    RhiSwapchainD3D12& operator=(const RhiSwapchainD3D12&) = delete; ///< Not copyable.

    [[nodiscard]] std::uint32_t width() const override
    {
        return m_width;
    }
    [[nodiscard]] std::uint32_t height() const override
    {
        return m_height;
    }
    [[nodiscard]] RhiFormat format() const override
    {
        return RhiFormat::Bgra8Srgb;
    }
    [[nodiscard]] bool needsResize() const override
    {
        return false; // DXGI stretches images to the window instead of failing, so there's nothing to report
    }
    [[nodiscard]] Result<void> resize(std::uint32_t width, std::uint32_t height) override;

    /// @brief Takes the next image for this frame.
    /// @return False if there's nothing to draw to.
    [[nodiscard]] bool acquire();

    /// @brief Shows the acquired image.
    /// @return The result of Present().
    [[nodiscard]] HRESULT present();

    /// @brief Ends the frame's use of the image.
    void released()
    {
        m_acquired = false;
    }

    /// @brief Whether acquire() succeeded this frame.
    /// @return True until released().
    [[nodiscard]] bool isAcquired() const
    {
        return m_acquired;
    }

    /// @brief The acquired image.
    /// @return The resource.
    [[nodiscard]] ID3D12Resource* image() const
    {
        return m_images[m_imageIndex].Get();
    }

    /// @brief The acquired image's render target view.
    /// @return The CPU descriptor.
    [[nodiscard]] D3D12_CPU_DESCRIPTOR_HANDLE view() const;

private:
    /// @brief Creates the swapchain, or resizes its images, and makes the views.
    /// @param width  Width in pixels; 0 leaves it with no images.
    /// @param height Height in pixels.
    /// @return An error if DXGI refuses.
    [[nodiscard]] Result<void> build(std::uint32_t width, std::uint32_t height);

    /// @brief Releases the images and their view slots. The GPU must be idle.
    void releaseImages();

    RhiDeviceD3D12& m_device;
    HWND m_window = nullptr;
    std::string m_name;
    bool m_vsync = true;
    Microsoft::WRL::ComPtr<IDXGISwapChain3> m_swapchain;
    std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>> m_images;
    std::vector<std::uint32_t> m_views;
    std::uint32_t m_width = 0;
    std::uint32_t m_height = 0;
    std::uint32_t m_imageIndex = 0;
    bool m_acquired = false;
};
} // namespace Meow2::Rhi
