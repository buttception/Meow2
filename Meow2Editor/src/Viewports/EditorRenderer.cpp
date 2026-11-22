/// @file
/// @brief EditorRenderer (EditorRenderer.h).

#include "Viewports/EditorRenderer.h"

#include <Meow2/Graphics/Renderer/RenderGraph.h>
#include <Meow2/Math/Bounds.h>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
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
/// @brief The selection mask's format: one channel (the RHI has no 8-bit single-channel format).
constexpr Rhi::RhiFormat c_maskFormat = Rhi::RhiFormat::R32Float;
/// @brief The pick buffer's size: the header (the record count) and MEOW2_PICK_CAPACITY records.
constexpr std::uint64_t c_pickBytes =
    MEOW2_PICK_HEADER_BYTES + std::uint64_t{MEOW2_PICK_CAPACITY} * MEOW2_PICK_RECORD_BYTES;
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
    Result<std::unique_ptr<Renderer::GpuScene>> scene = renderer->makeScene();
    if (!scene)
    {
        return Unexpected<Error>{scene.error()};
    }
    renderer->m_scene = std::move(*scene);
    renderer->m_toneMapShader = shaderId("01a1b5e2-3c17-8000-80a4-7b19d2e6c058");
    renderer->m_debug =
        std::make_unique<Renderer::RenderDebug>(*renderer->m_pipelines, renderer->m_toneMapShader, deps.log);
    if (Result<void> overlays = renderer->makeOverlayPipelines(); !overlays)
    {
        return Unexpected<Error>{overlays.error()};
    }
    Result<Renderer::PipelineId> outline = renderer->m_pipelines->add(
        Renderer::GraphicsPipelineRequest{.shader = shaderId("01a1b5e2-3c1f-8000-80a4-7b19d2e6c060"),
                                          .colorFormat = c_sceneFormat,
                                          .depthFormat = Rhi::RhiFormat::Unknown,
                                          .depthWrite = false,
                                          .name = "Selection outline"});
    if (!outline)
    {
        return Unexpected<Error>{outline.error()};
    }
    renderer->m_outlinePipeline = *outline;

    // The pick records: a count, then {instance, depth, distance} records (MEOW2_PICK_*); one GPU buffer, a readback
    // per slot.
    Rhi::IRhiDevice& rhi = renderer->m_device->device();
    Result<Rhi::BufferHandle> pick = rhi.createBuffer(
        {.size = c_pickBytes, .memory = Rhi::RhiMemory::GpuOnly, .tag = Memory::MemTag::Renderer, .name = "Pick"});
    if (!pick)
    {
        return Unexpected<Error>{pick.error()};
    }
    renderer->m_pickBuffer = *pick;
    for (std::uint32_t slot = 0; slot < Rhi::c_maxFramesInFlight; ++slot)
    {
        Result<Rhi::BufferHandle> readback = rhi.createBuffer({.size = c_pickBytes,
                                                               .memory = Rhi::RhiMemory::Readback,
                                                               .tag = Memory::MemTag::Renderer,
                                                               .name = std::format("Pick readback {}", slot)});
        if (!readback)
        {
            return Unexpected<Error>{readback.error()};
        }
        renderer->m_pickReadback[slot] = *readback;
    }
    return renderer;
}

Result<void> EditorRenderer::makeOverlayPipelines()
{
    // From the debug-view variant, whose vertex output carries the instance index. No depth test: a pick finds every
    // surface under the cursor (sorted on the CPU), and the outline shows a selected object behind others.
    const Renderer::DrawPipelineOptions common{
        .options = {{"DEBUG_VIEW", "1"}}, .depthTest = false, .depthWrite = false, .colorFormat = c_maskFormat};
    Renderer::DrawPipelineOptions pick = common;
    pick.pixelEntry = "psPick";
    pick.name = "Pick";
    Result<Renderer::DrawPipelines> pickPipelines = m_scene->makeDrawPipelines(pick);
    if (!pickPipelines)
    {
        return Unexpected<Error>{pickPipelines.error()};
    }
    Renderer::DrawPipelineOptions mask = common;
    mask.pixelEntry = "psSelectionMask";
    mask.name = "Selection mask";
    Result<Renderer::DrawPipelines> maskPipelines = m_scene->makeDrawPipelines(mask);
    if (!maskPipelines)
    {
        return Unexpected<Error>{maskPipelines.error()};
    }
    m_pickPipelines = *pickPipelines;
    m_maskPipelines = *maskPipelines;
    return {};
}

void EditorRenderer::requestPick(ViewportId viewport, std::uint32_t x, std::uint32_t y)
{
    m_pickRequest = PickRequest{.viewport = viewport, .x = x, .y = y};
}

std::optional<PickResult> EditorRenderer::takePickResult()
{
    return std::exchange(m_pickResult, std::nullopt);
}

bool EditorRenderer::pickPending() const
{
    return m_pickRequest.has_value() ||
           std::ranges::any_of(m_pickInFlight, [](const std::optional<PickRequest>& slot) { return slot.has_value(); });
}

void EditorRenderer::setOutline(ViewportId viewport, bool outline)
{
    m_viewports.at(viewport).outline = outline;
}

void EditorRenderer::setHasSelection(bool any)
{
    m_hasSelection = any;
}

void EditorRenderer::setDebugViewport(std::optional<ViewportId> viewport)
{
    m_debugViewport = viewport;
}

Renderer::RenderDebugState& EditorRenderer::debugState()
{
    return m_debug->state();
}

void EditorRenderer::setGrid(bool on)
{
    m_gridOn = on;
}

bool EditorRenderer::debugFailed() const
{
    return m_debug->failed();
}

RenderStats EditorRenderer::stats() const
{
    Rhi::IRhiDevice& device = m_device->device(); // memoryStats() asks the driver: not const
    return RenderStats{.backend = m_device->backend() == Rhi::RhiBackend::D3D12 ? "D3D12" : "Vulkan",
                       .gpu = device.adapter().name,
                       .gpuMemory = device.memoryStats(),
                       .validationErrors = device.validationErrorCount(),
                       .instances = m_scene->instanceCount(),
                       .meshes = m_scene->meshCount(),
                       .materials = m_scene->materialCount(),
                       .textures = m_scene->textureCount(),
                       .meshlets = m_scene->usesMeshlets()};
}

void EditorRenderer::setOverlay(ViewportId viewport, ViewportOverlay overlay)
{
    m_viewports.at(viewport).overlay = std::move(overlay);
}

void EditorRenderer::readPick(std::uint32_t slot)
{
    std::optional<PickRequest> request = std::exchange(m_pickInFlight[slot], std::nullopt);
    if (!request)
    {
        return;
    }
    const std::span<const std::byte> bytes = m_device->device().mappedMemory(m_pickReadback[slot]);
    const auto word = [&bytes](std::size_t offset)
    {
        std::uint32_t value = 0;
        std::memcpy(&value, bytes.data() + offset, sizeof(value));
        return value;
    };
    // Every fragment near the cursor appended one record. Each instance is kept once, at its closest pixel to the
    // cursor and its nearest depth there: what covers the cursor's own pixel beats something nearer that only covers
    // a pixel beside it (the window is there for thin things, not to reorder what's right under the cursor).
    const std::uint32_t count = std::min(word(0), static_cast<std::uint32_t>(MEOW2_PICK_CAPACITY));
    PickResult result{.viewport = request->viewport, .x = request->x, .y = request->y};
    for (std::uint32_t i = 0; i < count; ++i)
    {
        const std::size_t offset = MEOW2_PICK_HEADER_BYTES + static_cast<std::size_t>(i) * MEOW2_PICK_RECORD_BYTES;
        const PickHit record{
            .instance = word(offset), .depth = std::bit_cast<float>(word(offset + 4)), .distance = word(offset + 8)};
        const auto found = std::ranges::find_if(result.hits, [&record](const PickHit& hit)
                                                { return hit.instance == record.instance; });
        if (found == result.hits.end())
        {
            result.hits.push_back(record);
        }
        else if (record.distance < found->distance ||
                 (record.distance == found->distance && record.depth > found->depth))
        {
            *found = record;
        }
    }
    // Closest to the cursor first, then nearest (reversed Z: the larger depth); ties stable by instance.
    std::ranges::sort(result.hits,
                      [](const PickHit& a, const PickHit& b)
                      {
                          if (a.distance != b.distance)
                          {
                              return a.distance < b.distance;
                          }
                          return a.depth != b.depth ? a.depth > b.depth : a.instance < b.instance;
                      });
    m_pickResult = std::move(result);
}

bool EditorRenderer::ensureMask(Viewport& viewport)
{
    Rhi::IRhiDevice& device = m_device->device();
    if (viewport.mask.isSet() && viewport.maskWidth == viewport.sceneWidth &&
        viewport.maskHeight == viewport.sceneHeight)
    {
        return true;
    }
    device.destroy(viewport.mask);
    viewport.mask = {};
    Result<Rhi::TextureHandle> mask = device.createTexture({.width = viewport.sceneWidth,
                                                            .height = viewport.sceneHeight,
                                                            .format = c_maskFormat,
                                                            .renderTarget = true,
                                                            .tag = Memory::MemTag::Renderer,
                                                            .name = viewport.name + " selection mask"});
    if (!mask)
    {
        MEOW2_LOG_WARN(m_deps.log, "Viewport '{}': no selection mask: {}", viewport.name, mask.error());
        return false;
    }
    viewport.mask = *mask;
    viewport.maskState = Rhi::ResourceState::Undefined;
    viewport.maskWidth = viewport.sceneWidth;
    viewport.maskHeight = viewport.sceneHeight;
    return true;
}

Result<std::unique_ptr<Renderer::GpuScene>> EditorRenderer::makeScene() const
{
    Result<std::unique_ptr<Renderer::GpuScene>> scene =
        Renderer::GpuScene::create(*m_device, *m_pipelines,
                                   {.shader = shaderId("01a1b5e2-3c12-8000-80a4-7b19d2e6c053"),
                                    .deferredShader = shaderId("01a1b5e2-3c1c-8000-80a4-7b19d2e6c05d"),
                                    .shadowShader = shaderId("01a1b5e2-3c1e-8000-80a4-7b19d2e6c05f"),
                                    .instanceCapacity = m_desc.instanceCapacity,
                                    .colorFormat = c_sceneFormat});
    if (!scene)
    {
        return Unexpected<Error>{scene.error()};
    }
    (*scene)->setSky(m_sky.get());
    (*scene)->setDeferred(true);
    // 1024 per cascade: each view has its own maps (two maps a cascade), so two viewports at 2048 would take 256 MB.
    (*scene)->setShadows({.enabled = true, .cascades = {.count = 4, .resolution = 1024}});
    (*scene)->setLighting({.sunDirection = m_sunDirection, .sunIntensity = m_sunIntensity, .exposure = m_exposure});
    return scene;
}

Result<void> EditorRenderer::resetScene()
{
    Result<std::unique_ptr<Renderer::GpuScene>> fresh = makeScene();
    if (!fresh)
    {
        return Unexpected<Error>{fresh.error()};
    }
    // The old scene's buffers may still be in use by frames in flight; its destructor hands them to the device's
    // deferred destruction. Views are per scene, so each viewport after the first gets a view in the new one.
    m_scene = std::move(*fresh);
    for (std::size_t i = 1; i < m_viewports.size(); ++i)
    {
        Result<Renderer::ViewId> view = m_scene->addView();
        if (!view)
        {
            return Unexpected<Error>{view.error()};
        }
        m_viewports[i].view = *view;
    }
    // Draw pipelines belong to their scene; a pick in flight named the old scene's instances.
    m_pickInFlight = {};
    m_pickResult.reset();
    // The debug views' pipelines are the old scene's too: new tools, the same settings (frozen culling starts over).
    const Renderer::RenderDebugState debugState = m_debug->state();
    m_debug = std::make_unique<Renderer::RenderDebug>(*m_pipelines, m_toneMapShader, m_deps.log);
    m_debug->state() = debugState;
    return makeOverlayPipelines();
}

void EditorRenderer::setEnvironment(const Scene::Environment& environment)
{
    m_sunDirection = Scene::sunDirection(environment);
    m_sunIntensity = environment.sunIntensity;
    m_exposure = environment.exposure;
    m_scene->setLighting({.sunDirection = m_sunDirection,
                          .sunColor = environment.sunColor,
                          .sunIntensity = m_sunIntensity,
                          .exposure = m_exposure});
}

EditorRenderer::~EditorRenderer()
{
    if (m_device && m_device->hasDevice())
    {
        m_device->device().waitIdle();
        m_positionView.reset(); // their GPU objects go while the device is still there
        m_grid.reset();
        for (Viewport& viewport : m_viewports)
        {
            m_device->device().destroy(viewport.color);
            m_device->device().destroy(viewport.depth);
            m_device->device().destroy(viewport.mask);
            viewport.swapchain.reset();
            viewport.offscreen.reset();
            viewport.debugLines.reset();
            viewport.screenLines.reset();
        }
        m_device->device().destroy(m_pickBuffer);
        for (const Rhi::BufferHandle readback : m_pickReadback)
        {
            m_device->device().destroy(readback);
        }
    }
    m_viewports.clear();
    m_toneMaps.clear();
    m_debug.reset();
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
    // Swapchains are resized before the frame begins: the RHI forbids it while a frame records (the GPU may still be
    // using the old images, and the resize waits for it). A viewport that can't be resized sits this frame out.
    std::erase_if(drawing,
                  [this](ViewportId id)
                  {
                      Viewport& viewport = m_viewports[id];
                      if (!viewport.swapchain || (viewport.swapchain->width() == viewport.width &&
                                                  viewport.swapchain->height() == viewport.height))
                      {
                          return false;
                      }
                      if (Result<void> resized = viewport.swapchain->resize(viewport.width, viewport.height); !resized)
                      {
                          MEOW2_LOG_WARN(m_deps.log, "Viewport '{}': {}", viewport.name, resized.error());
                          return true;
                      }
                      return false;
                  });
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
    readPick(slot); // beginFrame() waited for this slot's last frame, so its copy has landed
    Renderer::RenderGraph graph;
    bool skyAdded = false;
    // Resources whose states carry over to the next frame, read back after compile().
    std::optional<Renderer::GraphResource> pickResource;
    std::vector<std::pair<ViewportId, Renderer::GraphResource>> masks;
    for (const ViewportId id : drawing)
    {
        Viewport& viewport = m_viewports[id];
        if (viewport.swapchain)
        {
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
                           {.sunDirection = m_sunDirection,
                            .sunIlluminance = {m_sunIntensity, m_sunIntensity, m_sunIntensity, 1.0F},
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
        const bool picking = m_pickRequest && m_pickRequest->viewport == id && !m_pickInFlight[slot];
        const bool outlining = viewport.outline && m_hasSelection;
        const bool overlays = (picking || outlining) && ensureMask(viewport);
        if (picking && overlays)
        {
            // The cursor in scene pixels: the scene may be drawn at a render scale.
            const auto toScene = [](std::uint32_t pixel, std::uint32_t size, std::uint32_t scene)
            {
                return std::min(scene - 1, static_cast<std::uint32_t>(std::uint64_t{pixel} * scene / size));
            };
            const std::uint32_t x = toScene(m_pickRequest->x, viewport.width, viewport.sceneWidth);
            const std::uint32_t y = toScene(m_pickRequest->y, viewport.height, viewport.sceneHeight);
            frame.pickBuffer = device.bindlessStorageIndex(m_pickBuffer);
            frame.pickPixel = (x & 0xFFFFU) | (y << 16U);
        }
        // The debug tools are for one viewport (the editor's): it gets their pipelines, frozen frustum and clear
        // colour; any other view of the scene is set back to normal before its passes are added.
        const bool debugging = m_debugViewport == id;
        Math::Frustum frustum = Math::frustumFromCamera(matrices);
        Math::Color clear = c_clear;
        if (debugging)
        {
            m_debug->apply(*m_scene, frame);
            frustum = m_debug->cullingFrustum(frustum, frame);
            clear = m_debug->clearColor(c_clear);
        }
        else
        {
            Renderer::RenderDebug::restore(*m_scene);
        }
        m_scene->setView(slot, frame, frustum, viewport.view);
        const Renderer::GraphResource hdr =
            m_scene->addPasses(graph, slot, nullptr, viewport.color, viewport.depth, clear, viewport.view);
        std::optional<Renderer::GraphResource> position; // the Position view's texture, shown instead of the scene
        if (debugging)
        {
            if (m_gridOn)
            {
                if (!m_grid)
                {
                    m_grid = std::make_unique<Renderer::InfiniteGrid>(*m_device, *m_pipelines,
                                                                      shaderId("01a1b5e2-3c1b-8000-80a4-7b19d2e6c05c"),
                                                                      c_sceneFormat, m_deps.log);
                }
                m_grid->addPasses(graph, slot, frame, hdr, viewport.color, m_scene->depthResource(viewport.view),
                                  viewport.depth, viewport.sceneWidth, viewport.sceneHeight);
            }
            m_debug->addOverlays(graph, *m_scene, hdr, viewport.color, viewport.depth, slot, viewport.view);
            if (m_debug->state().view == static_cast<int>(MEOW2_DEBUG_VIEW_POSITION))
            {
                if (!m_positionView)
                {
                    m_positionView = std::make_unique<Renderer::DepthPositionView>(
                        *m_device, *m_pipelines, shaderId("01a1b5e2-3c1d-8000-80a4-7b19d2e6c05e"), m_deps.log);
                }
                position = m_positionView->addPass(graph, slot, frame, m_scene->depthResource(viewport.view),
                                                   viewport.depth, viewport.sceneWidth, viewport.sceneHeight);
            }
        }
        if (overlays)
        {
            const Renderer::GraphResource mask =
                graph.import(viewport.mask, viewport.maskState, viewport.name + " selection mask");
            masks.emplace_back(id, mask);
            if (picking)
            {
                pickResource = graph.import(m_pickBuffer, m_pickBufferState, "Pick");
                const Renderer::GraphResource readback =
                    graph.import(m_pickReadback[slot], Rhi::ResourceState::Undefined, "Pick readback");
                const Rhi::BufferHandle pickBuffer = m_pickBuffer;
                const Rhi::BufferHandle readbackBuffer = m_pickReadback[slot];
                graph.addPass({.name = "Pick clear",
                               .accesses = {{*pickResource, Rhi::ResourceState::CopyTarget}},
                               .record = [pickBuffer](Rhi::IRhiCommandList& list) { list.fillBuffer(pickBuffer, 0); },
                               .small = true,
                               .debug = true});
                // The mask is only the pass's target here (psPick writes no colour); the mask passes clear it after.
                const std::array<Renderer::GraphAccess, 1> writes{
                    Renderer::GraphAccess{*pickResource, Rhi::ResourceState::ShaderWrite}};
                m_scene->addOverlayPass(graph, "Pick", m_pickPipelines, mask, viewport.mask, viewport.depth, slot, true,
                                        viewport.view, writes);
                graph.addPass({.name = "Pick copy",
                               .accesses = {{*pickResource, Rhi::ResourceState::CopySource},
                                            {readback, Rhi::ResourceState::CopyTarget}},
                               .record = [pickBuffer, readbackBuffer](Rhi::IRhiCommandList& list)
                               { list.copyBuffer(pickBuffer, readbackBuffer, c_pickBytes); },
                               .small = true,
                               .debug = true});
                m_pickInFlight[slot] = m_pickRequest;
                m_pickRequest.reset();
            }
            if (outlining)
            {
                const Rhi::TextureHandle maskTexture = viewport.mask;
                graph.addPass({.name = "Selection mask clear",
                               .accesses = {{mask, Rhi::ResourceState::ColorTarget}},
                               .record =
                                   [maskTexture](Rhi::IRhiCommandList& list)
                               {
                                   list.beginRendering(Rhi::RenderTargets{
                                       .color = maskTexture,
                                       .clearColor = Math::Color{.r = 0.0F, .g = 0.0F, .b = 0.0F, .a = 0.0F}});
                                   list.endRendering();
                               },
                               .small = true,
                               .debug = true});
                m_scene->addOverlayPass(graph, "Selection mask", m_maskPipelines, mask, viewport.mask, viewport.depth,
                                        slot, true, viewport.view);
                Shader::PushConstants push{};
                push.instance = device.bindlessIndex(viewport.mask);
                const Rhi::PipelineHandle pipeline = m_pipelines->get(m_outlinePipeline);
                const Rhi::TextureHandle colorTexture = viewport.color;
                graph.addPass(
                    {.name = "Selection outline",
                     .accesses = {{mask, Rhi::ResourceState::ShaderRead}, {hdr, Rhi::ResourceState::ColorTarget}},
                     .record =
                         [pipeline, push, colorTexture](Rhi::IRhiCommandList& list)
                     {
                         list.beginRendering(Rhi::RenderTargets{.color = colorTexture});
                         list.setPipeline(pipeline);
                         list.setPushConstants(push);
                         list.draw(3);
                         list.endRendering();
                     },
                     .small = true,
                     .debug = true});
            }
        }
        // While culling is frozen, the frozen camera's view volume (yellow), so what was culled can be seen from
        // outside.
        std::vector<Renderer::DebugLine> frozenLines;
        if (debugging)
        {
            if (const std::optional<Shader::FrameConstants> frozen = m_debug->frozenCamera())
            {
                constexpr float c_frozenFrustumLength = 200.0F; // metres: the far plane is infinite
                constexpr Math::Color c_frustumColor{.r = 1.0F, .g = 0.85F, .b = 0.1F, .a = 1.0F};
                const std::array<Renderer::DebugLine, 12> edges =
                    Renderer::frustumLines(*frozen, c_frozenFrustumLength, c_frustumColor);
                frozenLines.assign(edges.begin(), edges.end());
            }
        }
        if (!viewport.overlay.lines.empty() || !viewport.overlay.triangles.empty() ||
            !viewport.overlay.screenLines.empty() || !viewport.overlay.screenTriangles.empty() || !frozenLines.empty())
        {
            const auto make = [this]
            {
                return std::make_unique<Renderer::DebugLines>(*m_device, *m_pipelines,
                                                              shaderId("01a1b5e2-3c1a-8000-80a4-7b19d2e6c05b"),
                                                              c_sceneFormat, m_deps.log);
            };
            if (!viewport.debugLines)
            {
                viewport.debugLines = make();
                viewport.screenLines = make();
            }
            // Drawn before the tone map, which multiplies by the exposure: divided here, so the gizmos stay as bright
            // in a dim scene as in a bright one.
            const float unexpose = m_exposure > 0.0F ? 1.0F / m_exposure : 1.0F;
            const auto shown = [unexpose](Math::Color color)
            {
                color.r *= unexpose;
                color.g *= unexpose;
                color.b *= unexpose;
                return color;
            };
            const auto fill = [&shown](Renderer::DebugLines& target, const std::vector<Renderer::DebugLine>& lines,
                                       const std::vector<Renderer::DebugTriangle>& triangles)
            {
                target.clear();
                for (Renderer::DebugLine line : lines)
                {
                    line.color = shown(line.color);
                    target.add(line);
                }
                for (Renderer::DebugTriangle triangle : triangles)
                {
                    triangle.color = shown(triangle.color);
                    target.add(triangle);
                }
            };
            fill(*viewport.debugLines, viewport.overlay.lines, viewport.overlay.triangles);
            for (Renderer::DebugLine line : frozenLines)
            {
                line.color = shown(line.color);
                viewport.debugLines->add(line);
            }
            viewport.debugLines->addPass(graph, slot, matrices.viewProjection, hdr, viewport.color);
            // Screen-space shapes (the view-axis gizmo) in the viewport's pixels: x right, y down, to clip space.
            fill(*viewport.screenLines, viewport.overlay.screenLines, viewport.overlay.screenTriangles);
            Math::Mat4 pixels;
            DirectX::XMStoreFloat4x4(&pixels,
                                     DirectX::XMMatrixSet(2.0F / static_cast<float>(viewport.width), 0.0F, 0.0F, 0.0F,
                                                          0.0F, -2.0F / static_cast<float>(viewport.height), 0.0F, 0.0F,
                                                          0.0F, 0.0F, 1.0F, 0.0F, -1.0F, 1.0F, 0.0F, 1.0F));
            viewport.screenLines->addPass(graph, slot, pixels, hdr, viewport.color);
        }
        // A debug view is shown as it is, without the tone curve (the Position view from its own texture).
        const Renderer::GraphResource shown = position.value_or(hdr);
        const Rhi::TextureHandle shownTexture = position ? m_positionView->texture() : viewport.color;
        if (viewport.swapchain)
        {
            const bool presented =
                debugging && m_debug->addPresent(graph, shown, shownTexture, *viewport.swapchain, device);
            Renderer::ToneMapPass* toneMap = presented ? nullptr : toneMapFor(viewport.swapchain->format());
            if (toneMap != nullptr)
            {
                toneMap->addPass(graph, hdr, viewport.color, *viewport.swapchain, device);
            }
        }
        else
        {
            const Renderer::GraphResource target =
                graph.import(viewport.offscreen->texture(), Rhi::ResourceState::Undefined, viewport.name);
            const bool presented = debugging && m_debug->addPresent(graph, shown, shownTexture, target,
                                                                    viewport.offscreen->texture(), device);
            Renderer::ToneMapPass* toneMap = presented ? nullptr : toneMapFor(Renderer::OffscreenTarget::format());
            if (toneMap != nullptr)
            {
                toneMap->addPass(graph, hdr, viewport.color, target, viewport.offscreen->texture(), device);
            }
        }
        viewport.drawn = true;
    }

    graph.compile();
    if (pickResource)
    {
        m_pickBufferState = graph.finalState(*pickResource);
    }
    for (const auto& [id, mask] : masks)
    {
        m_viewports[id].maskState = graph.finalState(mask);
    }
    Result<std::vector<Rhi::IRhiCommandList*>> lists = graph.execute(device, m_deps.jobs);
    m_scene->endFrame(graph);
    if (m_positionView)
    {
        m_positionView->endFrame(graph);
    }
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
