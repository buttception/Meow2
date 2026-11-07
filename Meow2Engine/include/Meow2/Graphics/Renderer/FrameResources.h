/// @file
/// @brief Per-frame GPU helpers: UploadRing, ReadbackQueue and OffscreenTarget.

#pragma once

#include <Meow2/Core/Result.h>
#include <Meow2/Graphics/Rhi/IRhiDevice.h>
#include <Meow2/Math/Color.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace Meow2::Renderer
{
/// @brief A piece of upload memory, valid until this frame slot comes round again. Plain data.
struct UploadAllocation
{
    Rhi::BufferHandle buffer;    ///< The buffer it's in.
    std::uint32_t heapIndex = 0; ///< The buffer's heap slot, for shaders.
    std::uint64_t offset = 0;    ///< Where it starts in the buffer, in bytes.
    std::span<std::byte> memory; ///< Where the CPU writes.
};

/// @brief CPU-writable GPU memory for one frame: the GPU-side twin of the frame arena.
///
/// One buffer per frame in flight. Allocating bumps an offset, and a slot's buffer is reused once the GPU has finished
/// the frame that last used it. Nothing is freed one by one.
/// @par Thread safety
/// allocate() is thread-safe and lock-free. beginFrame() on one thread, with no job allocating.
class UploadRing
{
public:
    /// @brief Creates the buffers.
    /// @param device        The device. Must outlive the ring.
    /// @param bytesPerFrame How much one frame may use.
    /// @return The ring (not movable: it holds an atomic), or the error from creating a buffer.
    [[nodiscard]] static Result<std::unique_ptr<UploadRing>> create(Rhi::IRhiDevice& device,
                                                                    std::uint64_t bytesPerFrame);

    /// @brief Use create().
    /// @param device The device. Must outlive the ring.
    explicit UploadRing(Rhi::IRhiDevice& device);

    /// @brief Destroys the buffers.
    ~UploadRing();

    UploadRing(const UploadRing&) = delete;            ///< Not copyable: owns buffers.
    UploadRing& operator=(const UploadRing&) = delete; ///< Not copyable: owns buffers.

    /// @brief Starts a frame slot: the whole buffer is free again.
    /// @param frameSlot IRhiDevice::frameSlot(), after IRhiDevice::beginFrame().
    void beginFrame(std::uint32_t frameSlot);

    /// @brief Takes memory for this frame.
    /// @param size      Bytes; > 0.
    /// @param alignment Alignment in bytes, a power of two.
    /// @return The memory, or ErrorCode::OutOfMemory if the frame has used up its budget.
    [[nodiscard]] Result<UploadAllocation> allocate(std::uint64_t size, std::uint64_t alignment = 16);

private:
    Rhi::IRhiDevice& m_device;
    std::array<Rhi::BufferHandle, Rhi::c_maxFramesInFlight> m_buffers{};
    std::uint32_t m_slot = 0;
    std::uint64_t m_capacity = 0;
    std::atomic<std::uint64_t> m_used{0};
};

/// @brief Reads GPU data back without stalling: the answer arrives when its frame slot comes round again.
///
/// A delay of a couple of frames is fine for what this is for, such as editor picking. The alternative, waiting for the
/// GPU, would stop the whole frame.
/// @par Thread safety
/// Not thread-safe.
class ReadbackQueue
{
public:
    /// @brief Called with the bytes, on the thread that calls beginFrame(). They're only valid during the call.
    using Callback = std::function<void(std::span<const std::byte>)>;

    /// @brief An empty queue.
    /// @param device The device. Must outlive the queue.
    explicit ReadbackQueue(Rhi::IRhiDevice& device);

    /// @brief Drops what's pending without calling back.
    ~ReadbackQueue();

    ReadbackQueue(const ReadbackQueue&) = delete;            ///< Not copyable: owns buffers.
    ReadbackQueue& operator=(const ReadbackQueue&) = delete; ///< Not copyable: owns buffers.

    /// @brief Starts a frame slot: delivers everything requested the last time this slot ran.
    /// @param frameSlot IRhiDevice::frameSlot(), after IRhiDevice::beginFrame().
    void beginFrame(std::uint32_t frameSlot);

    /// @brief Copies a buffer to the CPU.
    /// @param list     The list to record into.
    /// @param source   Read from; in ResourceState::CopySource.
    /// @param size     Bytes.
    /// @param callback Gets the bytes.
    /// @return An error if the staging buffer can't be created; the callback then never runs.
    [[nodiscard]] Result<void> request(Rhi::IRhiCommandList& list, Rhi::BufferHandle source, std::uint64_t size,
                                       Callback callback);

    /// @brief Copies a texture's first mip level to the CPU.
    /// @param list     The list to record into.
    /// @param source   Read from; in ResourceState::CopySource.
    /// @param size     Bytes: width × height × bytes per pixel.
    /// @param callback Gets the bytes, tightly packed.
    /// @return An error if the staging buffer can't be created; the callback then never runs.
    [[nodiscard]] Result<void> request(Rhi::IRhiCommandList& list, Rhi::TextureHandle source, std::uint64_t size,
                                       Callback callback);

private:
    /// @brief One copy on its way.
    struct Pending
    {
        Rhi::BufferHandle buffer; ///< Where the GPU writes.
        std::uint64_t size = 0;   ///< Bytes.
        Callback callback;        ///< Who wants them.
    };

    Rhi::IRhiDevice& m_device;
    std::uint32_t m_slot = 0;
    std::array<std::vector<Pending>, Rhi::c_maxFramesInFlight> m_pending;
};

/// @brief A colour texture to draw to with no window, and its pixels on the CPU: headless rendering.
/// @par Thread safety
/// Not thread-safe.
class OffscreenTarget
{
public:
    /// @brief Creates the texture and the readback buffer.
    /// @param device The device, which may have no presentation. Must outlive the target.
    /// @param width  Width in pixels; > 0.
    /// @param height Height in pixels; > 0.
    /// @return The target, or the error from creating them.
    [[nodiscard]] static Result<std::unique_ptr<OffscreenTarget>> create(Rhi::IRhiDevice& device, std::uint32_t width,
                                                                         std::uint32_t height);

    /// @brief Use create().
    /// @param device The device. Must outlive the target.
    /// @param width  Width in pixels.
    /// @param height Height in pixels.
    OffscreenTarget(Rhi::IRhiDevice& device, std::uint32_t width, std::uint32_t height);

    /// @brief Destroys the texture and the buffer.
    ~OffscreenTarget();

    OffscreenTarget(const OffscreenTarget&) = delete;            ///< Not copyable: owns resources.
    OffscreenTarget& operator=(const OffscreenTarget&) = delete; ///< Not copyable: owns resources.

    /// @brief Starts drawing to it.
    /// @param list  The list.
    /// @param clear The colour to clear to.
    void begin(Rhi::IRhiCommandList& list, const Math::Color& clear);

    /// @brief Stops drawing, and copies the image to the CPU side.
    /// @param list The list.
    void end(Rhi::IRhiCommandList& list);

    /// @brief The pixels, RGBA8, top row first, once the GPU has finished the frame.
    /// @return The bytes, tightly packed.
    [[nodiscard]] std::span<const std::byte> pixels() const;

    /// @brief The format pipelines need to draw to it.
    /// @return RGBA8.
    [[nodiscard]] static constexpr Rhi::RhiFormat format()
    {
        return Rhi::RhiFormat::Rgba8Unorm;
    }

private:
    Rhi::IRhiDevice& m_device;
    std::uint32_t m_width;
    std::uint32_t m_height;
    Rhi::TextureHandle m_texture;
    Rhi::BufferHandle m_readback;
    bool m_used = false;
};
} // namespace Meow2::Renderer
