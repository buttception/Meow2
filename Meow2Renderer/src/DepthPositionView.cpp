/// @file
/// @brief DepthPositionView (DepthPositionView.h).

#include "DepthPositionView.h"

#include <DirectXMath.h>

#include <cstring>

namespace Meow2::RendererApp
{
DepthPositionView::DepthPositionView(Renderer::RenderDevice& device, Renderer::PipelineLibrary& pipelines,
                                     AssetId shader, Log::Logger& log)
    : m_device(device)
    , m_pipelines(pipelines)
    , m_log(log)
{
    if (Result<Renderer::PipelineId> pipeline = pipelines.add(
            Renderer::ComputePipelineRequest{.shader = shader, .entry = "csPosition", .name = "Position from depth"});
        pipeline)
    {
        m_pipeline = *pipeline;
    }
    else
    {
        MEOW2_LOG_ERROR(log, "Position view pipeline not built: {}", pipeline.error());
    }
    // The texture and buffers are made on demand, so a new device has nothing to restore.
    m_deviceUser = device.addUser(
        "DepthPositionView", [this](Rhi::IRhiDevice& old) { release(old); },
        [](Rhi::IRhiDevice&) -> Result<void> { return {}; });
}

DepthPositionView::~DepthPositionView()
{
    if (m_device.hasDevice())
    {
        release(m_device.device());
    }
}

void DepthPositionView::release(Rhi::IRhiDevice& device)
{
    if (m_texture.isSet())
    {
        device.destroy(m_texture); // deferred until the GPU is done with it
        m_texture = {};
    }
    m_width = 0;
    m_height = 0;
    m_state = Rhi::ResourceState::Undefined;
    for (Rhi::BufferHandle& buffer : m_constants)
    {
        if (buffer.isSet())
        {
            device.destroy(buffer);
            buffer = {};
        }
    }
}

std::optional<Renderer::GraphResource> DepthPositionView::addPass(Renderer::RenderGraph& graph, std::uint32_t frameSlot,
                                                                  const Shader::FrameConstants& frame,
                                                                  Renderer::GraphResource depth,
                                                                  Rhi::TextureHandle depthTexture, std::uint32_t width,
                                                                  std::uint32_t height)
{
    m_resource.reset();
    if (!m_pipeline || width == 0 || height == 0)
    {
        return std::nullopt;
    }
    Rhi::IRhiDevice& device = m_device.device();
    if (!m_texture.isSet() || m_width != width || m_height != height)
    {
        if (m_texture.isSet())
        {
            device.destroy(m_texture);
            m_texture = {};
        }
        // 16-bit floats: exact to about a millimetre within a few metres of the origin, half a metre 1 km out; enough
        // to see the reconstruction, which is all this is for.
        Result<Rhi::TextureHandle> made = device.createTexture({.width = width,
                                                                .height = height,
                                                                .format = Rhi::RhiFormat::Rgba16Float,
                                                                .storage = true,
                                                                .tag = Memory::MemTag::Renderer,
                                                                .name = "Position (debug)"});
        if (!made)
        {
            MEOW2_LOG_ERROR(m_log, "Can't create the position texture: {}", made.error());
            return std::nullopt;
        }
        m_texture = *made;
        m_width = width;
        m_height = height;
        m_state = Rhi::ResourceState::Undefined;
    }
    Rhi::BufferHandle& buffer = m_constants[frameSlot];
    if (!buffer.isSet())
    {
        Result<Rhi::BufferHandle> made = device.createBuffer({.size = sizeof(PositionConstants),
                                                              .memory = Rhi::RhiMemory::Upload,
                                                              .tag = Memory::MemTag::Renderer,
                                                              .name = "Position constants"});
        if (!made)
        {
            return std::nullopt;
        }
        buffer = *made;
    }
    using namespace DirectX;
    PositionConstants constants{};
    XMStoreFloat4x4(&constants.inverseViewProjection, XMMatrixInverse(nullptr, XMLoadFloat4x4(&frame.viewProjection)));
    constants.depthTexture = device.bindlessIndex(depthTexture);
    constants.output = device.bindlessStorageIndex(m_texture);
    constants.width = width;
    constants.height = height;
    // This slot's last frame is done (beginFrame() waited for it), so its buffer can be rewritten.
    std::memcpy(device.mappedMemory(buffer).data(), &constants, sizeof(constants));

    Shader::PushConstants push{};
    push.userData = device.bindlessIndex(buffer);
    const Renderer::GraphResource position = graph.import(m_texture, m_state, "Position (debug)");
    graph.addPass({.name = "Position from depth",
                   .accesses = {{depth, Rhi::ResourceState::ShaderRead}, {position, Rhi::ResourceState::ShaderWrite}},
                   .record =
                       [this, push, width, height](Rhi::IRhiCommandList& list)
                   {
                       list.setPipeline(m_pipelines.get(*m_pipeline));
                       list.setPushConstants(push);
                       list.dispatch((width + 7) / 8, (height + 7) / 8); // 8 × 8 threads per group
                   },
                   .small = true,
                   .debug = true});
    m_resource = position;
    return position;
}

void DepthPositionView::endFrame(const Renderer::RenderGraph& graph)
{
    if (m_resource)
    {
        m_state = graph.finalState(*m_resource);
        m_resource.reset(); // a frame without the pass mustn't read this index from its own graph
    }
}
} // namespace Meow2::RendererApp
