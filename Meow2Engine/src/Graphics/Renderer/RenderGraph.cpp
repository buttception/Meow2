/// @file
/// @brief RenderGraph: barrier tracking and parallel recording.

#include "Meow2/Graphics/Renderer/RenderGraph.h"

#include "Meow2/Core/Assert.h"

#include <atomic>
#include <mutex>
#include <optional>
#include <utility>

namespace Meow2::Renderer
{
GraphResource RenderGraph::import(Rhi::BufferHandle buffer, Rhi::ResourceState state, std::string name)
{
    m_resources.push_back({.handle = buffer, .state = state, .initial = state, .name = std::move(name)});
    return {.index = static_cast<std::uint32_t>(m_resources.size() - 1)};
}

GraphResource RenderGraph::import(Rhi::TextureHandle texture, Rhi::ResourceState state, std::string name)
{
    m_resources.push_back({.handle = texture, .state = state, .initial = state, .name = std::move(name)});
    return {.index = static_cast<std::uint32_t>(m_resources.size() - 1)};
}

void RenderGraph::addPass(GraphPass pass)
{
    m_passes.push_back(std::move(pass));
    m_compiled = false;
}

void RenderGraph::insertPassAfter(std::uint32_t after, GraphPass pass)
{
    MEOW2_ASSERT(after < m_passes.size(), "No pass at that position");
    MEOW2_ASSERT(!m_compiled, "Passes are inserted before compile()");
    m_passes.insert(m_passes.begin() + after + 1, std::move(pass));
}

const std::string& RenderGraph::resourceName(GraphResource resource) const
{
    MEOW2_ASSERT(resource.index < m_resources.size(), "This resource isn't in this graph");
    return m_resources[resource.index].name;
}

std::optional<Rhi::TextureHandle> RenderGraph::resourceTexture(GraphResource resource) const
{
    MEOW2_ASSERT(resource.index < m_resources.size(), "This resource isn't in this graph");
    const auto* texture = std::get_if<Rhi::TextureHandle>(&m_resources[resource.index].handle);
    return texture != nullptr ? std::optional{*texture} : std::nullopt;
}

Rhi::ResourceState RenderGraph::initialState(GraphResource resource) const
{
    MEOW2_ASSERT(resource.index < m_resources.size(), "This resource isn't in this graph");
    return m_resources[resource.index].initial;
}

void RenderGraph::compile()
{
    m_barriers.assign(m_passes.size(), {});
    m_batches.clear();
    for (std::size_t i = 0; i < m_passes.size(); ++i)
    {
        for (const GraphAccess& access : m_passes[i].accesses)
        {
            MEOW2_ASSERT(access.resource.index < m_resources.size(), "This resource isn't in this graph");
            Entry& entry = m_resources[access.resource.index];
            // Staying in ShaderWrite still needs a barrier: the second pass may read what the first wrote.
            if (entry.state != access.state || access.state == Rhi::ResourceState::ShaderWrite)
            {
                m_barriers[i].push_back({.resource = access.resource, .from = entry.state, .to = access.state});
                entry.state = access.state;
            }
        }
        const bool joinPrevious = m_passes[i].small && !m_batches.empty() &&
                                  m_passes[m_batches.back().firstPass + m_batches.back().passCount - 1].small;
        if (joinPrevious)
        {
            ++m_batches.back().passCount;
        }
        else
        {
            m_batches.push_back({.firstPass = static_cast<std::uint32_t>(i), .passCount = 1});
        }
    }
    m_compiled = true;
}

const std::vector<GraphBarrier>& RenderGraph::barriers(std::uint32_t pass) const
{
    MEOW2_ASSERT(m_compiled, "compile() hasn't run");
    return m_barriers[pass];
}

Rhi::ResourceState RenderGraph::finalState(GraphResource resource) const
{
    MEOW2_ASSERT(m_compiled, "compile() hasn't run");
    return m_resources[resource.index].state;
}

void RenderGraph::recordBatch(Rhi::IRhiCommandList& list, const GraphBatch& batch) const
{
    for (std::uint32_t i = batch.firstPass; i < batch.firstPass + batch.passCount; ++i)
    {
        const GraphPass& pass = m_passes[i];
        const Rhi::ScopedMarker marker{list, pass.name.c_str()};
        for (const GraphBarrier& barrier : m_barriers[i])
        {
            std::visit([&](auto handle) { list.barrier(handle, barrier.from, barrier.to); },
                       m_resources[barrier.resource.index].handle);
        }
        if (pass.record)
        {
            pass.record(list);
        }
    }
}

Result<std::vector<Rhi::IRhiCommandList*>> RenderGraph::execute(Rhi::IRhiDevice& device, Jobs::JobSystem& jobs)
{
    MEOW2_ASSERT(m_compiled, "compile() hasn't run");
    std::vector<Rhi::IRhiCommandList*> lists(m_batches.size(), nullptr);
    std::mutex errorMutex;
    std::optional<Error> firstError;
    Jobs::Job job = jobs.parallelFor(static_cast<std::uint32_t>(m_batches.size()), 1,
                                     [&](std::uint32_t begin, std::uint32_t end, std::uint32_t threadIndex)
                                     {
                                         for (std::uint32_t i = begin; i < end; ++i)
                                         {
                                             Result<Rhi::IRhiCommandList*> list = device.commandList(threadIndex);
                                             if (!list)
                                             {
                                                 const std::scoped_lock lock{errorMutex};
                                                 firstError = firstError.value_or(list.error());
                                                 continue;
                                             }
                                             lists[i] = *list;
                                             recordBatch(**list, m_batches[i]);
                                         }
                                     });
    jobs.wait(job);
    if (firstError)
    {
        return Unexpected<Error>{*firstError};
    }
    return lists;
}
} // namespace Meow2::Renderer
