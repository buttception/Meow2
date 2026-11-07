/// @file
/// @brief The list of GPU work recorded for one frame.

#pragma once

#include <Meow2/Graphics/Rhi/RhiTypes.h>
#include <Meow2/Graphics/Shader/BindlessLayout.h>
#include <Meow2/Math/Color.h>

#include <cstdint>
#include <optional>

namespace Meow2::Rhi
{
class IRhiSwapchain;

/// @brief What to draw to with beginRendering(). Plain data. The targets must be in ResourceState::ColorTarget and
///        ResourceState::DepthTarget.
struct RenderTargets
{
    TextureHandle color{};                   ///< The colour target.
    std::optional<Math::Color> clearColor{}; ///< Clear to this; empty keeps what's there.
    TextureHandle depth{};                   ///< The depth target; an unset handle means none.
    std::optional<float> clearDepth{};       ///< Clear to this (0 is far, reversed Z); empty keeps what's there.
};

/// @brief Records GPU work.
///
/// Owned by the device and valid from IRhiDevice::beginFrame() to IRhiDevice::endFrame(). Nothing is bound except the
/// heap, which is always bound: a draw needs a pipeline and push constants, and shaders fetch the rest by index
/// (techstack.md §11). Barriers are explicit, because the caller knows how a resource moves between states.
/// @par Thread safety
/// Not thread-safe. Record on one thread at a time.
class IRhiCommandList
{
public:
    /// @brief Destroyed by its device.
    virtual ~IRhiCommandList() = default;

    IRhiCommandList(const IRhiCommandList&) = delete;            ///< Not copyable.
    IRhiCommandList& operator=(const IRhiCommandList&) = delete; ///< Not copyable.

    /// @brief Starts drawing to a swapchain's current image. The viewport and scissor cover it.
    /// @param swapchain The swapchain.
    /// @param clear     Linear colour to clear to; the image format applies the sRGB encoding. Empty keeps what's
    /// there.
    /// @pre IRhiDevice::acquire() returned true for it this frame (asserted).
    virtual void beginRendering(IRhiSwapchain& swapchain, std::optional<Math::Color> clear) = 0;

    /// @brief Starts drawing to textures. The viewport and scissor cover the colour target.
    /// @param targets The targets.
    virtual void beginRendering(const RenderTargets& targets) = 0;

    /// @brief Ends drawing to the current targets.
    virtual void endRendering() = 0;

    /// @brief Selects a graphics or compute pipeline.
    /// @param pipeline The pipeline.
    virtual void setPipeline(PipelineHandle pipeline) = 0;

    /// @brief Sets the 16 bytes every shader reads as `g_push`.
    /// @param constants The indices.
    virtual void setPushConstants(const Shader::PushConstants& constants) = 0;

    /// @brief Draws without a vertex buffer: shaders fetch their vertices with SV_VertexID.
    /// @param vertexCount   Vertices per instance.
    /// @param instanceCount Instances.
    virtual void draw(std::uint32_t vertexCount, std::uint32_t instanceCount = 1) = 0;

    /// @brief Runs the current compute pipeline.
    /// @param x Thread groups along X.
    /// @param y Thread groups along Y.
    /// @param z Thread groups along Z.
    virtual void dispatch(std::uint32_t x, std::uint32_t y = 1, std::uint32_t z = 1) = 0;

    /// @brief Makes earlier work on a buffer finish before later work on it starts.
    /// @param buffer The buffer.
    /// @param from   What it was used for.
    /// @param to     What it's used for next.
    virtual void barrier(BufferHandle buffer, ResourceState from, ResourceState to) = 0;

    /// @brief Makes earlier work on a texture finish before later work on it starts, and changes its layout.
    /// @param texture The texture. Every mip level changes together.
    /// @param from    What it was used for.
    /// @param to      What it's used for next.
    virtual void barrier(TextureHandle texture, ResourceState from, ResourceState to) = 0;

    /// @brief Copies bytes between buffers.
    /// @param source       Read from; in ResourceState::CopySource, or an Upload buffer.
    /// @param target       Written to; in ResourceState::CopyTarget.
    /// @param size         Bytes.
    /// @param sourceOffset Where to start reading.
    /// @param targetOffset Where to start writing.
    virtual void copyBuffer(BufferHandle source, BufferHandle target, std::uint64_t size,
                            std::uint64_t sourceOffset = 0, std::uint64_t targetOffset = 0) = 0;

    /// @brief Copies tightly packed pixels from a buffer to a texture's first mip level.
    /// @param source Read from.
    /// @param target Written to; in ResourceState::CopyTarget.
    virtual void copyBufferToTexture(BufferHandle source, TextureHandle target) = 0;

    /// @brief Copies a texture's first mip level to a buffer, tightly packed.
    /// @param source Read from; in ResourceState::CopySource.
    /// @param target Written to; in ResourceState::CopyTarget.
    virtual void copyTextureToBuffer(TextureHandle source, BufferHandle target) = 0;

protected:
    /// @brief Only backends create command lists.
    IRhiCommandList() = default;
};
} // namespace Meow2::Rhi
