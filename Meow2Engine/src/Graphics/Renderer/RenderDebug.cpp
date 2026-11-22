/// @file
/// @brief RenderDebug (RenderDebug.h).

#include <Meow2/Graphics/Renderer/RenderDebug.h>

#include <algorithm>
#include <array>

namespace Meow2::Renderer
{
namespace
{
constexpr std::array<DebugViewEntry, 23> c_views{{
    {-1, "shaded", "Shaded", "Scene"},
    {MEOW2_DEBUG_VIEW_TRIANGLES, "triangles", "Triangles", "Geometry", false, "Drawn through the indirect path."},
    {MEOW2_DEBUG_VIEW_MESHLETS, "meshlets", "Meshlets", "Geometry", false,
     "A colour per meshlet. Grey: drawn by the indirect path (meshlets off, or no mesh shaders)."},
    {MEOW2_DEBUG_VIEW_INSTANCES, "instances", "Instances", "Geometry"},
    {MEOW2_DEBUG_VIEW_MATERIALS, "materials", "Materials", "Geometry"},
    {MEOW2_DEBUG_VIEW_OVERDRAW, "overdraw", "Overdraw", "Geometry", false,
     "Layers per pixel: blue 1, green 2, yellow 4, red 8, white 16+."},
    {MEOW2_DEBUG_VIEW_DEPTH, "depth", "Depth", "Geometry"},
    {MEOW2_DEBUG_VIEW_POSITION, "position", "Position", "Geometry", true,
     "Each pixel's position rebuilt from depth, as deferred lighting does, in 1 m bands (x red, y green, z blue). "
     "Made by a debug-only pass (dashed in the render graph): lighting never stores positions."},
    {MEOW2_DEBUG_VIEW_GEOMETRY_NORMAL, "geometryNormal", "Geometry normal", "Surface"},
    {MEOW2_DEBUG_VIEW_SHADING_NORMAL, "shadingNormal", "Shading normal", "Surface"},
    {MEOW2_DEBUG_VIEW_TANGENT, "tangent", "Tangent", "Surface"},
    {MEOW2_DEBUG_VIEW_UV, "uv", "UV", "Surface"},
    {MEOW2_DEBUG_VIEW_MIP_LEVEL, "mipLevel", "Mip level", "Surface", false,
     "Mip 0 red, 1 yellow, 2 green, 3 cyan, 4 blue, 5+ magenta."},
    {MEOW2_DEBUG_VIEW_BASE_COLOR, "baseColor", "Base colour", "Material"},
    {MEOW2_DEBUG_VIEW_METALLIC, "metallic", "Metallic", "Material"},
    {MEOW2_DEBUG_VIEW_ROUGHNESS, "roughness", "Roughness", "Material"},
    {MEOW2_DEBUG_VIEW_OCCLUSION, "occlusion", "Occlusion", "Material"},
    {MEOW2_DEBUG_VIEW_EMISSIVE, "emissive", "Emissive", "Material"},
    {MEOW2_DEBUG_VIEW_ALPHA, "alpha", "Alpha", "Material"},
    {MEOW2_DEBUG_VIEW_LIGHTING, "lighting", "Lighting only", "Lighting"},
    {MEOW2_DEBUG_VIEW_DIFFUSE, "diffuse", "Diffuse only", "Lighting"},
    {MEOW2_DEBUG_VIEW_SPECULAR, "specular", "Specular only", "Lighting"},
    {MEOW2_DEBUG_VIEW_CASCADES, "cascades", "Cascades", "Lighting", false,
     "The sun's shadow cascades, nearest first: red, green, blue, yellow; grey past the last. Darker where in "
     "shadow."},
}};

constexpr std::array<std::string_view, 3> c_wireframeNames{"off", "overlay", "only"};
} // namespace

std::string_view wireframeModeName(WireframeMode mode)
{
    return c_wireframeNames.at(static_cast<std::size_t>(mode));
}

std::optional<WireframeMode> wireframeModeFromName(std::string_view name)
{
    for (std::size_t i = 0; i < c_wireframeNames.size(); ++i)
    {
        if (c_wireframeNames[i] == name)
        {
            return static_cast<WireframeMode>(i);
        }
    }
    return std::nullopt;
}

std::span<const DebugViewEntry> debugViewEntries()
{
    return c_views;
}

const DebugViewEntry* findDebugView(int view)
{
    const auto found = std::ranges::find(c_views, view, &DebugViewEntry::view);
    return found != c_views.end() ? &*found : nullptr;
}

const DebugViewEntry* findDebugView(std::string_view key)
{
    const auto found = std::ranges::find(c_views, key, &DebugViewEntry::key);
    return found != c_views.end() ? &*found : nullptr;
}

RenderDebug::RenderDebug(PipelineLibrary& pipelines, AssetId toneMapShader, Log::Logger& log)
    : m_pipelines(pipelines)
    , m_log(log)
    , m_toneMapShader(toneMapShader)
{
}

bool RenderDebug::active() const
{
    return m_state.view >= 0 || m_state.wireframe == WireframeMode::Only;
}

bool RenderDebug::prepare(GpuScene& scene)
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
    const auto build = [&](std::optional<DrawPipelines>& out, const DrawPipelineOptions& options)
    {
        if (out)
        {
            return true;
        }
        Result<DrawPipelines> made = scene.makeDrawPipelines(options);
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
    return true;
}

std::optional<PipelineId> RenderDebug::presentFor(Rhi::RhiFormat format)
{
    if (const auto found = std::ranges::find(m_present, format, &std::pair<Rhi::RhiFormat, PipelineId>::first);
        found != m_present.end())
    {
        return found->second;
    }
    if (m_presentFailed)
    {
        return std::nullopt;
    }
    Result<PipelineId> present = m_pipelines.add(GraphicsPipelineRequest{.shader = m_toneMapShader,
                                                                         .options = {{"DEBUG_OUTPUT", "1"}},
                                                                         .colorFormat = format,
                                                                         .depthFormat = Rhi::RhiFormat::Unknown,
                                                                         .depthWrite = false,
                                                                         .name = "Debug present"});
    if (!present)
    {
        MEOW2_LOG_ERROR(m_log, "Debug present pipeline not built: {}", present.error());
        m_presentFailed = true;
        return std::nullopt;
    }
    m_present.emplace_back(format, *present);
    return *present;
}

void RenderDebug::apply(GpuScene& scene, Shader::FrameConstants& frame)
{
    const bool ready = prepare(scene);
    std::optional<DrawPipelines> override;
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

void RenderDebug::restore(GpuScene& scene)
{
    scene.setPipelineOverride(std::nullopt);
    scene.setSkyPass(true);
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

void RenderDebug::addOverlays(RenderGraph& graph, GpuScene& scene, GraphResource color, Rhi::TextureHandle texture,
                              Rhi::TextureHandle depth, std::uint32_t frameSlot, ViewId view)
{
    if (m_state.wireframe != WireframeMode::Off && m_scene.scene == &scene && m_scene.wire)
    {
        scene.addOverlayPass(graph, "Wireframe", *m_scene.wire, color, texture, depth, frameSlot, true, view);
    }
}

Shader::PushConstants RenderDebug::presentPush(Rhi::IRhiDevice& device, Rhi::TextureHandle texture,
                                               std::uint32_t targetWidth, std::uint32_t targetHeight) const
{
    Shader::PushConstants push{};
    push.instance = device.bindlessIndex(texture);
    // 1: overdraw as a heat map; 2: positions as 1 m bands; 0: colours as they are.
    push.userData = m_state.view == static_cast<int>(MEOW2_DEBUG_VIEW_OVERDRAW)   ? 1U
                    : m_state.view == static_cast<int>(MEOW2_DEBUG_VIEW_POSITION) ? 2U
                                                                                  : 0U;
    // As the tone map (ToneMapPass): a smaller source is scaled up with a linear sampler.
    const Rhi::TextureInfo source = device.textureInfo(texture);
    if (source.width != targetWidth || source.height != targetHeight)
    {
        push.frameBuffer = (targetWidth & 0xFFFFU) | (targetHeight << 16U);
        push.instanceBuffer = Rhi::c_defaultSamplerIndex;
    }
    return push;
}

bool RenderDebug::addPresent(RenderGraph& graph, GraphResource hdr, Rhi::TextureHandle texture,
                             Rhi::IRhiSwapchain& swapchain, Rhi::IRhiDevice& device)
{
    if (!active() || m_scene.failed)
    {
        return false;
    }
    const std::optional<PipelineId> pipeline = presentFor(swapchain.format());
    if (!pipeline)
    {
        return false;
    }
    const Shader::PushConstants push = presentPush(device, texture, swapchain.width(), swapchain.height());
    graph.addPass({.name = "Debug present",
                   .accesses = {{hdr, Rhi::ResourceState::ShaderRead}},
                   .record =
                       [this, id = *pipeline, push, &swapchain](Rhi::IRhiCommandList& list)
                   {
                       list.beginRendering(swapchain, std::nullopt);
                       list.setPipeline(m_pipelines.get(id));
                       list.setPushConstants(push);
                       list.draw(3);
                       list.endRendering();
                   },
                   .small = true,
                   .debug = true});
    return true;
}

bool RenderDebug::addPresent(RenderGraph& graph, GraphResource hdr, Rhi::TextureHandle texture, GraphResource target,
                             Rhi::TextureHandle targetTexture, Rhi::IRhiDevice& device)
{
    if (!active() || m_scene.failed)
    {
        return false;
    }
    const Rhi::TextureInfo targetInfo = device.textureInfo(targetTexture);
    const std::optional<PipelineId> pipeline = presentFor(targetInfo.format);
    if (!pipeline)
    {
        return false;
    }
    const Shader::PushConstants push = presentPush(device, texture, targetInfo.width, targetInfo.height);
    graph.addPass({.name = "Debug present",
                   .accesses = {{hdr, Rhi::ResourceState::ShaderRead}, {target, Rhi::ResourceState::ColorTarget}},
                   .record =
                       [this, id = *pipeline, push, targetTexture](Rhi::IRhiCommandList& list)
                   {
                       list.beginRendering(Rhi::RenderTargets{.color = targetTexture});
                       list.setPipeline(m_pipelines.get(id));
                       list.setPushConstants(push);
                       list.draw(3);
                       list.endRendering();
                   },
                   .small = true,
                   .debug = true});
    return true;
}
} // namespace Meow2::Renderer
