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
constexpr std::uint32_t c_maxMaterials = 4096;
constexpr std::uint32_t c_cullGroupSize = 64; // [numthreads] in GpuDriven.hlsl
} // namespace

Shader::PackedVertex packVertex(const Math::Vec3& position, const Math::Vec3& normal, const Math::Vec2& uv,
                                const Math::Aabb& bounds)
{
    const Math::QuantizedPosition quantized = Math::quantizePosition(position, bounds);
    Shader::PackedVertex vertex{};
    vertex.xy = std::uint32_t{quantized.components[0]} | (std::uint32_t{quantized.components[1]} << 16U);
    vertex.z = quantized.components[2];
    vertex.normal = Math::packOctahedral(normal);
    vertex.uv = std::uint32_t{Math::toHalf(uv.x)} | (std::uint32_t{Math::toHalf(uv.y)} << 16U);
    return vertex;
}

MeshData makeMesh(std::span<const Math::Vec3> positions, std::span<const Math::Vec3> normals,
                  std::span<const Math::Vec2> uvs, std::span<const std::uint32_t> indices, const Math::Aabb& bounds)
{
    MEOW2_ASSERT(normals.size() == positions.size() && uvs.size() == positions.size(),
                 "Every vertex needs a position, a normal and UVs");
    MeshData mesh;
    mesh.bounds = bounds;
    mesh.vertices.reserve(positions.size());
    for (std::size_t i = 0; i < positions.size(); ++i)
    {
        mesh.vertices.push_back(packVertex(positions[i], normals[i], uvs[i], bounds));
    }
    mesh.indices.assign(indices.begin(), indices.end());
    return mesh;
}

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
            const Math::Vec2 uv{(a + 1.0F) * 0.5F, (1.0F - b) * 0.5F};
            mesh.vertices.push_back(packVertex(position, face.normal, uv, mesh.bounds));
        }
        for (const std::uint32_t index : {0U, 1U, 2U, 0U, 2U, 3U})
        {
            mesh.indices.push_back(base + index);
        }
    }
    return mesh;
}

GpuScene::GpuScene(RenderDevice& device, PipelineLibrary& pipelines, const GpuSceneDesc& desc)
    : m_device(device)
    , m_pipelines(pipelines)
    , m_desc(desc)
{
}

Result<std::unique_ptr<GpuScene>> GpuScene::create(RenderDevice& device, PipelineLibrary& pipelines,
                                                   const GpuSceneDesc& desc)
{
    MEOW2_ASSERT(desc.instanceCapacity > 0, "A scene needs room for instances");
    auto scene = std::make_unique<GpuScene>(device, pipelines, desc);
    if (Result<void> made = scene->createBuffers(device.device()); !made)
    {
        return Unexpected<Error>{made.error()};
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

    // Registered after the pipeline library, so its pipelines are back before the scene restores.
    GpuScene* const self = scene.get();
    scene->m_deviceUser = device.addUser(
        "GpuScene", [self](Rhi::IRhiDevice& old) { self->destroyBuffers(old); },
        [self](Rhi::IRhiDevice& fresh) -> Result<void>
        {
            if (Result<void> made = self->restoreBuffers(fresh); !made)
            {
                self->destroyBuffers(fresh); // a failed restore isn't released
                return made;
            }
            return {};
        });
    return scene;
}

GpuScene::~GpuScene()
{
    if (m_device.hasDevice())
    {
        destroyBuffers(m_device.device());
    }
}

Result<void> GpuScene::createBuffers(Rhi::IRhiDevice& device)
{
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
    const std::uint64_t capacity = m_desc.instanceCapacity;
    for (Result<void> made :
         {makeBuffer(m_instanceBuffer, capacity * sizeof(Shader::InstanceData), Rhi::RhiMemory::Upload, "Instances"),
          makeBuffer(m_meshBuffer, std::uint64_t{c_maxMeshes} * sizeof(Shader::MeshDraw), Rhi::RhiMemory::Upload,
                     "Meshes"),
          makeBuffer(m_drawBuffer, capacity * sizeof(Shader::IndirectDraw), Rhi::RhiMemory::GpuOnly, "Draws"),
          makeBuffer(m_countBuffer, sizeof(std::uint32_t), Rhi::RhiMemory::GpuOnly, "Draw count"),
          makeBuffer(m_materialBuffer, std::uint64_t{c_maxMaterials} * sizeof(Shader::MaterialData),
                     Rhi::RhiMemory::Upload, "Materials")})
    {
        if (!made)
        {
            return made;
        }
    }
    for (std::uint32_t slot = 0; slot < Rhi::c_maxFramesInFlight; ++slot)
    {
        if (Result<void> made = makeBuffer(m_frameBuffers[slot], sizeof(Shader::FrameConstants), Rhi::RhiMemory::Upload,
                                           "Frame constants");
            !made)
        {
            return made;
        }
        if (Result<void> made = makeBuffer(m_cullBuffers[slot], sizeof(Shader::CullConstants), Rhi::RhiMemory::Upload,
                                           "Cull constants");
            !made)
        {
            return made;
        }
    }
    return {};
}

void GpuScene::destroyBuffers(Rhi::IRhiDevice& device)
{
    // Unset handles are ignored, so this also cleans up after a creation that stopped halfway.
    for (Rhi::BufferHandle* buffer :
         {&m_instanceBuffer, &m_meshBuffer, &m_drawBuffer, &m_countBuffer, &m_materialBuffer})
    {
        device.destroy(*buffer);
        *buffer = {};
    }
    for (std::uint32_t slot = 0; slot < Rhi::c_maxFramesInFlight; ++slot)
    {
        device.destroy(m_frameBuffers[slot]);
        device.destroy(m_cullBuffers[slot]);
        m_frameBuffers[slot] = {};
        m_cullBuffers[slot] = {};
    }
    for (const Rhi::BufferHandle buffer : m_meshResources)
    {
        device.destroy(buffer);
    }
    m_meshResources.clear();
    for (TextureRecord& record : m_textures)
    {
        device.destroy(record.texture);
        device.destroy(record.staging);
        record.texture = {};
        record.staging = {};
    }
    m_pendingTextures.clear();
    for (const Rhi::BufferHandle buffer : m_spentStaging)
    {
        device.destroy(buffer);
    }
    m_spentStaging.clear();
}

Result<void> GpuScene::restoreBuffers(Rhi::IRhiDevice& device)
{
    if (Result<void> made = createBuffers(device); !made)
    {
        return made;
    }
    // Mesh records hold heap indices, which are different on the new device: upload every mesh again.
    for (std::uint32_t mesh = 0; mesh < m_meshes.size(); ++mesh)
    {
        if (Result<void> uploaded = uploadMesh(device, m_meshes[mesh], mesh); !uploaded)
        {
            return uploaded;
        }
    }
    std::memcpy(device.mappedMemory(m_instanceBuffer).data(), m_instances.data(),
                m_instances.size() * sizeof(Shader::InstanceData));
    // Textures are copied again by the next frame; material records name their new heap indices.
    for (std::uint32_t texture = 0; texture < m_textures.size(); ++texture)
    {
        if (Result<void> made = createTexture(device, texture); !made)
        {
            return made;
        }
    }
    for (std::uint32_t material = 0; material < m_materials.size(); ++material)
    {
        writeMaterial(device, material);
    }
    // The new buffers and the next depth target start with no contents.
    m_drawState = Rhi::ResourceState::Undefined;
    m_countState = Rhi::ResourceState::Undefined;
    m_depthState = Rhi::ResourceState::Undefined;
    m_lastDepth = {};
    return {};
}

Result<void> GpuScene::uploadMesh(Rhi::IRhiDevice& device, const MeshData& mesh, std::uint32_t index)
{
    const std::span<const std::byte> vertices = std::as_bytes(std::span{mesh.vertices});
    const std::span<const std::byte> indices = std::as_bytes(std::span{mesh.indices});
    Result<Rhi::BufferHandle> vertexBuffer = device.createBuffer({.size = vertices.size(),
                                                                  .memory = Rhi::RhiMemory::Upload,
                                                                  .tag = Memory::MemTag::Renderer,
                                                                  .name = "Mesh vertices"});
    if (!vertexBuffer)
    {
        return Unexpected<Error>{vertexBuffer.error()};
    }
    m_meshResources.push_back(*vertexBuffer);
    Result<Rhi::BufferHandle> indexBuffer = device.createBuffer({.size = indices.size(),
                                                                 .memory = Rhi::RhiMemory::Upload,
                                                                 .tag = Memory::MemTag::Renderer,
                                                                 .name = "Mesh indices"});
    if (!indexBuffer)
    {
        return Unexpected<Error>{indexBuffer.error()};
    }
    m_meshResources.push_back(*indexBuffer);
    std::memcpy(device.mappedMemory(*vertexBuffer).data(), vertices.data(), vertices.size());
    std::memcpy(device.mappedMemory(*indexBuffer).data(), indices.data(), indices.size());

    const Math::Vec3& extents = mesh.bounds.extents;
    Shader::MeshDraw draw{};
    draw.vertexBuffer = device.bindlessIndex(*vertexBuffer);
    draw.indexBuffer = device.bindlessIndex(*indexBuffer);
    draw.firstIndex = 0;
    draw.indexCount = static_cast<std::uint32_t>(mesh.indices.size());
    draw.boundsCenter = {mesh.bounds.center.x, mesh.bounds.center.y, mesh.bounds.center.z,
                         std::sqrt(extents.x * extents.x + extents.y * extents.y + extents.z * extents.z)};
    draw.boundsExtents = {extents.x, extents.y, extents.z, 0.0F};
    std::memcpy(device.mappedMemory(m_meshBuffer).data() + std::size_t{index} * sizeof(draw), &draw, sizeof(draw));
    return {};
}

Result<void> GpuScene::createTexture(Rhi::IRhiDevice& device, std::uint32_t index)
{
    TextureRecord& record = m_textures[index];
    Result<Rhi::TextureHandle> texture =
        device.createTexture({.width = record.image.width,
                              .height = record.image.height,
                              .format = record.srgb ? Rhi::RhiFormat::Rgba8Srgb : Rhi::RhiFormat::Rgba8Unorm,
                              .tag = Memory::MemTag::Renderer,
                              .name = record.name});
    if (!texture)
    {
        return Unexpected<Error>{texture.error()};
    }
    record.texture = *texture;
    Result<Rhi::BufferHandle> staging = device.createBuffer({.size = record.image.pixels.size(),
                                                             .memory = Rhi::RhiMemory::Upload,
                                                             .tag = Memory::MemTag::Renderer,
                                                             .name = "Texture upload"});
    if (!staging)
    {
        return Unexpected<Error>{staging.error()};
    }
    record.staging = *staging;
    std::memcpy(device.mappedMemory(*staging).data(), record.image.pixels.data(), record.image.pixels.size());
    m_pendingTextures.push_back(index);
    return {};
}

Result<std::uint32_t> GpuScene::addTexture(const Assets::Image& image, bool srgb, std::string name)
{
    if (image.width == 0 || image.height == 0 || image.pixels.size() != std::size_t{image.width} * image.height * 4)
    {
        return fail(ErrorCode::InvalidArgument, std::format("Texture '{}' has no pixels", name));
    }
    const auto index = static_cast<std::uint32_t>(m_textures.size());
    m_textures.push_back({.image = image, .srgb = srgb, .name = std::move(name)});
    if (Result<void> made = createTexture(m_device.device(), index); !made)
    {
        m_device.device().destroy(m_textures.back().texture);
        m_textures.pop_back();
        return Unexpected<Error>{made.error()};
    }
    return index;
}

void GpuScene::writeMaterial(Rhi::IRhiDevice& device, std::uint32_t index)
{
    const MaterialDesc& material = m_materials[index];
    Shader::MaterialData data{};
    data.baseColorFactor = {material.baseColorFactor.r, material.baseColorFactor.g, material.baseColorFactor.b,
                            material.baseColorFactor.a};
    data.parameters = {material.alphaCutoff, material.metallic, material.roughness, 1.0F};
    const bool textured =
        material.baseColorTexture >= 0 && static_cast<std::size_t>(material.baseColorTexture) < m_textures.size();
    data.baseColorTexture =
        textured ? device.bindlessIndex(m_textures[static_cast<std::size_t>(material.baseColorTexture)].texture)
                 : Rhi::c_invalidBindlessIndex;
    data.sampler = Rhi::c_defaultSamplerIndex;
    data.flags = material.alphaMask ? MEOW2_MATERIAL_ALPHA_MASK : 0U;
    std::memcpy(device.mappedMemory(m_materialBuffer).data() + std::size_t{index} * sizeof(data), &data, sizeof(data));
}

Result<std::uint32_t> GpuScene::addMaterial(const MaterialDesc& material)
{
    if (m_materials.size() == c_maxMaterials)
    {
        return fail(ErrorCode::OutOfMemory, "Too many materials");
    }
    const auto index = static_cast<std::uint32_t>(m_materials.size());
    m_materials.push_back(material);
    writeMaterial(m_device.device(), index);
    return index;
}

Result<std::uint32_t> GpuScene::addMesh(const MeshData& mesh)
{
    if (m_meshes.size() == c_maxMeshes)
    {
        return fail(ErrorCode::OutOfMemory, "Too many meshes");
    }
    const auto index = static_cast<std::uint32_t>(m_meshes.size());
    if (Result<void> uploaded = uploadMesh(m_device.device(), mesh, index); !uploaded)
    {
        return Unexpected<Error>{uploaded.error()};
    }
    m_meshes.push_back(mesh); // kept for the next device, after a backend switch
    return index;
}

void GpuScene::setInstances(std::span<const Shader::InstanceData> instances)
{
    MEOW2_ASSERT(instances.size() <= m_desc.instanceCapacity, "More instances than GpuSceneDesc::instanceCapacity");
    m_instances.assign(instances.begin(), instances.end()); // kept for the next device, after a backend switch
    std::memcpy(m_device.device().mappedMemory(m_instanceBuffer).data(), instances.data(), instances.size_bytes());
}

void GpuScene::setView(std::uint32_t frameSlot, const Shader::FrameConstants& frame, const Math::Frustum& frustum)
{
    Shader::FrameConstants constants = frame;
    constants.materialBuffer = m_device.device().bindlessIndex(m_materialBuffer);
    constants.materialCount = static_cast<std::uint32_t>(m_materials.size());
    std::memcpy(m_device.device().mappedMemory(m_frameBuffers[frameSlot]).data(), &constants, sizeof(constants));
    Shader::CullConstants cull{};
    cull.plane0 = frustum.planes[0];
    cull.plane1 = frustum.planes[1];
    cull.plane2 = frustum.planes[2];
    cull.plane3 = frustum.planes[3];
    cull.plane4 = frustum.planes[4];
    cull.plane5 = frustum.planes[5];
    cull.instanceBuffer = m_device.device().bindlessIndex(m_instanceBuffer);
    cull.meshBuffer = m_device.device().bindlessIndex(m_meshBuffer);
    cull.drawBuffer = m_device.device().bindlessStorageIndex(m_drawBuffer);
    cull.countBuffer = m_device.device().bindlessStorageIndex(m_countBuffer);
    cull.instanceCount = instanceCount();
    std::memcpy(m_device.device().mappedMemory(m_cullBuffers[frameSlot]).data(), &cull, sizeof(cull));
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

    // New textures are copied first. The copies run before every later pass, which see them as shader inputs.
    if (!m_pendingTextures.empty())
    {
        std::vector<std::pair<Rhi::BufferHandle, Rhi::TextureHandle>> uploads;
        for (const std::uint32_t texture : m_pendingTextures)
        {
            TextureRecord& record = m_textures[texture];
            uploads.emplace_back(record.staging, record.texture);
            m_spentStaging.push_back(std::exchange(record.staging, {}));
        }
        m_pendingTextures.clear();
        graph.addPass({.name = "Upload textures",
                       .accesses = {},
                       .record =
                           [uploads = std::move(uploads)](Rhi::IRhiCommandList& list)
                       {
                           for (const auto& [staging, texture] : uploads)
                           {
                               list.barrier(texture, ResourceState::Undefined, ResourceState::CopyTarget);
                               list.copyBufferToTexture(staging, texture);
                               list.barrier(texture, ResourceState::CopyTarget, ResourceState::ShaderRead);
                           }
                       },
                       .small = true});
    }

    const std::uint32_t cullIndex = m_device.device().bindlessIndex(m_cullBuffers[frameSlot]);
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
             list.dispatch((instanceCount() + c_cullGroupSize - 1) / c_cullGroupSize);
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
    Rhi::IRhiDevice& device = m_device.device();
    Shader::PushConstants push{};
    push.frameBuffer = device.bindlessIndex(m_frameBuffers[frameSlot]);
    push.instanceBuffer = device.bindlessIndex(m_instanceBuffer);
    push.userData = device.bindlessIndex(m_meshBuffer);
    graph.addPass({.name = "Draw instances",
                   .accesses = std::move(drawAccesses),
                   .record = [this, swapchain, color, depth, clear, push](Rhi::IRhiCommandList& list)
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
                       list.setPipeline(m_pipelines.get(*m_drawPipeline));
                       list.setPushConstants(push);
                       list.drawIndirectCount(m_drawBuffer, m_countBuffer, m_desc.instanceCapacity);
                       list.endRendering();
                   }});
}

void GpuScene::endFrame(const RenderGraph& graph)
{
    // Their copies are recorded; the device keeps them until the GPU has run them (deferred destruction).
    for (const Rhi::BufferHandle buffer : m_spentStaging)
    {
        m_device.device().destroy(buffer);
    }
    m_spentStaging.clear();
    m_drawState = graph.finalState(m_drawResource);
    m_countState = graph.finalState(m_countResource);
    m_depthState = graph.finalState(m_depthResource);
}
} // namespace Meow2::Renderer
