/// @file
/// @brief FrameInspector (FrameInspector.h).

#include "FrameInspector.h"

#include <Meow2/Graphics/Shader/BindlessLayout.h>

#include <imgui.h>

#include <algorithm>
#include <cstring>
#include <format>
#include <set>
#include <tuple>

namespace Meow2::RendererApp
{
namespace
{
constexpr std::uint32_t c_maxDisplays = 16; // DebugDisplay records per frame: live outputs plus the capture viewer

bool isDepthFormat(Rhi::RhiFormat format)
{
    return format == Rhi::RhiFormat::D32Float || format == Rhi::RhiFormat::R32Float;
}

const char* formatName(Rhi::RhiFormat format)
{
    switch (format)
    {
        case Rhi::RhiFormat::Bgra8Srgb:
            return "BGRA8 sRGB";
        case Rhi::RhiFormat::Rgba8Srgb:
            return "RGBA8 sRGB";
        case Rhi::RhiFormat::Rgba8Unorm:
            return "RGBA8";
        case Rhi::RhiFormat::Rgba16Float:
            return "RGBA16F";
        case Rhi::RhiFormat::R32Float:
            return "R32F";
        case Rhi::RhiFormat::D32Float:
            return "D32F";
        case Rhi::RhiFormat::Bc7Unorm:
        case Rhi::RhiFormat::Bc7Srgb:
            return "BC7";
        case Rhi::RhiFormat::Bc5Unorm:
            return "BC5";
        case Rhi::RhiFormat::Unknown:
            break;
    }
    return "?";
}

/// @brief A resource kind's colour for pins and wires: textures warm, buffers cool.
ImVec4 kindColour(ResourceKind kind)
{
    switch (kind)
    {
        case ResourceKind::ColorTexture:
            return ImVec4{0.96F, 0.62F, 0.22F, 1.0F}; // orange
        case ResourceKind::DepthTexture:
            return ImVec4{0.30F, 0.78F, 0.92F, 1.0F}; // cyan
        case ResourceKind::Buffer:
            return ImVec4{0.48F, 0.82F, 0.45F, 1.0F}; // green
        case ResourceKind::IndirectBuffer:
            return ImVec4{0.74F, 0.50F, 0.98F, 1.0F}; // violet
    }
    return ImVec4{1.0F, 1.0F, 1.0F, 1.0F};
}

/// @brief A dashed cubic Bézier, as ImDrawList::AddBezierCubic draws a solid one, through 32 points along it.
void dashedBezier(ImDrawList& draw, const std::array<ImVec2, 4>& control, ImU32 colour, float thickness, float dash)
{
    constexpr int c_segments = 32;
    std::array<ImVec2, c_segments + 1> points{};
    for (int i = 0; i <= c_segments; ++i)
    {
        const float t = static_cast<float>(i) / c_segments;
        const float u = 1.0F - t;
        const float w0 = u * u * u;
        const float w1 = 3.0F * u * u * t;
        const float w2 = 3.0F * u * t * t;
        const float w3 = t * t * t;
        points[static_cast<std::size_t>(i)] =
            ImVec2{w0 * control[0].x + w1 * control[1].x + w2 * control[2].x + w3 * control[3].x,
                   w0 * control[0].y + w1 * control[1].y + w2 * control[2].y + w3 * control[3].y};
    }
    dashedPolyline(draw, points, colour, thickness, dash);
}

/// @brief A state's name, as the pin details show it.
const char* stateName(Rhi::ResourceState state)
{
    switch (state)
    {
        case Rhi::ResourceState::Undefined:
            return "undefined";
        case Rhi::ResourceState::ColorTarget:
            return "colour target";
        case Rhi::ResourceState::DepthTarget:
            return "depth target";
        case Rhi::ResourceState::ShaderRead:
            return "shader read";
        case Rhi::ResourceState::ShaderWrite:
            return "shader write";
        case Rhi::ResourceState::CopySource:
            return "copy source";
        case Rhi::ResourceState::CopyTarget:
            return "copy target";
        case Rhi::ResourceState::IndirectArgs:
            return "indirect arguments";
        case Rhi::ResourceState::HostRead:
            return "CPU read";
    }
    return "?";
}

/// @brief How a new display starts: depth spread over the near range (reversed Z puts most of a scene near 0), colour
///        as it is.
FrameInspector::DisplaySettings defaultSettings(bool depth)
{
    return depth ? FrameInspector::DisplaySettings{.channels = 0b0001, .rangeMin = 0.0F, .rangeMax = 0.05F}
                 : FrameInspector::DisplaySettings{};
}
} // namespace

FrameFlow describeGraph(const Renderer::RenderGraph& graph, Rhi::IRhiDevice& device)
{
    FrameFlow flow;
    for (std::uint32_t i = 0; i < graph.resourceCount(); ++i)
    {
        const Renderer::GraphResource resource{.index = i};
        FlowResource out;
        out.name = graph.resourceName(resource).empty() ? std::format("Resource {}", i) : graph.resourceName(resource);
        if (const std::optional<Rhi::TextureHandle> texture = graph.resourceTexture(resource))
        {
            out.texture = true;
            out.info = device.textureInfo(*texture);
        }
        flow.resources.push_back(std::move(out));
    }
    for (const Renderer::GraphPass& pass : graph.passes())
    {
        FlowPass out{.name = pass.name, .accesses = {}, .debug = pass.debug, .skipped = pass.skipped};
        for (const Renderer::GraphAccess& access : pass.accesses)
        {
            out.accesses.push_back({.resource = access.resource.index,
                                    .state = access.state,
                                    .writes = Renderer::isWriteState(access.state)});
        }
        flow.passes.push_back(std::move(out));
    }
    markDebugResources(flow);
    return flow;
}

void markDebugResources(FrameFlow& flow)
{
    std::vector<bool> written(flow.resources.size(), false);
    std::vector<bool> onlyDebug(flow.resources.size(), true);
    for (const FlowPass& pass : flow.passes)
    {
        for (const FlowAccess& access : pass.accesses)
        {
            if (access.writes && access.resource < flow.resources.size())
            {
                written[access.resource] = true;
                onlyDebug[access.resource] = onlyDebug[access.resource] && pass.debug;
            }
        }
    }
    for (std::size_t i = 0; i < flow.resources.size(); ++i)
    {
        flow.resources[i].debug = written[i] && onlyDebug[i];
    }
}

std::optional<std::uint32_t> writerBefore(const FrameFlow& flow, std::uint32_t resource, std::uint32_t before)
{
    for (std::uint32_t pass = std::min<std::uint32_t>(before, static_cast<std::uint32_t>(flow.passes.size()));
         pass-- > 0;)
    {
        for (const FlowAccess& access : flow.passes[pass].accesses)
        {
            if (access.resource == resource && access.writes)
            {
                return pass;
            }
        }
    }
    return std::nullopt;
}

FlowLayout layoutFlow(const FrameFlow& flow)
{
    FlowLayout layout;
    layout.column.assign(flow.passes.size(), 0);
    layout.row.assign(flow.passes.size(), 0);
    std::vector<std::uint32_t> perColumn;
    for (std::uint32_t pass = 0; pass < flow.passes.size(); ++pass)
    {
        // Every access depends on the last writer: a read needs its data, and a later write (the sky over the opaque
        // pass's colour) must come after the earlier one.
        for (const FlowAccess& access : flow.passes[pass].accesses)
        {
            if (const std::optional<std::uint32_t> writer = writerBefore(flow, access.resource, pass))
            {
                layout.column[pass] = std::max(layout.column[pass], layout.column[*writer] + 1);
            }
        }
        if (layout.column[pass] >= perColumn.size())
        {
            perColumn.resize(layout.column[pass] + 1, 0);
        }
        layout.row[pass] = perColumn[layout.column[pass]]++;
    }
    layout.columns = static_cast<std::uint32_t>(perColumn.size());
    layout.rows = perColumn.empty() ? 0 : *std::ranges::max_element(perColumn);

    // Fewer crossings: column by column, left to right, order passes by the average row of the passes their inputs
    // come from (the barycentre heuristic). Earlier columns are final when a column is sorted. Passes with no
    // inputs from the frame keep their place in the order they were added.
    for (std::uint32_t column = 1; column < layout.columns; ++column)
    {
        struct Keyed
        {
            float key = 0.0F;
            std::uint32_t pass = 0;
        };
        std::vector<Keyed> passes;
        for (std::uint32_t pass = 0; pass < flow.passes.size(); ++pass)
        {
            if (layout.column[pass] != column)
            {
                continue;
            }
            float sum = 0.0F;
            std::uint32_t count = 0;
            for (const FlowAccess& access : flow.passes[pass].accesses)
            {
                if (const std::optional<std::uint32_t> writer = writerBefore(flow, access.resource, pass))
                {
                    sum += static_cast<float>(layout.row[*writer]);
                    ++count;
                }
            }
            passes.push_back({.key = count > 0 ? sum / static_cast<float>(count) : static_cast<float>(layout.row[pass]),
                              .pass = pass});
        }
        std::ranges::stable_sort(passes, {}, &Keyed::key);
        for (std::uint32_t row = 0; row < passes.size(); ++row)
        {
            layout.row[passes[row].pass] = row;
        }
    }
    return layout;
}

ResourceKind resourceKind(const FrameFlow& flow, std::uint32_t resource)
{
    const FlowResource& info = flow.resources[resource];
    if (info.texture)
    {
        return isDepthFormat(info.info.format) ? ResourceKind::DepthTexture : ResourceKind::ColorTexture;
    }
    for (const FlowPass& pass : flow.passes)
    {
        for (const FlowAccess& access : pass.accesses)
        {
            if (access.resource == resource && access.state == Rhi::ResourceState::IndirectArgs)
            {
                return ResourceKind::IndirectBuffer;
            }
        }
    }
    return ResourceKind::Buffer;
}

const char* resourceKindName(ResourceKind kind)
{
    switch (kind)
    {
        case ResourceKind::ColorTexture:
            return "Colour texture";
        case ResourceKind::DepthTexture:
            return "Depth texture";
        case ResourceKind::Buffer:
            return "Buffer";
        case ResourceKind::IndirectBuffer:
            return "Indirect arguments";
    }
    return "?";
}

FlowGeometry placeFlow(const FrameFlow& flow, const FlowMetrics& metrics,
                       const std::function<float(std::string_view)>& textWidth)
{
    const FlowLayout layout = layoutFlow(flow);
    const auto passCount = static_cast<std::uint32_t>(flow.passes.size());
    FlowGeometry geometry;
    geometry.nodes.resize(passCount);

    // Each node: wide enough for its title, and for its longest input and output label side by side.
    const auto widest = [&](const std::vector<std::uint32_t>& resources)
    {
        float width = 0.0F;
        for (const std::uint32_t resource : resources)
        {
            width = std::max(width, textWidth(flow.resources[resource].name));
        }
        return width;
    };
    for (std::uint32_t pass = 0; pass < passCount; ++pass)
    {
        FlowNode& node = geometry.nodes[pass];
        node.pins = passPins(flow, pass);
        // A skipped pass keeps its place (the layout doesn't jump) and says why nothing happened.
        node.title =
            std::format("{}  {}{}", pass, flow.passes[pass].name, flow.passes[pass].skipped ? " (cached)" : "");
        const float pinsWidth = widest(node.pins.inputs) + widest(node.pins.outputs) + metrics.dot * 4.0F +
                                metrics.pad * 2.0F + metrics.sideGap;
        node.width = std::max({metrics.minWidth, textWidth(node.title) + metrics.pad * 2.0F, pinsWidth});
        const auto rows = static_cast<float>(
            std::max<std::size_t>({node.pins.inputs.size(), node.pins.outputs.size(), std::size_t{1}}));
        node.height = metrics.titleHeight + rows * metrics.pinRow + metrics.pad * 0.5F;
    }

    // Columns as wide as their widest node; nodes stacked in each column in the layout's row order.
    std::vector<float> columnWidth(layout.columns, 0.0F);
    for (std::uint32_t pass = 0; pass < passCount; ++pass)
    {
        columnWidth[layout.column[pass]] = std::max(columnWidth[layout.column[pass]], geometry.nodes[pass].width);
    }
    std::vector<float> columnX(layout.columns, metrics.margin);
    for (std::uint32_t column = 1; column < layout.columns; ++column)
    {
        columnX[column] = columnX[column - 1] + columnWidth[column - 1] + metrics.columnGap;
    }
    std::vector<std::uint32_t> order(passCount);
    for (std::uint32_t pass = 0; pass < passCount; ++pass)
    {
        order[pass] = pass;
    }
    std::ranges::sort(
        order, [&](std::uint32_t a, std::uint32_t b)
        { return std::pair{layout.column[a], layout.row[a]} < std::pair{layout.column[b], layout.row[b]}; });
    std::vector<float> columnBottom(layout.columns, metrics.margin);
    for (const std::uint32_t pass : order)
    {
        const std::uint32_t column = layout.column[pass];
        geometry.nodes[pass].x = columnX[column];
        geometry.nodes[pass].y = columnBottom[column];
        columnBottom[column] += geometry.nodes[pass].height + metrics.rowGap;
    }
    if (layout.columns > 0)
    {
        geometry.width = columnX.back() + columnWidth.back() + metrics.margin;
        geometry.height = *std::ranges::max_element(columnBottom) - metrics.rowGap + metrics.margin;
    }
    return geometry;
}

FlowPins passPins(const FrameFlow& flow, std::uint32_t pass)
{
    FlowPins pins;
    const auto add = [](std::vector<std::uint32_t>& list, std::uint32_t resource)
    {
        if (std::ranges::find(list, resource) == list.end())
        {
            list.push_back(resource);
        }
    };
    for (const FlowAccess& access : flow.passes[pass].accesses)
    {
        if (!access.writes || writerBefore(flow, access.resource, pass))
        {
            add(pins.inputs, access.resource);
        }
        if (access.writes)
        {
            add(pins.outputs, access.resource);
        }
    }
    return pins;
}

FrameInspector::FrameInspector(Renderer::RenderDevice& device, Renderer::PipelineLibrary& pipelines, AssetId shader,
                               Log::Logger& log)
    : m_device(device)
    , m_pipelines(pipelines)
    , m_log(log)
{
    const auto make = [&](const char* entry, Rhi::RhiFormat format,
                          const char* name) -> std::optional<Renderer::PipelineId>
    {
        Result<Renderer::PipelineId> pipeline = pipelines.add(Renderer::GraphicsPipelineRequest{
            .shader = shader, .pixelEntry = entry, .colorFormat = format, .depthWrite = false, .name = name});
        if (!pipeline)
        {
            MEOW2_LOG_ERROR(m_log, "Frame inspector pipeline '{}' not built: {}", name, pipeline.error());
            return std::nullopt;
        }
        return *pipeline;
    };
    m_display = make("psDisplay", Rhi::RhiFormat::Rgba8Srgb, "Inspector display");
    m_copyColor = make("psCopy", Rhi::RhiFormat::Rgba16Float, "Capture copy (colour)");
    m_copyDepth = make("psCopy", Rhi::RhiFormat::R32Float, "Capture copy (depth)");
    // Textures are made on demand; on a backend switch they're freed and made again when next needed. The capture's
    // snapshots can't be made again (the frame is gone), so a switch ends the capture.
    m_deviceUser = device.addUser(
        "FrameInspector", [this](Rhi::IRhiDevice& old) { release(old); },
        [](Rhi::IRhiDevice&) -> Result<void> { return {}; });
}

FrameInspector::~FrameInspector()
{
    if (m_device.hasDevice())
    {
        m_device.device().waitIdle();
        release(m_device.device());
    }
}

void FrameInspector::release(Rhi::IRhiDevice& device)
{
    const auto free = [&device](OwnedTexture& texture)
    {
        if (texture.texture.isSet())
        {
            device.destroy(texture.texture);
        }
        texture = {};
    };
    for (LiveOutput& output : m_live)
    {
        free(output.display);
    }
    for (Snapshot& snapshot : m_snapshots)
    {
        free(snapshot.raw);
    }
    m_snapshots.clear();
    m_capture.reset();
    m_capturePass.reset();
    m_capturePin.reset();
    m_captureSnapshot.reset();
    free(m_captureDisplay);
    for (const Rhi::TextureHandle texture : m_retired)
    {
        device.destroy(texture);
    }
    m_retired.clear();
    for (Rhi::BufferHandle& buffer : m_displayBuffers)
    {
        if (buffer.isSet())
        {
            device.destroy(buffer);
        }
        buffer = {};
    }
}

bool FrameInspector::ensure(OwnedTexture& texture, const Rhi::TextureInfo& info, const char* name)
{
    if (texture.texture.isSet() && texture.info.width == info.width && texture.info.height == info.height &&
        texture.info.format == info.format)
    {
        return true;
    }
    Rhi::IRhiDevice& device = m_device.device();
    if (texture.texture.isSet())
    {
        // This frame's graph may already name it (imported for the UI): free it after the frame, not now.
        if (texture.imported)
        {
            m_retired.push_back(texture.texture);
        }
        else
        {
            device.destroy(texture.texture);
        }
    }
    texture = {};
    Result<Rhi::TextureHandle> made = device.createTexture({.width = std::max(info.width, 1U),
                                                            .height = std::max(info.height, 1U),
                                                            .format = info.format,
                                                            .renderTarget = true,
                                                            .tag = Memory::MemTag::Renderer,
                                                            .name = name});
    if (!made)
    {
        MEOW2_LOG_ERROR(m_log, "Inspector texture not made: {}", made.error());
        return false;
    }
    texture.texture = *made;
    texture.info = info;
    texture.state = Rhi::ResourceState::Undefined;
    return true;
}

void FrameInspector::import(Renderer::RenderGraph& graph, OwnedTexture& texture, const std::string& name)
{
    if (texture.texture.isSet() && !texture.imported)
    {
        texture.resource = graph.import(texture.texture, texture.state, name);
        texture.imported = true;
    }
}

std::optional<std::uint32_t> FrameInspector::displayRecord(std::uint32_t frameSlot, Rhi::TextureHandle source,
                                                           bool depth, const DisplaySettings& settings)
{
    Rhi::IRhiDevice& device = m_device.device();
    Rhi::BufferHandle& buffer = m_displayBuffers[frameSlot];
    if (!buffer.isSet())
    {
        Result<Rhi::BufferHandle> made = device.createBuffer({.size = c_maxDisplays * sizeof(Shader::DebugDisplay),
                                                              .memory = Rhi::RhiMemory::Upload,
                                                              .tag = Memory::MemTag::Renderer,
                                                              .name = "Inspector displays"});
        if (!made)
        {
            MEOW2_LOG_ERROR(m_log, "Inspector buffer not made: {}", made.error());
            return std::nullopt;
        }
        buffer = *made;
    }
    if (m_displayCount >= c_maxDisplays)
    {
        return std::nullopt;
    }
    Shader::DebugDisplay record{};
    record.source = device.bindlessIndex(source);
    record.sampler = Rhi::c_defaultSamplerIndex;
    record.channels = settings.channels;
    record.isDepth = depth ? 1U : 0U;
    record.rangeMin = settings.rangeMin;
    record.rangeMax = settings.rangeMax;
    const std::uint32_t index = m_displayCount++;
    std::memcpy(device.mappedMemory(buffer).data() + std::size_t{index} * sizeof(record), &record, sizeof(record));
    return index;
}

Renderer::GraphPass FrameInspector::displayPass(std::string name, Renderer::GraphResource source,
                                                Rhi::TextureHandle sourceTexture, OwnedTexture& target, bool depth,
                                                const DisplaySettings& settings, std::uint32_t frameSlot)
{
    const std::optional<std::uint32_t> record = displayRecord(frameSlot, sourceTexture, depth, settings);
    Shader::PushConstants push{};
    if (record)
    {
        push.frameBuffer = m_device.device().bindlessIndex(m_displayBuffers[frameSlot]);
        push.instance = *record;
    }
    return {.name = std::move(name),
            .accesses = {{source, Rhi::ResourceState::ShaderRead}, {target.resource, Rhi::ResourceState::ColorTarget}},
            .record =
                [this, push, valid = record.has_value() && m_display.has_value(),
                 texture = target.texture](Rhi::IRhiCommandList& list)
            {
                list.beginRendering(
                    Rhi::RenderTargets{.color = texture, .clearColor = Math::Color{.r = 0, .g = 0, .b = 0, .a = 1}});
                if (valid)
                {
                    list.setPipeline(m_pipelines.get(*m_display));
                    list.setPushConstants(push);
                    list.draw(3);
                }
                list.endRendering();
            },
            .small = true,
            .debug = true};
}

void FrameInspector::beginFrame(Renderer::RenderGraph& graph, std::uint32_t frameSlot)
{
    ++m_frame;
    m_displayCount = 0;
    m_uiReads.clear();
    for (LiveOutput& output : m_live)
    {
        import(graph, output.display, "Inspector: " + output.name);
        if (output.display.imported)
        {
            m_uiReads.push_back(output.display.resource);
        }
    }
    // The capture viewer: the selected snapshot through the display settings, every frame (the sliders move).
    if (m_captureSnapshot && *m_captureSnapshot < m_snapshots.size())
    {
        Snapshot& snapshot = m_snapshots[*m_captureSnapshot];
        if (snapshot.raw.texture.isSet() && ensure(m_captureDisplay,
                                                   {.width = snapshot.raw.info.width,
                                                    .height = snapshot.raw.info.height,
                                                    .format = Rhi::RhiFormat::Rgba8Srgb},
                                                   "Capture display"))
        {
            import(graph, snapshot.raw, "Capture snapshot");
            import(graph, m_captureDisplay, "Capture display");
            graph.addPass(displayPass("Capture viewer", snapshot.raw.resource, snapshot.raw.texture, m_captureDisplay,
                                      snapshot.depth, m_captureSettings, frameSlot));
            m_uiReads.push_back(m_captureDisplay.resource);
        }
    }
}

void FrameInspector::afterPasses(Renderer::RenderGraph& graph, std::uint32_t frameSlot)
{
    Rhi::IRhiDevice& device = m_device.device();
    m_liveFlow = describeGraph(graph, device);

    // A capture: after every pass that writes a texture, a raw copy of it. Last pass first, so inserting doesn't move
    // the passes still to visit.
    if (m_captureRequested)
    {
        m_captureRequested = false;
        for (Snapshot& old : m_snapshots)
        {
            if (old.raw.texture.isSet())
            {
                m_retired.push_back(old.raw.texture); // the capture viewer may have imported it this frame
            }
        }
        m_snapshots.clear();
        m_captureSnapshot.reset();
        m_capturePass.reset();
        m_capture = m_liveFlow;
        m_captureFrame = m_frame;
        for (std::uint32_t pass = static_cast<std::uint32_t>(m_capture->passes.size()); pass-- > 0;)
        {
            for (const FlowAccess& access : m_capture->passes[pass].accesses)
            {
                const FlowResource& resource = m_capture->resources[access.resource];
                if (!access.writes || !resource.texture)
                {
                    continue;
                }
                const bool depth = isDepthFormat(resource.info.format);
                Snapshot snapshot{.pass = pass, .resource = access.resource, .depth = depth, .raw = {}};
                if (!ensure(snapshot.raw,
                            {.width = resource.info.width,
                             .height = resource.info.height,
                             .format = depth ? Rhi::RhiFormat::R32Float : Rhi::RhiFormat::Rgba16Float},
                            "Capture snapshot"))
                {
                    continue;
                }
                import(graph, snapshot.raw,
                       std::format("Capture: {} after {}", resource.name, m_capture->passes[pass].name));
                const std::optional<Rhi::TextureHandle> source =
                    graph.resourceTexture(Renderer::GraphResource{.index = access.resource});
                const std::optional<Renderer::PipelineId> pipeline = depth ? m_copyDepth : m_copyColor;
                Shader::PushConstants push{};
                push.instance = device.bindlessIndex(*source);
                graph.insertPassAfter(
                    pass,
                    {.name = std::format("Capture {}", resource.name),
                     .accesses = {{Renderer::GraphResource{.index = access.resource}, Rhi::ResourceState::ShaderRead},
                                  {snapshot.raw.resource, Rhi::ResourceState::ColorTarget}},
                     .record =
                         [this, push, pipeline, texture = snapshot.raw.texture](Rhi::IRhiCommandList& list)
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
                m_snapshots.push_back(std::move(snapshot));
            }
        }
        std::ranges::reverse(m_snapshots); // in pass order
        MEOW2_LOG_INFO(m_log, "Frame captured: {} passes, {} textures kept", m_capture->passes.size(),
                       m_snapshots.size());
    }

    // The live view: the selected pass's texture outputs, made displayable right after it.
    if (!m_selectedPass)
    {
        for (LiveOutput& output : m_live)
        {
            if (output.display.texture.isSet())
            {
                m_retired.push_back(output.display.texture); // not watched any more
            }
        }
        m_live.clear();
        return;
    }
    const std::span<const Renderer::GraphPass> passes = graph.passes();
    const auto found = std::ranges::find(passes, *m_selectedPass, &Renderer::GraphPass::name);
    if (found == passes.end())
    {
        return; // not in this frame (e.g. no blended objects)
    }
    const auto index = static_cast<std::uint32_t>(found - passes.begin());
    std::vector<Renderer::GraphAccess> outputs;
    for (const Renderer::GraphAccess& access : found->accesses)
    {
        if (Renderer::isWriteState(access.state) && graph.resourceTexture(access.resource))
        {
            outputs.push_back(access);
        }
    }
    // Keep each output's display settings across frames, matched by name.
    std::vector<LiveOutput> live;
    for (const Renderer::GraphAccess& access : outputs)
    {
        const std::string name = graph.resourceName(access.resource).empty()
                                     ? std::format("Resource {}", access.resource.index)
                                     : graph.resourceName(access.resource);
        const auto previous = std::ranges::find(m_live, name, &LiveOutput::name);
        const Rhi::TextureInfo info = device.textureInfo(*graph.resourceTexture(access.resource));
        if (previous != m_live.end())
        {
            live.push_back(std::move(*previous));
            previous->display = {}; // moved: the texture now belongs to the new entry, not to the leftovers below
        }
        else
        {
            live.push_back({.name = name,
                            .depth = isDepthFormat(info.format),
                            .display = {},
                            .settings = defaultSettings(isDepthFormat(info.format))});
        }
    }
    // Whatever wasn't moved over is an output this pass no longer has.
    for (LiveOutput& stale : m_live)
    {
        if (stale.display.texture.isSet())
        {
            m_retired.push_back(stale.display.texture); // imported for the UI this frame: freed after it
        }
    }
    m_live = std::move(live);
    for (std::size_t i = 0; i < outputs.size(); ++i)
    {
        LiveOutput& output = m_live[i];
        const Rhi::TextureHandle source = *graph.resourceTexture(outputs[i].resource);
        const Rhi::TextureInfo info = device.textureInfo(source);
        if (!ensure(output.display, {.width = info.width, .height = info.height, .format = Rhi::RhiFormat::Rgba8Srgb},
                    "Pass view"))
        {
            continue;
        }
        import(graph, output.display, "Inspector: " + output.name);
        graph.insertPassAfter(index, displayPass(std::format("Inspect {}", output.name), outputs[i].resource, source,
                                                 output.display, output.depth, output.settings, frameSlot));
    }
}

void FrameInspector::endFrame(const Renderer::RenderGraph& graph)
{
    const auto keep = [&graph](OwnedTexture& texture)
    {
        if (texture.imported)
        {
            texture.state = graph.finalState(texture.resource);
            texture.imported = false;
        }
    };
    for (LiveOutput& output : m_live)
    {
        keep(output.display);
    }
    for (Snapshot& snapshot : m_snapshots)
    {
        keep(snapshot.raw);
    }
    keep(m_captureDisplay);
    for (const Rhi::TextureHandle texture : m_retired)
    {
        m_device.device().destroy(texture); // the device defers it until the GPU is done
    }
    m_retired.clear();
}

void FrameInspector::drawDisplayControls(DisplaySettings& settings, bool depth)
{
    if (!depth)
    {
        constexpr std::array<const char*, 4> c_names{"R", "G", "B", "A"};
        for (std::uint32_t bit = 0; bit < 4; ++bit)
        {
            bool on = (settings.channels & (1U << bit)) != 0;
            if (ImGui::Checkbox(c_names[bit], &on))
            {
                settings.channels = on ? settings.channels | (1U << bit) : settings.channels & ~(1U << bit);
            }
            ImGui::SameLine();
        }
        ImGui::NewLine();
    }
    ImGui::DragFloatRange2("RANGE", &settings.rangeMin, &settings.rangeMax, depth ? 0.0005F : 0.01F, 0.0F,
                           depth ? 1.0F : 1000.0F, depth ? "%.4f" : "%.2f");
    ImGui::SetItemTooltip(depth ? "Raw depth shown black to white. Reversed Z: 1 at the near plane, 0 at infinity."
                                : "Values shown black to white; HDR colour can go above 1.");
}

FrameInspector::FlowClick FrameInspector::drawFlow(const FrameFlow& flow, std::optional<std::uint32_t> selected,
                                                   std::optional<FlowPin> pin, const char* id, float height)
{
    FlowClick click;
    const auto passCount = static_cast<std::uint32_t>(flow.passes.size());
    const float scale = ImGui::GetStyle().FontScaleDpi;
    const float line = ImGui::GetTextLineHeight();
    const FlowMetrics metrics{.pad = 8.0F * scale,
                              .titleHeight = line + 8.0F * scale,
                              .pinRow = line + 6.0F * scale,
                              .dot = 4.5F * scale,
                              .columnGap = 90.0F * scale,
                              .rowGap = 24.0F * scale,
                              .margin = 16.0F * scale,
                              .minWidth = 140.0F * scale,
                              .sideGap = 24.0F * scale};
    const FlowGeometry geometry = placeFlow(flow, metrics, [](std::string_view text)
                                            { return ImGui::CalcTextSize(text.data(), text.data() + text.size()).x; });
    const float pad = metrics.pad;
    const float titleHeight = metrics.titleHeight;
    const float pinRow = metrics.pinRow;
    const float dot = metrics.dot;
    std::vector<FlowPins> pins(passCount);
    std::vector<ImVec2> size(passCount);
    std::vector<ImVec2> position(passCount);
    std::vector<std::string> titles(passCount);
    for (std::uint32_t pass = 0; pass < passCount; ++pass)
    {
        const FlowNode& node = geometry.nodes[pass];
        pins[pass] = node.pins;
        size[pass] = ImVec2{node.width, node.height};
        position[pass] = ImVec2{node.x, node.y};
        titles[pass] = node.title;
    }
    const float contentWidth = geometry.width;
    const float contentHeight = geometry.height;

    ImGui::BeginChild(id, ImVec2{0.0F, height}, ImGuiChildFlags_Borders,
                      ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_NoMove);
    // Pan with the right or middle button, like a node editor.
    if (ImGui::IsWindowHovered() &&
        (ImGui::IsMouseDragging(ImGuiMouseButton_Right) || ImGui::IsMouseDragging(ImGuiMouseButton_Middle)))
    {
        const ImVec2 delta = ImGui::GetIO().MouseDelta;
        ImGui::SetScrollX(ImGui::GetScrollX() - delta.x);
        ImGui::SetScrollY(ImGui::GetScrollY() - delta.y);
    }
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const auto nodeMin = [&](std::uint32_t pass)
    {
        return ImVec2{origin.x + position[pass].x, origin.y + position[pass].y};
    };
    const auto inputAt = [&](std::uint32_t pass, std::size_t index)
    {
        const ImVec2 min = nodeMin(pass);
        return ImVec2{min.x, min.y + titleHeight + pinRow * (static_cast<float>(index) + 0.5F)};
    };
    const auto outputAt = [&](std::uint32_t pass, std::size_t index)
    {
        const ImVec2 min = nodeMin(pass);
        return ImVec2{min.x + size[pass].x, min.y + titleHeight + pinRow * (static_cast<float>(index) + 0.5F)};
    };
    const auto touches = [&](std::uint32_t pass, std::uint32_t resource)
    {
        return pin && pin->pass == pass && pin->resource == resource;
    };

    // Wires first, under the nodes: from the output of the pass that last wrote a resource to each input that uses
    // it. Dim ones first, highlighted ones (the selected pass's or pin's) over them.
    for (int layer = 0; layer < 2; ++layer)
    {
        for (std::uint32_t pass = 0; pass < passCount; ++pass)
        {
            for (std::size_t input = 0; input < pins[pass].inputs.size(); ++input)
            {
                const std::uint32_t resource = pins[pass].inputs[input];
                const std::optional<std::uint32_t> writer = writerBefore(flow, resource, pass);
                if (!writer)
                {
                    continue;
                }
                const auto output = static_cast<std::size_t>(std::ranges::find(pins[*writer].outputs, resource) -
                                                             pins[*writer].outputs.begin());
                const bool highlighted = (selected && (*selected == pass || *selected == *writer)) ||
                                         touches(pass, resource) || touches(*writer, resource);
                if (highlighted != (layer == 1))
                {
                    continue;
                }
                ImVec4 colour = kindColour(resourceKind(flow, resource));
                colour.w = highlighted ? 1.0F : 0.5F;
                const ImVec2 from = outputAt(*writer, output);
                const ImVec2 to = inputAt(pass, input);
                const float bend = std::max(std::abs(to.x - from.x) * 0.5F, 40.0F * scale);
                // Dashed when only debugging needs it: a debug pass at either end, or a debug-only resource.
                const bool debugWire =
                    flow.passes[pass].debug || flow.passes[*writer].debug || flow.resources[resource].debug;
                const float thickness = (highlighted ? 3.0F : 1.6F) * scale;
                if (debugWire)
                {
                    dashedBezier(*draw, {from, ImVec2{from.x + bend, from.y}, ImVec2{to.x - bend, to.y}, to},
                                 ImGui::GetColorU32(colour), thickness, 6.0F * scale);
                }
                else
                {
                    draw->AddBezierCubic(from, ImVec2{from.x + bend, from.y}, ImVec2{to.x - bend, to.y}, to,
                                         ImGui::GetColorU32(colour), thickness);
                }
            }
        }
    }

    // A pin: its dot (hollow when nothing in the frame connects to it), the resource's name, and a hit area.
    const auto drawPin = [&](std::uint32_t pass, std::uint32_t resource, ImVec2 at, bool output, bool connected)
    {
        const ResourceKind kind = resourceKind(flow, resource);
        const ImU32 colour = ImGui::GetColorU32(kindColour(kind));
        if (connected && !flow.resources[resource].debug)
        {
            draw->AddCircleFilled(at, dot, colour);
        }
        else
        {
            draw->AddCircle(at, dot, colour, 0, 1.5F * scale);
        }
        if (touches(pass, resource) && pin->output == output)
        {
            draw->AddCircle(at, dot + 3.0F * scale, ImGui::GetColorU32(UiColors::c_text), 0, 1.5F * scale);
        }
        const FlowResource& info = flow.resources[resource];
        const float textWidth = ImGui::CalcTextSize(info.name.c_str()).x;
        const ImVec2 text{output ? at.x - dot * 2.0F - textWidth : at.x + dot * 2.0F, at.y - line * 0.5F};
        // A debug-only resource: a ring and a dimmed name, like its node's dashed outline.
        draw->AddText(text, ImGui::GetColorU32(info.debug ? UiColors::c_textDim : UiColors::c_text), info.name.c_str());
        const float hitWidth = textWidth + dot * 4.0F;
        ImGui::SetCursorScreenPos(
            ImVec2{output ? at.x - hitWidth + dot * 2.0F : at.x - dot * 2.0F, at.y - pinRow * 0.5F});
        ImGui::PushID(static_cast<int>(resource * 2 + (output ? 1U : 0U)));
        if (ImGui::InvisibleButton("pin", ImVec2{hitWidth, pinRow}))
        {
            click.pin = FlowPin{.pass = pass, .resource = resource, .output = output};
        }
        if (ImGui::IsItemHovered())
        {
            if (info.texture)
            {
                ImGui::SetTooltip("%s\n%s, %u x %u %s\nClick to show it.", info.name.c_str(), resourceKindName(kind),
                                  info.info.width, info.info.height, formatName(info.info.format));
            }
            else
            {
                ImGui::SetTooltip("%s\n%s\nClick for who writes and reads it.", info.name.c_str(),
                                  resourceKindName(kind));
            }
        }
        ImGui::PopID();
    };

    // The nodes: a title bar (click to watch the pass), then a row per pin.
    for (std::uint32_t pass = 0; pass < passCount; ++pass)
    {
        const ImVec2 min = nodeMin(pass);
        const ImVec2 max{min.x + size[pass].x, min.y + size[pass].y};
        const bool isSelected = selected && *selected == pass;
        ImGui::PushID(static_cast<int>(pass));
        ImGui::SetCursorScreenPos(min);
        if (ImGui::InvisibleButton("node", ImVec2{size[pass].x, titleHeight}))
        {
            click.pass = pass;
        }
        const bool hovered = ImGui::IsItemHovered();
        const float rounding = 4.0F * scale;
        draw->AddRectFilled(min, max, ImGui::GetColorU32(UiColors::c_frame), rounding);
        draw->AddRectFilled(min, ImVec2{max.x, min.y + titleHeight},
                            ImGui::GetColorU32(isSelected ? UiColors::c_accent
                                                          : (hovered ? UiColors::c_line : UiColors::c_frameHovered)),
                            rounding, ImDrawFlags_RoundTop);
        const ImU32 border = ImGui::GetColorU32(isSelected ? UiColors::c_accentBright : UiColors::c_line);
        if (flow.passes[pass].debug)
        {
            // A debugging tool's pass: dashed, in the dim text colour so it reads on the dark panel.
            dashedRect(*draw, min, max, isSelected ? border : ImGui::GetColorU32(UiColors::c_textDim),
                       isSelected ? 2.0F : 1.0F, 5.0F * scale);
        }
        else
        {
            draw->AddRect(min, max, border, rounding, isSelected ? 2.0F : 1.0F);
        }
        draw->PushClipRect(min, ImVec2{max.x, min.y + titleHeight}, true);
        draw->AddText(ImVec2{min.x + pad, min.y + (titleHeight - line) * 0.5F},
                      ImGui::GetColorU32(flow.passes[pass].skipped ? UiColors::c_textDim : UiColors::c_text),
                      titles[pass].c_str());
        draw->PopClipRect();
        for (std::size_t input = 0; input < pins[pass].inputs.size(); ++input)
        {
            const std::uint32_t resource = pins[pass].inputs[input];
            drawPin(pass, resource, inputAt(pass, input), false, writerBefore(flow, resource, pass).has_value());
        }
        for (std::size_t output = 0; output < pins[pass].outputs.size(); ++output)
        {
            const std::uint32_t resource = pins[pass].outputs[output];
            // Connected when a later pass uses what this one wrote.
            bool used = false;
            for (std::uint32_t later = pass + 1; later < passCount && !used; ++later)
            {
                used = std::ranges::find(pins[later].inputs, resource) != pins[later].inputs.end() &&
                       writerBefore(flow, resource, later) == pass;
            }
            drawPin(pass, resource, outputAt(pass, output), true, used);
        }
        ImGui::PopID();
    }
    // The child's scrollable size.
    ImGui::SetCursorScreenPos(origin);
    ImGui::Dummy(ImVec2{contentWidth, contentHeight});
    ImGui::EndChild();
    return click;
}

void FrameInspector::drawLegend()
{
    constexpr std::array<ResourceKind, 4> c_kinds{ResourceKind::ColorTexture, ResourceKind::DepthTexture,
                                                  ResourceKind::Buffer, ResourceKind::IndirectBuffer};
    for (std::size_t i = 0; i < c_kinds.size(); ++i)
    {
        if (i > 0)
        {
            ImGui::SameLine(0.0F, ImGui::GetStyle().ItemSpacing.x * 3.0F);
        }
        // A dot like the pins', then the name.
        const float radius = ImGui::GetTextLineHeight() * 0.22F;
        const ImVec2 at = ImGui::GetCursorScreenPos();
        ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2{at.x + radius, at.y + ImGui::GetTextLineHeight() * 0.5F},
                                                    radius, ImGui::GetColorU32(kindColour(c_kinds[i])));
        ImGui::Dummy(ImVec2{radius * 2.0F, ImGui::GetTextLineHeight()});
        ImGui::SameLine();
        ImGui::TextColored(UiColors::c_textDim, "%s", resourceKindName(c_kinds[i]));
    }
    // And what dashes mean.
    ImGui::SameLine(0.0F, ImGui::GetStyle().ItemSpacing.x * 3.0F);
    const float height = ImGui::GetTextLineHeight();
    const ImVec2 at = ImGui::GetCursorScreenPos();
    dashedRect(*ImGui::GetWindowDrawList(), ImVec2{at.x, at.y + height * 0.2F},
               ImVec2{at.x + height, at.y + height * 0.8F}, ImGui::GetColorU32(UiColors::c_textDim), 1.0F,
               height * 0.2F);
    ImGui::Dummy(ImVec2{height, height});
    ImGui::SameLine();
    ImGui::TextColored(UiColors::c_textDim, "Dashed: debug only (not part of the game's frame)");
}

void FrameInspector::drawPinInfo(const FrameFlow& flow, const FlowPin& pin, const UiFonts& fonts)
{
    if (pin.pass >= flow.passes.size() || pin.resource >= flow.resources.size())
    {
        return; // the frame changed since it was clicked
    }
    const FlowResource& resource = flow.resources[pin.resource];
    const ResourceKind kind = resourceKind(flow, pin.resource);
    ImGui::TextColored(kindColour(kind), "%s", resource.name.c_str());
    ImGui::SameLine();
    if (resource.texture)
    {
        ImGui::TextColored(UiColors::c_textDim, "%s, %u x %u %s", resourceKindName(kind), resource.info.width,
                           resource.info.height, formatName(resource.info.format));
    }
    else
    {
        ImGui::TextColored(UiColors::c_textDim, "%s: not an image, so it can't be shown", resourceKindName(kind));
    }
    pushMonoFont(fonts);
    if (!pin.output)
    {
        const std::optional<std::uint32_t> writer = writerBefore(flow, pin.resource, pin.pass);
        const std::string from = writer ? std::format("from {} {}", *writer, flow.passes[*writer].name)
                                        : std::string{"from before the frame (imported as it was)"};
        ImGui::TextColored(UiColors::c_textDim, "Input of %u %s, %s", pin.pass, flow.passes[pin.pass].name.c_str(),
                           from.c_str());
    }
    // Every use in the frame, in order.
    for (std::uint32_t pass = 0; pass < flow.passes.size(); ++pass)
    {
        for (const FlowAccess& access : flow.passes[pass].accesses)
        {
            if (access.resource == pin.resource)
            {
                ImGui::TextColored(pass == pin.pass ? UiColors::c_text : UiColors::c_textDim, "%-6s %2u %s (%s)",
                                   access.writes ? "WRITE" : "READ", pass, flow.passes[pass].name.c_str(),
                                   stateName(access.state));
            }
        }
    }
    popMonoFont();
}

void FrameInspector::drawWindows(ImGuiLayer& ui, const UiFonts& fonts)
{
    const float scale = ImGui::GetStyle().FontScaleDpi;

    // The live graph: click a pass to see what it writes.
    if (ImGui::Begin("RENDER GRAPH###RenderGraph"))
    {
        drawWindowAccents();
        std::optional<std::uint32_t> selected;
        for (std::uint32_t pass = 0; pass < m_liveFlow.passes.size(); ++pass)
        {
            if (m_selectedPass && m_liveFlow.passes[pass].name == *m_selectedPass)
            {
                selected = pass;
            }
        }
        ImGui::TextColored(UiColors::c_textDim,
                           "%zu passes, %zu resources. Click a pass to watch its outputs, a pin to see its resource. "
                           "Right-drag to pan.",
                           m_liveFlow.passes.size(), m_liveFlow.resources.size());
        drawLegend();
        // The graph takes the room left after the button and the pin's details.
        const float footer = ImGui::GetFrameHeightWithSpacing() * (m_livePin ? 7.0F : 1.0F);
        const FlowClick click = drawFlow(m_liveFlow, selected, m_livePin, "live",
                                         std::max(160.0F * scale, ImGui::GetContentRegionAvail().y - footer));
        if (click.pass)
        {
            m_livePin.reset();
            m_selectedPass = (m_selectedPass && *m_selectedPass == m_liveFlow.passes[*click.pass].name)
                                 ? std::nullopt // a second click stops watching
                                 : std::optional{m_liveFlow.passes[*click.pass].name};
        }
        if (click.pin)
        {
            m_livePin = click.pin;
            // A texture is shown in the Pass view: watch the pass that wrote it, on its tab.
            if (m_liveFlow.resources[click.pin->resource].texture)
            {
                const std::optional<std::uint32_t> writer =
                    click.pin->output ? std::optional{click.pin->pass}
                                      : writerBefore(m_liveFlow, click.pin->resource, click.pin->pass);
                if (writer)
                {
                    m_selectedPass = m_liveFlow.passes[*writer].name;
                    m_focusOutput = m_liveFlow.resources[click.pin->resource].name;
                }
            }
        }
        if (ImGui::Button("CAPTURE FRAME (F9)"))
        {
            requestCapture();
        }
        if (m_livePin)
        {
            drawPinInfo(m_liveFlow, *m_livePin, fonts);
        }
    }
    ImGui::End();

    // The picture in picture: the watched pass's outputs, live.
    if (m_selectedPass)
    {
        const ImVec2 display = ImGui::GetIO().DisplaySize;
        ImGui::SetNextWindowSize(ImVec2{420.0F * scale, 300.0F * scale}, ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowPos(ImVec2{display.x - 430.0F * scale, display.y - 310.0F * scale}, ImGuiCond_FirstUseEver);
        bool open = true;
        if (ImGui::Begin(std::format("PASS: {}###PassView", *m_selectedPass).c_str(), &open))
        {
            if (m_live.empty())
            {
                ImGui::TextColored(UiColors::c_textDim,
                                   "This pass writes no texture of the graph\n(only buffers, or the "
                                   "swapchain).");
            }
            if (ImGui::BeginTabBar("outputs"))
            {
                for (LiveOutput& output : m_live)
                {
                    // A pin clicked in the graph brings its tab forward, once.
                    const bool focus = m_focusOutput && *m_focusOutput == output.name;
                    if (focus)
                    {
                        m_focusOutput.reset();
                    }
                    if (!ImGui::BeginTabItem(output.name.c_str(), nullptr,
                                             focus ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None))
                    {
                        continue;
                    }
                    drawDisplayControls(output.settings, output.depth);
                    if (output.display.texture.isSet())
                    {
                        const ImVec2 room = ImGui::GetContentRegionAvail();
                        const float aspect = static_cast<float>(output.display.info.width) /
                                             static_cast<float>(std::max(output.display.info.height, 1U));
                        const float width = std::min(room.x, room.y * aspect);
                        ImGui::Image(ImTextureRef{ui.engineTexture(output.display.texture)},
                                     ImVec2{width, width / aspect});
                        pushMonoFont(fonts);
                        ImGui::TextColored(UiColors::c_textDim, "%u x %u", output.display.info.width,
                                           output.display.info.height);
                        popMonoFont();
                    }
                    ImGui::EndTabItem();
                }
                ImGui::EndTabBar();
            }
        }
        ImGui::End();
        if (!open)
        {
            m_selectedPass.reset();
        }
    }

    // The captured frame: the same flow, frozen, with every texture as each pass left it.
    if (ImGui::Begin("FRAME CAPTURE###FrameCapture"))
    {
        drawWindowAccents();
        if (!m_capture)
        {
            ImGui::TextColored(UiColors::c_textDim, "No capture. F9, or CAPTURE FRAME in the Render graph window,\n"
                                                    "keeps one frame in memory: every pass and every texture\n"
                                                    "it wrote. RenderDoc (F11) is for keeping captures.");
        }
        else
        {
            ImGui::TextColored(UiColors::c_textDim, "Frame %llu: %zu passes, %zu textures kept.",
                               static_cast<unsigned long long>(m_captureFrame), m_capture->passes.size(),
                               m_snapshots.size());
            ImGui::SameLine();
            if (ImGui::SmallButton("CLEAR"))
            {
                m_device.device().waitIdle();
                for (Snapshot& snapshot : m_snapshots)
                {
                    m_device.device().destroy(snapshot.raw.texture);
                }
                m_snapshots.clear();
                m_capture.reset();
                m_capturePin.reset();
                m_capturePass.reset();
                m_captureSnapshot.reset();
                ImGui::End();
                return;
            }
            // Shows a texture as a pass left it.
            const auto select = [&](std::uint32_t writer, std::uint32_t resource)
            {
                for (std::size_t i = 0; i < m_snapshots.size(); ++i)
                {
                    if (m_snapshots[i].pass == writer && m_snapshots[i].resource == resource)
                    {
                        m_captureSnapshot = i;
                        m_captureSettings = defaultSettings(m_snapshots[i].depth);
                    }
                }
            };
            drawLegend();
            const FlowClick click = drawFlow(*m_capture, m_capturePass, m_capturePin, "capture",
                                             std::max(160.0F * scale, ImGui::GetContentRegionAvail().y * 0.45F));
            if (click.pass)
            {
                m_capturePass = click.pass;
                m_capturePin.reset();
            }
            if (click.pin)
            {
                m_capturePin = click.pin;
                m_capturePass = click.pin->pass;
                const std::optional<std::uint32_t> writer =
                    click.pin->output ? std::optional{click.pin->pass}
                                      : writerBefore(*m_capture, click.pin->resource, click.pin->pass);
                if (writer && m_capture->resources[click.pin->resource].texture)
                {
                    select(*writer, click.pin->resource);
                }
            }
            if (m_capturePin)
            {
                drawPinInfo(*m_capture, *m_capturePin, fonts);
            }
            if (m_capturePass)
            {
                const FlowPass& pass = m_capture->passes[*m_capturePass];
                ImGui::SeparatorText(std::format("{}  {}", *m_capturePass, pass.name).c_str());
                // Inputs: what it reads, and which pass wrote it; outputs: what it wrote. Clicking shows the texture.
                pushMonoFont(fonts);
                for (const FlowAccess& access : pass.accesses)
                {
                    const FlowResource& resource = m_capture->resources[access.resource];
                    const std::optional<std::uint32_t> writer =
                        access.writes ? std::optional{*m_capturePass}
                                      : writerBefore(*m_capture, access.resource, *m_capturePass);
                    std::string label = std::format("{} {}", access.writes ? "OUT" : "IN ", resource.name);
                    if (resource.texture)
                    {
                        label += std::format("  {}x{} {}", resource.info.width, resource.info.height,
                                             formatName(resource.info.format));
                    }
                    if (!access.writes)
                    {
                        label += writer ? std::format("  from {}", m_capture->passes[*writer].name)
                                        : "  (from before the frame)";
                    }
                    const bool showable = resource.texture && writer.has_value();
                    ImGui::BeginDisabled(!showable);
                    if (ImGui::Selectable(label.c_str()) && showable)
                    {
                        select(*writer, access.resource);
                    }
                    ImGui::EndDisabled();
                }
                popMonoFont();
            }
            if (m_captureSnapshot && *m_captureSnapshot < m_snapshots.size() && m_captureDisplay.texture.isSet())
            {
                const Snapshot& snapshot = m_snapshots[*m_captureSnapshot];
                ImGui::SeparatorText(std::format("{} after {}", m_capture->resources[snapshot.resource].name,
                                                 m_capture->passes[snapshot.pass].name)
                                         .c_str());
                drawDisplayControls(m_captureSettings, snapshot.depth);
                const float aspect = static_cast<float>(m_captureDisplay.info.width) /
                                     static_cast<float>(std::max(m_captureDisplay.info.height, 1U));
                const float width = ImGui::GetContentRegionAvail().x;
                ImGui::Image(ImTextureRef{ui.engineTexture(m_captureDisplay.texture)}, ImVec2{width, width / aspect});
            }
        }
    }
    ImGui::End();
}
} // namespace Meow2::RendererApp
