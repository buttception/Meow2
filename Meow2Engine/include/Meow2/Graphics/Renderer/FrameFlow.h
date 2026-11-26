/// @file
/// @brief A frame's render graph as plain data (passes, resources, who writes what) and its layout as a node graph:
///        what Meow2Renderer's render-graph view and the editor's frame capture draw. No UI library in here.

#pragma once

#include <Meow2/Graphics/Renderer/RenderGraph.h>
#include <Meow2/Graphics/Rhi/IRhiDevice.h>

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Meow2::Renderer
{
/// @brief Whether a format holds depth (a depth target, or a float copy of one): shown as one grey channel.
/// @param format The format.
/// @return True for D32Float and R32Float.
[[nodiscard]] bool isDepthFormat(Rhi::RhiFormat format);

/// @brief A format's name, as the views show it ("RGBA16F", "D32F", …).
/// @param format The format.
/// @return The name.
[[nodiscard]] const char* formatName(Rhi::RhiFormat format);

/// @brief A resource state's name, as the pin details show it ("Shader read", "Colour target", …).
/// @param state The state.
/// @return The name.
[[nodiscard]] const char* resourceStateName(Rhi::ResourceState state);

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
    float pad = 8.0F;             ///< Inside a node, around its text.
    float titleHeight = 22.0F;    ///< A node's title bar.
    float pinRow = 20.0F;         ///< One pin's row.
    float dot = 4.5F;             ///< A pin's radius.
    float levelGap = 70.0F;       ///< Between one level's band and the next one down, where the wires run.
    float nodeGap = 60.0F;        ///< Between nodes side by side in a band (wires pass there too).
    float maxBandWidth = 1400.0F; ///< A band wider than this wraps onto another line (at least one node per line).
    float margin = 16.0F;         ///< Around everything.
    float minWidth = 140.0F;      ///< The narrowest node.
    float sideGap = 24.0F;        ///< Between a node's input labels and its output labels.
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

/// @brief Places a flow's nodes top to bottom: each level of layoutFlow() (its `column`) is a band, the bands stack
///        downwards, and a level's passes sit side by side in their `row` order, wrapping onto another line when the
///        band gets wider than FlowMetrics::maxBandWidth. Each node is wide enough for its title and its input and
///        output labels.
///
/// A frame's dependency chain is long (shadow cascades alone add a dozen levels) but each level holds few passes, so
/// stacking levels keeps the view a column you scroll down, rather than a strip far wider than the window.
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

/// @brief Lays a flow out in levels (`column`): a pass that reads what another wrote goes one level after it, so
///        independent passes (the sky's lookup tables beside the culling) share a level. placeFlow() draws levels as
///        bands from top to bottom. Within a level, passes are ordered by where their inputs come from (the average
///        row of their writers), so wires cross less; passes with no inputs keep their order.
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
} // namespace Meow2::Renderer
