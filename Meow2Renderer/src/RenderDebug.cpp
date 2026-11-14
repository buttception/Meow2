/// @file
/// @brief RenderDebug (RenderDebug.h).

#include "RenderDebug.h"

#include <imgui.h>

#include <algorithm>
#include <string>

namespace Meow2::RendererApp
{
namespace
{
constexpr std::array<DebugViewEntry, 22> c_views{{
    {-1, "Shaded", "Scene"},
    {MEOW2_DEBUG_VIEW_TRIANGLES, "Triangles", "Geometry"},
    {MEOW2_DEBUG_VIEW_MESHLETS, "Meshlets", "Geometry"},
    {MEOW2_DEBUG_VIEW_INSTANCES, "Instances", "Geometry"},
    {MEOW2_DEBUG_VIEW_MATERIALS, "Materials", "Geometry"},
    {MEOW2_DEBUG_VIEW_OVERDRAW, "Overdraw", "Geometry"},
    {MEOW2_DEBUG_VIEW_DEPTH, "Depth", "Geometry"},
    {MEOW2_DEBUG_VIEW_POSITION, "Position", "Geometry", true},
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
        m_scene = ScenePipelines{};
        m_scene.scene = &scene;
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
    // Position is drawn by DepthPositionView from the depth of the normal frame: no shader variant.
    const bool needsView = m_state.view >= 0 && m_state.view != static_cast<int>(MEOW2_DEBUG_VIEW_OVERDRAW) &&
                           m_state.view != static_cast<int>(MEOW2_DEBUG_VIEW_POSITION);
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
    else if (ready && m_state.view == static_cast<int>(MEOW2_DEBUG_VIEW_POSITION))
    {
        // The scene draws as usual (deferred or forward); the debug present shows the position texture instead.
    }
    else if (ready && m_state.view >= 0)
    {
        override = m_scene.view;
        frame.debugView = static_cast<std::uint32_t>(m_state.view);
        if (m_state.view == static_cast<int>(MEOW2_DEBUG_VIEW_TRIANGLES))
        {
            // A colour per triangle needs a per-triangle value in the pixel shader. Mesh shaders share vertices
            // between triangles, and DXC can't mark a per-primitive input for Vulkan, so this view draws through
            // the indirect path (same triangles, same culling of instances).
            override->hasMeshLists = false;
        }
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

Math::Frustum RenderDebug::cullingFrustum(const Math::Frustum& current, const Shader::FrameConstants& frame)
{
    if (!m_state.freezeCulling)
    {
        m_frozen.reset();
        m_frozenCamera.reset();
        return current;
    }
    if (!m_frozen)
    {
        m_frozen = current;
        m_frozenCamera = frame;
    }
    return *m_frozen;
}

std::optional<Shader::FrameConstants> RenderDebug::frozenCamera() const
{
    return m_state.showFrustum ? m_frozenCamera : std::nullopt;
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
        scene.addOverlayPass(graph, "Wireframe", *m_scene.wire, color, texture, depth, frameSlot, true);
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
    // 1: overdraw as a heat map; 2: positions as 1 m bands; 0: colours as they are.
    push.userData = m_state.view == static_cast<int>(MEOW2_DEBUG_VIEW_OVERDRAW)   ? 1U
                    : m_state.view == static_cast<int>(MEOW2_DEBUG_VIEW_POSITION) ? 2U
                                                                                  : 0U;
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
                   .small = true,
                   .debug = true});
    return true;
}

void RenderDebug::drawViewButtons()
{
    // Every view is one click away: a row of equal buttons per group, wrapping to the window's width, the active one
    // in the accent colour.
    const ImGuiStyle& style = ImGui::GetStyle();
    float buttonWidth = 0.0F;
    for (const DebugViewEntry& entry : c_views)
    {
        buttonWidth =
            std::max(buttonWidth, ImGui::CalcTextSize(entry.label.data(), entry.label.data() + entry.label.size()).x);
    }
    buttonWidth += style.FramePadding.x * 2.0F;
    const float available = ImGui::GetContentRegionAvail().x;
    const int columns =
        std::max(1, static_cast<int>((available + style.ItemSpacing.x) / (buttonWidth + style.ItemSpacing.x)));
    buttonWidth = std::max(buttonWidth, (available - style.ItemSpacing.x * static_cast<float>(columns - 1)) /
                                            static_cast<float>(columns)); // fill the row
    std::string_view group;
    int column = 0;
    for (const DebugViewEntry& entry : c_views)
    {
        if (entry.group != group)
        {
            group = entry.group;
            column = 0;
            ImGui::TextColored(UiColors::c_textDim, "%.*s", static_cast<int>(group.size()), group.data());
        }
        if (column > 0)
        {
            ImGui::SameLine();
        }
        const bool active = m_state.view == entry.view;
        if (active)
        {
            ImGui::PushStyleColor(ImGuiCol_Button, UiColors::c_accent);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, UiColors::c_accentBright);
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, UiColors::c_accentBright);
        }
        ImGui::PushID(entry.view);
        if (ImGui::Button(std::string{entry.label}.c_str(), ImVec2{buttonWidth, 0.0F}))
        {
            m_state.view = entry.view;
        }
        if (entry.generated)
        {
            // A dotted outline: this view shows a buffer a debug-only pass makes for it, not one the frame has.
            const float inset = 2.0F;
            const ImVec2 min = ImGui::GetItemRectMin();
            const ImVec2 max = ImGui::GetItemRectMax();
            dashedRect(*ImGui::GetWindowDrawList(), ImVec2{min.x + inset, min.y + inset},
                       ImVec2{max.x - inset, max.y - inset},
                       ImGui::GetColorU32(active ? UiColors::c_text : UiColors::c_textDim), 1.0F, 3.0F);
            ImGui::SetItemTooltip("Shown from a buffer made only for this view (dotted): a debug pass rebuilds it, "
                                  "and nothing in the frame uses it.");
        }
        ImGui::PopID();
        if (active)
        {
            ImGui::PopStyleColor(3);
        }
        column = (column + 1) % columns;
    }
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
    drawViewButtons();
    if (m_state.view == static_cast<int>(MEOW2_DEBUG_VIEW_OVERDRAW))
    {
        ImGui::TextColored(UiColors::c_textDim, "Layers per pixel: blue 1, green 2, yellow 4,\nred 8, white 16+.");
    }
    else if (m_state.view == static_cast<int>(MEOW2_DEBUG_VIEW_MIP_LEVEL))
    {
        ImGui::TextColored(UiColors::c_textDim, "Mip 0 red, 1 yellow, 2 green, 3 cyan,\n4 blue, 5+ magenta.");
    }
    else if (m_state.view == static_cast<int>(MEOW2_DEBUG_VIEW_MESHLETS))
    {
        ImGui::TextColored(UiColors::c_textDim, "A colour per meshlet. Grey: drawn by the\nindirect path (MESHLETS off "
                                                "in Settings,\nor no mesh shaders).");
    }
    else if (m_state.view == static_cast<int>(MEOW2_DEBUG_VIEW_TRIANGLES))
    {
        ImGui::TextColored(UiColors::c_textDim, "Drawn through the indirect path.");
    }
    else if (m_state.view == static_cast<int>(MEOW2_DEBUG_VIEW_POSITION))
    {
        ImGui::TextColored(UiColors::c_textDim,
                           "Each pixel's position rebuilt from depth, as deferred\nlighting does, in "
                           "1 m bands (x red, y green, z blue).\nMade by a debug-only pass (dashed "
                           "in the render\ngraph): lighting never stores positions.");
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
    ImGui::SameLine();
    ImGui::Checkbox("SHOW FRUSTUM", &m_state.showFrustum);
    ImGui::SetItemTooltip("While frozen, draw the frozen camera's view volume (yellow), through the scene.");
    if (m_scene.failed)
    {
        pushMonoFont(fonts);
        ImGui::TextColored(UiColors::c_accentBright, "Debug pipelines failed to build; see the console.");
        popMonoFont();
    }
    ImGui::End();
}
} // namespace Meow2::RendererApp
