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

/// @brief How a new display starts: depth spread over the near range (reversed Z puts most of a scene near 0), colour
///        as it is.
FrameInspector::DisplaySettings defaultSettings(bool depth)
{
    return depth ? FrameInspector::DisplaySettings{.channels = 0b0001, .rangeMin = 0.0F, .rangeMax = 0.05F}
                 : FrameInspector::DisplaySettings{};
}
} // namespace

FrameInspector::FrameInspector(Renderer::RenderDevice& device, Renderer::PipelineLibrary& pipelines, AssetId shader,
                               Log::Logger& log)
    : m_device(device)
    , m_pipelines(pipelines)
    , m_log(log)
    , m_frameCapture(device, pipelines, shader, log)
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
    // Textures are made on demand; on a backend switch they're freed and made again when next needed. The capture
    // (FrameCapture) drops its snapshots itself: they can't be made again, the frame is gone.
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
    m_frameCapture.beginFrame(frameSlot);
    const std::span<const Renderer::CaptureSnapshot> snapshots = m_frameCapture.snapshots();
    if (m_captureSnapshot && *m_captureSnapshot < snapshots.size())
    {
        const auto index = static_cast<std::uint32_t>(*m_captureSnapshot);
        const Renderer::CaptureSnapshot& snapshot = snapshots[index];
        if (ensure(m_captureDisplay,
                   {.width = snapshot.info.width, .height = snapshot.info.height, .format = Rhi::RhiFormat::Rgba8Srgb},
                   "Capture display"))
        {
            if (const std::optional<Renderer::GraphResource> source = m_frameCapture.importSnapshot(graph, index))
            {
                import(graph, m_captureDisplay, "Capture display");
                graph.addPass(displayPass("Capture viewer", *source, snapshot.texture, m_captureDisplay, snapshot.depth,
                                          m_captureSettings, frameSlot));
                m_uiReads.push_back(m_captureDisplay.resource);
            }
        }
    }
}

void FrameInspector::afterPasses(Renderer::RenderGraph& graph, std::uint32_t frameSlot)
{
    Rhi::IRhiDevice& device = m_device.device();
    m_liveFlow = describeGraph(graph, device);

    // A capture (FrameCapture): a copy after every pass that writes a texture. A new one drops the old selection.
    if (m_frameCapture.requested())
    {
        m_captureSnapshot.reset();
        m_capturePass.reset();
        m_capturePin.reset();
    }
    m_frameCapture.afterPasses(graph, frameSlot);

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
    keep(m_captureDisplay);
    m_frameCapture.endFrame(graph);
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
                              .levelGap = 70.0F * scale,
                              .nodeGap = 60.0F * scale,
                              .maxBandWidth = 1400.0F * scale,
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
                // Pins are on the sides, so wires leave and arrive sideways; a wire going down a long way bends wider,
                // so it clears the nodes it starts and ends at.
                const float bend =
                    std::max({std::abs(to.x - from.x) * 0.5F, std::abs(to.y - from.y) * 0.35F, 40.0F * scale});
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
                                   Renderer::resourceStateName(access.state));
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
        const std::optional<FrameFlow>& captured = m_frameCapture.flow();
        const std::span<const Renderer::CaptureSnapshot> snapshots = m_frameCapture.snapshots();
        if (!captured)
        {
            ImGui::TextColored(UiColors::c_textDim, "No capture. F9, or CAPTURE FRAME in the Render graph window,\n"
                                                    "keeps one frame in memory: every pass and every texture\n"
                                                    "it wrote. RenderDoc (F11) is for keeping captures.");
        }
        else
        {
            ImGui::TextColored(UiColors::c_textDim, "Frame %llu: %zu passes, %zu textures kept.",
                               static_cast<unsigned long long>(m_frameCapture.frame()), captured->passes.size(),
                               snapshots.size());
            ImGui::SameLine();
            if (ImGui::SmallButton("CLEAR"))
            {
                m_frameCapture.clear();
                m_capturePin.reset();
                m_capturePass.reset();
                m_captureSnapshot.reset();
                ImGui::End();
                return;
            }
            // Shows a texture as a pass left it.
            const auto select = [&](std::uint32_t writer, std::uint32_t resource)
            {
                if (const std::optional<std::uint32_t> index = m_frameCapture.snapshotFor(writer, resource))
                {
                    m_captureSnapshot = *index;
                    m_captureSettings = defaultSettings(snapshots[*index].depth);
                }
            };
            drawLegend();
            const FlowClick click = drawFlow(*captured, m_capturePass, m_capturePin, "capture",
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
                                      : writerBefore(*captured, click.pin->resource, click.pin->pass);
                if (writer && captured->resources[click.pin->resource].texture)
                {
                    select(*writer, click.pin->resource);
                }
            }
            if (m_capturePin)
            {
                drawPinInfo(*captured, *m_capturePin, fonts);
            }
            if (m_capturePass)
            {
                const FlowPass& pass = captured->passes[*m_capturePass];
                ImGui::SeparatorText(std::format("{}  {}", *m_capturePass, pass.name).c_str());
                // Inputs: what it reads, and which pass wrote it; outputs: what it wrote. Clicking shows the texture.
                pushMonoFont(fonts);
                for (const FlowAccess& access : pass.accesses)
                {
                    const FlowResource& resource = captured->resources[access.resource];
                    const std::optional<std::uint32_t> writer =
                        access.writes ? std::optional{*m_capturePass}
                                      : writerBefore(*captured, access.resource, *m_capturePass);
                    std::string label = std::format("{} {}", access.writes ? "OUT" : "IN ", resource.name);
                    if (resource.texture)
                    {
                        label += std::format("  {}x{} {}", resource.info.width, resource.info.height,
                                             formatName(resource.info.format));
                    }
                    if (!access.writes)
                    {
                        label += writer ? std::format("  from {}", captured->passes[*writer].name)
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
            if (m_captureSnapshot && *m_captureSnapshot < snapshots.size() && m_captureDisplay.texture.isSet())
            {
                const Renderer::CaptureSnapshot& snapshot = snapshots[*m_captureSnapshot];
                ImGui::SeparatorText(std::format("{} after {}", captured->resources[snapshot.resource].name,
                                                 captured->passes[snapshot.pass].name)
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
