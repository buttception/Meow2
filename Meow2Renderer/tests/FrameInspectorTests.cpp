/// @file
/// @brief The frame inspector's flow: which pass's output another reads, and the columns the view puts passes in.

#include "FrameInspector.h"

#include <gtest/gtest.h>

namespace Meow2::RendererApp
{
namespace
{
FlowAccess reads(std::uint32_t resource)
{
    return {.resource = resource, .state = Rhi::ResourceState::ShaderRead, .writes = false};
}

FlowAccess writes(std::uint32_t resource)
{
    return {.resource = resource, .state = Rhi::ResourceState::ColorTarget, .writes = true};
}

/// @brief A frame like the Renderer's: the sky's tables and the culling don't depend on each other; the opaque pass
///        reads the culling's draws, the sky draws over its colour, tone mapping reads the result.
FrameFlow sampleFlow()
{
    FrameFlow flow;
    flow.resources = {{.name = "Draws"}, {.name = "Colour", .texture = true}, {.name = "Sky table", .texture = true}};
    flow.passes = {
        {.name = "Sky table", .accesses = {writes(2)}},        // 0
        {.name = "Cull", .accesses = {writes(0)}},             // 1
        {.name = "Opaque", .accesses = {reads(0), writes(1)}}, // 2
        {.name = "Sky", .accesses = {reads(2), writes(1)}},    // 3
        {.name = "Tone map", .accesses = {reads(1)}},          // 4
    };
    return flow;
}
} // namespace

TEST(FrameInspector, FindsWhereAnInputComesFrom)
{
    const FrameFlow flow = sampleFlow();
    EXPECT_EQ(writerBefore(flow, 0, 2), 1U) << "the opaque pass's draws come from the culling";
    EXPECT_EQ(writerBefore(flow, 1, 4), 3U) << "tone mapping reads the colour as the sky left it";
    EXPECT_EQ(writerBefore(flow, 1, 3), 2U);
    EXPECT_FALSE(writerBefore(flow, 0, 1).has_value()) << "nothing wrote it earlier in the frame";
}

TEST(FrameInspector, LaysOutPassesInColumns)
{
    const FlowLayout layout = layoutFlow(sampleFlow());
    ASSERT_EQ(layout.column.size(), 5U);
    EXPECT_EQ(layout.column[0], 0U) << "the sky table depends on nothing";
    EXPECT_EQ(layout.column[1], 0U) << "neither does the culling: side by side";
    EXPECT_EQ(layout.row[1], 1U);
    EXPECT_EQ(layout.column[2], 1U) << "after the culling";
    EXPECT_EQ(layout.column[3], 2U) << "after the opaque pass (it draws over its colour) and the sky table";
    EXPECT_EQ(layout.column[4], 3U);
    EXPECT_EQ(layout.columns, 4U);
    EXPECT_EQ(layout.rows, 2U);
}
} // namespace Meow2::RendererApp

namespace Meow2::RendererApp
{
TEST(FrameInspector, NodesHavePinsForEachAccess)
{
    const FrameFlow flow = sampleFlow();
    const FlowPins opaque = passPins(flow, 2);
    EXPECT_EQ(opaque.inputs, (std::vector<std::uint32_t>{0})) << "reads the draws; its colour write starts fresh";
    EXPECT_EQ(opaque.outputs, (std::vector<std::uint32_t>{1}));
    const FlowPins sky = passPins(flow, 3);
    EXPECT_EQ(sky.inputs, (std::vector<std::uint32_t>{2, 1})) << "draws over the opaque pass's colour: an input too";
    EXPECT_EQ(sky.outputs, (std::vector<std::uint32_t>{1}));
    const FlowPins toneMap = passPins(flow, 4);
    EXPECT_EQ(toneMap.inputs, (std::vector<std::uint32_t>{1}));
    EXPECT_TRUE(toneMap.outputs.empty());
}

TEST(FrameInspector, NodesInAColumnDontOverlap)
{
    // Three independent passes in one column, with different numbers of pins and names of different lengths.
    FrameFlow flow;
    flow.resources = {{.name = "A"}, {.name = "A much longer resource name"}, {.name = "C"}, {.name = "D"}};
    flow.passes = {
        {.name = "One", .accesses = {writes(0)}},
        {.name = "Two", .accesses = {writes(1), writes(2), writes(3)}},
        {.name = "Three with a long name", .accesses = {writes(2)}},
    };
    const FlowMetrics metrics{};
    const FlowGeometry geometry =
        placeFlow(flow, metrics, [](std::string_view text) { return 7.0F * static_cast<float>(text.size()); });
    ASSERT_EQ(geometry.nodes.size(), 3U);
    for (std::size_t i = 0; i < geometry.nodes.size(); ++i)
    {
        const FlowNode& node = geometry.nodes[i];
        EXPECT_GE(node.width, 7.0F * static_cast<float>(node.title.size())) << "the title fits";
        EXPECT_LE(node.x + node.width, geometry.width);
        EXPECT_LE(node.y + node.height, geometry.height);
        for (std::size_t j = i + 1; j < geometry.nodes.size(); ++j)
        {
            const FlowNode& other = geometry.nodes[j];
            const bool apart = node.x + node.width <= other.x || other.x + other.width <= node.x ||
                               node.y + node.height <= other.y || other.y + other.height <= node.y;
            EXPECT_TRUE(apart) << node.title << " overlaps " << other.title;
        }
    }
    EXPECT_GT(geometry.nodes[1].height, geometry.nodes[0].height) << "three pins need three rows";
    EXPECT_GE(geometry.nodes[1].width, 7.0F * 27.0F) << "the long resource name fits";
}

TEST(FrameInspector, OrdersRowsSoWiresCrossLess)
{
    // A and B write in column 0; C reads B's and D reads A's. In the order added, C's wire would cross D's.
    FrameFlow flow;
    flow.resources = {{.name = "a"}, {.name = "b"}};
    flow.passes = {
        {.name = "A", .accesses = {writes(0)}},
        {.name = "B", .accesses = {writes(1)}},
        {.name = "C", .accesses = {reads(1)}},
        {.name = "D", .accesses = {reads(0)}},
    };
    const FlowLayout layout = layoutFlow(flow);
    EXPECT_EQ(layout.column[2], 1U);
    EXPECT_EQ(layout.column[3], 1U);
    EXPECT_EQ(layout.row[3], 0U) << "D reads A's (row 0), so it goes on top";
    EXPECT_EQ(layout.row[2], 1U);
}

TEST(FrameInspector, ResourceKinds)
{
    FrameFlow flow;
    flow.resources = {{.name = "Colour", .texture = true, .info = {.format = Rhi::RhiFormat::Rgba16Float}},
                      {.name = "Depth", .texture = true, .info = {.format = Rhi::RhiFormat::D32Float}},
                      {.name = "Draws"},
                      {.name = "Instances"}};
    flow.passes = {{.name = "Draw",
                    .accesses = {{.resource = 2, .state = Rhi::ResourceState::IndirectArgs, .writes = false},
                                 {.resource = 3, .state = Rhi::ResourceState::ShaderRead, .writes = false}}}};
    EXPECT_EQ(resourceKind(flow, 0), ResourceKind::ColorTexture);
    EXPECT_EQ(resourceKind(flow, 1), ResourceKind::DepthTexture);
    EXPECT_EQ(resourceKind(flow, 2), ResourceKind::IndirectBuffer) << "some pass reads it as indirect arguments";
    EXPECT_EQ(resourceKind(flow, 3), ResourceKind::Buffer);
}
} // namespace Meow2::RendererApp

namespace Meow2::RendererApp
{
TEST(FrameInspector, ResourcesOnlyDebugPassesWriteAreDebug)
{
    FrameFlow flow;
    flow.resources = {{.name = "Depth"}, {.name = "Position (debug)"}, {.name = "Colour"}, {.name = "Imported"}};
    flow.passes = {
        {.name = "Opaque", .accesses = {writes(0), writes(2)}},
        {.name = "Position from depth", .accesses = {reads(0), writes(1)}, .debug = true},
        {.name = "Grid", .accesses = {reads(0), writes(2)}, .debug = true},
        {.name = "Tone map", .accesses = {reads(2), reads(3)}},
    };
    markDebugResources(flow);
    EXPECT_FALSE(flow.resources[0].debug) << "written by the frame";
    EXPECT_TRUE(flow.resources[1].debug) << "only a debug pass writes it";
    EXPECT_FALSE(flow.resources[2].debug) << "a debug pass draws over it, but the frame wrote it first";
    EXPECT_FALSE(flow.resources[3].debug) << "imported, written by no one";
}
} // namespace Meow2::RendererApp
