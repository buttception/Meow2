/// @file
/// @brief RenderDebug (RenderDebug.h).

#include "RenderDebug.h"

#include <imgui.h>

#include <format>
#include <string>

namespace Meow2::RendererApp
{
namespace
{
constexpr std::array<DebugViewEntry, 20> c_views{{
    {-1, "Shaded", "Scene"},
    {MEOW2_DEBUG_VIEW_TRIANGLES, "Triangles", "Geometry"},
    {MEOW2_DEBUG_VIEW_INSTANCES, "Instances", "Geometry"},
    {MEOW2_DEBUG_VIEW_MATERIALS, "Materials", "Geometry"},
    {MEOW2_DEBUG_VIEW_OVERDRAW, "Overdraw", "Geometry"},
    {MEOW2_DEBUG_VIEW_DEPTH, "Depth", "Geometry"},
    {MEOW2_DEBUG_VIEW_GEOMETRY_NORMAL, "Geometry normal", "Surface"},
    {MEOW2_DEBUG_VIEW_SHADING_NORMAL, "Shading normal", "Surface"},
    {MEOW2_DEBUG_VIEW_TANGENT, "Tangent", "Surface"},
    {MEOW2_DEBUG_VIEW_UV, "UV", "Surface"},
    {MEOW2_DEBUG_VIEW_MIP_LEVEL, "Mip level", "Surface"},
    {MEOW2_DEBUG_VIEW_BASE_COLOR, "Base colour", "Material"},
    {MEOW2_DEBUG_VIEW_METALLIC, "Metallic", "Material"},
    {MEOW2_DEBUG_VIEW_ROUGHNESS, "Roughness", "Material"},
    {MEOW2_DEBUG_VIEW_OCCLUSION, "Occlusion", "Material"},
    {MEOW2_DEBUG_VIEW_EMISSIVE, "Emissive", "Material"},
    {MEOW2_DEBUG_VIEW_ALPHA, "Alpha", "Material"},
    {MEOW2_DEBUG_VIEW_LIGHTING, "Lighting only", "Lighting"},
    {MEOW2_DEBUG_VIEW_DIFFUSE, "Diffuse only", "Lighting"},
    {MEOW2_DEBUG_VIEW_SPECULAR, "Specular only", "Lighting"},
}};

/// @brief The menu's label for a view.
std::string_view labelOf(int view)
{
    for (const DebugViewEntry& entry : c_views)
    {
        if (entry.view == view)
        {
            return entry.label;
        }
    }
    return "?";
}
} // namespace

std::span<const DebugViewEntry> debugViewEntries()
{
    return c_views;
}

RenderDebug::RenderDebug(Renderer::PipelineLibrary& pipelines, AssetId toneMapShader, Rhi::RhiFormat presentFormat,
                         Log::Logger& log)
    : m_pipelines(pipelines)
    , m_log(log)
    , m_toneMapShader(toneMapShader)
    , m_presentFormat(presentFormat)
{
}

bool RenderDebug::active() const
{
    return m_state.view >= 0 || m_state.wireframe == WireframeMode::Only;
}

bool RenderDebug::prepare(Renderer::GpuScene& scene)
{
    if (m_scene.scene != &scene)
    {
        m_scene = ScenePipelines{.scene = &scene};
    }
    if (m_scene.failed)
    {
        return false;
    }
    const auto build = [&](std::optional<Renderer::DrawPipelines>& out, const Renderer::DrawPipelineOptions& options)
    {
        if (out)
        {
            return true;
        }
        Result<Renderer::DrawPipelines> made = scene.makeDrawPipelines(options);
        if (!made)
        {
            MEOW2_LOG_ERROR(m_log, "Debug view pipelines ({}) not built: {}", options.name, made.error());
            m_scene.failed = true;
            return false;
        }
        out = *made;
        return true;
    };
    const bool needsView = m_state.view >= 0 && m_state.view != static_cast<int>(MEOW2_DEBUG_VIEW_OVERDRAW);
    if ((needsView || m_state.wireframe == WireframeMode::Only) &&
        !build(m_scene.view, {.options = {{"DEBUG_VIEW", "1"}}, .name = "Debug view"}))
    {
        return false;
    }
    if (m_state.view == static_cast<int>(MEOW2_DEBUG_VIEW_OVERDRAW) &&
        !build(m_scene.overdraw, {.options = {{"DEBUG_VIEW", "1"}},
                                  .blend = Rhi::BlendMode::Additive,
                                  .depthTest = false,
                                  .depthWrite = false,
                                  .name = "Overdraw"}))
    {
        return false;
    }
    if (m_state.wireframe != WireframeMode::Off && !build(m_scene.wire, {.options = {{"DEBUG_VIEW", "2"}},
                                                                         .fill = Rhi::FillMode::Wireframe,
                                                                         .depthWrite = false,
                                                                         .name = "Wireframe"}))
    {
        return false;
    }
    if (active() && !m_present)
    {
        Result<Renderer::PipelineId> present =
            m_pipelines.add(Renderer::GraphicsPipelineRequest{.shader = m_toneMapShader,
                                                              .options = {{"DEBUG_OUTPUT", "1"}},
                                                              .colorFormat = m_presentFormat,
                                                              .depthFormat = Rhi::RhiFormat::Unknown,
                                                              .depthWrite = false,
                                                              .name = "Debug present"});
        if (!present)
        {
            MEOW2_LOG_ERROR(m_log, "Debug present pipeline not built: {}", present.error());
            m_scene.failed = true;
            return false;
        }
        m_present = *present;
    }
    return true;
}

void RenderDebug::apply(Renderer::GpuScene& scene, Shader::FrameConstants& frame)
{
    const bool ready = prepare(scene);
    std::optional<Renderer::DrawPipelines> override;
    frame.debugView = 0;
    if (ready && m_state.view == static_cast<int>(MEOW2_DEBUG_VIEW_OVERDRAW))
    {
        override = m_scene.overdraw;
        frame.debugView = MEOW2_DEBUG_VIEW_OVERDRAW;
    }
    else if (ready && m_state.view >= 0)
    {
        override = m_scene.view;
        frame.debugView = static_cast<std::uint32_t>(m_state.view);
    }
    else if (ready && m_state.wireframe == WireframeMode::Only)
    {
        // Black surfaces that still write depth: the wireframe over them hides the edges behind.
        override = m_scene.view;
        frame.debugView = MEOW2_DEBUG_VIEW_BLACK;
    }
    scene.setPipelineOverride(override);
    // Debug views get a plain background: no sky drawn behind (it still lights the lighting views).
    scene.setSkyPass(!(ready && active()));
}

Math::Frustum RenderDebug::cullingFrustum(const Math::Frustum& current)
{
    if (!m_state.freezeCulling)
    {
        m_frozen.reset();
        return current;
    }
    if (!m_frozen)
    {
        m_frozen = current;
    }
    return *m_frozen;
}

Math::Color RenderDebug::clearColor(const Math::Color& normal) const
{
    return active() ? Math::Color{.r = 0.0F, .g = 0.0F, .b = 0.0F, .a = 1.0F} : normal;
}

void RenderDebug::addOverlays(Renderer::RenderGraph& graph, Renderer::GpuScene& scene, Renderer::GraphResource color,
                              Rhi::TextureHandle texture, Rhi::TextureHandle depth, std::uint32_t frameSlot)
{
    if (m_state.wireframe != WireframeMode::Off && m_scene.scene == &scene && m_scene.wire)
    {
        scene.addOverlayPass(graph, "Wireframe", *m_scene.wire, color, texture, depth, frameSlot);
    }
}

bool RenderDebug::addPresent(Renderer::RenderGraph& graph, Renderer::GraphResource hdr, Rhi::TextureHandle texture,
                             Rhi::IRhiSwapchain& swapchain, Rhi::IRhiDevice& device)
{
    if (!active() || !m_present || m_scene.failed)
    {
        return false;
    }
    Shader::PushConstants push{};
    push.instance = device.bindlessIndex(texture);
    push.userData = m_state.view == static_cast<int>(MEOW2_DEBUG_VIEW_OVERDRAW) ? 1U : 0U; // heat map, or as is
    graph.addPass({.name = "Debug present",
                   .accesses = {{hdr, Rhi::ResourceState::ShaderRead}},
                   .record =
                       [this, push, &swapchain](Rhi::IRhiCommandList& list)
                   {
                       list.beginRendering(swapchain, std::nullopt);
                       list.setPipeline(m_pipelines.get(*m_present));
                       list.setPushConstants(push);
                       list.draw(3);
                       list.endRendering();
                   },
                   .small = true});
    return true;
}

void RenderDebug::drawWindow(const UiFonts& fonts)
{
    if (!ImGui::Begin("DEBUG###Debug"))
    {
        ImGui::End();
        return;
    }
    drawWindowAccents();
    ImGui::SeparatorText("VIEW");
    if (ImGui::BeginCombo("##view", std::string{labelOf(m_state.view)}.c_str()))
    {
        std::string_view group;
        for (const DebugViewEntry& entry : c_views)
        {
            if (entry.group != group)
            {
                group = entry.group;
                ImGui::TextDisabled("%.*s", static_cast<int>(group.size()), group.data());
            }
            if (ImGui::Selectable(std::format("   {}", entry.label).c_str(), m_state.view == entry.view))
            {
                m_state.view = entry.view;
            }
        }
        ImGui::EndCombo();
    }
    if (m_state.view == static_cast<int>(MEOW2_DEBUG_VIEW_OVERDRAW))
    {
        ImGui::TextColored(UiColors::c_textDim, "Layers per pixel: blue 1, green 2, yellow 4,\nred 8, white 16+.");
    }
    else if (m_state.view == static_cast<int>(MEOW2_DEBUG_VIEW_MIP_LEVEL))
    {
        ImGui::TextColored(UiColors::c_textDim, "Mip 0 red, 1 yellow, 2 green, 3 cyan,\n4 blue, 5+ magenta.");
    }
    ImGui::SeparatorText("WIREFRAME");
    int wireframe = static_cast<int>(m_state.wireframe);
    ImGui::RadioButton("OFF", &wireframe, static_cast<int>(WireframeMode::Off));
    ImGui::SameLine();
    ImGui::RadioButton("OVERLAY", &wireframe, static_cast<int>(WireframeMode::Overlay));
    ImGui::SameLine();
    ImGui::RadioButton("ONLY", &wireframe, static_cast<int>(WireframeMode::Only));
    m_state.wireframe = static_cast<WireframeMode>(wireframe);
    ImGui::SeparatorText("CULLING");
    ImGui::Checkbox("FREEZE", &m_state.freezeCulling);
    ImGui::SetItemTooltip("Keep culling with the camera's frustum from now: fly away and see what was culled.");
    if (m_scene.failed)
    {
        pushMonoFont(fonts);
        ImGui::TextColored(UiColors::c_accentBright, "Debug pipelines failed to build; see the console.");
        popMonoFont();
    }
    ImGui::End();
}
} // namespace Meow2::RendererApp
