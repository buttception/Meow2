/// @file
/// @brief RenderGraph: a frame's passes, the barriers between them, and recording them on jobs.

#pragma once

#include <Meow2/Core/Jobs/JobSystem.h>
#include <Meow2/Core/Result.h>
#include <Meow2/Graphics/Rhi/IRhiDevice.h>

#include <cstdint>
#include <functional>
#include <string>
#include <variant>
#include <vector>

namespace Meow2::Renderer
{
/// @brief A resource known to a RenderGraph. Plain data.
struct GraphResource
{
    std::uint32_t index = 0; ///< Position in the graph.
};

/// @brief What a pass does with a resource. Plain data.
struct GraphAccess
{
    GraphResource resource{};                                  ///< The resource.
    Rhi::ResourceState state = Rhi::ResourceState::ShaderRead; ///< What it needs to be in.
};

/// @brief One pass of a frame. Plain data.
struct GraphPass
{
    std::string name;                                    ///< Shown by RenderDoc, PIX, Nsight and Tracy.
    std::vector<GraphAccess> accesses{};                 ///< Everything it touches, so the graph can place barriers.
    std::function<void(Rhi::IRhiCommandList&)> record{}; ///< Records it, on a job. Must only touch what it declared.
    bool small = false; ///< Too little work for its own job: it shares a list with the small passes next to it.
};

/// @brief A barrier the graph decided on. Plain data.
struct GraphBarrier
{
    GraphResource resource{};                                ///< The resource.
    Rhi::ResourceState from = Rhi::ResourceState::Undefined; ///< What it was used for.
    Rhi::ResourceState to = Rhi::ResourceState::Undefined;   ///< What it's used for next.
};

/// @brief Passes recorded together into one command list. Plain data.
struct GraphBatch
{
    std::uint32_t firstPass = 0; ///< The first pass.
    std::uint32_t passCount = 0; ///< How many.
};

/// @brief Works out a frame's barriers, then records its passes in parallel.
///
/// Passes run in the order they're added. compile(), on one thread, walks them and tracks each resource's state, which
/// fixes every barrier before anything records. execute() then records each batch on a job, into its own list, and
/// returns the lists in pass order. Because barriers are already decided, jobs don't depend on each other, so they can
/// finish in any order. A pass is never split across lists (techstack.md §19).
///
/// Build a new graph each frame. Passes are recorded in the frame's lists, so nothing outlives endFrame().
/// @par Thread safety
/// Not thread-safe, apart from `record` functions, which run on jobs at the same time.
class RenderGraph
{
public:
    /// @brief Adds a buffer.
    /// @param buffer The buffer.
    /// @param state  The state it's in now.
    /// @return Its ID in this graph.
    [[nodiscard]] GraphResource import(Rhi::BufferHandle buffer, Rhi::ResourceState state);

    /// @brief Adds a texture.
    /// @param texture The texture.
    /// @param state   The state it's in now.
    /// @return Its ID in this graph.
    [[nodiscard]] GraphResource import(Rhi::TextureHandle texture, Rhi::ResourceState state);

    /// @brief Adds a pass, after the passes already added.
    /// @param pass The pass.
    void addPass(GraphPass pass);

    /// @brief Decides the barriers and batches.
    /// @pre Each access names a resource of this graph (asserted).
    void compile();

    /// @brief The barriers before a pass.
    /// @param pass Its position.
    /// @return The barriers; valid after compile().
    [[nodiscard]] const std::vector<GraphBarrier>& barriers(std::uint32_t pass) const;

    /// @brief The batches.
    /// @return One per command list; valid after compile().
    [[nodiscard]] const std::vector<GraphBatch>& batches() const
    {
        return m_batches;
    }

    /// @brief A resource's state after every pass.
    /// @param resource The resource.
    /// @return The state, to pass to import() next frame; valid after compile().
    [[nodiscard]] Rhi::ResourceState finalState(GraphResource resource) const;

    /// @brief Records every pass, in parallel.
    /// @param device A device in a frame, with enough RhiDeviceDesc::recordingThreads.
    /// @param jobs   Runs the recording.
    /// @return The lists in pass order, for IRhiDevice::endFrame(), or the first error.
    /// @pre compile() has run (asserted).
    [[nodiscard]] Result<std::vector<Rhi::IRhiCommandList*>> execute(Rhi::IRhiDevice& device, Jobs::JobSystem& jobs);

private:
    /// @brief A resource and its tracked state.
    struct Entry
    {
        std::variant<Rhi::BufferHandle, Rhi::TextureHandle> handle; ///< The resource.
        Rhi::ResourceState state = Rhi::ResourceState::Undefined;   ///< Its state as compile() walks the passes.
    };

    /// @brief Records one batch.
    /// @param list  Where to.
    /// @param batch The batch.
    void recordBatch(Rhi::IRhiCommandList& list, const GraphBatch& batch) const;

    std::vector<Entry> m_resources;
    std::vector<GraphPass> m_passes;
    std::vector<std::vector<GraphBarrier>> m_barriers;
    std::vector<GraphBatch> m_batches;
    bool m_compiled = false;
};
} // namespace Meow2::Renderer
