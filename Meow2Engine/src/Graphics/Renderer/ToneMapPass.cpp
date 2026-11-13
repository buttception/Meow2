/// @file
/// @brief ToneMapPass (ToneMapPass.h).

#include "Meow2/Graphics/Renderer/ToneMapPass.h"

namespace Meow2::Renderer
{
Result<std::unique_ptr<ToneMapPass>> ToneMapPass::create(PipelineLibrary& pipelines, AssetId shader,
                                                         Rhi::RhiFormat targetFormat)
{
    Result<PipelineId> pipeline = pipelines.add(GraphicsPipelineRequest{.shader = shader,
                                                                        .options = {},
                                                                        .vertexEntry = "vsMain",
                                                                        .pixelEntry = "psMain",
                                                                        .colorFormat = targetFormat,
                                                                        .depthFormat = Rhi::RhiFormat::Unknown,
                                                                        .depthWrite = false,
                                                                        .cull = Rhi::CullMode::None,
                                                                        .blend = Rhi::BlendMode::None,
                                                                        .fill = Rhi::FillMode::Solid,
                                                                        .name = "Tone map"});
    if (!pipeline)
    {
        return Unexpected<Error>{pipeline.error()};
    }
    return std::make_unique<ToneMapPass>(pipelines, *pipeline);
}

ToneMapPass::ToneMapPass(PipelineLibrary& pipelines, PipelineId pipeline)
    : m_pipelines(pipelines)
    , m_pipeline(pipeline)
{
}

void ToneMapPass::addPass(RenderGraph& graph, GraphResource hdr, Rhi::TextureHandle texture,
                          Rhi::IRhiSwapchain& swapchain, Rhi::IRhiDevice& device)
{
    Shader::PushConstants push{};
    push.instance = device.bindlessIndex(texture);
    graph.addPass({.name = "Tone map",
                   .accesses = {{hdr, Rhi::ResourceState::ShaderRead}},
                   .record =
                       [this, push, &swapchain](Rhi::IRhiCommandList& list)
                   {
                       // Every texel is written, so nothing needs clearing.
                       list.beginRendering(swapchain, std::nullopt);
                       list.setPipeline(m_pipelines.get(m_pipeline));
                       list.setPushConstants(push);
                       list.draw(3);
                       list.endRendering();
                   },
                   .small = true});
}

void ToneMapPass::addPass(RenderGraph& graph, GraphResource hdr, Rhi::TextureHandle texture, GraphResource target,
                          Rhi::TextureHandle targetTexture, Rhi::IRhiDevice& device)
{
    Shader::PushConstants push{};
    push.instance = device.bindlessIndex(texture);
    graph.addPass({.name = "Tone map",
                   .accesses = {{hdr, Rhi::ResourceState::ShaderRead}, {target, Rhi::ResourceState::ColorTarget}},
                   .record =
                       [this, push, targetTexture](Rhi::IRhiCommandList& list)
                   {
                       list.beginRendering(Rhi::RenderTargets{.color = targetTexture});
                       list.setPipeline(m_pipelines.get(m_pipeline));
                       list.setPushConstants(push);
                       list.draw(3);
                       list.endRendering();
                   },
                   .small = true});
}
} // namespace Meow2::Renderer
