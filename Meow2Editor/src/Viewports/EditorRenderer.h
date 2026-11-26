#pragma once

/// @file
/// @brief EditorRenderer: the GPU device, the shared GPU scene, and one view per editor viewport.

#include <Meow2/Assets/AssetManager.h>
#include <Meow2/Core/Events/EventBus.h>
#include <Meow2/Core/Jobs/JobSystem.h>
#include <Meow2/Core/Log/Logger.h>
#include <Meow2/Core/Result.h>
#include <Meow2/Graphics/Renderer/DebugLines.h>
#include <Meow2/Graphics/Renderer/DepthPositionView.h>
#include <Meow2/Graphics/Renderer/FrameCapture.h>
#include <Meow2/Graphics/Renderer/FrameResources.h>
#include <Meow2/Graphics/Renderer/GpuScene.h>
#include <Meow2/Graphics/Renderer/InfiniteGrid.h>
#include <Meow2/Graphics/Renderer/PipelineLibrary.h>
#include <Meow2/Graphics/Renderer/RenderDebug.h>
#include <Meow2/Graphics/Renderer/RenderDevice.h>
#include <Meow2/Graphics/Renderer/SkyAtmosphere.h>
#include <Meow2/Graphics/Renderer/ToneMapPass.h>
#include <Meow2/Math/Camera.h>
#include <Meow2/Math/Transform.h>
#include <Meow2/Platform/Window/Window.h>
#include <Meow2/Scene/Environment.h>

#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace Meow2::Editor
{
/// @brief Identifies a viewport of an EditorRenderer, in the order they were added (0, 1, ...).
using ViewportId = std::uint32_t;

/// @brief The GPU and its scene, for the Stats dock. Plain data.
struct RenderStats
{
    std::string backend;                ///< "Vulkan" or "D3D12".
    std::string gpu;                    ///< The adapter's name.
    Rhi::GpuMemoryStats gpuMemory{};    ///< GPU memory: used, budget, and live resources per MemTag.
    std::uint64_t validationErrors = 0; ///< From the API's validation layer.
    std::uint32_t instances = 0;        ///< Instances in the GPU scene (before culling).
    std::uint32_t meshes = 0;           ///< GPU meshes.
    std::uint32_t materials = 0;        ///< Materials.
    std::uint32_t textures = 0;         ///< Textures.
    bool meshlets = false;              ///< Drawn through the meshlet path (mesh shaders), else the indirect one.
};

/// @brief What an EditorRenderer uses. Plain data (references); everything must outlive it.
struct EditorRendererDeps
{
    Assets::AssetManager& assets; ///< Loads the cooked shaders (mount `cooked`).
    Events::EventBus& events;     ///< Shader reloads rebuild the pipelines.
    Jobs::JobSystem& jobs;        ///< Records the render graph's passes.
    Log::Logger& log;             ///< The RHI's messages and the renderer's.
};

/// @brief How an EditorRenderer starts. Plain data.
struct EditorRendererDesc
{
    Rhi::RhiBackend backend = Rhi::RhiBackend::Vulkan; ///< The graphics API (`rhi.backend`).
    bool validation = false;                           ///< The API's validation layer (`rhi.validation`).
    bool presentation = true;                          ///< False for tests: offscreen viewports only, no swapchains.
    std::uint32_t instanceCapacity = 65536;            ///< The GPU scene's most instances.
    bool readBackOffscreen = false;                    ///< Copy offscreen viewports to the CPU each frame (tests).
};

/// @brief Where one viewport is seen from this frame. Plain data.
struct ViewportCamera
{
    Math::Transform transform{}; ///< The camera's place and direction (+Z forward, +Y up).
    Math::CameraLens lens{};     ///< Its projection.
};

/// @brief One surface under the cursor at a pick. Plain data.
struct PickHit
{
    std::uint32_t instance = 0; ///< The GpuScene instance slot.
    float depth = 0.0F;         ///< Its nearest depth at `distance` (reversed Z: larger is nearer).
    std::uint32_t distance = 0; ///< Squared distance in pixels from the picked pixel to its closest one (0: covers it).
};

/// @brief What a pick found. Plain data.
struct PickResult
{
    ViewportId viewport = 0;     ///< Where it was asked for.
    std::uint32_t x = 0;         ///< The pixel asked for, in the viewport's pixels.
    std::uint32_t y = 0;         ///< The pixel asked for.
    std::vector<PickHit> hits{}; ///< Each instance under the cursor once, closest to the cursor, then nearest, first;
                                 ///< empty: nothing there.
};

/// @brief Shapes drawn over a viewport: the gizmos. Plain data.
struct ViewportOverlay
{
    std::vector<Renderer::DebugLine> lines{};               ///< In world space.
    std::vector<Renderer::DebugTriangle> triangles{};       ///< In world space (the transform gizmo).
    std::vector<Renderer::DebugLine> screenLines{};         ///< In viewport pixels: x right, y down.
    std::vector<Renderer::DebugTriangle> screenTriangles{}; ///< In viewport pixels (the view-axis gizmo).
};

/// @brief The editor's renderer: one device, one GpuScene shared by every viewport, and a view per viewport.
///
/// Each viewport has a target (a swapchain on a Qt widget's native window, or an offscreen texture in tests), a
/// GpuScene view (its own culling, G-buffer and shadow maps, `GpuScene::addView()`), and an HDR colour and depth
/// texture at its **render scale**: the scene can be drawn smaller than the viewport, and the tone-map pass scales it
/// up, which lowers the GPU's load (the game viewport's 100/75/50/25 % drop-down). A viewport that's hidden, empty
/// or has no camera this frame isn't drawn at all: no culling, no passes, no swapchain image taken.
///
/// renderFrame() runs one frame for every visible viewport: the sky's passes once, then each viewport's scene
/// passes and tone map, in one render graph.
///
/// **Picking:** requestPick() adds, to the viewport's next frame, a pass that draws its culled instances with
/// GpuDriven.hlsl's psPick, which appends `{instance, depth}` for every fragment within MEOW2_PICK_RADIUS pixels of the
/// cursor into a buffer; the buffer is copied to a readback buffer of that frame slot and read when the slot comes
/// round again (the GPU is done with it then), so takePickResult() has it a frame or two later.
/// **Outline:** in a viewport with setOutline(), instances marked InstanceDesc::selected are drawn into a mask
/// (psSelectionMask, no depth test), and SelectionOutline.hlsl draws an orange edge around the mask over the HDR
/// colour, before the tone map.
/// @par Thread safety
/// Not thread-safe; the GUI thread. The graph's passes record on jobs.
/// @par Lifetime
/// Everything in EditorRendererDeps must outlive it; native windows must outlive their viewports.
class EditorRenderer
{
public:
    /// @brief Creates the device, the pipelines, the sky, the tone map and the GPU scene.
    /// @param deps What it uses.
    /// @param desc How it starts.
    /// @return The renderer, or the first error (no GPU, no cooked shaders).
    [[nodiscard]] static Result<std::unique_ptr<EditorRenderer>> create(const EditorRendererDeps& deps,
                                                                        const EditorRendererDesc& desc);

    /// @brief Use create().
    /// @param deps What it uses.
    /// @param desc How it starts.
    EditorRenderer(const EditorRendererDeps& deps, const EditorRendererDesc& desc);

    /// @brief Waits for the GPU, then destroys every viewport before the device.
    ~EditorRenderer();

    EditorRenderer(const EditorRenderer&) = delete;            ///< Not copyable: owns the device.
    EditorRenderer& operator=(const EditorRenderer&) = delete; ///< Not copyable: owns the device.

    /// @brief Adds a viewport drawn into a native window (a swapchain on it).
    /// @param window The window; NativeWindowType::None isn't accepted.
    /// @param width  Its size in pixels; 0 while it has none.
    /// @param height Its size in pixels.
    /// @param name   For debugging tools, e.g. "Game".
    /// @return Its id, or the swapchain's error.
    [[nodiscard]] Result<ViewportId> addWindowViewport(const Platform::NativeWindowHandle& window, std::uint32_t width,
                                                       std::uint32_t height, std::string name);

    /// @brief Adds a viewport drawn into an offscreen RGBA8 texture (tests, headless).
    /// @param width  Its size in pixels; > 0.
    /// @param height Its size in pixels; > 0.
    /// @param name   For debugging tools.
    /// @return Its id, or the error from creating the texture or the view.
    [[nodiscard]] Result<ViewportId> addOffscreenViewport(std::uint32_t width, std::uint32_t height, std::string name);

    /// @brief Changes a viewport's size (its widget was resized). The swapchain follows at the next frame.
    /// @param viewport The viewport.
    /// @param width    New size in pixels; 0 draws nothing.
    /// @param height   New size in pixels.
    void resize(ViewportId viewport, std::uint32_t width, std::uint32_t height);

    /// @brief Shows or hides a viewport: a hidden one isn't drawn.
    /// @param viewport The viewport.
    /// @param visible  Whether its widget can be seen.
    void setVisible(ViewportId viewport, bool visible);

    /// @brief Sets the fraction of the viewport's size the scene is drawn at; the tone map scales it up.
    /// @param viewport The viewport.
    /// @param scale    0.25 to 1 (clamped).
    void setRenderScale(ViewportId viewport, float scale);

    /// @brief A viewport's render scale.
    /// @param viewport The viewport.
    /// @return 0.25 to 1.
    [[nodiscard]] float renderScale(ViewportId viewport) const;

    /// @brief Draws one frame of every visible viewport that has a camera.
    /// @param cameras One entry per viewport id: its camera, or nothing to skip it this frame.
    /// @return Nothing, or an error if the device was lost.
    Result<void> renderFrame(std::span<const std::optional<ViewportCamera>> cameras);

    /// @brief Asks for what's under a pixel of a viewport, found by its next drawn frame (see the class comment).
    ///        A new request replaces one that hasn't been drawn yet.
    /// @param viewport The viewport.
    /// @param x        The pixel, in the viewport's pixels (not the render scale's).
    /// @param y        The pixel.
    void requestPick(ViewportId viewport, std::uint32_t x, std::uint32_t y);

    /// @brief The result of the last pick, once its frame has come back from the GPU; it's handed out once.
    /// @return The result, or nothing yet.
    [[nodiscard]] std::optional<PickResult> takePickResult();

    /// @brief Whether a pick is waiting to be drawn or to come back.
    /// @return True while one is.
    [[nodiscard]] bool pickPending() const;

    /// @brief Draws the selection outline in a viewport (the editor viewport; not the game view).
    /// @param viewport The viewport.
    /// @param outline  Whether to draw it; with nothing selected the passes are skipped anyway.
    void setOutline(ViewportId viewport, bool outline);

    /// @brief Shapes to draw over a viewport's scene each frame until changed (the gizmos), on top of everything,
    ///        before the tone map. Their colours are divided by the exposure, so they keep their brightness.
    /// @param viewport The viewport.
    /// @param overlay  The shapes; empty draws none.
    void setOverlay(ViewportId viewport, ViewportOverlay overlay);

    /// @brief Tells the renderer whether any instance is marked selected, so the outline passes can be skipped.
    /// @param any True if some InstanceDesc has `selected`.
    void setHasSelection(bool any);

    /// @brief Which viewport the debug tools (debug views, wireframes, frozen culling, the grid) apply to; the others
    ///        draw as usual. The editor viewport; none until set.
    /// @param viewport The viewport, or nothing for none.
    void setDebugViewport(std::optional<ViewportId> viewport);

    /// @brief The debug tools' settings, to change; they apply from the next renderFrame().
    /// @return Them.
    [[nodiscard]] Renderer::RenderDebugState& debugState();

    /// @brief Turns the ground grid on or off in the debug viewport.
    /// @param on True to draw it.
    void setGrid(bool on);

    /// @brief Whether the ground grid is on.
    /// @return True if it's drawn.
    [[nodiscard]] bool grid() const
    {
        return m_gridOn;
    }

    /// @brief Whether a debug-view pipeline couldn't be built (logged); the views stay off until the next scene.
    /// @return True after a failure.
    [[nodiscard]] bool debugFailed() const;

    /// @brief The frame capture: request() captures the next frame drawn (every viewport's passes).
    /// @return It.
    [[nodiscard]] Renderer::FrameCapture& frameCapture()
    {
        return *m_capture;
    }

    /// @brief What the Stats dock shows about the GPU and the scene on it.
    /// @return The figures, as of the last frame.
    [[nodiscard]] RenderStats stats() const;

    /// @brief Whether a viewport was drawn by the last renderFrame().
    /// @param viewport The viewport.
    /// @return True if its passes ran.
    [[nodiscard]] bool wasDrawn(ViewportId viewport) const;

    /// @brief The size the scene was last drawn at in a viewport: its size times the render scale.
    /// @param viewport The viewport.
    /// @return Width and height in pixels; 0 × 0 before the first frame.
    [[nodiscard]] std::pair<std::uint32_t, std::uint32_t> sceneSize(ViewportId viewport) const;

    /// @brief An offscreen viewport's target, for tests (its pixels after a frame with `readBackOffscreen`).
    /// @param viewport The viewport.
    /// @return The target, or null for a window viewport.
    [[nodiscard]] Renderer::OffscreenTarget* offscreenTarget(ViewportId viewport);

    /// @brief The GPU scene every viewport draws.
    /// @return It.
    /// @brief Replaces the GPU scene with an empty one (its meshes, materials, textures and instances gone), keeping
    ///        every viewport and its view: for opening another scene.
    /// @return Nothing, or the error from creating the new scene (the old one is kept then).
    [[nodiscard]] Result<void> resetScene();

    /// @brief The scene's light from its Environment: the sun for shading, shadows and the sky, and the exposure.
    /// @param environment The scene's Environment.
    void setEnvironment(const Scene::Environment& environment);

    [[nodiscard]] Renderer::GpuScene& scene()
    {
        return *m_scene;
    }

    /// @brief The device's owner.
    /// @return It.
    [[nodiscard]] Renderer::RenderDevice& device()
    {
        return *m_device;
    }

private:
    /// @brief One viewport: its target, its GpuScene view, and its scene textures.
    struct Viewport
    {
        std::string name;                                     ///< For debugging tools.
        Platform::NativeWindowHandle window{};                ///< A window viewport's window; None for offscreen.
        std::unique_ptr<Rhi::IRhiSwapchain> swapchain;        ///< A window viewport's swapchain.
        std::unique_ptr<Renderer::OffscreenTarget> offscreen; ///< An offscreen viewport's target.
        std::uint32_t width = 0;                              ///< The output's size in pixels.
        std::uint32_t height = 0;
        float renderScale = 1.0F;                     ///< setRenderScale().
        bool visible = true;                          ///< setVisible().
        bool drawn = false;                           ///< Drawn by the last frame.
        Renderer::ViewId view = Renderer::c_mainView; ///< Its GpuScene view.
        Rhi::TextureHandle color;                     ///< HDR scene colour at the render scale.
        Rhi::TextureHandle depth;                     ///< Depth at the render scale.
        std::uint32_t sceneWidth = 0;                 ///< The scene textures' size.
        std::uint32_t sceneHeight = 0;
        bool outline = false;                                         ///< setOutline().
        Rhi::TextureHandle mask;                                      ///< Selection mask (and pick target), scene size.
        Rhi::ResourceState maskState = Rhi::ResourceState::Undefined; ///< The mask's state after the last frame.
        std::uint32_t maskWidth = 0;                                  ///< The mask's size.
        std::uint32_t maskHeight = 0;
        ViewportOverlay overlay;                           ///< setOverlay().
        std::unique_ptr<Renderer::DebugLines> debugLines;  ///< Draws its world shapes; made on first use.
        std::unique_ptr<Renderer::DebugLines> screenLines; ///< Draws its screen shapes; made with debugLines.
    };

    /// @brief A pick asked for. Plain data.
    struct PickRequest
    {
        ViewportId viewport = 0; ///< Where.
        std::uint32_t x = 0;     ///< The viewport's pixel.
        std::uint32_t y = 0;
    };

    /// @brief Makes the pick and selection-mask draw pipelines in the current GPU scene.
    /// @return Nothing, or the error.
    [[nodiscard]] Result<void> makeOverlayPipelines();

    /// @brief Makes a viewport's mask at its scene size, if it isn't already.
    /// @return False if it couldn't be made.
    bool ensureMask(Viewport& viewport);

    /// @brief Reads a pick's records from this frame slot's readback buffer into m_pickResult.
    /// @param slot The frame slot, whose GPU work has finished.
    void readPick(std::uint32_t slot);

    /// @brief Makes a viewport's scene textures at its size times its render scale, if they aren't already.
    /// @return False if a texture couldn't be made (the viewport is skipped this frame).
    bool ensureSceneTextures(Viewport& viewport);

    /// @brief A new, empty GPU scene with the editor's settings (deferred, shadows, the sky, the current light).
    /// @return It, or the error.
    [[nodiscard]] Result<std::unique_ptr<Renderer::GpuScene>> makeScene() const;

    /// @brief The tone-map pass for a target format, made on first use (a swapchain's format and RGBA8 differ).
    /// @return The pass, or null if its pipeline couldn't be built.
    Renderer::ToneMapPass* toneMapFor(Rhi::RhiFormat format);

    EditorRendererDeps m_deps;
    EditorRendererDesc m_desc;
    std::unique_ptr<Renderer::RenderDevice> m_device;
    std::unique_ptr<Renderer::PipelineLibrary> m_pipelines;
    std::unique_ptr<Renderer::SkyAtmosphere> m_sky;
    std::unique_ptr<Renderer::GpuScene> m_scene;
    std::map<Rhi::RhiFormat, std::unique_ptr<Renderer::ToneMapPass>> m_toneMaps; ///< By target format.
    AssetId m_toneMapShader{};
    Math::Vec3 m_sunDirection{0.4F, 0.8F, -0.6F}; ///< Towards the sun (setEnvironment()).
    float m_sunIntensity = 3.0F;                  ///< The sun's illuminance.
    float m_exposure = 1.0F;                      ///< Before the tone curve.
    std::vector<Viewport> m_viewports;            ///< By ViewportId.

    Renderer::DrawPipelines m_pickPipelines{}; ///< psPick, in the current GPU scene.
    Renderer::DrawPipelines m_maskPipelines{}; ///< psSelectionMask, in the current GPU scene.
    Renderer::PipelineId m_outlinePipeline{};  ///< SelectionOutline.hlsl.
    Rhi::BufferHandle m_pickBuffer;            ///< The records psPick appends to (GPU only).
    Rhi::ResourceState m_pickBufferState = Rhi::ResourceState::Undefined;              ///< After the last frame.
    std::array<Rhi::BufferHandle, Rhi::c_maxFramesInFlight> m_pickReadback{};          ///< A copy per frame slot.
    std::array<std::optional<PickRequest>, Rhi::c_maxFramesInFlight> m_pickInFlight{}; ///< What each slot picked.
    std::optional<PickRequest> m_pickRequest;                                          ///< Asked for, not drawn yet.
    std::optional<PickResult> m_pickResult;                                            ///< Come back, not taken yet.
    bool m_hasSelection = false;                                                       ///< setHasSelection().

    /// @brief Debug views, wireframes and frozen culling; remade with the scene (its pipelines are the scene's).
    std::unique_ptr<Renderer::RenderDebug> m_debug;
    std::unique_ptr<Renderer::InfiniteGrid> m_grid;              ///< The ground grid; made on first use.
    std::unique_ptr<Renderer::DepthPositionView> m_positionView; ///< The Position view's pass; made on first use.
    std::optional<ViewportId> m_debugViewport;                   ///< setDebugViewport().
    bool m_gridOn = false;                                       ///< setGrid().
    std::unique_ptr<Renderer::FrameCapture> m_capture;           ///< The frame capture (F9).
};
} // namespace Meow2::Editor
