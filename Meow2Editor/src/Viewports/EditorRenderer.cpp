/// @file
/// @brief EditorRenderer (EditorRenderer.h).

#include "Viewports/EditorRenderer.h"

#include <Meow2/Graphics/Renderer/RenderGraph.h>
#include <Meow2/Math/Bounds.h>

#include <algorithm>
#include <cmath>
#include <format>
#include <string_view>
#include <utility>

namespace Meow2::Editor
{
namespace
{
/// @brief A cooked shader's AssetId from its `.meta` id.
AssetId shaderId(std::string_view text)
{
    return Uuid::parse(text).and_then([](const Uuid& uuid) { return AssetId::fromUuid(uuid); }).value_or(AssetId{});
}

constexpr Rhi::RhiFormat c_sceneFormat = Rhi::RhiFormat::Rgba16Float; ///< The scene's HDR colour.
constexpr Math::Color c_clear{.r = 0.0F, .g = 0.0F, .b = 0.0F, .a = 1.0F};
constexpr Math::Vec3 c_sunDirection{0.4F, 0.8F, -0.6F}; ///< Behind the default camera (on -Z), lighting what it sees.
} // namespace

EditorRenderer::EditorRenderer(const EditorRendererDeps& deps, const EditorRendererDesc& desc)
    : m_deps(deps)
    , m_desc(desc)
{
}

Result<std::unique_ptr<EditorRenderer>> EditorRenderer::create(const EditorRendererDeps& deps,
                                                               const EditorRendererDesc& desc)
{
    auto renderer = std::make_unique<EditorRenderer>(deps, desc);
    Result<std::unique_ptr<Renderer::RenderDevice>> device =
        Renderer::RenderDevice::create({.backend = desc.backend,
                                        .appName = "Meow2Editor",
                                        .validation = desc.validation,
                                        .presentation = desc.presentation,
                                        .recordingThreads = deps.jobs.threadCount()},
                                       deps.log);
    if (!device)
    {
        return Unexpected<Error>{device.error()};
    }
    renderer->m_device = std::move(*device);
    renderer->m_pipelines = std::make_unique<Renderer::PipelineLibrary>(
        Renderer::PipelineLibraryDeps{*renderer->m_device, deps.assets, deps.events, deps.log});

    // The sky lights the scene and is drawn behind it; the GPU scene draws through the deferred path with shadows.
    Result<std::unique_ptr<Renderer::SkyAtmosphere>> sky = Renderer::SkyAtmosphere::create(
        *renderer->m_device, *renderer->m_pipelines,
        {.shader = shaderId("01a1b5e2-3c16-8000-80a4-7b19d2e6c057"), .colorFormat = c_sceneFormat});
    if (!sky)
    {
        return Unexpected<Error>{sky.error()};
    }
    renderer->m_sky = std::move(*sky);
    Result<std::unique_ptr<Renderer::GpuScene>> scene =
        Renderer::GpuScene::create(*renderer->m_device, *renderer->m_pipelines,
                                   {.shader = shaderId("01a1b5e2-3c12-8000-80a4-7b19d2e6c053"),
                                    .deferredShader = shaderId("01a1b5e2-3c1c-8000-80a4-7b19d2e6c05d"),
                                    .shadowShader = shaderId("01a1b5e2-3c1e-8000-80a4-7b19d2e6c05f"),
                                    .instanceCapacity = desc.instanceCapacity,
                                    .colorFormat = c_sceneFormat});
    if (!scene)
    {
        return Unexpected<Error>{scene.error()};
    }
    renderer->m_scene = std::move(*scene);
    renderer->m_scene->setSky(renderer->m_sky.get());
    renderer->m_scene->setDeferred(true);
    // 1024 per cascade: each view has its own maps (two maps a cascade), so two viewports at 2048 would take 256 MB.
    renderer->m_scene->setShadows({.enabled = true, .cascades = {.count = 4, .resolution = 1024}});
    renderer->m_scene->setLighting({.sunDirection = c_sunDirection});
    renderer->m_toneMapShader = shaderId("01a1b5e2-3c17-8000-80a4-7b19d2e6c058");
    return renderer;
}

EditorRenderer::~EditorRenderer()
{
    if (m_device && m_device->hasDevice())
    {
        m_device->device().waitIdle();
        for (Viewport& viewport : m_viewports)
        {
            m_device->device().destroy(viewport.color);
            m_device->device().destroy(viewport.depth);
            viewport.swapchain.reset();
            viewport.offscreen.reset();
        }
    }
    m_viewports.clear();
    m_toneMaps.clear();
    m_scene.reset();
    m_sky.reset();
    m_pipelines.reset();
}

Result<ViewportId> EditorRenderer::addWindowViewport(const Platform::NativeWindowHandle& window, std::uint32_t width,
                                                     std::uint32_t height, std::string name)
{
    if (window.type == Platform::NativeWindowType::None)
    {
        return fail(ErrorCode::Unsupported, std::format("viewport '{}' has no native window to draw into", name));
    }
    Viewport viewport;
    viewport.name = std::move(name);
    viewport.window = window;
    viewport.width = width;
    viewport.height = height;
    Result<std::unique_ptr<Rhi::IRhiSwapchain>> swapchain =
        m_device->device().createSwapchain({.window = window,
                                            .width = std::max(width, 1U),
                                            .height = std::max(height, 1U),
                                            .vsync = true, // an editor shouldn't spin the GPU at hundreds of frames
                                            .name = viewport.name});
    if (!swapchain)
    {
        return Unexpected<Error>{swapchain.error()};
    }
    viewport.swapchain = std::move(*swapchain);
    if (!m_viewports.empty())
    {
        Result<Renderer::ViewId> view = m_scene->addView();
        if (!view)
        {
            return Unexpected<Error>{view.error()};
        }
        viewport.view = *view;
    }
    m_viewports.push_back(std::move(viewport));
    return static_cast<ViewportId>(m_viewports.size() - 1);
}

Result<ViewportId> EditorRenderer::addOffscreenViewport(std::uint32_t width, std::uint32_t height, std::string name)
{
    Viewport viewport;
    viewport.name = std::move(name);
    viewport.width = width;
    viewport.height = height;
    Result<std::unique_ptr<Renderer::OffscreenTarget>> target =
        Renderer::OffscreenTarget::create(m_device->device(), width, height);
    if (!target)
    {
        return Unexpected<Error>{target.error()};
    }
    viewport.offscreen = std::move(*target);
    if (!m_viewports.empty())
    {
        Result<Renderer::ViewId> view = m_scene->addView();
        if (!view)
        {
            return Unexpected<Error>{view.error()};
        }
        viewport.view = *view;
    }
    m_viewports.push_back(std::move(viewport));
    return static_cast<ViewportId>(m_viewports.size() - 1);
}

void EditorRenderer::resize(ViewportId viewport, std::uint32_t width, std::uint32_t height)
{
    m_viewports.at(viewport).width = width;
    m_viewports.at(viewport).height = height;
}

void EditorRenderer::setVisible(ViewportId viewport, bool visible)
{
    m_viewports.at(viewport).visible = visible;
}

void EditorRenderer::setRenderScale(ViewportId viewport, float scale)
{
    m_viewports.at(viewport).renderScale = std::clamp(scale, 0.25F, 1.0F);
}

float EditorRenderer::renderScale(ViewportId viewport) const
{
    return m_viewports.at(viewport).renderScale;
}

bool EditorRenderer::wasDrawn(ViewportId viewport) const
{
    return m_viewports.at(viewport).drawn;
}

std::pair<std::uint32_t, std::uint32_t> EditorRenderer::sceneSize(ViewportId viewport) const
{
    const Viewport& found = m_viewports.at(viewport);
    return {found.sceneWidth, found.sceneHeight};
}

Renderer::OffscreenTarget* EditorRenderer::offscreenTarget(ViewportId viewport)
{
    return m_viewports.at(viewport).offscreen.get();
}

bool EditorRenderer::ensureSceneTextures(Viewport& viewport)
{
    // The scene is drawn at the render scale; the tone map scales it up to the output.
    const auto scaled = [&viewport](std::uint32_t size)
    {
        return std::max(1U, static_cast<std::uint32_t>(std::lround(static_cast<float>(size) * viewport.renderScale)));
    };
    const std::uint32_t width = scaled(viewport.width);
    const std::uint32_t height = scaled(viewport.height);
    if (viewport.color.isSet() && viewport.sceneWidth == width && viewport.sceneHeight == height)
    {
        return true;
    }
    Rhi::IRhiDevice& device = m_device->device();
    device.destroy(viewport.color); // deferred until the GPU is done with them
    device.destroy(viewport.depth);
    viewport.color = {};
    viewport.depth = {};
    Result<Rhi::TextureHandle> color = device.createTexture({.width = width,
                                                             .height = height,
                                                             .format = c_sceneFormat,
                                                             .renderTarget = true,
                                                             .tag = Memory::MemTag::Renderer,
                                                             .name = viewport.name + " scene colour"});
    Result<Rhi::TextureHandle> depth = device.createTexture({.width = width,
                                                             .height = height,
                                                             .format = Rhi::RhiFormat::D32Float,
                                                             .renderTarget = true,
                                                             .tag = Memory::MemTag::Renderer,
                                                             .name = viewport.name + " depth"});
    if (!color || !depth)
    {
        if (color)
        {
            device.destroy(*color);
        }
        if (depth)
        {
            device.destroy(*depth);
        }
        MEOW2_LOG_WARN(m_deps.log, "Viewport '{}': no scene textures at {}x{}", viewport.name, width, height);
        return false;
    }
    viewport.color = *color;
    viewport.depth = *depth;
    viewport.sceneWidth = width;
    viewport.sceneHeight = height;
    return true;
}

Renderer::ToneMapPass* EditorRenderer::toneMapFor(Rhi::RhiFormat format)
{
    if (const auto found = m_toneMaps.find(format); found != m_toneMaps.end())
    {
        return found->second.get();
    }
    Result<std::unique_ptr<Renderer::ToneMapPass>> pass =
        Renderer::ToneMapPass::create(*m_pipelines, m_toneMapShader, format);
    if (!pass)
    {
        MEOW2_LOG_ERROR(m_deps.log, "No tone map pass: {}", pass.error());
        return nullptr;
    }
    return m_toneMaps.emplace(format, std::move(*pass)).first->second.get();
}

Result<void> EditorRenderer::renderFrame(std::span<const std::optional<ViewportCamera>> cameras)
{
    // Which viewports draw: visible, with a size and a camera. None: no frame at all.
    std::vector<ViewportId> drawing;
    for (ViewportId id = 0; id < m_viewports.size(); ++id)
    {
        Viewport& viewport = m_viewports[id];
        viewport.drawn = false;
        if (viewport.visible && viewport.width > 0 && viewport.height > 0 && id < cameras.size() && cameras[id])
        {
            drawing.push_back(id);
        }
    }
    if (drawing.empty())
    {
        return {};
    }

    Rhi::IRhiDevice& device = m_device->device();
    Result<Rhi::IRhiCommandList*> first = device.beginFrame();
    if (!first)
    {
        return Unexpected<Error>{first.error()};
    }
    const std::uint32_t slot = device.frameSlot();
    Renderer::RenderGraph graph;
    bool skyAdded = false;
    for (const ViewportId id : drawing)
    {
        Viewport& viewport = m_viewports[id];
        if (viewport.swapchain)
        {
            if (viewport.swapchain->width() != viewport.width || viewport.swapchain->height() != viewport.height)
            {
                if (Result<void> resized = viewport.swapchain->resize(viewport.width, viewport.height); !resized)
                {
                    MEOW2_LOG_WARN(m_deps.log, "Viewport '{}': {}", viewport.name, resized.error());
                    continue;
                }
            }
            if (!device.acquire(*viewport.swapchain))
            {
                continue; // minimised or mid-resize: nothing to draw to this frame
            }
        }
        if (!ensureSceneTextures(viewport))
        {
            continue;
        }
        const ViewportCamera& camera = *cameras[id];
        const float aspect = static_cast<float>(viewport.sceneWidth) / static_cast<float>(viewport.sceneHeight);
        const Math::CameraMatrices matrices = Math::cameraMatrices(camera.transform, camera.lens, aspect);
        if (!skyAdded)
        {
            // The sky's lookup tables depend only on the camera's altitude, so one update serves every view; each
            // view's own matrix turns them into its sky.
            m_sky->setView(slot,
                           {.sunDirection = c_sunDirection,
                            .cameraAltitudeKm = 0.05F + std::max(camera.transform.position.y, 0.0F) * 0.001F},
                           matrices.viewProjection);
            m_sky->addPasses(graph, slot);
            skyAdded = true;
        }
        Shader::FrameConstants frame{};
        frame.view = matrices.view;
        frame.projection = matrices.projection;
        frame.viewProjection = matrices.viewProjection;
        frame.cameraPosition = {camera.transform.position.x, camera.transform.position.y, camera.transform.position.z,
                                0.0F};
        frame.viewport = {static_cast<float>(viewport.sceneWidth), static_cast<float>(viewport.sceneHeight),
                          1.0F / static_cast<float>(viewport.sceneWidth),
                          1.0F / static_cast<float>(viewport.sceneHeight)};
        m_scene->setView(slot, frame, Math::frustumFromCamera(matrices), viewport.view);
        const Renderer::GraphResource hdr =
            m_scene->addPasses(graph, slot, nullptr, viewport.color, viewport.depth, c_clear, viewport.view);
        if (viewport.swapchain)
        {
            if (Renderer::ToneMapPass* toneMap = toneMapFor(viewport.swapchain->format()))
            {
                toneMap->addPass(graph, hdr, viewport.color, *viewport.swapchain, device);
            }
        }
        else if (Renderer::ToneMapPass* toneMap = toneMapFor(Renderer::OffscreenTarget::format()))
        {
            const Renderer::GraphResource target =
                graph.import(viewport.offscreen->texture(), Rhi::ResourceState::Undefined, viewport.name);
            toneMap->addPass(graph, hdr, viewport.color, target, viewport.offscreen->texture(), device);
        }
        viewport.drawn = true;
    }

    graph.compile();
    Result<std::vector<Rhi::IRhiCommandList*>> lists = graph.execute(device, m_deps.jobs);
    m_scene->endFrame(graph);
    m_sky->endFrame();
    if (!lists)
    {
        return Unexpected<Error>{lists.error()};
    }
    if (m_desc.readBackOffscreen)
    {
        Result<Rhi::IRhiCommandList*> tail = device.commandList(0);
        if (!tail)
        {
            return Unexpected<Error>{tail.error()};
        }
        for (Viewport& viewport : m_viewports)
        {
            if (viewport.drawn && viewport.offscreen)
            {
                (*tail)->barrier(viewport.offscreen->texture(), Rhi::ResourceState::ColorTarget,
                                 Rhi::ResourceState::CopySource);
                viewport.offscreen->copyToCpu(**tail);
            }
        }
        lists->push_back(*tail);
    }
    return device.endFrame(*lists);
}
} // namespace Meow2::Editor
