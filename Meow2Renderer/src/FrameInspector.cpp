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
        FlowPass out{.name = pass.name, .accesses = {}};
        for (const Renderer::GraphAccess& access : pass.accesses)
        {
            out.accesses.push_back({.resource = access.resource.index,
                                    .state = access.state,
                                    .writes = Renderer::isWriteState(access.state)});
        }
        flow.passes.push_back(std::move(out));
    }
    return flow;
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
    return layout;
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
            .small = true};
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
                     .small = true});
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

std::optional<std::uint32_t> FrameInspector::drawFlow(const FrameFlow& flow, std::optional<std::uint32_t> selected,
                                                      const char* id)
{
    std::optional<std::uint32_t> clicked;
    const FlowLayout layout = layoutFlow(flow);
    const float scale = ImGui::GetStyle().FontScaleDpi;
    const ImVec2 box{150.0F * scale, 30.0F * scale};
    const ImVec2 gap{70.0F * scale, 14.0F * scale};
    ImGui::BeginChild(
        id, ImVec2{0.0F, std::min(260.0F * scale, (box.y + gap.y) * static_cast<float>(layout.rows) + gap.y * 2.0F)},
        ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar);
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const auto boxMin = [&](std::uint32_t pass)
    {
        return ImVec2{origin.x + gap.y + static_cast<float>(layout.column[pass]) * (box.x + gap.x),
                      origin.y + gap.y + static_cast<float>(layout.row[pass]) * (box.y + gap.y)};
    };

    // Links first, under the boxes: from the pass that wrote a resource to each pass that uses it after.
    std::set<std::tuple<std::uint32_t, std::uint32_t, std::uint32_t>> drawn;
    for (std::uint32_t pass = 0; pass < flow.passes.size(); ++pass)
    {
        for (const FlowAccess& access : flow.passes[pass].accesses)
        {
            const std::optional<std::uint32_t> writer = writerBefore(flow, access.resource, pass);
            if (!writer || !drawn.insert({*writer, pass, access.resource}).second)
            {
                continue;
            }
            const bool highlighted = selected && (*selected == pass || *selected == *writer);
            const ImU32 colour = ImGui::GetColorU32(highlighted ? UiColors::c_accent : UiColors::c_line);
            const ImVec2 from{boxMin(*writer).x + box.x, boxMin(*writer).y + box.y * 0.5F};
            const ImVec2 to{boxMin(pass).x, boxMin(pass).y + box.y * 0.5F};
            const float bend = std::max((to.x - from.x) * 0.5F, 20.0F * scale);
            draw->AddBezierCubic(from, ImVec2{from.x + bend, from.y}, ImVec2{to.x - bend, to.y}, to, colour,
                                 highlighted ? 2.0F : 1.0F);
            if (highlighted)
            {
                const std::string& name = flow.resources[access.resource].name;
                draw->AddText(ImVec2{(from.x + to.x) * 0.5F - ImGui::CalcTextSize(name.c_str()).x * 0.5F,
                                     (from.y + to.y) * 0.5F - ImGui::GetTextLineHeight()},
                              ImGui::GetColorU32(UiColors::c_text), name.c_str());
            }
        }
    }
    for (std::uint32_t pass = 0; pass < flow.passes.size(); ++pass)
    {
        const ImVec2 min = boxMin(pass);
        const ImVec2 max{min.x + box.x, min.y + box.y};
        ImGui::SetCursorScreenPos(min);
        ImGui::PushID(static_cast<int>(pass));
        if (ImGui::InvisibleButton("pass", box))
        {
            clicked = pass;
        }
        const bool hovered = ImGui::IsItemHovered();
        ImGui::PopID();
        const bool isSelected = selected && *selected == pass;
        draw->AddRectFilled(min, max, ImGui::GetColorU32(hovered ? UiColors::c_frameHovered : UiColors::c_frame));
        draw->AddRect(min, max, ImGui::GetColorU32(isSelected ? UiColors::c_accentBright : UiColors::c_line), 0.0F,
                      isSelected ? 2.0F : 1.0F);
        const std::string label = std::format("{}  {}", pass, flow.passes[pass].name);
        draw->PushClipRect(min, max, true);
        draw->AddText(ImVec2{min.x + 6.0F * scale, min.y + (box.y - ImGui::GetTextLineHeight()) * 0.5F},
                      ImGui::GetColorU32(UiColors::c_text), label.c_str());
        draw->PopClipRect();
    }
    // The child's scrollable size.
    ImGui::SetCursorScreenPos(origin);
    ImGui::Dummy(ImVec2{gap.y * 2.0F + static_cast<float>(layout.columns) * (box.x + gap.x),
                        gap.y * 2.0F + static_cast<float>(layout.rows) * (box.y + gap.y)});
    ImGui::EndChild();
    return clicked;
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
        ImGui::TextColored(UiColors::c_textDim, "%zu passes, %zu resources. Click a pass to watch its outputs.",
                           m_liveFlow.passes.size(), m_liveFlow.resources.size());
        if (const std::optional<std::uint32_t> clicked = drawFlow(m_liveFlow, selected, "live"))
        {
            m_selectedPass = (m_selectedPass && *m_selectedPass == m_liveFlow.passes[*clicked].name)
                                 ? std::nullopt // a second click stops watching
                                 : std::optional{m_liveFlow.passes[*clicked].name};
        }
        if (ImGui::Button("CAPTURE FRAME (F9)"))
        {
            requestCapture();
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
                    if (!ImGui::BeginTabItem(output.name.c_str()))
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
                m_capturePass.reset();
                m_captureSnapshot.reset();
                ImGui::End();
                return;
            }
            if (const std::optional<std::uint32_t> clicked = drawFlow(*m_capture, m_capturePass, "capture"))
            {
                m_capturePass = clicked;
            }
            if (m_capturePass)
            {
                const FlowPass& pass = m_capture->passes[*m_capturePass];
                ImGui::SeparatorText(std::format("{}  {}", *m_capturePass, pass.name).c_str());
                // Inputs: what it reads, and which pass wrote it; outputs: what it wrote. Clicking shows the texture.
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
