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

namespace
{
/// @brief The pass's push constants: the HDR texture, and when the target is a different size, its size and a linear
///        sampler, so the shader scales the image (a game view drawn at a lower render scale).
Shader::PushConstants tonePush(Rhi::IRhiDevice& device, Rhi::TextureHandle texture, std::uint32_t targetWidth,
                               std::uint32_t targetHeight)
{
    Shader::PushConstants push{};
    push.instance = device.bindlessIndex(texture);
    const Rhi::TextureInfo source = device.textureInfo(texture);
    if (source.width != targetWidth || source.height != targetHeight)
    {
        // frameBuffer = the target's size (16 bits each), instanceBuffer = the sampler; 0 means "same size".
        push.frameBuffer = (targetWidth & 0xFFFFU) | (targetHeight << 16U);
        push.instanceBuffer = Rhi::c_defaultSamplerIndex;
    }
    return push;
}
} // namespace

void ToneMapPass::addPass(RenderGraph& graph, GraphResource hdr, Rhi::TextureHandle texture,
                          Rhi::IRhiSwapchain& swapchain, Rhi::IRhiDevice& device)
{
    const Shader::PushConstants push = tonePush(device, texture, swapchain.width(), swapchain.height());
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
    const Rhi::TextureInfo targetInfo = device.textureInfo(targetTexture);
    const Shader::PushConstants push = tonePush(device, texture, targetInfo.width, targetInfo.height);
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
