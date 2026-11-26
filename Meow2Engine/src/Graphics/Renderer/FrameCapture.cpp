/// @file
/// @brief FrameCapture (FrameCapture.h).

#include <Meow2/Graphics/Renderer/FrameCapture.h>

#include <Meow2/Graphics/Shader/BindlessLayout.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <format>
#include <utility>

namespace Meow2::Renderer
{
TextureDisplaySettings defaultDisplaySettings(bool depth)
{
    return depth ? TextureDisplaySettings{.channels = 0b0001, .rangeMin = 0.0F, .rangeMax = 0.05F}
                 : TextureDisplaySettings{};
}

FrameCapture::FrameCapture(RenderDevice& device, PipelineLibrary& pipelines, AssetId shader, Log::Logger& log)
    : m_device(device)
    , m_pipelines(pipelines)
    , m_log(log)
{
    const auto make = [&](const char* entry, Rhi::RhiFormat format, const char* name) -> std::optional<PipelineId>
    {
        Result<PipelineId> pipeline = pipelines.add(GraphicsPipelineRequest{
            .shader = shader, .pixelEntry = entry, .colorFormat = format, .depthWrite = false, .name = name});
        if (!pipeline)
        {
            MEOW2_LOG_ERROR(m_log, "Frame capture pipeline '{}' not built: {}", name, pipeline.error());
            return std::nullopt;
        }
        return *pipeline;
    };
    m_copyColor = make("psCopy", Rhi::RhiFormat::Rgba16Float, "Capture copy (colour)");
    m_copyDepth = make("psCopy", Rhi::RhiFormat::R32Float, "Capture copy (depth)");
    m_display = make("psDisplay", Rhi::RhiFormat::Rgba8Srgb, "Capture picture");
    // The copies can't be made again on another device (the frame is gone), so a backend switch drops the capture.
    m_deviceUser = device.addUser(
        "FrameCapture", [this](Rhi::IRhiDevice& old) { release(old); },
        [](Rhi::IRhiDevice&) -> Result<void> { return {}; });
}

FrameCapture::~FrameCapture()
{
    if (m_device.hasDevice())
    {
        m_device.device().waitIdle();
        release(m_device.device());
    }
}

void FrameCapture::release(Rhi::IRhiDevice& device)
{
    const auto destroyTexture = [&device](Rhi::TextureHandle& texture)
    {
        if (texture.isSet())
        {
            device.destroy(texture);
        }
        texture = {};
    };
    const auto destroyBuffer = [&device](Rhi::BufferHandle& buffer)
    {
        if (buffer.isSet())
        {
            device.destroy(buffer);
        }
        buffer = {};
    };
    for (CaptureSnapshot& snapshot : m_snapshots)
    {
        destroyTexture(snapshot.texture);
    }
    m_snapshots.clear();
    m_states.clear();
    m_flow.reset();
    for (Rhi::TextureHandle& texture : m_retired)
    {
        destroyTexture(texture);
    }
    m_retired.clear();
    destroyTexture(m_imageTarget);
    m_imageTargetInfo = {};
    m_imageTargetState = Rhi::ResourceState::Undefined;
    m_imageTargetResource.reset();
    for (Rhi::BufferHandle& buffer : m_readback)
    {
        destroyBuffer(buffer);
    }
    m_readbackBytes = {};
    for (Rhi::BufferHandle& buffer : m_displayRecords)
    {
        destroyBuffer(buffer);
    }
    m_imageRequest.reset();
    m_imageInFlight = {};
    m_image.reset();
}

void FrameCapture::clear()
{
    // This frame's graph may already name the copies (a picture being drawn): freed after the frame.
    for (CaptureSnapshot& snapshot : m_snapshots)
    {
        if (snapshot.texture.isSet())
        {
            m_retired.push_back(snapshot.texture);
        }
    }
    m_snapshots.clear();
    m_states.clear();
    m_flow.reset();
    m_imageRequest.reset();
}

std::optional<std::uint32_t> FrameCapture::snapshotFor(std::uint32_t pass, std::uint32_t resource) const
{
    for (std::uint32_t i = 0; i < m_snapshots.size(); ++i)
    {
        if (m_snapshots[i].pass == pass && m_snapshots[i].resource == resource)
        {
            return i;
        }
    }
    return std::nullopt;
}

void FrameCapture::beginFrame(std::uint32_t frameSlot)
{
    ++m_frame;
    // This slot's last frame has finished (IRhiDevice::beginFrame() waited for its fence): its picture has landed.
    if (std::optional<ImageInFlight> flight = std::exchange(m_imageInFlight[frameSlot], std::nullopt))
    {
        const std::span<const std::byte> bytes = m_device.device().mappedMemory(m_readback[frameSlot]);
        const std::size_t size = std::size_t{flight->width} * flight->height * 4;
        if (bytes.size() >= size)
        {
            m_image = CapturedImage{.snapshot = flight->request.snapshot,
                                    .settings = flight->request.settings,
                                    .width = flight->width,
                                    .height = flight->height,
                                    .pixels = {bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(size)}};
        }
    }
}

std::optional<GraphResource> FrameCapture::importSnapshot(RenderGraph& graph, std::uint32_t index)
{
    if (index >= m_snapshots.size() || !m_states[index].ready)
    {
        return std::nullopt; // no such snapshot, or its copy is still being made in this frame
    }
    SnapshotState& state = m_states[index];
    if (!state.resource)
    {
        const CaptureSnapshot& snapshot = m_snapshots[index];
        state.resource = graph.import(snapshot.texture, state.state,
                                      std::format("Capture: {} after {}", m_flow->resources[snapshot.resource].name,
                                                  m_flow->passes[snapshot.pass].name));
    }
    return state.resource;
}

void FrameCapture::requestImage(std::uint32_t index, const TextureDisplaySettings& settings, std::uint32_t maxSide)
{
    m_imageRequest = ImageRequest{.snapshot = index, .settings = settings, .maxSide = std::max(maxSide, 1U)};
}

bool FrameCapture::imagePending() const
{
    return m_imageRequest.has_value() ||
           std::ranges::any_of(m_imageInFlight, [](const std::optional<ImageInFlight>& f) { return f.has_value(); });
}

std::optional<CapturedImage> FrameCapture::takeImage()
{
    return std::exchange(m_image, std::nullopt);
}

void FrameCapture::afterPasses(RenderGraph& graph, std::uint32_t frameSlot)
{
    if (m_requested)
    {
        capture(graph);
    }
    if (m_imageRequest && m_imageRequest->snapshot < m_snapshots.size() &&
        m_states[m_imageRequest->snapshot].ready && m_display)
    {
        addImagePasses(graph, frameSlot, *std::exchange(m_imageRequest, std::nullopt));
    }
}

void FrameCapture::capture(RenderGraph& graph)
{
    m_requested = false;
    clear();
    Rhi::IRhiDevice& device = m_device.device();
    m_flow = describeGraph(graph, device);
    m_capturedFrame = m_frame;
    // After every pass that writes a texture, a raw copy of it. Last pass first, so inserting doesn't move the
    // passes still to visit.
    for (std::uint32_t pass = static_cast<std::uint32_t>(m_flow->passes.size()); pass-- > 0;)
    {
        for (const FlowAccess& access : m_flow->passes[pass].accesses)
        {
            const FlowResource& resource = m_flow->resources[access.resource];
            if (!access.writes || !resource.texture)
            {
                continue;
            }
            const bool depth = isDepthFormat(resource.info.format);
            const Rhi::TextureInfo info{.width = std::max(resource.info.width, 1U),
                                        .height = std::max(resource.info.height, 1U),
                                        .format = depth ? Rhi::RhiFormat::R32Float : Rhi::RhiFormat::Rgba16Float};
            Result<Rhi::TextureHandle> copy = device.createTexture({.width = info.width,
                                                                    .height = info.height,
                                                                    .format = info.format,
                                                                    .renderTarget = true,
                                                                    .tag = Memory::MemTag::Renderer,
                                                                    .name = "Capture snapshot"});
            if (!copy)
            {
                MEOW2_LOG_ERROR(m_log, "Capture snapshot not made: {}", copy.error());
                continue;
            }
            const GraphResource target = graph.import(
                *copy, Rhi::ResourceState::Undefined,
                std::format("Capture: {} after {}", resource.name, m_flow->passes[pass].name));
            const std::optional<Rhi::TextureHandle> source = graph.resourceTexture(GraphResource{.index = access.resource});
            const std::optional<PipelineId> pipeline = depth ? m_copyDepth : m_copyColor;
            Shader::PushConstants push{};
            push.instance = source ? device.bindlessIndex(*source) : 0U;
            graph.insertPassAfter(
                pass, {.name = std::format("Capture {}", resource.name),
                       .accesses = {{GraphResource{.index = access.resource}, Rhi::ResourceState::ShaderRead},
                                    {target, Rhi::ResourceState::ColorTarget}},
                       .record =
                           [this, push, pipeline, texture = *copy](Rhi::IRhiCommandList& list)
                       {
                           list.beginRendering(Rhi::RenderTargets{.color = texture});
                           if (pipeline)
                           {
                               list.setPipeline(m_pipelines.get(*pipeline));
                               list.setPushConstants(push);
                               list.draw(3);
                           }
                           list.endRendering();
                       },
                       .small = true,
                       .debug = true});
            m_snapshots.push_back(
                {.pass = pass, .resource = access.resource, .depth = depth, .texture = *copy, .info = info});
            m_states.push_back({.state = Rhi::ResourceState::Undefined, .resource = target, .ready = false});
        }
    }
    std::ranges::reverse(m_snapshots); // in pass order
    std::ranges::reverse(m_states);
    MEOW2_LOG_INFO(m_log, "Frame captured: {} passes, {} textures kept", m_flow->passes.size(), m_snapshots.size());
}

void FrameCapture::addImagePasses(RenderGraph& graph, std::uint32_t frameSlot, const ImageRequest& request)
{
    Rhi::IRhiDevice& device = m_device.device();
    const CaptureSnapshot& snapshot = m_snapshots[request.snapshot];
    // At most maxSide pixels a side, never larger than the texture: a preview, not a copy of it.
    const float scale = std::min(1.0F, static_cast<float>(request.maxSide) /
                                           static_cast<float>(std::max(snapshot.info.width, snapshot.info.height)));
    const auto width = std::max(1U, static_cast<std::uint32_t>(std::lround(static_cast<float>(snapshot.info.width) * scale)));
    const auto height =
        std::max(1U, static_cast<std::uint32_t>(std::lround(static_cast<float>(snapshot.info.height) * scale)));

    // The picture on the GPU, made (again) when the size changes.
    if (!m_imageTarget.isSet() || m_imageTargetInfo.width != width || m_imageTargetInfo.height != height)
    {
        if (m_imageTarget.isSet())
        {
            m_retired.push_back(m_imageTarget);
        }
        m_imageTarget = {};
        Result<Rhi::TextureHandle> target = device.createTexture({.width = width,
                                                                  .height = height,
                                                                  .format = Rhi::RhiFormat::Rgba8Srgb,
                                                                  .renderTarget = true,
                                                                  .tag = Memory::MemTag::Renderer,
                                                                  .name = "Capture picture"});
        if (!target)
        {
            MEOW2_LOG_ERROR(m_log, "Capture picture not made: {}", target.error());
            return;
        }
        m_imageTarget = *target;
        m_imageTargetInfo = {.width = width, .height = height, .format = Rhi::RhiFormat::Rgba8Srgb};
        m_imageTargetState = Rhi::ResourceState::Undefined;
    }
    // This slot's readback buffer and display record, made (again) when too small.
    const std::uint64_t bytes = std::uint64_t{width} * height * 4;
    if (!m_readback[frameSlot].isSet() || m_readbackBytes[frameSlot] != bytes)
    {
        if (m_readback[frameSlot].isSet())
        {
            device.destroy(m_readback[frameSlot]); // its last frame is done: beginFrame() waited for this slot
        }
        Result<Rhi::BufferHandle> readback = device.createBuffer({.size = bytes,
                                                                  .memory = Rhi::RhiMemory::Readback,
                                                                  .tag = Memory::MemTag::Renderer,
                                                                  .name = "Capture picture readback"});
        if (!readback)
        {
            MEOW2_LOG_ERROR(m_log, "Capture readback not made: {}", readback.error());
            m_readback[frameSlot] = {};
            return;
        }
        m_readback[frameSlot] = *readback;
        m_readbackBytes[frameSlot] = bytes;
    }
    if (!m_displayRecords[frameSlot].isSet())
    {
        Result<Rhi::BufferHandle> records = device.createBuffer({.size = sizeof(Shader::DebugDisplay),
                                                                 .memory = Rhi::RhiMemory::Upload,
                                                                 .tag = Memory::MemTag::Renderer,
                                                                 .name = "Capture picture settings"});
        if (!records)
        {
            MEOW2_LOG_ERROR(m_log, "Capture picture settings not made: {}", records.error());
            return;
        }
        m_displayRecords[frameSlot] = *records;
    }
    const std::optional<GraphResource> source = importSnapshot(graph, request.snapshot);
    if (!source)
    {
        return;
    }
    Shader::DebugDisplay record{};
    record.source = device.bindlessIndex(snapshot.texture);
    // Linear when shrinking (a smooth preview); its own size is copied texel for texel either way.
    record.sampler = Rhi::c_defaultSamplerIndex;
    record.channels = request.settings.channels;
    record.isDepth = snapshot.depth ? 1U : 0U;
    record.rangeMin = request.settings.rangeMin;
    record.rangeMax = request.settings.rangeMax;
    std::memcpy(device.mappedMemory(m_displayRecords[frameSlot]).data(), &record, sizeof(record));

    m_imageTargetResource = graph.import(m_imageTarget, m_imageTargetState, "Capture picture");
    const GraphResource readback = graph.import(m_readback[frameSlot], Rhi::ResourceState::Undefined, "Capture readback");
    Shader::PushConstants push{};
    push.frameBuffer = device.bindlessIndex(m_displayRecords[frameSlot]);
    push.instance = 0;
    graph.addPass({.name = "Capture picture",
                   .accesses = {{*source, Rhi::ResourceState::ShaderRead},
                                {*m_imageTargetResource, Rhi::ResourceState::ColorTarget}},
                   .record =
                       [this, push, texture = m_imageTarget](Rhi::IRhiCommandList& list)
                   {
                       list.beginRendering(Rhi::RenderTargets{
                           .color = texture, .clearColor = Math::Color{.r = 0.0F, .g = 0.0F, .b = 0.0F, .a = 1.0F}});
                       list.setPipeline(m_pipelines.get(*m_display));
                       list.setPushConstants(push);
                       list.draw(3);
                       list.endRendering();
                   },
                   .small = true,
                   .debug = true});
    graph.addPass({.name = "Capture picture readback",
                   .accesses = {{*m_imageTargetResource, Rhi::ResourceState::CopySource},
                                {readback, Rhi::ResourceState::CopyTarget}},
                   .record = [texture = m_imageTarget, buffer = m_readback[frameSlot]](Rhi::IRhiCommandList& list)
                   { list.copyTextureToBuffer(texture, buffer); },
                   .small = true,
                   .debug = true});
    m_imageInFlight[frameSlot] = ImageInFlight{.request = request, .width = width, .height = height};
}

void FrameCapture::endFrame(const RenderGraph& graph)
{
    for (SnapshotState& state : m_states)
    {
        if (state.resource)
        {
            state.state = graph.finalState(*state.resource);
            state.resource.reset();
            state.ready = true; // the copy pass ran in this graph
        }
    }
    if (m_imageTargetResource)
    {
        m_imageTargetState = graph.finalState(*m_imageTargetResource);
        m_imageTargetResource.reset();
    }
    // The device defers destruction until the GPU is done with them.
    for (const Rhi::TextureHandle texture : m_retired)
    {
        m_device.device().destroy(texture);
    }
    m_retired.clear();
}
} // namespace Meow2::Renderer
