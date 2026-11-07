/// @file
/// @brief The RHI's entry point: one GPU, the frames sent to it and the objects created on it.

#pragma once

#include <Meow2/Core/Log/Logger.h>
#include <Meow2/Core/Result.h>
#include <Meow2/Graphics/Rhi/IRhiCommandList.h>
#include <Meow2/Graphics/Rhi/IRhiSwapchain.h>
#include <Meow2/Graphics/Rhi/RhiTypes.h>

#include <cstdint>
#include <memory>

namespace Meow2::Rhi
{
/// @brief One GPU behind one graphics API. The RHI uses virtual interfaces because the backend is chosen at runtime
///        and can change while the app runs (techstack.md §3, §11).
///
/// A frame runs in this order:
/// @code
/// IRhiCommandList* list = *device->beginFrame();        // waits until a frame slot is free
/// if (device->acquire(*swapchain))                      // false: nothing to draw to right now
/// {
///     list->clearSwapchain(*swapchain, Colors::c_black);
/// }
/// device->endFrame();                                   // submits, then shows every acquired swapchain
/// @endcode
/// Only one Vulkan device can exist at a time, because volk keeps its function pointers in globals.
/// @par Thread safety
/// Not thread-safe. Use on the thread that renders.
/// @see techstack.md §11, docs/wiki/Rhi.md
class IRhiDevice
{
public:
    /// @brief Waits for the GPU and destroys the device.
    /// @pre Every swapchain is gone (asserted).
    virtual ~IRhiDevice() = default;

    IRhiDevice(const IRhiDevice&) = delete;            ///< Not copyable.
    IRhiDevice& operator=(const IRhiDevice&) = delete; ///< Not copyable.

    /// @brief Which API this device uses.
    /// @return The backend.
    [[nodiscard]] virtual RhiBackend backend() const = 0;

    /// @brief The GPU it runs on.
    /// @return Its name and API version.
    [[nodiscard]] virtual const RhiAdapterInfo& adapter() const = 0;

    /// @brief Creates a swapchain for a window.
    /// @param desc Window and size. A size of 0 makes a swapchain with no images until resize().
    /// @return The swapchain, or an error (e.g. this GPU can't show that window).
    /// @pre The device was created with RhiDeviceDesc::presentation.
    [[nodiscard]] virtual Result<std::unique_ptr<IRhiSwapchain>> createSwapchain(const RhiSwapchainDesc& desc) = 0;

    /// @brief Starts a frame, waiting until the GPU has finished the oldest frame in flight.
    /// @return The command list for this frame, or an error if the device was lost.
    /// @pre No frame is being recorded (asserted).
    [[nodiscard]] virtual Result<IRhiCommandList*> beginFrame() = 0;

    /// @brief Takes the next image of a swapchain for this frame.
    /// @param swapchain The swapchain. Call at most once per frame.
    /// @return True if the image can be drawn to. False if there's nothing to draw to (minimised, or it needs a
    ///         resize); skip drawing to it this frame.
    /// @pre A frame is being recorded (asserted).
    [[nodiscard]] virtual bool acquire(IRhiSwapchain& swapchain) = 0;

    /// @brief Submits the frame to the GPU and shows every acquired swapchain.
    /// @return An error if the device was lost.
    /// @pre A frame is being recorded (asserted).
    [[nodiscard]] virtual Result<void> endFrame() = 0;

    /// @brief Waits until the GPU has finished everything.
    virtual void waitIdle() = 0;

    /// @brief How many errors the API's validation layer has reported. Tests require it to stay 0.
    /// @return The count; always 0 without validation. May be read from any thread.
    [[nodiscard]] virtual std::uint64_t validationErrorCount() const = 0;

    /// @brief Whether the validation layer is running.
    /// @return False if it wasn't asked for or isn't installed.
    [[nodiscard]] virtual bool validationActive() const = 0;

protected:
    /// @brief Only backends create devices; use createRhiDevice().
    IRhiDevice() = default;
};

/// @brief Creates a device on the best GPU that meets the engine's requirements.
/// @param desc Backend and options.
/// @param log  Receives validation messages and device information. Must outlive the device.
/// @return The device. ErrorCode::Unsupported if the backend isn't built in or no GPU qualifies, in which case the
///         message names each GPU and what it's missing.
[[nodiscard]] Result<std::unique_ptr<IRhiDevice>> createRhiDevice(const RhiDeviceDesc& desc, Log::Logger& log);
} // namespace Meow2::Rhi
