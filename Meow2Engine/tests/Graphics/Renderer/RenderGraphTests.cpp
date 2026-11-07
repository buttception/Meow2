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
} // namespace Meow2::Renderer
