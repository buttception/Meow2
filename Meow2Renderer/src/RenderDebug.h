/// @file
/// @brief Meow2Renderer's debug views: what the scene shows instead of its shading (normals, overdraw, …), wireframes,
///        frozen culling, and the present pass that shows them without the tone curve. Renderer only, never in games.

#pragma once

#include "UiTheme.h"

#include <Meow2/Core/Id/Uuid.h>
#include <Meow2/Core/Log/Logger.h>
#include <Meow2/Graphics/Renderer/GpuScene.h>
#include <Meow2/Graphics/Renderer/PipelineLibrary.h>
#include <Meow2/Graphics/Renderer/RenderGraph.h>
#include <Meow2/Math/Bounds.h>

#include <array>
#include <optional>
#include <span>
#include <string_view>

namespace Meow2::RendererApp
{
/// @brief How edges are drawn.
enum class WireframeMode : std::uint8_t
{
    Off,     ///< No wireframe.
    Overlay, ///< Edges over the shaded (or debug-view) scene, hidden where something is in front.
    Only     ///< Edges on black, hidden lines removed.
};

/// @brief One entry of the view menu. Plain data.
struct DebugViewEntry
{
    int view = -1;            ///< MEOW2_DEBUG_VIEW_*, or -1 for the shaded scene.
    std::string_view label{}; ///< Shown in the menu.
    std::string_view group{}; ///< Heading it's listed under.
};

/// @brief The view menu, in order: the shaded scene, then the debug views by group.
[[nodiscard]] std::span<const DebugViewEntry> debugViewEntries();

/// @brief What the Debug window chose. Plain data.
struct RenderDebugState
{
    int view = -1;                                ///< MEOW2_DEBUG_VIEW_*, or -1 for the shaded scene.
    WireframeMode wireframe = WireframeMode::Off; ///< Edges.
    bool freezeCulling = false;                   ///< Keep culling with the frustum from when this was turned on.
    bool showFrustum = true;                      ///< While frozen, draw the frozen camera's frustum.
};

/// @brief The debug views' pipelines and the state that picks them, applied to a GpuScene each frame.
///
/// Pipelines are built only when a view is first used, per GpuScene (each open scene has its own); the PipelineLibrary
/// keeps them across backend switches and shader reloads.
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
    /// @param presentFormat The swapchain's format.
    /// @param log           Receives pipelines that couldn't be built.
    RenderDebug(Renderer::PipelineLibrary& pipelines, AssetId toneMapShader, Rhi::RhiFormat presentFormat,
                Log::Logger& log);

    /// @brief The current choices, to change (the Debug window does).
    /// @return Them.
    [[nodiscard]] RenderDebugState& state()
    {
        return m_state;
    }

    /// @brief Whether anything differs from the plain shaded scene (a view or a wireframe-only image).
    /// @return True if the frame needs the debug present pass.
    [[nodiscard]] bool active() const;

    /// @brief Sets up a scene for this frame: its draw pipelines, sky pass and the frame's debug view.
    /// @param scene The scene about to be drawn.
    /// @param frame Its frame constants, before GpuScene::setView(); `debugView` is set.
    void apply(Renderer::GpuScene& scene, Shader::FrameConstants& frame);

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
    void addOverlays(Renderer::RenderGraph& graph, Renderer::GpuScene& scene, Renderer::GraphResource color,
                     Rhi::TextureHandle texture, Rhi::TextureHandle depth, std::uint32_t frameSlot);

    /// @brief Adds the debug present pass (instead of the tone curve) when active().
    /// @param graph     The frame's graph.
    /// @param hdr       The scene's colour in the graph.
    /// @param texture   The same texture.
    /// @param swapchain The acquired swapchain.
    /// @param device    For heap indices.
    /// @return True if it added the pass; false means the caller tone-maps as usual.
    bool addPresent(Renderer::RenderGraph& graph, Renderer::GraphResource hdr, Rhi::TextureHandle texture,
                    Rhi::IRhiSwapchain& swapchain, Rhi::IRhiDevice& device);

    /// @brief Draws the Debug window.
    /// @param fonts The theme's fonts.
    void drawWindow(const UiFonts& fonts = {});

private:
    /// @brief The scene's debug pipelines, built on first use.
    struct ScenePipelines
    {
        const Renderer::GpuScene* scene = nullptr;       ///< Whose they are.
        std::optional<Renderer::DrawPipelines> view;     ///< DEBUG_VIEW = 1, solid.
        std::optional<Renderer::DrawPipelines> overdraw; ///< DEBUG_VIEW = 1, additive, no depth test.
        std::optional<Renderer::DrawPipelines> wire;     ///< DEBUG_VIEW = 2, line fill.
        bool failed = false;                             ///< A build failed; don't retry every frame.
    };

    /// @brief The view buttons: one per view, grouped, the active one highlighted.
    void drawViewButtons();

    /// @brief Builds what the current state needs for a scene.
    /// @return False if a pipeline couldn't be built (the views stay off for it).
    bool prepare(Renderer::GpuScene& scene);

    Renderer::PipelineLibrary& m_pipelines;
    Log::Logger& m_log;
    AssetId m_toneMapShader;
    Rhi::RhiFormat m_presentFormat;
    std::optional<Renderer::PipelineId> m_present; ///< ToneMap.hlsl's DEBUG_OUTPUT = 1, built on first use.
    ScenePipelines m_scene;
    RenderDebugState m_state;
    std::optional<Math::Frustum> m_frozen;                ///< Kept while culling is frozen.
    std::optional<Shader::FrameConstants> m_frozenCamera; ///< The camera it was frozen with.
};
} // namespace Meow2::RendererApp
