/// @file
/// @brief InfiniteGrid (InfiniteGrid.h).

#include "InfiniteGrid.h"

#include <DirectXMath.h>

#include <cstring>

namespace Meow2::RendererApp
{
InfiniteGrid::InfiniteGrid(Renderer::RenderDevice& device, Renderer::PipelineLibrary& pipelines, AssetId shader,
                           Rhi::RhiFormat colorFormat, Log::Logger& log)
    : m_device(device)
    , m_pipelines(pipelines)
    , m_log(log)
{
    if (Result<Renderer::PipelineId> compute =
            pipelines.add(Renderer::ComputePipelineRequest{.shader = shader, .entry = "csGrid", .name = "Grid"});
        compute)
    {
        m_compute = *compute;
    }
    else
    {
        MEOW2_LOG_ERROR(log, "Grid pipeline not built: {}", compute.error());
    }
    if (Result<Renderer::PipelineId> composite =
            pipelines.add(Renderer::GraphicsPipelineRequest{.shader = shader,
                                                            .vertexEntry = "vsComposite",
                                                            .pixelEntry = "psComposite",
                                                            .colorFormat = colorFormat,
                                                            .depthFormat = Rhi::RhiFormat::Unknown,
                                                            .depthWrite = false,
                                                            .blend = Rhi::BlendMode::Alpha,
                                                            .name = "Grid composite"});
        composite)
    {
        m_composite = *composite;
    }
    else
    {
        MEOW2_LOG_ERROR(log, "Grid composite pipeline not built: {}", composite.error());
    }
    // The layer and buffers are made on demand, so a new device has nothing to restore.
    m_deviceUser = device.addUser(
        "InfiniteGrid", [this](Rhi::IRhiDevice& old) { release(old); },
        [](Rhi::IRhiDevice&) -> Result<void> { return {}; });
}

InfiniteGrid::~InfiniteGrid()
{
    if (m_device.hasDevice())
    {
        release(m_device.device());
    }
}

void InfiniteGrid::release(Rhi::IRhiDevice& device)
{
    if (m_layer.isSet())
    {
        device.destroy(m_layer); // deferred until the GPU is done with it
        m_layer = {};
    }
    m_width = 0;
    m_height = 0;
    m_layerState = Rhi::ResourceState::Undefined;
    for (Rhi::BufferHandle& buffer : m_constants)
    {
        if (buffer.isSet())
        {
            device.destroy(buffer);
            buffer = {};
        }
    }
}

GridConstants InfiniteGrid::constantsFor(const Shader::FrameConstants& frame, std::uint32_t width, std::uint32_t height)
{
    using namespace DirectX;
    GridConstants constants{};
    XMStoreFloat4x4(&constants.inverseViewProjection, XMMatrixInverse(nullptr, XMLoadFloat4x4(&frame.viewProjection)));
    constants.viewProjection = frame.viewProjection;
    constants.camera = {frame.cameraPosition.x, frame.cameraPosition.y, frame.cameraPosition.z, 1.0F};
    constants.width = width;
    constants.height = height;
    return constants;
}

void InfiniteGrid::addPasses(Renderer::RenderGraph& graph, std::uint32_t frameSlot, const Shader::FrameConstants& frame,
                             Renderer::GraphResource color, Rhi::TextureHandle colorTexture,
                             Renderer::GraphResource depth, Rhi::TextureHandle depthTexture, std::uint32_t width,
                             std::uint32_t height)
{
    if (!ready() || width == 0 || height == 0)
    {
        return;
    }
    Rhi::IRhiDevice& device = m_device.device();
    if (!m_layer.isSet() || m_width != width || m_height != height)
    {
        if (m_layer.isSet())
        {
            device.destroy(m_layer);
            m_layer = {};
        }
        Result<Rhi::TextureHandle> layer = device.createTexture({.width = width,
                                                                 .height = height,
                                                                 .format = Rhi::RhiFormat::Rgba16Float,
                                                                 .storage = true,
                                                                 .tag = Memory::MemTag::Renderer,
                                                                 .name = "Grid"});
        if (!layer)
        {
            MEOW2_LOG_ERROR(m_log, "Can't create the grid layer: {}", layer.error());
            return;
        }
        m_layer = *layer;
        m_width = width;
        m_height = height;
        m_layerState = Rhi::ResourceState::Undefined;
    }
    Rhi::BufferHandle& buffer = m_constants[frameSlot];
    if (!buffer.isSet())
    {
        Result<Rhi::BufferHandle> made = device.createBuffer({.size = sizeof(GridConstants),
                                                              .memory = Rhi::RhiMemory::Upload,
                                                              .tag = Memory::MemTag::Renderer,
                                                              .name = "Grid constants"});
        if (!made)
        {
            return;
        }
        buffer = *made;
    }
    GridConstants constants = constantsFor(frame, width, height);
    constants.depthTexture = device.bindlessIndex(depthTexture);
    constants.gridTexture = device.bindlessStorageIndex(m_layer);
    constants.gridRead = device.bindlessIndex(m_layer);
    // This slot's last frame is done (beginFrame() waited for it), so its buffer can be rewritten.
    std::memcpy(device.mappedMemory(buffer).data(), &constants, sizeof(constants));

    Shader::PushConstants push{};
    push.userData = device.bindlessIndex(buffer);
    const Renderer::GraphResource layer = graph.import(m_layer, m_layerState, "Grid");
    m_layerState = Rhi::ResourceState::ShaderRead; // where the composite leaves it
    graph.addPass({.name = "Grid",
                   .accesses = {{depth, Rhi::ResourceState::ShaderRead}, {layer, Rhi::ResourceState::ShaderWrite}},
                   .record =
                       [this, push, width, height](Rhi::IRhiCommandList& list)
                   {
                       list.setPipeline(m_pipelines.get(*m_compute));
                       list.setPushConstants(push);
                       list.dispatch((width + 7) / 8, (height + 7) / 8); // 8 × 8 threads per group
                   },
                   .small = true,
                   .debug = true});
    graph.addPass({.name = "Grid composite",
                   .accesses = {{layer, Rhi::ResourceState::ShaderRead}, {color, Rhi::ResourceState::ColorTarget}},
                   .record =
                       [this, push, colorTexture](Rhi::IRhiCommandList& list)
                   {
                       list.beginRendering(Rhi::RenderTargets{.color = colorTexture});
                       list.setPipeline(m_pipelines.get(*m_composite));
                       list.setPushConstants(push);
                       list.draw(3);
                       list.endRendering();
                   },
                   .small = true,
                   .debug = true});
}
} // namespace Meow2::RendererApp
