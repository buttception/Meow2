/// @file
/// @brief The RHI's entry point: one GPU, the frames sent to it and the objects created on it.

#pragma once

#include <Meow2/Core/Log/Logger.h>
#include <Meow2/Core/Result.h>
#include <Meow2/Graphics/Rhi/IRhiCommandList.h>
#include <Meow2/Graphics/Rhi/IRhiSwapchain.h>
#include <Meow2/Graphics/Rhi/RhiTypes.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

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
///     list->beginRendering(*swapchain, Colors::c_black);
///     list->endRendering();
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

    /// @brief Creates a buffer and gives it a slot in the heap.
    /// @param desc Size and memory type.
    /// @return The handle, or an error (out of memory, or the heap is full).
    /// @note Thread-safe.
    [[nodiscard]] virtual Result<BufferHandle> createBuffer(const BufferDesc& desc) = 0;

    /// @brief Destroys a buffer. The handle stops working at once; the GPU object and its heap slot stay until the GPU
    ///        has finished every frame that could use it.
    /// @param buffer The buffer. A stale handle is ignored.
    /// @note Thread-safe.
    virtual void destroy(BufferHandle buffer) = 0;

    /// @brief A buffer's memory, for RhiMemory::Upload and RhiMemory::Readback buffers.
    /// @param buffer The buffer.
    /// @return Its bytes, valid until it's destroyed; empty for a RhiMemory::GpuOnly buffer or a stale handle.
    /// @note Thread-safe.
    [[nodiscard]] virtual std::span<std::byte> mappedMemory(BufferHandle buffer) = 0;

    /// @brief A buffer's heap slot, for `ResourceDescriptorHeap[index]` as a (RW)ByteAddressBuffer.
    /// @param buffer The buffer.
    /// @return The index; c_invalidBindlessIndex for a stale handle.
    /// @note Thread-safe.
    [[nodiscard]] virtual std::uint32_t bindlessIndex(BufferHandle buffer) = 0;

    /// @brief Creates a texture and gives it a slot in the heap. It starts in ResourceState::Undefined.
    /// @param desc Size and format.
    /// @return The handle, or an error.
    /// @note Thread-safe.
    [[nodiscard]] virtual Result<TextureHandle> createTexture(const TextureDesc& desc) = 0;

    /// @brief Destroys a texture, with the same delay as destroy(BufferHandle).
    /// @param texture The texture. A stale handle is ignored.
    /// @note Thread-safe.
    virtual void destroy(TextureHandle texture) = 0;

    /// @brief A texture's heap slot, for `ResourceDescriptorHeap[index]` as a Texture2D.
    /// @param texture The texture.
    /// @return The index; c_invalidBindlessIndex for a stale handle or a depth texture.
    /// @note Thread-safe.
    [[nodiscard]] virtual std::uint32_t bindlessIndex(TextureHandle texture) = 0;

    /// @brief A texture's writable heap slot, for `ResourceDescriptorHeap[index]` as a RWTexture2D.
    /// @param texture The texture, created with TextureDesc::storage.
    /// @return The index; c_invalidBindlessIndex otherwise.
    /// @note Thread-safe.
    [[nodiscard]] virtual std::uint32_t bindlessStorageIndex(TextureHandle texture) = 0;

    /// @brief Creates a sampler and gives it a slot in the sampler heap.
    /// @param desc Filtering and addressing.
    /// @return The handle, or an error.
    /// @note Thread-safe.
    [[nodiscard]] virtual Result<SamplerHandle> createSampler(const SamplerDesc& desc) = 0;

    /// @brief Destroys a sampler, with the same delay as destroy(BufferHandle).
    /// @param sampler The sampler. A stale handle is ignored.
    /// @note Thread-safe.
    virtual void destroy(SamplerHandle sampler) = 0;

    /// @brief A sampler's slot, for `SamplerDescriptorHeap[index]`.
    /// @param sampler The sampler.
    /// @return The index; for a stale handle, c_defaultSamplerIndex.
    /// @note Thread-safe.
    [[nodiscard]] virtual std::uint32_t bindlessIndex(SamplerHandle sampler) = 0;

    /// @brief Creates a graphics pipeline. This is where the driver compiles shaders for the exact GPU, so it's slow
    ///        unless the pipeline cache already has the result: create pipelines up front.
    /// @param desc Shaders and targets.
    /// @return The handle, or an error.
    /// @note Thread-safe.
    [[nodiscard]] virtual Result<PipelineHandle> createGraphicsPipeline(const GraphicsPipelineDesc& desc) = 0;

    /// @brief Creates a compute pipeline.
    /// @param desc The shader.
    /// @return The handle, or an error.
    /// @note Thread-safe.
    [[nodiscard]] virtual Result<PipelineHandle> createComputePipeline(const ComputePipelineDesc& desc) = 0;

    /// @brief Destroys a pipeline, with the same delay as destroy(BufferHandle).
    /// @param pipeline The pipeline. A stale handle is ignored.
    /// @note Thread-safe.
    virtual void destroy(PipelineHandle pipeline) = 0;

    /// @brief What the driver has compiled so far, to save and pass to the next run through
    ///        RhiDeviceDesc::pipelineCache.
    /// @return The bytes; empty if there's nothing or the driver refused.
    /// @note Thread-safe.
    [[nodiscard]] virtual std::vector<std::byte> pipelineCacheData() = 0;

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
