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
