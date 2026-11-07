/// @file
/// @brief The images a window shows, handed to the display one after another.

#pragma once

#include <Meow2/Core/Result.h>
#include <Meow2/Graphics/Rhi/RhiTypes.h>

#include <cstdint>

namespace Meow2::Rhi
{
/// @brief The swapchain of one window. A device can have several (ImGui viewports, the editor's viewport).
///
/// Create with IRhiDevice::createSwapchain(). Destroy it before its device and before its window.
/// @par Thread safety
/// Not thread-safe. Use on the thread that renders.
class IRhiSwapchain
{
public:
    /// @brief Waits for the GPU and destroys the swapchain.
    /// @pre No frame is being recorded (asserted).
    virtual ~IRhiSwapchain() = default;

    IRhiSwapchain(const IRhiSwapchain&) = delete;            ///< Not copyable.
    IRhiSwapchain& operator=(const IRhiSwapchain&) = delete; ///< Not copyable.

    /// @brief Current width.
    /// @return Width in pixels; 0 while the window is minimised. The driver may have adjusted the requested size.
    [[nodiscard]] virtual std::uint32_t width() const = 0;

    /// @brief Current height.
    /// @return Height in pixels; 0 while the window is minimised.
    [[nodiscard]] virtual std::uint32_t height() const = 0;

    /// @brief The format of its images.
    /// @return The format chosen when it was created.
    [[nodiscard]] virtual RhiFormat format() const = 0;

    /// @brief Whether the display told us the images no longer fit the window.
    /// @return True if resize() should be called, even with the same size.
    [[nodiscard]] virtual bool needsResize() const = 0;

    /// @brief Recreates the images for a new size. Waits for the GPU, so call it only when the size really changed.
    /// @param width  Drawable width in pixels; 0 while minimised, which leaves it with no images until the next resize.
    /// @param height Drawable height in pixels; 0 while minimised.
    /// @return An error if the images can't be created.
    /// @pre No frame is being recorded (asserted).
    [[nodiscard]] virtual Result<void> resize(std::uint32_t width, std::uint32_t height) = 0;

protected:
    /// @brief Only backends create swapchains.
    IRhiSwapchain() = default;
};
} // namespace Meow2::Rhi
