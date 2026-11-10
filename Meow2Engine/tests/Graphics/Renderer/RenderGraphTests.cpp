/// @file
/// @brief RenderGraph tests that need no GPU: which barriers and batches compile() decides on.

#include <Meow2/Graphics/Renderer/RenderGraph.h>

#include <gtest/gtest.h>

namespace Meow2::Renderer
{
namespace
{
using Rhi::ResourceState;

Rhi::BufferHandle buffer(std::uint32_t index)
{
    return {.index = index, .generation = 0};
}
} // namespace

TEST(RenderGraph, FirstUseTransitionsFromTheImportedState)
{
    RenderGraph graph;
    const GraphResource resource = graph.import(buffer(1), ResourceState::CopyTarget);
    graph.addPass({.name = "Read", .accesses = {{resource, ResourceState::ShaderRead}}});
    graph.compile();

    ASSERT_EQ(graph.barriers(0).size(), 1U);
    EXPECT_EQ(graph.barriers(0)[0].from, ResourceState::CopyTarget);
    EXPECT_EQ(graph.barriers(0)[0].to, ResourceState::ShaderRead);
}

TEST(RenderGraph, SameStateNeedsNoBarrier)
{
    RenderGraph graph;
    const GraphResource resource = graph.import(buffer(1), ResourceState::ShaderRead);
    graph.addPass({.name = "A", .accesses = {{resource, ResourceState::ShaderRead}}});
    graph.addPass({.name = "B", .accesses = {{resource, ResourceState::ShaderRead}}});
    graph.compile();

    EXPECT_TRUE(graph.barriers(0).empty());
    EXPECT_TRUE(graph.barriers(1).empty());
}

TEST(RenderGraph, WriteAfterWriteStillNeedsABarrier)
{
    RenderGraph graph;
    const GraphResource resource = graph.import(buffer(1), ResourceState::ShaderWrite);
    graph.addPass({.name = "A", .accesses = {{resource, ResourceState::ShaderWrite}}});
    graph.addPass({.name = "B", .accesses = {{resource, ResourceState::ShaderWrite}}});
    graph.compile();

    EXPECT_EQ(graph.barriers(1).size(), 1U);
}

TEST(RenderGraph, StatesCarryAcrossPasses)
{
    RenderGraph graph;
    const GraphResource resource = graph.import(buffer(1), ResourceState::Undefined);
    graph.addPass({.name = "Write", .accesses = {{resource, ResourceState::ShaderWrite}}});
    graph.addPass({.name = "Args", .accesses = {{resource, ResourceState::IndirectArgs}}});
    graph.compile();

    ASSERT_EQ(graph.barriers(1).size(), 1U);
    EXPECT_EQ(graph.barriers(1)[0].from, ResourceState::ShaderWrite);
    EXPECT_EQ(graph.finalState(resource), ResourceState::IndirectArgs);
}

TEST(RenderGraph, EachPassGetsItsOwnList)
{
    RenderGraph graph;
    graph.addPass({.name = "A"});
    graph.addPass({.name = "B"});
    graph.compile();

    EXPECT_EQ(graph.batches().size(), 2U);
}

TEST(RenderGraph, SmallPassesShareAList)
{
    RenderGraph graph;
    graph.addPass({.name = "A", .small = true});
    graph.addPass({.name = "B", .small = true});
    graph.addPass({.name = "Big"});
    graph.addPass({.name = "C", .small = true});
    graph.compile();

    ASSERT_EQ(graph.batches().size(), 3U);
    EXPECT_EQ(graph.batches()[0].passCount, 2U);
    EXPECT_EQ(graph.batches()[1].firstPass, 2U);
    EXPECT_EQ(graph.batches()[2].firstPass, 3U);
}

TEST(RenderGraph, ReportsPassesAndResources)
{
    RenderGraph graph;
    const GraphResource draws = graph.import(buffer(1), ResourceState::IndirectArgs, "Draw commands");
    const GraphResource colour =
        graph.import(Rhi::TextureHandle{.index = 2, .generation = 0}, ResourceState::Undefined, "Scene colour");
    const GraphResource unnamed = graph.import(buffer(3), ResourceState::ShaderRead);
    graph.addPass({.name = "Cull", .accesses = {{draws, ResourceState::ShaderWrite}}});
    graph.addPass(
        {.name = "Draw", .accesses = {{draws, ResourceState::IndirectArgs}, {colour, ResourceState::ColorTarget}}});

    ASSERT_EQ(graph.passes().size(), 2U);
    EXPECT_EQ(graph.passes()[0].name, "Cull");
    EXPECT_EQ(graph.passes()[1].accesses.size(), 2U);
    EXPECT_EQ(graph.resourceCount(), 3U);
    EXPECT_EQ(graph.resourceName(colour), "Scene colour");
    EXPECT_TRUE(graph.resourceName(unnamed).empty());
    EXPECT_FALSE(graph.resourceTexture(draws).has_value()) << "a buffer";
    ASSERT_TRUE(graph.resourceTexture(colour).has_value());
    EXPECT_EQ(graph.resourceTexture(colour)->index, 2U);
    EXPECT_EQ(graph.initialState(draws), ResourceState::IndirectArgs);
    EXPECT_TRUE(isWriteState(ResourceState::ColorTarget));
    EXPECT_TRUE(isWriteState(ResourceState::ShaderWrite));
    EXPECT_FALSE(isWriteState(ResourceState::IndirectArgs));
    EXPECT_FALSE(isWriteState(ResourceState::ShaderRead));
}

TEST(RenderGraph, InsertedPassSeesTheOutputInBetween)
{
    // What the frame inspector does: a pass that reads a texture right after the pass that wrote it, before the next
    // pass writes it again. The graph must put it in that order and move the texture between the states.
    RenderGraph graph;
    const GraphResource colour =
        graph.import(Rhi::TextureHandle{.index = 1, .generation = 0}, ResourceState::Undefined, "Colour");
    graph.addPass({.name = "Red", .accesses = {{colour, ResourceState::ColorTarget}}});
    graph.addPass({.name = "Green", .accesses = {{colour, ResourceState::ColorTarget}}});
    graph.insertPassAfter(0, {.name = "Inspect Red", .accesses = {{colour, ResourceState::ShaderRead}}});
    graph.compile();

    ASSERT_EQ(graph.passes().size(), 3U);
    EXPECT_EQ(graph.passes()[1].name, "Inspect Red");
    EXPECT_EQ(graph.passes()[2].name, "Green");
    ASSERT_EQ(graph.barriers(1).size(), 1U);
    EXPECT_EQ(graph.barriers(1)[0].from, ResourceState::ColorTarget);
    EXPECT_EQ(graph.barriers(1)[0].to, ResourceState::ShaderRead);
    ASSERT_EQ(graph.barriers(2).size(), 1U);
    EXPECT_EQ(graph.barriers(2)[0].to, ResourceState::ColorTarget) << "back to a target for the next writer";
}
} // namespace Meow2::Renderer
