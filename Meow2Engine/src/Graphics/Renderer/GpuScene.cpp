/// @file
/// @brief GpuScene: the culling and drawing passes.

#include "Meow2/Graphics/Renderer/GpuScene.h"

#include "Meow2/Core/Assert.h"
#include "Meow2/Math/Packing.h"

#include <cmath>
#include <cstring>
#include <format>
#include <utility>

namespace Meow2::Renderer
{
namespace
{
constexpr std::uint32_t c_maxMeshes = 4096;
constexpr std::uint32_t c_cullGroupSize = 64; // [numthreads] in GpuDriven.hlsl

/// @brief Encodes one vertex.
/// @param position Its position.
/// @param normal   Its normal.
/// @param bounds   The box positions are quantised in.
/// @return The packed vertex.
Shader::PackedVertex packVertex(const Math::Vec3& position, const Math::Vec3& normal, const Math::Aabb& bounds)
{
    const Math::QuantizedPosition quantized = Math::quantizePosition(position, bounds);
    Shader::PackedVertex vertex{};
    vertex.xy = std::uint32_t{quantized.components[0]} | (std::uint32_t{quantized.components[1]} << 16U);
    vertex.z = quantized.components[2];
    vertex.normal = Math::packOctahedral(normal);
    return vertex;
}
} // namespace

MeshData makeCube()
{
    MeshData mesh;
    mesh.bounds = {.center = {0.0F, 0.0F, 0.0F}, .extents = {0.5F, 0.5F, 0.5F}};
    // Each face: its normal, and two axes across it, chosen so the corners below run clockwise seen from outside.
    struct Face
    {
        Math::Vec3 normal;
        Math::Vec3 u;
        Math::Vec3 v;
    };
    constexpr float c_half = 0.5F;
    const std::array<Face, 6> faces{Face{{1, 0, 0}, {0, 0, 1}, {0, 1, 0}},  Face{{-1, 0, 0}, {0, 0, -1}, {0, 1, 0}},
                                    Face{{0, 1, 0}, {1, 0, 0}, {0, 0, 1}},  Face{{0, -1, 0}, {1, 0, 0}, {0, 0, -1}},
                                    Face{{0, 0, 1}, {-1, 0, 0}, {0, 1, 0}}, Face{{0, 0, -1}, {1, 0, 0}, {0, 1, 0}}};
    for (const Face& face : faces)
    {
        const auto base = static_cast<std::uint32_t>(mesh.vertices.size());
        for (const auto& [a, b] :
             {std::pair{-1.0F, -1.0F}, std::pair{-1.0F, 1.0F}, std::pair{1.0F, 1.0F}, std::pair{1.0F, -1.0F}})
        {
            const Math::Vec3 position{(face.normal.x + face.u.x * a + face.v.x * b) * c_half,
                                      (face.normal.y + face.u.y * a + face.v.y * b) * c_half,
                                      (face.normal.z + face.u.z * a + face.v.z * b) * c_half};
            mesh.vertices.push_back(packVertex(position, face.normal, mesh.bounds));
        }
        for (const std::uint32_t index : {0U, 1U, 2U, 0U, 2U, 3U})
        {
            mesh.indices.push_back(base + index);
        }
    }
    return mesh;
}

GpuScene::GpuScene(Rhi::IRhiDevice& device, PipelineLibrary& pipelines, const GpuSceneDesc& desc)
    : m_device(device)
    , m_pipelines(pipelines)
    , m_desc(desc)
{
}

Result<std::unique_ptr<GpuScene>> GpuScene::create(Rhi::IRhiDevice& device, PipelineLibrary& pipelines,
                                                   const GpuSceneDesc& desc)
{
    MEOW2_ASSERT(desc.instanceCapacity > 0, "A scene needs room for instances");
    auto scene = std::make_unique<GpuScene>(device, pipelines, desc);
    const auto makeBuffer = [&device](Rhi::BufferHandle& out, std::uint64_t size, Rhi::RhiMemory memory,
                                      const char* name) -> Result<void>
    {
        Result<Rhi::BufferHandle> buffer =
            device.createBuffer({.size = size, .memory = memory, .tag = Memory::MemTag::Renderer, .name = name});
        if (!buffer)
        {
            return Unexpected<Error>{buffer.error()};
        }
        out = *buffer;
        return {};
    };
    const std::uint64_t capacity = desc.instanceCapacity;
    for (Result<void> made :
         {makeBuffer(scene->m_instanceBuffer, capacity * sizeof(Shader::InstanceData), Rhi::RhiMemory::Upload,
                     "Instances"),
          makeBuffer(scene->m_meshBuffer, std::uint64_t{c_maxMeshes} * sizeof(Shader::MeshDraw), Rhi::RhiMemory::Upload,
                     "Meshes"),
          makeBuffer(scene->m_drawBuffer, capacity * sizeof(Shader::IndirectDraw), Rhi::RhiMemory::GpuOnly, "Draws"),
          makeBuffer(scene->m_countBuffer, sizeof(std::uint32_t), Rhi::RhiMemory::GpuOnly, "Draw count")})
    {
        if (!made)
        {
            return Unexpected<Error>{made.error()};
        }
    }
    for (std::uint32_t slot = 0; slot < Rhi::c_maxFramesInFlight; ++slot)
    {
        if (Result<void> made = makeBuffer(scene->m_frameBuffers[slot], sizeof(Shader::FrameConstants),
                                           Rhi::RhiMemory::Upload, "Frame constants");
            !made)
        {
            return Unexpected<Error>{made.error()};
        }
        if (Result<void> made = makeBuffer(scene->m_cullBuffers[slot], sizeof(Shader::CullConstants),
                                           Rhi::RhiMemory::Upload, "Cull constants");
            !made)
        {
            return Unexpected<Error>{made.error()};
        }
    }

    Result<PipelineId> cull =
        pipelines.add(ComputePipelineRequest{.shader = desc.shader, .entry = "csCull", .name = "Cull"});
    if (!cull)
    {
        return Unexpected<Error>{cull.error()};
    }
    scene->m_cullPipeline = *cull;
    Result<PipelineId> draw = pipelines.add(GraphicsPipelineRequest{.shader = desc.shader,
                                                                    .colorFormat = desc.colorFormat,
                                                                    .depthFormat = Rhi::RhiFormat::D32Float,
                                                                    .cull = Rhi::CullMode::Back,
                                                                    .name = "Draw instances"});
    if (!draw)
    {
        return Unexpected<Error>{draw.error()};
    }
    scene->m_drawPipeline = *draw;
    return scene;
}

GpuScene::~GpuScene()
{
    for (const Rhi::BufferHandle buffer : {m_instanceBuffer, m_meshBuffer, m_drawBuffer, m_countBuffer})
    {
        m_device.destroy(buffer);
    }
    for (std::uint32_t slot = 0; slot < Rhi::c_maxFramesInFlight; ++slot)
    {
        m_device.destroy(m_frameBuffers[slot]);
        m_device.destroy(m_cullBuffers[slot]);
    }
    for (const Rhi::BufferHandle buffer : m_meshResources)
    {
        m_device.destroy(buffer);
    }
}

Result<std::uint32_t> GpuScene::addMesh(const MeshData& mesh)
{
    if (m_meshCount == c_maxMeshes)
    {
        return fail(ErrorCode::OutOfMemory, "Too many meshes");
    }
    const std::span<const std::byte> vertices = std::as_bytes(std::span{mesh.vertices});
    const std::span<const std::byte> indices = std::as_bytes(std::span{mesh.indices});
    Result<Rhi::BufferHandle> vertexBuffer = m_device.createBuffer({.size = vertices.size(),
                                                                    .memory = Rhi::RhiMemory::Upload,
                                                                    .tag = Memory::MemTag::Renderer,
                                                                    .name = "Mesh vertices"});
    if (!vertexBuffer)
    {
        return Unexpected<Error>{vertexBuffer.error()};
    }
    m_meshResources.push_back(*vertexBuffer);
    Result<Rhi::BufferHandle> indexBuffer = m_device.createBuffer({.size = indices.size(),
                                                                   .memory = Rhi::RhiMemory::Upload,
                                                                   .tag = Memory::MemTag::Renderer,
                                                                   .name = "Mesh indices"});
    if (!indexBuffer)
    {
        return Unexpected<Error>{indexBuffer.error()};
    }
    m_meshResources.push_back(*indexBuffer);
    std::memcpy(m_device.mappedMemory(*vertexBuffer).data(), vertices.data(), vertices.size());
    std::memcpy(m_device.mappedMemory(*indexBuffer).data(), indices.data(), indices.size());

    const Math::Vec3& extents = mesh.bounds.extents;
    Shader::MeshDraw draw{};
    draw.vertexBuffer = m_device.bindlessIndex(*vertexBuffer);
    draw.indexBuffer = m_device.bindlessIndex(*indexBuffer);
    draw.firstIndex = 0;
    draw.indexCount = static_cast<std::uint32_t>(mesh.indices.size());
    draw.boundsCenter = {mesh.bounds.center.x, mesh.bounds.center.y, mesh.bounds.center.z,
                         std::sqrt(extents.x * extents.x + extents.y * extents.y + extents.z * extents.z)};
    draw.boundsExtents = {extents.x, extents.y, extents.z, 0.0F};
    std::memcpy(m_device.mappedMemory(m_meshBuffer).data() + std::size_t{m_meshCount} * sizeof(draw), &draw,
                sizeof(draw));
    return m_meshCount++;
}

void GpuScene::setInstances(std::span<const Shader::InstanceData> instances)
{
    MEOW2_ASSERT(instances.size() <= m_desc.instanceCapacity, "More instances than GpuSceneDesc::instanceCapacity");
    m_instanceCount = static_cast<std::uint32_t>(instances.size());
    std::memcpy(m_device.mappedMemory(m_instanceBuffer).data(), instances.data(), instances.size_bytes());
}

void GpuScene::setView(std::uint32_t frameSlot, const Shader::FrameConstants& frame, const Math::Frustum& frustum)
{
    std::memcpy(m_device.mappedMemory(m_frameBuffers[frameSlot]).data(), &frame, sizeof(frame));
    Shader::CullConstants cull{};
    cull.plane0 = frustum.planes[0];
    cull.plane1 = frustum.planes[1];
    cull.plane2 = frustum.planes[2];
    cull.plane3 = frustum.planes[3];
    cull.plane4 = frustum.planes[4];
    cull.plane5 = frustum.planes[5];
    cull.instanceBuffer = m_device.bindlessIndex(m_instanceBuffer);
    cull.meshBuffer = m_device.bindlessIndex(m_meshBuffer);
    cull.drawBuffer = m_device.bindlessIndex(m_drawBuffer);
    cull.countBuffer = m_device.bindlessIndex(m_countBuffer);
    cull.instanceCount = m_instanceCount;
    std::memcpy(m_device.mappedMemory(m_cullBuffers[frameSlot]).data(), &cull, sizeof(cull));
}

void GpuScene::addPasses(RenderGraph& graph, std::uint32_t frameSlot, Rhi::IRhiSwapchain* swapchain,
                         Rhi::TextureHandle color, Rhi::TextureHandle depth, const Math::Color& clear)
{
    using Rhi::ResourceState;
    if (depth != m_lastDepth) // a new target, e.g. after a resize
    {
        m_lastDepth = depth;
        m_depthState = ResourceState::Undefined;
    }
    m_drawResource = graph.import(m_drawBuffer, m_drawState);
    m_countResource = graph.import(m_countBuffer, m_countState);
    m_depthResource = graph.import(depth, m_depthState);

    const std::uint32_t cullIndex = m_device.bindlessIndex(m_cullBuffers[frameSlot]);
    graph.addPass({.name = "Clear draw count",
                   .accesses = {{m_countResource, ResourceState::CopyTarget}},
                   .record = [this](Rhi::IRhiCommandList& list) { list.fillBuffer(m_countBuffer, 0); },
                   .small = true});
    graph.addPass(
        {.name = "Cull instances",
         .accesses = {{m_countResource, ResourceState::ShaderWrite}, {m_drawResource, ResourceState::ShaderWrite}},
         .record =
             [this, cullIndex](Rhi::IRhiCommandList& list)
         {
             Shader::PushConstants push{};
             push.userData = cullIndex;
             list.setPipeline(m_pipelines.get(*m_cullPipeline));
             list.setPushConstants(push);
             list.dispatch((m_instanceCount + c_cullGroupSize - 1) / c_cullGroupSize);
         },
         .small = true});

    std::vector<GraphAccess> drawAccesses{{m_countResource, ResourceState::IndirectArgs},
                                          {m_drawResource, ResourceState::IndirectArgs},
                                          {m_depthResource, ResourceState::DepthTarget}};
    GraphResource colorResource{};
    if (swapchain == nullptr)
    {
        colorResource = graph.import(color, ResourceState::Undefined);
        drawAccesses.push_back({colorResource, ResourceState::ColorTarget});
    }
    const std::uint32_t frameIndex = m_device.bindlessIndex(m_frameBuffers[frameSlot]);
    graph.addPass({.name = "Draw instances",
                   .accesses = std::move(drawAccesses),
                   .record = [this, swapchain, color, depth, clear, frameIndex](Rhi::IRhiCommandList& list)
                   {
                       if (swapchain != nullptr)
                       {
                           list.beginRendering(*swapchain, clear, depth, 0.0F);
                       }
                       else
                       {
                           list.beginRendering(Rhi::RenderTargets{
                               .color = color, .clearColor = clear, .depth = depth, .clearDepth = 0.0F});
                       }
                       Shader::PushConstants push{};
                       push.frameBuffer = frameIndex;
                       push.instanceBuffer = m_device.bindlessIndex(m_instanceBuffer);
                       push.userData = m_device.bindlessIndex(m_meshBuffer);
                       list.setPipeline(m_pipelines.get(*m_drawPipeline));
                       list.setPushConstants(push);
                       list.drawIndirectCount(m_drawBuffer, m_countBuffer, m_desc.instanceCapacity);
                       list.endRendering();
                   }});
}

void GpuScene::endFrame(const RenderGraph& graph)
{
    m_drawState = graph.finalState(m_drawResource);
    m_countState = graph.finalState(m_countResource);
    m_depthState = graph.finalState(m_depthResource);
}
} // namespace Meow2::Renderer
