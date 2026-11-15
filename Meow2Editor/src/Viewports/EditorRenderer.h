#pragma once

/// @file
/// @brief EditorRenderer: the GPU device, the shared GPU scene, and one view per editor viewport.

#include <Meow2/Assets/AssetManager.h>
#include <Meow2/Core/Events/EventBus.h>
#include <Meow2/Core/Jobs/JobSystem.h>
#include <Meow2/Core/Log/Logger.h>
#include <Meow2/Core/Result.h>
#include <Meow2/Graphics/Renderer/FrameResources.h>
#include <Meow2/Graphics/Renderer/GpuScene.h>
#include <Meow2/Graphics/Renderer/PipelineLibrary.h>
#include <Meow2/Graphics/Renderer/RenderDevice.h>
#include <Meow2/Graphics/Renderer/SkyAtmosphere.h>
#include <Meow2/Graphics/Renderer/ToneMapPass.h>
#include <Meow2/Math/Camera.h>
#include <Meow2/Math/Transform.h>
#include <Meow2/Platform/Window/Window.h>

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
    };

    /// @brief Makes a viewport's scene textures at its size times its render scale, if they aren't already.
    /// @return False if a texture couldn't be made (the viewport is skipped this frame).
    bool ensureSceneTextures(Viewport& viewport);

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
    std::vector<Viewport> m_viewports; ///< By ViewportId.
};
} // namespace Meow2::Editor
