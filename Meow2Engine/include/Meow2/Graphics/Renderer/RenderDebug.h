/// @file
/// @brief Rendering debug views: what the scene shows instead of its shading (normals, overdraw, …), wireframes,
///        frozen culling, and the present pass that shows them without the tone curve. Debugging tools of
///        Meow2Renderer and the editor's viewport, never in games.

#pragma once

#include <Meow2/Core/Id/Uuid.h>
#include <Meow2/Core/Log/Logger.h>
#include <Meow2/Graphics/Renderer/GpuScene.h>
#include <Meow2/Graphics/Renderer/PipelineLibrary.h>
#include <Meow2/Graphics/Renderer/RenderGraph.h>
#include <Meow2/Math/Bounds.h>

#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace Meow2::Renderer
{
/// @brief How edges are drawn.
enum class WireframeMode : std::uint8_t
{
    Off,     ///< No wireframe.
    Overlay, ///< Edges over the shaded (or debug-view) scene, hidden where something is in front.
    Only     ///< Edges on black, hidden lines removed.
};

/// @brief A wireframe mode's name in commands and config files: "off", "overlay" or "only".
/// @param mode The mode.
/// @return Its name.
[[nodiscard]] std::string_view wireframeModeName(WireframeMode mode);

/// @brief The mode a name stands for (wireframeModeName()).
/// @param name "off", "overlay" or "only".
/// @return The mode, or nothing for another name.
[[nodiscard]] std::optional<WireframeMode> wireframeModeFromName(std::string_view name);

/// @brief One entry of the view menu. Plain data.
struct DebugViewEntry
{
    int view = -1;            ///< MEOW2_DEBUG_VIEW_*, or -1 for the shaded scene.
    std::string_view key{};   ///< Its name in commands and config files ("shaded", "baseColor", …); never changes.
    std::string_view label{}; ///< Shown on its button.
    std::string_view group{}; ///< Heading it's listed under.
    /// @brief Shown from a buffer a debug-only pass makes for it (the frame itself never has one): its button gets a
    ///        dotted outline. The Position view: deferred lighting rebuilds positions without storing them.
    bool generated = false;
    std::string_view hint{}; ///< How to read it, shown under the buttons while it's on; empty if it needs none.
};

/// @brief The view menu, in order: the shaded scene, then the debug views by group.
/// @return Every entry.
[[nodiscard]] std::span<const DebugViewEntry> debugViewEntries();

/// @brief The entry for a view.
/// @param view MEOW2_DEBUG_VIEW_*, or -1 for the shaded scene.
/// @return It, or null for a number no entry has.
[[nodiscard]] const DebugViewEntry* findDebugView(int view);

/// @brief The entry with a key (DebugViewEntry::key).
/// @param key The key.
/// @return It, or null for an unknown key.
[[nodiscard]] const DebugViewEntry* findDebugView(std::string_view key);

/// @brief What the debug tools are set to. Plain data.
struct RenderDebugState
{
    int view = -1;                                ///< MEOW2_DEBUG_VIEW_*, or -1 for the shaded scene.
    WireframeMode wireframe = WireframeMode::Off; ///< Edges.
    bool freezeCulling = false;                   ///< Keep culling with the frustum from when this was turned on.
    bool showFrustum = true;                      ///< While frozen, draw the frozen camera's frustum.
};

/// @brief The debug views' pipelines and the state that picks them, applied to a GpuScene each frame.
///
/// Pipelines are built only when a view is first used, per GpuScene (each open scene has its own), and the present
/// pipeline once per target format; the PipelineLibrary keeps them across backend switches and shader reloads.
///
/// A scene drawn by several views in one frame (the editor's two viewports): apply() before the view the tools are
/// for, restore() before the others, so they draw as usual.
/// @par Thread safety
/// Not thread-safe; use on the main thread.
/// @par Lifetime
/// The PipelineLibrary and the Logger must outlive it.
class RenderDebug
{
public:
    /// @brief Debug views off.
    /// @param pipelines     Builds the pipelines.
    /// @param toneMapShader The AssetId of `shaders/ToneMap.hlsl`, for its debug present variant.
    /// @param log           Receives pipelines that couldn't be built.
    RenderDebug(PipelineLibrary& pipelines, AssetId toneMapShader, Log::Logger& log);

    /// @brief The current choices, to change.
    /// @return Them.
    [[nodiscard]] RenderDebugState& state()
    {
        return m_state;
    }

    /// @brief The current choices.
    /// @return Them.
    [[nodiscard]] const RenderDebugState& state() const
    {
        return m_state;
    }

    /// @brief Whether anything differs from the plain shaded scene (a view or a wireframe-only image).
    /// @return True if the frame needs the debug present pass.
    [[nodiscard]] bool active() const;

    /// @brief Whether a debug pipeline couldn't be built (logged once); the views stay off until another scene.
    /// @return True after a failure.
    [[nodiscard]] bool failed() const
    {
        return m_scene.failed;
    }

    /// @brief Sets up a scene for this frame: its draw pipelines, sky pass and the frame's debug view.
    /// @param scene The scene about to be drawn.
    /// @param frame Its frame constants, before GpuScene::setView(); `debugView` is set.
    void apply(GpuScene& scene, Shader::FrameConstants& frame);

    /// @brief Undoes apply() for another view of the same scene this frame: no override, the sky drawn.
    /// @param scene The scene.
    static void restore(GpuScene& scene);

    /// @brief The frustum to cull with: the camera's, or the one kept when culling was frozen.
    /// @param current The camera's frustum this frame.
    /// @param frame   The camera's frame constants, kept with the frustum when culling freezes (frozenCamera()).
    /// @return The frustum to pass to GpuScene::setView().
    [[nodiscard]] Math::Frustum cullingFrustum(const Math::Frustum& current, const Shader::FrameConstants& frame);

    /// @brief The camera culling was frozen with, while it is and its frustum is to be drawn.
    /// @return Its frame constants, or nothing (not frozen, or showFrustum off).
    [[nodiscard]] std::optional<Shader::FrameConstants> frozenCamera() const;

    /// @brief The colour to clear the scene to: the scene's, or black for debug views.
    /// @param normal The scene's clear colour.
    /// @return The colour.
    [[nodiscard]] Math::Color clearColor(const Math::Color& normal) const;

    /// @brief Adds the wireframe pass, if one is on.
    /// @param graph     The frame's graph, after GpuScene::addPasses().
    /// @param scene     The scene.
    /// @param color     Its colour target in the graph.
    /// @param texture   The same texture.
    /// @param depth     Its depth target.
    /// @param frameSlot IRhiDevice::frameSlot().
    /// @param view      Which of the scene's views.
    void addOverlays(RenderGraph& graph, GpuScene& scene, GraphResource color, Rhi::TextureHandle texture,
                     Rhi::TextureHandle depth, std::uint32_t frameSlot, ViewId view = c_mainView);

    /// @brief Adds the debug present pass (instead of the tone curve) to a swapchain, when active().
    /// @param graph     The frame's graph.
    /// @param hdr       The scene's colour in the graph (or the Position view's texture).
    /// @param texture   The same texture.
    /// @param swapchain The acquired swapchain.
    /// @param device    For heap indices.
    /// @return True if it added the pass; false means the caller tone-maps as usual.
    bool addPresent(RenderGraph& graph, GraphResource hdr, Rhi::TextureHandle texture, Rhi::IRhiSwapchain& swapchain,
                    Rhi::IRhiDevice& device);

    /// @brief Adds the debug present pass to a texture (headless rendering, tests), when active().
    /// @param graph         The frame's graph.
    /// @param hdr           The scene's colour in the graph (or the Position view's texture).
    /// @param texture       The same texture.
    /// @param target        The texture to write, in the graph.
    /// @param targetTexture The same texture; when bigger than `texture`, the image is scaled up.
    /// @param device        For heap indices and the target's format.
    /// @return True if it added the pass; false means the caller tone-maps as usual.
    bool addPresent(RenderGraph& graph, GraphResource hdr, Rhi::TextureHandle texture, GraphResource target,
                    Rhi::TextureHandle targetTexture, Rhi::IRhiDevice& device);

private:
    /// @brief The scene's debug pipelines, built on first use.
    struct ScenePipelines
    {
        const GpuScene* scene = nullptr;       ///< Whose they are.
        std::optional<DrawPipelines> view;     ///< DEBUG_VIEW = 1, solid.
        std::optional<DrawPipelines> overdraw; ///< DEBUG_VIEW = 1, additive, no depth test.
        std::optional<DrawPipelines> wire;     ///< DEBUG_VIEW = 2, line fill.
        bool failed = false;                   ///< A build failed; don't retry every frame.
    };

    /// @brief Builds what the current state needs for a scene.
    /// @return False if a pipeline couldn't be built (the views stay off for it).
    bool prepare(GpuScene& scene);

    /// @brief The present pipeline for a target format, built on first use.
    /// @return It, or nothing if it couldn't be built (logged; the caller tone-maps instead).
    std::optional<PipelineId> presentFor(Rhi::RhiFormat format);

    /// @brief The present pass's push constants: the texture, what to show, and scaling to a bigger target.
    [[nodiscard]] Shader::PushConstants presentPush(Rhi::IRhiDevice& device, Rhi::TextureHandle texture,
                                                    std::uint32_t targetWidth, std::uint32_t targetHeight) const;

    PipelineLibrary& m_pipelines;
    Log::Logger& m_log;
    AssetId m_toneMapShader;
    std::vector<std::pair<Rhi::RhiFormat, PipelineId>> m_present; ///< ToneMap.hlsl's DEBUG_OUTPUT = 1, per format.
    bool m_presentFailed = false;                                 ///< A present pipeline failed; logged once.
    ScenePipelines m_scene;
    RenderDebugState m_state;
    std::optional<Math::Frustum> m_frozen;                ///< Kept while culling is frozen.
    std::optional<Shader::FrameConstants> m_frozenCamera; ///< The camera it was frozen with.
};
} // namespace Meow2::Renderer
