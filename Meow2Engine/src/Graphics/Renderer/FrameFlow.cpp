/// @file
/// @brief FrameFlow (FrameFlow.h): moved from Meow2Renderer's FrameInspector so the editor shares it.

#include <Meow2/Graphics/Renderer/FrameFlow.h>

#include <algorithm>
#include <format>
#include <utility>

namespace Meow2::Renderer
{
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


/// @brief A state's name, as the pin details show it.
const char* resourceStateName(Rhi::ResourceState state)
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

    // Levels (the layout's columns) as bands from top to bottom; a level's passes side by side in its row order. A
    // line of a band ends before it would pass maxBandWidth, and the band goes on below; every line holds at least one
    // node, however wide.
    std::vector<std::uint32_t> order(passCount);
    for (std::uint32_t pass = 0; pass < passCount; ++pass)
    {
        order[pass] = pass;
    }
    std::ranges::sort(
        order, [&](std::uint32_t a, std::uint32_t b)
        { return std::pair{layout.column[a], layout.row[a]} < std::pair{layout.column[b], layout.row[b]}; });
    float lineTop = metrics.margin;    // where the current line starts
    float lineBottom = metrics.margin; // its tallest node's bottom so far
    float x = metrics.margin;
    float right = metrics.margin; // the widest line's right edge
    std::optional<std::uint32_t> level;
    for (const std::uint32_t pass : order)
    {
        FlowNode& node = geometry.nodes[pass];
        const bool newLevel = level != layout.column[pass];
        const bool full = !newLevel && x > metrics.margin && x + node.width > metrics.margin + metrics.maxBandWidth;
        if (newLevel || full)
        {
            if (level)
            {
                // Below the last line: the gap between levels leaves room for the wires going down; a wrapped line
                // of the same level only needs the node gap.
                lineTop = lineBottom + (newLevel ? metrics.levelGap : metrics.nodeGap * 0.5F);
            }
            lineBottom = lineTop;
            x = metrics.margin;
            level = layout.column[pass];
        }
        node.x = x;
        node.y = lineTop;
        x += node.width + metrics.nodeGap;
        right = std::max(right, node.x + node.width);
        lineBottom = std::max(lineBottom, node.y + node.height);
    }
    if (passCount > 0)
    {
        geometry.width = right + metrics.margin;
        geometry.height = lineBottom + metrics.margin;
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
} // namespace Meow2::Renderer
