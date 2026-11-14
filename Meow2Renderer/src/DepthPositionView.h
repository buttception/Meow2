/// @file
/// @brief Meow2Renderer's Position debug view: a debug-only pass that writes the position each pixel's depth stands for
///        into a texture of its own. Renderer only, never in games.

#pragma once

#include <Meow2/Core/Log/Logger.h>
#include <Meow2/Graphics/Renderer/PipelineLibrary.h>
#include <Meow2/Graphics/Renderer/RenderDevice.h>
#include <Meow2/Graphics/Renderer/RenderGraph.h>
#include <Meow2/Graphics/Shader/BindlessLayout.h>

#include <array>
#include <cstdint>
#include <optional>

namespace Meow2::RendererApp
{
/// @brief The pass's constants, as `shaders/DebugPosition.hlsl` reads them. Plain data; 80 bytes.
struct PositionConstants
{
    Math::Mat4 inverseViewProjection{}; ///< Clip space → the instances' space.
    std::uint32_t depthTexture = 0;     ///< Heap index of the scene's depth.
    std::uint32_t output = 0;           ///< Writable heap index of the position texture.
    std::uint32_t width = 0;            ///< The textures' width in pixels.
    std::uint32_t height = 0;           ///< Their height.
};
static_assert(sizeof(PositionConstants) == 80, "shaders/DebugPosition.hlsl reads 80 bytes");

/// @brief Makes the position texture behind the Position debug view: **Position from depth** (a compute pass, marked
///        Renderer::GraphPass::debug) turns each pixel's depth back into a position with the same positionFromDepth()
///        deferred lighting uses, into an RGBA16F texture (xyz the position, alpha 1 where something was drawn).
///
/// Nothing in the frame reads it: deferred lighting rebuilds positions itself, without storing them. The pass runs
/// only while the view is selected, so it costs nothing otherwise; the render graph shows it, and its texture, dashed.
/// @par Thread safety
/// Not thread-safe; use on the main thread.
/// @par Lifetime
/// The RenderDevice, the PipelineLibrary and the Logger must outlive it.
class DepthPositionView
{
public:
    /// @brief Builds the pipeline; the texture and buffers are made when first needed.
    /// @param device    The device.
    /// @param pipelines Builds the pipeline.
    /// @param shader    `shaders/DebugPosition.hlsl`.
    /// @param log       Receives a pipeline that couldn't be built.
    DepthPositionView(Renderer::RenderDevice& device, Renderer::PipelineLibrary& pipelines, AssetId shader,
                      Log::Logger& log);

    /// @brief Releases the texture and the buffers.
    ~DepthPositionView();

    DepthPositionView(const DepthPositionView&) = delete;            ///< Not copyable: owns GPU resources.
    DepthPositionView& operator=(const DepthPositionView&) = delete; ///< Not copyable: owns GPU resources.

    /// @brief Adds the pass, after the scene has drawn its depth.
    /// @param graph        The frame's graph.
    /// @param frameSlot    IRhiDevice::frameSlot().
    /// @param frame        The camera (its viewProjection).
    /// @param depth        The scene's depth in the graph (GpuScene::depthResource()).
    /// @param depthTexture The same texture.
    /// @param width        The targets' width in pixels.
    /// @param height       Their height.
    /// @return The position texture in the graph, to show; std::nullopt if the pass couldn't be added.
    [[nodiscard]] std::optional<Renderer::GraphResource>
    addPass(Renderer::RenderGraph& graph, std::uint32_t frameSlot, const Shader::FrameConstants& frame,
            Renderer::GraphResource depth, Rhi::TextureHandle depthTexture, std::uint32_t width, std::uint32_t height);

    /// @brief The position texture (valid after addPass() succeeded).
    /// @return It.
    [[nodiscard]] Rhi::TextureHandle texture() const
    {
        return m_texture;
    }

    /// @brief Keeps the texture's state for the next frame. After the graph has run.
    /// @param graph The frame's graph.
    void endFrame(const Renderer::RenderGraph& graph);

private:
    /// @brief Destroys the texture and the buffers (a backend switch, or the end).
    /// @param device The device they were made on.
    void release(Rhi::IRhiDevice& device);

    Renderer::RenderDevice& m_device;
    Renderer::PipelineLibrary& m_pipelines;
    Log::Logger& m_log;
    std::optional<Renderer::PipelineId> m_pipeline;
    Rhi::TextureHandle m_texture;
    std::uint32_t m_width = 0;
    std::uint32_t m_height = 0;
    Rhi::ResourceState m_state = Rhi::ResourceState::Undefined;            ///< Where the last frame left the texture.
    std::optional<Renderer::GraphResource> m_resource;                     ///< This frame's, if the pass was added.
    std::array<Rhi::BufferHandle, Rhi::c_maxFramesInFlight> m_constants{}; ///< PositionConstants, per frame slot.
    Renderer::DeviceUser m_deviceUser; ///< Last: stops its callbacks before the members they use go away.
};
} // namespace Meow2::RendererApp
