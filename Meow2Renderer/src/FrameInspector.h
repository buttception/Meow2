/// @file
/// @brief Meow2Renderer's render-graph inspector: the live frame as a flow of passes and the textures between them, a
///        picture-in-picture view of any pass's outputs, and an in-memory frame capture (F9). Renderer only; for
///        keeping a capture, RenderDoc (F11) is the tool.

#pragma once

#include "ImGuiLayer.h"
#include "UiTheme.h"

#include <Meow2/Core/Id/Uuid.h>
#include <Meow2/Core/Log/Logger.h>
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
/// @brief A resource of a frame, as the inspector shows it. Plain data.
struct FlowResource
{
    std::string name{};      ///< As given to RenderGraph::import(), or "Resource n".
    bool texture = false;    ///< A texture (it can be shown), or a buffer.
    Rhi::TextureInfo info{}; ///< Size and format, for a texture.
    bool debug = false; ///< Only debug passes write it (markDebugResources()): made for debugging, not by the frame.
};

/// @brief What a pass does with one resource. Plain data.
struct FlowAccess
{
    std::uint32_t resource = 0;                                ///< Index into FrameFlow::resources.
    Rhi::ResourceState state = Rhi::ResourceState::ShaderRead; ///< The state it declared.
    bool writes = false;                                       ///< Renderer::isWriteState(state).
};

/// @brief A pass of a frame. Plain data.
struct FlowPass
{
    std::string name{};                 ///< Its name.
    std::vector<FlowAccess> accesses{}; ///< What it reads and writes.
    bool debug = false;                 ///< Renderer::GraphPass::debug: a debugging tool's pass.
    bool skipped = false;               ///< Renderer::GraphPass::skipped: in the graph, but not run this frame.
};

/// @brief A frame's render graph as plain data: passes in the order they run and the resources between them. Plain
/// data.
struct FrameFlow
{
    std::vector<FlowPass> passes{};        ///< In order.
    std::vector<FlowResource> resources{}; ///< Every imported resource.
};

/// @brief Where each pass goes in the flow view. Plain data.
struct FlowLayout
{
    std::vector<std::uint32_t>
        column{}; ///< Per pass: one right of the latest pass whose output it reads (0: reads nothing written).
    std::vector<std::uint32_t> row{}; ///< Per pass: its place among the passes in its column, top to bottom.
    std::uint32_t columns = 0;        ///< How many columns.
    std::uint32_t rows = 0;           ///< The tallest column's passes.
};

/// @brief What kind of resource a pin stands for; it picks the pin's and wire's colour.
enum class ResourceKind : std::uint8_t
{
    ColorTexture,  ///< A texture of colour or other data, which the Pass view can show.
    DepthTexture,  ///< A depth texture (shown as grey).
    Buffer,        ///< A buffer: draw data, counts, constants.
    IndirectBuffer ///< A buffer some pass reads as indirect draw or dispatch arguments.
};

/// @brief The kind of a resource of a flow.
/// @param flow     The flow.
/// @param resource Index into FrameFlow::resources.
/// @return Its kind.
[[nodiscard]] ResourceKind resourceKind(const FrameFlow& flow, std::uint32_t resource);

/// @brief A kind's name, as the view shows it ("Colour texture", "Indirect arguments", …).
/// @param kind The kind.
/// @return The name.
[[nodiscard]] const char* resourceKindName(ResourceKind kind);

/// @brief A pass's pins in the flow view, each a resource index, in the order of its accesses. Plain data.
struct FlowPins
{
    std::vector<std::uint32_t> inputs{};  ///< What it reads, and what it draws over (a write after another pass's).
    std::vector<std::uint32_t> outputs{}; ///< What it writes.
};

/// @brief A pass's input and output pins: a read is an input; a write is an output, and also an input when an earlier
///        pass wrote the resource (the pass draws over that, so the data flows through it).
/// @param flow The flow.
/// @param pass The pass.
/// @return Its pins, without repeats.
[[nodiscard]] FlowPins passPins(const FrameFlow& flow, std::uint32_t pass);

/// @brief The flow view's spacing, in pixels (already scaled for the display). Plain data.
struct FlowMetrics
{
    float pad = 8.0F;          ///< Inside a node, around its text.
    float titleHeight = 22.0F; ///< A node's title bar.
    float pinRow = 20.0F;      ///< One pin's row.
    float dot = 4.5F;          ///< A pin's radius.
    float columnGap = 90.0F;   ///< Between columns, where the wires run.
    float rowGap = 24.0F;      ///< Between nodes in a column.
    float margin = 16.0F;      ///< Around everything.
    float minWidth = 140.0F;   ///< The narrowest node.
    float sideGap = 24.0F;     ///< Between a node's input labels and its output labels.
};

/// @brief One node of the flow view: where it is, its size and pins. Plain data.
struct FlowNode
{
    float x = 0.0F;      ///< Left edge, from the view's top left.
    float y = 0.0F;      ///< Top edge.
    float width = 0.0F;  ///< Width.
    float height = 0.0F; ///< Height: the title bar and a row per pin on the side with more.
    FlowPins pins{};     ///< Its pins.
    std::string title{}; ///< "<index>  <name>".
};

/// @brief Every node of a flow view, placed. Plain data.
struct FlowGeometry
{
    std::vector<FlowNode> nodes{}; ///< Per pass.
    float width = 0.0F;            ///< The whole view's size.
    float height = 0.0F;           ///< Ditto.
};

/// @brief Places a flow's nodes: columns from layoutFlow(), each as wide as its widest node, nodes stacked in each
///        column in row order with a gap, each node wide enough for its title and its input and output labels.
/// @param flow      The flow.
/// @param metrics   The spacing.
/// @param textWidth How wide a text is drawn, in pixels.
/// @return The nodes.
[[nodiscard]] FlowGeometry placeFlow(const FrameFlow& flow, const FlowMetrics& metrics,
                                     const std::function<float(std::string_view)>& textWidth);

/// @brief A pin of the flow view: a pass's input or output for one resource. Plain data.
struct FlowPin
{
    std::uint32_t pass = 0;     ///< The pass.
    std::uint32_t resource = 0; ///< Index into FrameFlow::resources.
    bool output = false;        ///< An output (right side) or an input (left side).

    /// @brief Pins are equal when every field is.
    friend bool operator==(const FlowPin&, const FlowPin&) = default;
};

/// @brief Marks the resources only debug passes write (FlowResource::debug): textures made for debugging, which the
///        frame itself doesn't produce (a position rebuilt from depth, the inspector's images). A resource no pass
///        writes (imported as it is) isn't marked.
/// @param flow The flow; its passes' `debug` set.
void markDebugResources(FrameFlow& flow);

/// @brief A render graph's passes and resources, before compile().
/// @param graph  The frame's graph.
/// @param device For the textures' sizes and formats.
/// @return The flow.
[[nodiscard]] FrameFlow describeGraph(const Renderer::RenderGraph& graph, Rhi::IRhiDevice& device);

/// @brief Lays a flow out in columns: a pass that reads what another wrote goes to its right, so independent passes
///        (the sky's lookup tables beside the culling) stand side by side instead of in one long line. Within a
///        column, passes are ordered by where their inputs come from (the average row of their writers), so wires
///        cross less; passes with no inputs keep their order.
/// @param flow The flow.
/// @return The layout.
[[nodiscard]] FlowLayout layoutFlow(const FrameFlow& flow);

/// @brief The pass that last wrote a resource before another pass, i.e. where that pass's input comes from.
/// @param flow     The flow.
/// @param resource The resource.
/// @param before   The pass that reads it.
/// @return The writing pass, or std::nullopt if it was written before the frame (imported as it was).
[[nodiscard]] std::optional<std::uint32_t> writerBefore(const FrameFlow& flow, std::uint32_t resource,
                                                        std::uint32_t before);

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
        m_captureRequested = true;
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

    /// @brief One snapshot of a capture: a texture as a pass left it.
    struct Snapshot
    {
        std::uint32_t pass = 0;     ///< The pass that wrote it.
        std::uint32_t resource = 0; ///< Index into the capture's FrameFlow::resources.
        bool depth = false;         ///< Raw R32 depth rather than RGBA16F.
        OwnedTexture raw{};         ///< The copy.
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
    std::optional<Renderer::PipelineId> m_copyColor;                            ///< psCopy into RGBA16F.
    std::optional<Renderer::PipelineId> m_copyDepth;                            ///< psCopy into R32F.
    std::array<Rhi::BufferHandle, Rhi::c_maxFramesInFlight> m_displayBuffers{}; ///< DebugDisplay records per frame.
    std::uint32_t m_displayCount = 0;                                           ///< Records written this frame.

    FrameFlow m_liveFlow;                           ///< The last frame's flow.
    std::optional<std::string> m_selectedPass;      ///< The pass shown live, by name (indices move between frames).
    std::optional<FlowPin> m_livePin;               ///< The pin clicked in the live view (indices of m_liveFlow).
    std::optional<std::string> m_focusOutput;       ///< An output tab to bring forward in the Pass view, by name.
    std::optional<FlowPin> m_capturePin;            ///< The pin clicked in the capture.
    std::vector<LiveOutput> m_live;                 ///< Its outputs.
    std::vector<Renderer::GraphResource> m_uiReads; ///< This frame's textures shown by the UI.

    bool m_captureRequested = false;
    std::optional<FrameFlow> m_capture;           ///< The captured frame's flow.
    std::vector<Snapshot> m_snapshots;            ///< Its textures, as each pass left them.
    std::optional<std::uint32_t> m_capturePass;   ///< Selected in the capture.
    std::optional<std::size_t> m_captureSnapshot; ///< Shown full size.
    OwnedTexture m_captureDisplay;                ///< The selected snapshot as an image.
    DisplaySettings m_captureSettings;
    std::uint64_t m_captureFrame = 0; ///< Which frame was captured (frame counter).
    std::uint64_t m_frame = 0;        ///< Frames seen.
    std::vector<Rhi::TextureHandle>
        m_retired; ///< Replaced this frame while the graph still names them; freed in endFrame().

    Renderer::DeviceUser m_deviceUser; ///< Last: unregisters first, before what its callbacks use.
};
} // namespace Meow2::RendererApp
