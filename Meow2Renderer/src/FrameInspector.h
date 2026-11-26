/// @file
/// @brief Meow2Renderer's render-graph inspector: the live frame as a flow of passes and the textures between them, a
///        picture-in-picture view of any pass's outputs, and an in-memory frame capture (F9). Renderer only; for
///        keeping a capture, RenderDoc (F11) is the tool.

#pragma once

#include "ImGuiLayer.h"
#include "UiTheme.h"

#include <Meow2/Core/Id/Uuid.h>
#include <Meow2/Core/Log/Logger.h>
#include <Meow2/Graphics/Renderer/FrameCapture.h>
#include <Meow2/Graphics/Renderer/FrameFlow.h>
#include <Meow2/Graphics/Renderer/PipelineLibrary.h>
#include <Meow2/Graphics/Renderer/RenderDevice.h>
#include <Meow2/Graphics/Renderer/RenderGraph.h>

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Meow2::RendererApp
{
// The render graph as plain data and its node layout live in the engine (Meow2/Graphics/Renderer/FrameFlow.h), shared
// with the editor's frame capture; the inspector names them as before.
using Renderer::describeGraph;
using Renderer::FlowAccess;
using Renderer::FlowGeometry;
using Renderer::FlowLayout;
using Renderer::FlowMetrics;
using Renderer::FlowNode;
using Renderer::FlowPass;
using Renderer::FlowPin;
using Renderer::FlowPins;
using Renderer::FlowResource;
using Renderer::formatName;
using Renderer::FrameFlow;
using Renderer::isDepthFormat;
using Renderer::layoutFlow;
using Renderer::markDebugResources;
using Renderer::passPins;
using Renderer::placeFlow;
using Renderer::ResourceKind;
using Renderer::resourceKind;
using Renderer::resourceKindName;
using Renderer::writerBefore;

/// @brief The live render-graph view, its picture-in-picture pass view and frame captures.
///
/// Each frame, in order:
/// 1. drawWindows() (during the UI): the windows, from the last frame's flow.
/// 2. beginFrame() (after the scene's passes): imports its own textures and adds the capture viewer's pass.
/// 3. ImGuiLayer::addPasses() with uiReads().
/// 4. afterPasses() (before compile): records the flow, inserts the selected pass's view after it, and, when a capture
///    was asked for, a copy after every pass that writes a texture.
/// 5. endFrame() (after execution): keeps its textures' states for the next frame.
/// @par Thread safety
/// Not thread-safe; use on the main thread.
/// @par Lifetime
/// The RenderDevice, PipelineLibrary and Logger must outlive it.
class FrameInspector
{
public:
    /// @brief How one texture is shown. Plain data.
    struct DisplaySettings
    {
        std::uint32_t channels = 0b0111; ///< R, G, B shown (A off); one bit alone shows that channel as grey.
        float rangeMin = 0.0F;           ///< The value shown black.
        float rangeMax = 1.0F;           ///< The value shown white.
    };

    /// @brief Builds its pipelines and registers with the device (its textures are freed on a backend switch).
    /// @param device    The render device.
    /// @param pipelines Builds the pipelines.
    /// @param shader    The AssetId of `shaders/DebugTexture.hlsl`.
    /// @param log       Receives errors.
    FrameInspector(Renderer::RenderDevice& device, Renderer::PipelineLibrary& pipelines, AssetId shader,
                   Log::Logger& log);

    /// @brief Frees its textures and buffers.
    ~FrameInspector();

    FrameInspector(const FrameInspector&) = delete;            ///< Not copyable: owns GPU objects.
    FrameInspector& operator=(const FrameInspector&) = delete; ///< Not copyable: owns GPU objects.

    /// @brief Watches a pass in the Pass view, as clicking it in the Render graph window does.
    /// @param pass Its name; empty stops watching.
    void watch(const std::string& pass)
    {
        m_selectedPass = pass.empty() ? std::nullopt : std::optional{pass};
    }

    /// @brief Asks for a capture of the next frame (F9, or the button).
    void requestCapture()
    {
        m_frameCapture.request();
    }

    /// @brief Draws the Render graph, Pass view and Frame capture windows.
    /// @param ui    For the textures shown.
    /// @param fonts The theme's fonts.
    void drawWindows(ImGuiLayer& ui, const UiFonts& fonts = {});

    /// @brief Step 2: imports its textures and adds the capture viewer's display pass.
    /// @param graph     The frame's graph, after the scene's passes.
    /// @param frameSlot IRhiDevice::frameSlot().
    void beginFrame(Renderer::RenderGraph& graph, std::uint32_t frameSlot);

    /// @brief The textures the windows show this frame, for ImGuiLayer::addPasses().
    /// @return Them, as graph resources.
    [[nodiscard]] std::span<const Renderer::GraphResource> uiReads() const
    {
        return m_uiReads;
    }

    /// @brief Step 4: the flow, the selected pass's view and a requested capture.
    /// @param graph     The frame's graph, every pass added, not compiled.
    /// @param frameSlot IRhiDevice::frameSlot().
    void afterPasses(Renderer::RenderGraph& graph, std::uint32_t frameSlot);

    /// @brief Step 5: keeps its textures' final states.
    /// @param graph The executed graph.
    void endFrame(const Renderer::RenderGraph& graph);

private:
    /// @brief A texture of the inspector and its state between frames.
    struct OwnedTexture
    {
        Rhi::TextureHandle texture{};                             ///< On the current device; unset if none.
        Rhi::TextureInfo info{};                                  ///< What it was made as.
        Rhi::ResourceState state = Rhi::ResourceState::Undefined; ///< After the last frame.
        Renderer::GraphResource resource{};                       ///< In this frame's graph.
        bool imported = false;                                    ///< resource is valid this frame.
    };

    /// @brief A pass output shown live: the graph resource it comes from and its display image.
    struct LiveOutput
    {
        std::string name{};     ///< The resource's name.
        bool depth = false;     ///< A depth texture.
        OwnedTexture display{}; ///< RGBA8 sRGB, the source's size.
        DisplaySettings settings{};
    };

    /// @brief Makes (or remakes, if the size or format changed) a texture.
    bool ensure(OwnedTexture& texture, const Rhi::TextureInfo& info, const char* name);
    /// @brief Imports a texture into this frame's graph, if it exists.
    void import(Renderer::RenderGraph& graph, OwnedTexture& texture, const std::string& name);
    /// @brief Writes a DebugDisplay record for this frame; returns its index (or nullopt when full).
    std::optional<std::uint32_t> displayRecord(std::uint32_t frameSlot, Rhi::TextureHandle source, bool depth,
                                               const DisplaySettings& settings);
    /// @brief Adds a display pass from `source` into `target` after pass `after` (or at the end when nullopt).
    Renderer::GraphPass displayPass(std::string name, Renderer::GraphResource source, Rhi::TextureHandle sourceTexture,
                                    OwnedTexture& target, bool depth, const DisplaySettings& settings,
                                    std::uint32_t frameSlot);
    /// @brief The controls of a display: channels and range.
    static void drawDisplayControls(DisplaySettings& settings, bool depth);
    /// @brief What was clicked in a flow view this frame. Plain data.
    struct FlowClick
    {
        std::optional<std::uint32_t> pass{}; ///< A pass's node.
        std::optional<FlowPin> pin{};        ///< A pin.
    };

    /// @brief Draws a flow as a node graph: a node per pass, input pins left, output pins right, wires from each
    ///        output to the inputs that read it, coloured by resource kind. Drag with the right or middle mouse
    ///        button to pan.
    /// @param flow     The flow.
    /// @param selected The selected pass, drawn highlighted with its wires.
    /// @param pin      The selected pin, drawn highlighted.
    /// @param id       ImGui ID of the view.
    /// @param height   The view's height in pixels.
    /// @return What was clicked.
    FlowClick drawFlow(const FrameFlow& flow, std::optional<std::uint32_t> selected, std::optional<FlowPin> pin,
                       const char* id, float height);
    /// @brief The colours of the resource kinds, on one line.
    static void drawLegend();
    /// @brief The details of a selected pin under a flow view: kind, size, who writes and reads it.
    static void drawPinInfo(const FrameFlow& flow, const FlowPin& pin, const UiFonts& fonts);
    /// @brief Frees every GPU object (backend switch, destruction).
    void release(Rhi::IRhiDevice& device);

    Renderer::RenderDevice& m_device;
    Renderer::PipelineLibrary& m_pipelines;
    Log::Logger& m_log;
    std::optional<Renderer::PipelineId> m_display;                              ///< psDisplay into RGBA8 sRGB.
    std::array<Rhi::BufferHandle, Rhi::c_maxFramesInFlight> m_displayBuffers{}; ///< DebugDisplay records per frame.
    std::uint32_t m_displayCount = 0;                                           ///< Records written this frame.

    FrameFlow m_liveFlow;                           ///< The last frame's flow.
    std::optional<std::string> m_selectedPass;      ///< The pass shown live, by name (indices move between frames).
    std::optional<FlowPin> m_livePin;               ///< The pin clicked in the live view (indices of m_liveFlow).
    std::optional<std::string> m_focusOutput;       ///< An output tab to bring forward in the Pass view, by name.
    std::optional<FlowPin> m_capturePin;            ///< The pin clicked in the capture.
    std::vector<LiveOutput> m_live;                 ///< Its outputs.
    std::vector<Renderer::GraphResource> m_uiReads; ///< This frame's textures shown by the UI.

    Renderer::FrameCapture m_frameCapture;        ///< The captured frame and its textures (shared with the editor).
    std::optional<std::uint32_t> m_capturePass;   ///< Selected in the capture.
    std::optional<std::size_t> m_captureSnapshot; ///< Shown full size.
    OwnedTexture m_captureDisplay;                ///< The selected snapshot as an image.
    DisplaySettings m_captureSettings;
    std::uint64_t m_frame = 0;        ///< Frames seen.
    std::vector<Rhi::TextureHandle>
        m_retired; ///< Replaced this frame while the graph still names them; freed in endFrame().

    Renderer::DeviceUser m_deviceUser; ///< Last: unregisters first, before what its callbacks use.
};
} // namespace Meow2::RendererApp
