/// @file
/// @brief ToneMapPass: the scene's HDR colour through the tone curve into the swapchain.

#pragma once

#include <Meow2/Core/Id/Uuid.h>
#include <Meow2/Core/Result.h>
#include <Meow2/Graphics/Renderer/PipelineLibrary.h>
#include <Meow2/Graphics/Renderer/RenderGraph.h>
#include <Meow2/Graphics/Rhi/IRhiDevice.h>

#include <memory>

namespace Meow2::Renderer
{
/// @brief Turns the scene's HDR colour into what the display shows: the Khronos PBR Neutral tone curve, written to a
///        swapchain (whose sRGB view encodes it).
///
/// The scene is drawn in linear light into a floating-point texture (exposure already applied by the shading), so
/// every pass before this one can be inspected as it really is, and post-processing has room to add to it. This is the
/// last scene pass; the UI draws on top of its result.
/// @par Thread safety
/// Not thread-safe; use on the main thread.
/// @par Lifetime
/// The PipelineLibrary must outlive it.
class ToneMapPass
{
public:
    /// @brief Builds its pipeline.
    /// @param pipelines   Builds and keeps the pipeline (rebuilt on a backend switch or shader reload).
    /// @param shader      The AssetId of `shaders/ToneMap.hlsl`.
    /// @param targetFormat The swapchain's format.
    /// @return The pass, or the error from building the pipeline.
    [[nodiscard]] static Result<std::unique_ptr<ToneMapPass>> create(PipelineLibrary& pipelines, AssetId shader,
                                                                     Rhi::RhiFormat targetFormat);

    /// @brief Use create().
    /// @param pipelines The library holding its pipeline.
    /// @param pipeline  The pipeline.
    ToneMapPass(PipelineLibrary& pipelines, PipelineId pipeline);

    /// @brief Adds the pass: reads `hdr`, writes the swapchain's acquired image.
    /// @param graph     The frame's graph, after every pass that writes `hdr`.
    /// @param hdr       The scene's colour in the graph.
    /// @param texture   The same texture (its heap index is read by the shader).
    /// @param swapchain The acquired swapchain; the same size as `texture`.
    /// @param device    For the texture's heap index.
    void addPass(RenderGraph& graph, GraphResource hdr, Rhi::TextureHandle texture, Rhi::IRhiSwapchain& swapchain,
                 Rhi::IRhiDevice& device);

private:
    PipelineLibrary& m_pipelines; ///< Holds the pipeline.
    PipelineId m_pipeline;        ///< The full-screen pass.
};
} // namespace Meow2::Renderer
