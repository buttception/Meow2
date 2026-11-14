/// @file
/// @brief PipelineLibrary: building pipelines from shader assets.

#include "Meow2/Graphics/Renderer/PipelineLibrary.h"

#include "Meow2/Core/Assert.h"

#include <format>
#include <memory>
#include <span>
#include <utility>

namespace Meow2::Renderer
{
namespace
{
/// @brief The bytecode format of a device's backend.
/// @param backend The backend.
/// @return The target.
Shader::ShaderTarget targetOf(Rhi::RhiBackend backend)
{
    return backend == Rhi::RhiBackend::D3D12 ? Shader::ShaderTarget::Dxil : Shader::ShaderTarget::Spirv;
}
} // namespace

PipelineLibrary::PipelineLibrary(const PipelineLibraryDeps& deps)
    : m_deps(deps)
    , m_deviceUser(deps.device.addUser(
          "Pipelines",
          [this](Rhi::IRhiDevice& device)
          {
              for (Entry& entry : m_entries)
              {
                  device.destroy(entry.handle);
                  entry.handle = {};
              }
          },
          [this](Rhi::IRhiDevice& device) -> Result<void>
          {
              // build() uses the new device, which RenderDevice has made current already.
              for (Entry& entry : m_entries)
              {
                  Result<Rhi::PipelineHandle> pipeline = build(entry);
                  if (!pipeline)
                  {
                      // A failed restore isn't released, so destroy what this one did build.
                      for (Entry& built : m_entries)
                      {
                          device.destroy(built.handle);
                          built.handle = {};
                      }
                      return fail(pipeline.error().code,
                                  std::format("'{}': {}",
                                              entry.compute ? entry.computeRequest.name : entry.graphics.name,
                                              pipeline.error().message));
                  }
                  entry.handle = *pipeline;
              }
              return {};
          }))
    , m_onReload(
          deps.events.subscribe<Assets::ShaderReloaded>([this](const Assets::ShaderReloaded& e) { rebuild(e.id); }))
{
}

PipelineLibrary::~PipelineLibrary()
{
    if (!m_deps.device.hasDevice())
    {
        return;
    }
    for (const Entry& entry : m_entries)
    {
        m_deps.device.device().destroy(entry.handle);
    }
}

Result<Rhi::PipelineHandle> PipelineLibrary::build(const Entry& entry)
{
    const AssetId shaderId = entry.compute ? entry.computeRequest.shader : entry.graphics.shader;
    Result<Assets::AssetHandle<Assets::ShaderAsset>> handle = m_deps.assets.load<Assets::ShaderAsset>(shaderId);
    if (!handle)
    {
        return Unexpected<Error>{handle.error()};
    }
    const std::shared_ptr<const Assets::ShaderAsset> shader = handle->get();
    const std::span<const Assets::ShaderOptionChoice> options =
        entry.compute ? std::span<const Assets::ShaderOptionChoice>{entry.computeRequest.options}
                      : std::span<const Assets::ShaderOptionChoice>{entry.graphics.options};
    Result<Assets::ShaderVariantKey> key = Assets::variantKey(shader->meta(), options);
    if (!key)
    {
        return Unexpected<Error>{key.error()};
    }
    const Shader::ShaderTarget target = targetOf(m_deps.device.backend());
    if (entry.compute)
    {
        Result<std::span<const std::byte>> code = shader->bytecode(*key, entry.computeRequest.entry, target);
        if (!code)
        {
            return Unexpected<Error>{code.error()};
        }
        return m_deps.device.device().createComputePipeline(
            {.code = *code, .entry = entry.computeRequest.entry, .name = entry.computeRequest.name});
    }

    const GraphicsPipelineRequest& request = entry.graphics;
    // The stages it has: vertex + pixel, or (task +) mesh + pixel. An empty entry stays empty bytecode.
    const auto code = [&](const std::string& entryPoint) -> Result<std::span<const std::byte>>
    {
        if (entryPoint.empty())
        {
            return std::span<const std::byte>{};
        }
        return shader->bytecode(*key, entryPoint, target);
    };
    const bool mesh = !request.meshEntry.empty();
    Result<std::span<const std::byte>> vertex = code(mesh ? std::string{} : request.vertexEntry);
    Result<std::span<const std::byte>> task = code(mesh ? request.taskEntry : std::string{});
    Result<std::span<const std::byte>> meshCode = code(request.meshEntry);
    Result<std::span<const std::byte>> pixel = code(request.pixelEntry);
    for (const Result<std::span<const std::byte>>* stage : {&vertex, &task, &meshCode, &pixel})
    {
        if (!*stage)
        {
            return Unexpected<Error>{stage->error()};
        }
    }
    return m_deps.device.device().createGraphicsPipeline({.vertexCode = *vertex,
                                                          .vertexEntry = request.vertexEntry,
                                                          .taskCode = *task,
                                                          .taskEntry = request.taskEntry,
                                                          .meshCode = *meshCode,
                                                          .meshEntry = request.meshEntry,
                                                          .pixelCode = *pixel,
                                                          .pixelEntry = request.pixelEntry,
                                                          .colorFormat = request.colorFormat,
                                                          .extraColorFormats = request.extraColorFormats,
                                                          .depthFormat = request.depthFormat,
                                                          .depthTest = request.depthTest,
                                                          .depthWrite = request.depthWrite,
                                                          .cull = request.cull,
                                                          .blend = request.blend,
                                                          .fill = request.fill,
                                                          .topology = request.topology,
                                                          .name = request.name});
}

Result<PipelineId> PipelineLibrary::add(GraphicsPipelineRequest request)
{
    // The same request again (a scene opened twice) shares the pipeline instead of building another.
    for (std::uint32_t i = 0; i < m_entries.size(); ++i)
    {
        if (!m_entries[i].compute && m_entries[i].graphics == request)
        {
            return PipelineId{.index = i};
        }
    }
    Entry entry;
    entry.graphics = std::move(request);
    Result<Rhi::PipelineHandle> pipeline = build(entry);
    if (!pipeline)
    {
        return Unexpected<Error>{pipeline.error()};
    }
    entry.handle = *pipeline;
    m_entries.push_back(std::move(entry));
    return PipelineId{.index = static_cast<std::uint32_t>(m_entries.size() - 1)};
}

Result<PipelineId> PipelineLibrary::add(ComputePipelineRequest request)
{
    for (std::uint32_t i = 0; i < m_entries.size(); ++i)
    {
        if (m_entries[i].compute && m_entries[i].computeRequest == request)
        {
            return PipelineId{.index = i};
        }
    }
    Entry entry;
    entry.compute = true;
    entry.computeRequest = std::move(request);
    Result<Rhi::PipelineHandle> pipeline = build(entry);
    if (!pipeline)
    {
        return Unexpected<Error>{pipeline.error()};
    }
    entry.handle = *pipeline;
    m_entries.push_back(std::move(entry));
    return PipelineId{.index = static_cast<std::uint32_t>(m_entries.size() - 1)};
}

Rhi::PipelineHandle PipelineLibrary::get(PipelineId id) const
{
    MEOW2_ASSERT(id.index < m_entries.size(), "This PipelineId isn't from this library");
    return m_entries[id.index].handle;
}

void PipelineLibrary::rebuild(AssetId shader)
{
    for (Entry& entry : m_entries)
    {
        if ((entry.compute ? entry.computeRequest.shader : entry.graphics.shader) != shader)
        {
            continue;
        }
        Result<Rhi::PipelineHandle> pipeline = build(entry);
        if (!pipeline)
        {
            MEOW2_LOG_ERROR(m_deps.log, "Keeping the old pipeline '{}': {}",
                            entry.compute ? entry.computeRequest.name : entry.graphics.name, pipeline.error());
            continue;
        }
        m_deps.device.device().destroy(entry.handle); // deferred: frames in flight may still use it
        entry.handle = *pipeline;
    }
}
} // namespace Meow2::Renderer
