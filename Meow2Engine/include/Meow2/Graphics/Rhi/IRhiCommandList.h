/// @file
/// @brief The list of GPU work recorded for one frame.

#pragma once

#include <Meow2/Math/Color.h>

namespace Meow2::Rhi
{
class IRhiSwapchain;

/// @brief Records GPU work. Grows with the RHI; for now it can only clear a window.
///
/// Owned by the device and valid from IRhiDevice::beginFrame() to IRhiDevice::endFrame().
/// @par Thread safety
/// Not thread-safe. Record on one thread at a time.
class IRhiCommandList
{
public:
    /// @brief Destroyed by its device.
    virtual ~IRhiCommandList() = default;

    IRhiCommandList(const IRhiCommandList&) = delete;            ///< Not copyable.
    IRhiCommandList& operator=(const IRhiCommandList&) = delete; ///< Not copyable.

    /// @brief Clears the swapchain's current image.
    /// @param swapchain The swapchain.
    /// @param color     Linear colour; the image format applies the sRGB encoding.
    /// @pre IRhiDevice::acquire() returned true for it this frame (asserted).
    virtual void clearSwapchain(IRhiSwapchain& swapchain, const Math::Color& color) = 0;

protected:
    /// @brief Only backends create command lists.
    IRhiCommandList() = default;
};
} // namespace Meow2::Rhi
