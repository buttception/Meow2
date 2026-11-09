/// @file
/// @brief GpuScene: the culling and drawing passes.

#include "Meow2/Graphics/Renderer/GpuScene.h"

#include "Meow2/Core/Assert.h"
#include "Meow2/Math/Packing.h"

#include <DirectXMath.h>

#include <algorithm>
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

/// @brief The RHI format of a cooked texture.
Rhi::RhiFormat rhiFormat(Assets::CookedFormat format)
{
    switch (format)
    {
        case Assets::CookedFormat::Rgba8Unorm:
            return Rhi::RhiFormat::Rgba8Unorm;
        case Assets::CookedFormat::Rgba8Srgb:
            return Rhi::RhiFormat::Rgba8Srgb;
        case Assets::CookedFormat::Bc7Unorm:
            return Rhi::RhiFormat::Bc7Unorm;
        case Assets::CookedFormat::Bc7Srgb:
            return Rhi::RhiFormat::Bc7Srgb;
        case Assets::CookedFormat::Bc5Unorm:
            return Rhi::RhiFormat::Bc5Unorm;
    }
    return Rhi::RhiFormat::Rgba8Unorm;
}
} // namespace

Shader::PackedVertex packVertex(const VertexAttributes& vertex, const Math::Aabb& bounds)
{
    const Math::QuantizedPosition quantized = Math::quantizePosition(vertex.position, bounds);
    Shader::PackedVertex packed{};
    packed.xy = std::uint32_t{quantized.components[0]} | (std::uint32_t{quantized.components[1]} << 16U);
    // The bitangent's sign fits in z's unused high half.
    packed.z = quantized.components[2] | (vertex.tangent.w < 0.0F ? 0x80000000U : 0U);
    packed.normal = Math::packOctahedral(vertex.normal);
    packed.tangent = Math::packOctahedral(Math::Vec3{vertex.tangent.x, vertex.tangent.y, vertex.tangent.z});
    packed.uv = std::uint32_t{Math::toHalf(vertex.uv.x)} | (std::uint32_t{Math::toHalf(vertex.uv.y)} << 16U);
    packed.color = std::uint32_t{Math::packUnorm8(vertex.color.r)} |
                   (std::uint32_t{Math::packUnorm8(vertex.color.g)} << 8U) |
                   (std::uint32_t{Math::packUnorm8(vertex.color.b)} << 16U) |
                   (std::uint32_t{Math::packUnorm8(vertex.color.a)} << 24U);
    return packed;
}

MeshData makeMesh(const MeshArrays& arrays, const Math::Aabb& bounds)
{
    const std::size_t count = arrays.positions.size();
    MEOW2_ASSERT(arrays.normals.size() == count && arrays.uvs.size() == count &&
                     (arrays.tangents.empty() || arrays.tangents.size() == count) &&
                     (arrays.colors.empty() || arrays.colors.size() == count),
                 "Every vertex array needs one element per position");
    MeshData mesh;
    mesh.bounds = bounds;
    mesh.vertices.reserve(count);
    for (std::size_t i = 0; i < count; ++i)
    {
        VertexAttributes vertex;
        vertex.position = arrays.positions[i];
        vertex.normal = arrays.normals[i];
        vertex.uv = arrays.uvs[i];
        if (!arrays.tangents.empty())
        {
            vertex.tangent = arrays.tangents[i];
        }
        if (!arrays.colors.empty())
        {
            vertex.color = arrays.colors[i];
        }
        mesh.vertices.push_back(packVertex(vertex, bounds));
    }
    mesh.indices.assign(arrays.indices.begin(), arrays.indices.end());
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
            // U runs along face.u and V against face.v, so the bitangent (towards decreasing V) is face.v.
            VertexAttributes vertex;
            vertex.position = position;
            vertex.normal = face.normal;
            vertex.uv = Math::Vec2{(a + 1.0F) * 0.5F, (1.0F - b) * 0.5F};
            const DirectX::XMVECTOR crossed = DirectX::XMVector3Cross(Math::load(face.normal), Math::load(face.u));
            const float sign =
                DirectX::XMVectorGetX(DirectX::XMVector3Dot(crossed, Math::load(face.v))) < 0.0F ? -1.0F : 1.0F;
            vertex.tangent = Math::Vec4{face.u.x, face.u.y, face.u.z, sign};
            mesh.vertices.push_back(packVertex(vertex, mesh.bounds));
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
    // One pipeline per draw list: they differ only in which side is culled (see MEOW2_DRAW_LIST_*).
    constexpr std::array<std::pair<Rhi::CullMode, const char*>, MEOW2_DRAW_LIST_COUNT> c_lists{
        std::pair{Rhi::CullMode::Back, "Draw instances"}, std::pair{Rhi::CullMode::Front, "Draw mirrored instances"},
        std::pair{Rhi::CullMode::None, "Draw double-sided instances"}};
    for (std::uint32_t list = 0; list < MEOW2_DRAW_LIST_COUNT; ++list)
    {
        Result<PipelineId> draw = pipelines.add(GraphicsPipelineRequest{.shader = desc.shader,
                                                                        .colorFormat = desc.colorFormat,
                                                                        .depthFormat = Rhi::RhiFormat::D32Float,
                                                                        .cull = c_lists[list].first,
                                                                        .name = c_lists[list].second});
        if (!draw)
        {
            return Unexpected<Error>{draw.error()};
        }
        scene->m_drawPipelines[list] = *draw;
    }
    // Blended surfaces test depth but don't write it, so what's behind them stays visible through later ones. Both
    // sides are drawn: a sorted transparent surface rarely gains from culling, and it keeps mirrored ones right.
    Result<PipelineId> blend = pipelines.add(GraphicsPipelineRequest{.shader = desc.shader,
                                                                     .vertexEntry = "vsBlend",
                                                                     .colorFormat = desc.colorFormat,
                                                                     .depthFormat = Rhi::RhiFormat::D32Float,
                                                                     .depthWrite = false,
                                                                     .cull = Rhi::CullMode::None,
                                                                     .blend = Rhi::BlendMode::Alpha,
                                                                     .name = "Draw blended instances"});
    if (!blend)
    {
        return Unexpected<Error>{blend.error()};
    }
    scene->m_blendPipeline = *blend;

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
          // Every list has room for every instance: an instance lands in exactly one, but which isn't known up front.
          makeBuffer(m_drawBuffer, capacity * MEOW2_DRAW_LIST_COUNT * sizeof(Shader::IndirectDraw),
                     Rhi::RhiMemory::GpuOnly, "Draws"),
          makeBuffer(m_countBuffer, MEOW2_DRAW_LIST_COUNT * sizeof(std::uint32_t), Rhi::RhiMemory::GpuOnly,
                     "Draw counts"),
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
    for (SamplerRecord& record : m_samplers)
    {
        device.destroy(record.sampler);
        record.sampler = {};
    }
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
    for (SamplerRecord& record : m_samplers)
    {
        Result<Rhi::SamplerHandle> sampler = device.createSampler(record.desc);
        if (!sampler)
        {
            return Unexpected<Error>{sampler.error()};
        }
        record.sampler = *sampler;
    }
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
    const Assets::CookedMip& top = record.cooked.mips.front();
    Result<Rhi::TextureHandle> texture =
        device.createTexture({.width = top.width,
                              .height = top.height,
                              .format = rhiFormat(record.cooked.format),
                              .mipLevels = static_cast<std::uint32_t>(record.cooked.mips.size()),
                              .tag = Memory::MemTag::Renderer,
                              .name = record.name});
    if (!texture)
    {
        return Unexpected<Error>{texture.error()};
    }
    record.texture = *texture;
    // Every level goes into one staging buffer, one after another; the upload pass copies each from its offset.
    // Each level's size is a multiple of 16 bytes for block formats (whole blocks), the offset alignment Vulkan asks
    // for; D3D12 copies at any offset with the unrestricted copy pitch the requirements check asks for.
    std::uint64_t bytes = 0;
    for (const Assets::CookedMip& level : record.cooked.mips)
    {
        bytes += level.bytes.size();
    }
    Result<Rhi::BufferHandle> staging = device.createBuffer(
        {.size = bytes, .memory = Rhi::RhiMemory::Upload, .tag = Memory::MemTag::Renderer, .name = "Texture upload"});
    if (!staging)
    {
        return Unexpected<Error>{staging.error()};
    }
    record.staging = *staging;
    std::byte* const mapped = device.mappedMemory(*staging).data();
    std::uint64_t offset = 0;
    for (const Assets::CookedMip& level : record.cooked.mips)
    {
        std::memcpy(mapped + offset, level.bytes.data(), level.bytes.size());
        offset += level.bytes.size();
    }
    m_pendingTextures.push_back(index);
    return {};
}

Result<std::uint32_t> GpuScene::addTexture(const Assets::Image& image, bool srgb, std::string name)
{
    if (image.width == 0 || image.height == 0 || image.pixels.size() != std::size_t{image.width} * image.height * 4)
    {
        return fail(ErrorCode::InvalidArgument, std::format("Texture '{}' has no pixels", name));
    }
    Result<Assets::CookedTexture> cooked =
        Assets::cookTexture(image, srgb ? Assets::TextureKind::Color : Assets::TextureKind::Data, false);
    if (!cooked)
    {
        return Unexpected<Error>{cooked.error()};
    }
    return addTexture(std::move(*cooked), std::move(name));
}

Result<std::uint32_t> GpuScene::addTexture(Assets::CookedTexture texture, std::string name)
{
    if (texture.mips.empty() || texture.mips.front().width == 0 || texture.mips.front().height == 0)
    {
        return fail(ErrorCode::InvalidArgument, std::format("Texture '{}' has no levels", name));
    }
    const auto index = static_cast<std::uint32_t>(m_textures.size());
    // The levels are kept: a backend switch uploads them again without cooking again.
    m_textures.push_back({.cooked = std::move(texture), .name = std::move(name)});
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
    // Heap indices of a slot's texture and sampler; an index out of range means none (0 = white or a flat normal).
    const auto slot = [&](const MaterialTexture& texture, std::uint32_t& textureIndex, std::uint32_t& samplerIndex)
    {
        const bool hasTexture = texture.texture >= 0 && static_cast<std::size_t>(texture.texture) < m_textures.size();
        textureIndex = hasTexture ? device.bindlessIndex(m_textures[static_cast<std::size_t>(texture.texture)].texture)
                                  : Rhi::c_invalidBindlessIndex;
        const bool hasSampler = texture.sampler >= 0 && static_cast<std::size_t>(texture.sampler) < m_samplers.size();
        samplerIndex = hasSampler ? device.bindlessIndex(m_samplers[static_cast<std::size_t>(texture.sampler)].sampler)
                                  : Rhi::c_defaultSamplerIndex;
    };
    // KHR_texture_transform as two rows of translation × rotation × scale (the extension's GLSL, written out).
    const auto uvRows = [](const MaterialTexture& texture, Shader::float4& row0, Shader::float4& row1)
    {
        const float c = std::cos(texture.uvRotation);
        const float s = std::sin(texture.uvRotation);
        row0 = {c * texture.uvScale.x, -s * texture.uvScale.y, texture.uvOffset.x, 0.0F};
        row1 = {s * texture.uvScale.x, c * texture.uvScale.y, texture.uvOffset.y, 0.0F};
    };
    Shader::MaterialData data{};
    data.baseColorFactor = {material.baseColorFactor.r, material.baseColorFactor.g, material.baseColorFactor.b,
                            material.baseColorFactor.a};
    data.emissiveFactor = {material.emissiveFactor.r, material.emissiveFactor.g, material.emissiveFactor.b,
                           material.occlusionStrength};
    data.parameters = {material.alphaCutoff, material.metallic, material.roughness, material.normalScale};
    slot(material.baseColor, data.baseColorTexture, data.baseColorSampler);
    slot(material.metallicRoughness, data.metallicRoughnessTexture, data.metallicRoughnessSampler);
    slot(material.normal, data.normalTexture, data.normalSampler);
    slot(material.occlusion, data.occlusionTexture, data.occlusionSampler);
    slot(material.emissive, data.emissiveTexture, data.emissiveSampler);
    uvRows(material.baseColor, data.baseColorUv0, data.baseColorUv1);
    uvRows(material.metallicRoughness, data.metallicRoughnessUv0, data.metallicRoughnessUv1);
    uvRows(material.normal, data.normalUv0, data.normalUv1);
    uvRows(material.occlusion, data.occlusionUv0, data.occlusionUv1);
    uvRows(material.emissive, data.emissiveUv0, data.emissiveUv1);
    data.flags = (material.alphaMask ? MEOW2_MATERIAL_ALPHA_MASK : 0U) |
                 (material.doubleSided ? MEOW2_MATERIAL_DOUBLE_SIDED : 0U) |
                 (material.blend ? MEOW2_MATERIAL_BLEND : 0U);
    std::memcpy(device.mappedMemory(m_materialBuffer).data() + std::size_t{index} * sizeof(data), &data, sizeof(data));
}

Result<std::uint32_t> GpuScene::addSampler(const Rhi::SamplerDesc& desc)
{
    for (std::uint32_t i = 0; i < m_samplers.size(); ++i)
    {
        const Rhi::SamplerDesc& known = m_samplers[i].desc;
        if (known.filter == desc.filter && known.addressU == desc.addressU && known.addressV == desc.addressV &&
            known.maxAnisotropy == desc.maxAnisotropy)
        {
            return i; // glTF files repeat the same few samplers; one heap slot each is enough
        }
    }
    Result<Rhi::SamplerHandle> sampler = m_device.device().createSampler(desc);
    if (!sampler)
    {
        return Unexpected<Error>{sampler.error()};
    }
    m_samplers.push_back({.desc = desc, .sampler = *sampler});
    return static_cast<std::uint32_t>(m_samplers.size() - 1);
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
    m_blended.clear();
    m_blendOrder.clear(); // its indices were into the old instances; the next setView() sorts the new ones
    for (std::uint32_t i = 0; i < m_instances.size(); ++i)
    {
        Shader::InstanceData& instance = m_instances[i];
        // A negative determinant mirrors the mesh, which reverses its triangles' winding on screen.
        const DirectX::XMMATRIX world = DirectX::XMLoadFloat4x4(&instance.world);
        const bool mirrored = DirectX::XMVectorGetX(DirectX::XMMatrixDeterminant(world)) < 0.0F;
        instance.flags = mirrored ? MEOW2_INSTANCE_MIRRORED : 0U;
        if (instance.material < m_materials.size() && m_materials[instance.material].blend)
        {
            m_blended.push_back(i);
        }
    }
    std::memcpy(m_device.device().mappedMemory(m_instanceBuffer).data(), m_instances.data(),
                m_instances.size() * sizeof(Shader::InstanceData));
}

void GpuScene::setView(std::uint32_t frameSlot, const Shader::FrameConstants& frame, const Math::Frustum& frustum)
{
    using namespace DirectX;
    Shader::FrameConstants constants = frame;
    constants.materialBuffer = m_device.device().bindlessIndex(m_materialBuffer);
    constants.materialCount = static_cast<std::uint32_t>(m_materials.size());
    Math::Vec3 sun;
    XMStoreFloat3(&sun, XMVector3Normalize(Math::load(m_lighting.sunDirection)));
    constants.sunDirection = {sun.x, sun.y, sun.z, m_lighting.sunIntensity};
    constants.sunColor = {m_lighting.sunColor.r, m_lighting.sunColor.g, m_lighting.sunColor.b, m_lighting.exposure};
    constants.skyColor = {m_lighting.skyColor.r, m_lighting.skyColor.g, m_lighting.skyColor.b, 0.0F};
    constants.groundColor = {m_lighting.groundColor.r, m_lighting.groundColor.g, m_lighting.groundColor.b, 0.0F};
    if (m_sky != nullptr)
    {
        const SkyShadingInputs inputs = m_sky->shadingInputs();
        constants.environmentMap = inputs.environmentMap;
        constants.environmentLevels = inputs.environmentLevels;
        constants.irradianceBuffer = inputs.irradianceBuffer;
        constants.transmittanceLut = inputs.transmittanceLut;
        constants.environmentSampler = inputs.environmentSampler;
        constants.groundColor.w = m_sky->settings().cameraAltitudeKm;
    }
    std::memcpy(m_device.device().mappedMemory(m_frameBuffers[frameSlot]).data(), &constants, sizeof(constants));

    // Blended instances are drawn farthest first, so each mixes with what's behind it, by the distance from the
    // camera to the box centre. Sorting whole instances is the usual compromise: overlapping
    // surfaces of one mesh can still come out in the wrong order.
    std::vector<std::pair<float, std::uint32_t>> distances;
    distances.reserve(m_blended.size());
    const XMVECTOR camera = XMVectorSet(frame.cameraPosition.x, frame.cameraPosition.y, frame.cameraPosition.z, 0.0F);
    for (const std::uint32_t index : m_blended)
    {
        const Shader::InstanceData& instance = m_instances[index];
        const Math::Vec3 local = instance.mesh < m_meshes.size() ? m_meshes[instance.mesh].bounds.center : Math::Vec3{};
        const XMVECTOR center = XMVector3Transform(Math::load(local), XMLoadFloat4x4(&instance.world));
        distances.emplace_back(XMVectorGetX(XMVector3LengthSq(XMVectorSubtract(center, camera))), index);
    }
    std::sort(distances.begin(), distances.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    m_blendOrder.clear();
    for (const auto& [distance, index] : distances)
    {
        m_blendOrder.push_back(index);
    }

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
    cull.materialBuffer = constants.materialBuffer;
    cull.materialCount = constants.materialCount;
    cull.drawCapacity = m_desc.instanceCapacity;
    std::memcpy(m_device.device().mappedMemory(m_cullBuffers[frameSlot]).data(), &cull, sizeof(cull));
}

GraphResource GpuScene::addPasses(RenderGraph& graph, std::uint32_t frameSlot, Rhi::IRhiSwapchain* swapchain,
                                  Rhi::TextureHandle color, Rhi::TextureHandle depth, const Math::Color& clear)
{
    using Rhi::ResourceState;
    if (depth != m_lastDepth) // a new target, e.g. after a resize
    {
        m_lastDepth = depth;
        m_depthState = ResourceState::Undefined;
    }
    m_drawResource = graph.import(m_drawBuffer, m_drawState, "Draw commands");
    m_countResource = graph.import(m_countBuffer, m_countState, "Draw counts");
    m_depthResource = graph.import(depth, m_depthState, "Depth");

    // New textures are copied first. The copies run before every later pass, which see them as shader inputs.
    if (!m_pendingTextures.empty())
    {
        /// One texture's copy: where each level starts in its staging buffer.
        struct Upload
        {
            Rhi::BufferHandle staging;
            Rhi::TextureHandle texture;
            std::vector<std::uint64_t> offsets;
        };
        std::vector<Upload> uploads;
        for (const std::uint32_t texture : m_pendingTextures)
        {
            TextureRecord& record = m_textures[texture];
            Upload upload{.staging = record.staging, .texture = record.texture, .offsets = {}};
            std::uint64_t offset = 0;
            for (const Assets::CookedMip& level : record.cooked.mips)
            {
                upload.offsets.push_back(offset);
                offset += level.bytes.size();
            }
            uploads.push_back(std::move(upload));
            m_spentStaging.push_back(std::exchange(record.staging, {}));
        }
        m_pendingTextures.clear();
        graph.addPass({.name = "Upload textures",
                       .accesses = {},
                       .record =
                           [uploads = std::move(uploads)](Rhi::IRhiCommandList& list)
                       {
                           for (const Upload& upload : uploads)
                           {
                               list.barrier(upload.texture, ResourceState::Undefined, ResourceState::CopyTarget);
                               for (std::uint32_t level = 0; level < upload.offsets.size(); ++level)
                               {
                                   list.copyBufferToTexture(upload.staging, upload.texture, level,
                                                            upload.offsets[level]);
                               }
                               list.barrier(upload.texture, ResourceState::CopyTarget, ResourceState::ShaderRead);
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

    // Three passes, so debugging tools can show each step's output: everything opaque (clearing the targets), the
    // sky behind it (where depth is still far), then what's blended over both. Each keeps what the one before drew.
    GraphResource colorResource{};
    if (swapchain == nullptr)
    {
        colorResource = graph.import(color, ResourceState::Undefined, "Scene colour");
    }
    const auto targets = [&](bool indirect)
    {
        std::vector<GraphAccess> accesses{{m_depthResource, ResourceState::DepthTarget}};
        if (indirect)
        {
            accesses.push_back({m_countResource, ResourceState::IndirectArgs});
            accesses.push_back({m_drawResource, ResourceState::IndirectArgs});
        }
        if (swapchain == nullptr)
        {
            accesses.push_back({colorResource, ResourceState::ColorTarget});
        }
        return accesses;
    };
    // Starts drawing into the targets: cleared for the first pass, kept for the others.
    const auto begin = [swapchain, color, depth](Rhi::IRhiCommandList& list, std::optional<Math::Color> clearColor,
                                                 std::optional<float> clearDepth)
    {
        if (swapchain != nullptr)
        {
            if (!clearColor)
            {
                list.barrier(*swapchain); // an earlier pass, maybe in another list, drew to this image
            }
            list.beginRendering(*swapchain, clearColor, depth, clearDepth);
        }
        else
        {
            list.beginRendering(
                Rhi::RenderTargets{.color = color, .clearColor = clearColor, .depth = depth, .clearDepth = clearDepth});
        }
    };
    Rhi::IRhiDevice& device = m_device.device();
    Shader::PushConstants push{};
    push.frameBuffer = device.bindlessIndex(m_frameBuffers[frameSlot]);
    push.instanceBuffer = device.bindlessIndex(m_instanceBuffer);
    push.userData = device.bindlessIndex(m_meshBuffer);

    graph.addPass({.name = "Opaque",
                   .accesses = targets(true),
                   .record = [this, begin, clear, push](Rhi::IRhiCommandList& list)
                   {
                       begin(list, clear, 0.0F);
                       const std::uint64_t listBytes =
                           std::uint64_t{m_desc.instanceCapacity} * sizeof(Shader::IndirectDraw);
                       for (std::uint32_t drawList = 0; drawList < MEOW2_DRAW_LIST_COUNT; ++drawList)
                       {
                           list.setPipeline(
                               m_pipelines.get(m_override ? m_override->lists[drawList] : *m_drawPipelines[drawList]));
                           list.setPushConstants(push);
                           list.drawIndirectCount(m_drawBuffer, m_countBuffer, m_desc.instanceCapacity,
                                                  drawList * listBytes, drawList * sizeof(std::uint32_t));
                       }
                       list.endRendering();
                   }});

    if (m_sky != nullptr)
    {
        graph.addPass({.name = "Sky",
                       .accesses = targets(false),
                       .record =
                           [begin, frameSlot, push, sky = m_sky](Rhi::IRhiCommandList& list)
                       {
                           // Behind everything opaque: it only fills pixels whose depth is still far.
                           begin(list, std::nullopt, std::nullopt);
                           sky->drawSky(list, frameSlot, push.frameBuffer);
                           list.endRendering();
                       },
                       .small = true});
    }

    // Blended draws are recorded directly, one per instance, in this frame's order: {instance, index count}.
    std::vector<std::pair<std::uint32_t, std::uint32_t>> blended;
    blended.reserve(m_blendOrder.size());
    for (const std::uint32_t index : m_blendOrder)
    {
        if (index >= m_instances.size())
        {
            continue;
        }
        const std::uint32_t mesh = m_instances[index].mesh;
        if (mesh < m_meshes.size())
        {
            blended.emplace_back(index, static_cast<std::uint32_t>(m_meshes[mesh].indices.size()));
        }
    }
    m_blendDraws = blended; // for overlay passes this frame
    if (!blended.empty())
    {
        graph.addPass({.name = "Blended",
                       .accesses = targets(false),
                       .record =
                           [this, begin, push, blended = std::move(blended)](Rhi::IRhiCommandList& list)
                       {
                           // After everything opaque, so they mix with it; vsBlend reads the instance from g_push.
                           begin(list, std::nullopt, std::nullopt);
                           list.setPipeline(m_pipelines.get(m_override ? m_override->blend : *m_blendPipeline));
                           for (const auto& [instance, indexCount] : blended)
                           {
                               Shader::PushConstants draw = push;
                               draw.instance = instance;
                               list.setPushConstants(draw);
                               list.draw(indexCount);
                           }
                           list.endRendering();
                       },
                       .small = true});
    }
    return colorResource;
}

Result<DrawPipelines> GpuScene::makeDrawPipelines(const DrawPipelineOptions& options)
{
    // The same lists as the scene's own pipelines (see create()), with the variant and states asked for.
    constexpr std::array<std::pair<Rhi::CullMode, const char*>, MEOW2_DRAW_LIST_COUNT> c_lists{
        std::pair{Rhi::CullMode::Back, "instances"}, std::pair{Rhi::CullMode::Front, "mirrored instances"},
        std::pair{Rhi::CullMode::None, "double-sided instances"}};
    DrawPipelines out;
    for (std::uint32_t list = 0; list < MEOW2_DRAW_LIST_COUNT; ++list)
    {
        Result<PipelineId> draw =
            m_pipelines.add(GraphicsPipelineRequest{.shader = m_desc.shader,
                                                    .options = options.options,
                                                    .colorFormat = m_desc.colorFormat,
                                                    .depthFormat = Rhi::RhiFormat::D32Float,
                                                    .depthTest = options.depthTest,
                                                    .depthWrite = options.depthWrite,
                                                    .cull = c_lists[list].first,
                                                    .blend = options.blend,
                                                    .fill = options.fill,
                                                    .name = std::format("{}: {}", options.name, c_lists[list].second)});
        if (!draw)
        {
            return Unexpected<Error>{draw.error()};
        }
        out.lists[list] = *draw;
    }
    Result<PipelineId> blend = m_pipelines.add(GraphicsPipelineRequest{
        .shader = m_desc.shader,
        .options = options.options,
        .vertexEntry = "vsBlend",
        .colorFormat = m_desc.colorFormat,
        .depthFormat = Rhi::RhiFormat::D32Float,
        .depthTest = options.depthTest,
        .depthWrite = false,
        .cull = Rhi::CullMode::None,
        .blend = options.blend == Rhi::BlendMode::Additive ? Rhi::BlendMode::Additive : Rhi::BlendMode::Alpha,
        .fill = options.fill,
        .name = std::format("{}: blended instances", options.name)});
    if (!blend)
    {
        return Unexpected<Error>{blend.error()};
    }
    out.blend = *blend;
    return out;
}

void GpuScene::addOverlayPass(RenderGraph& graph, std::string name, const DrawPipelines& pipelines, GraphResource color,
                              Rhi::TextureHandle colorTexture, Rhi::TextureHandle depth, std::uint32_t frameSlot)
{
    Rhi::IRhiDevice& device = m_device.device();
    Shader::PushConstants push{};
    push.frameBuffer = device.bindlessIndex(m_frameBuffers[frameSlot]);
    push.instanceBuffer = device.bindlessIndex(m_instanceBuffer);
    push.userData = device.bindlessIndex(m_meshBuffer);
    graph.addPass(
        {.name = std::move(name),
         .accesses = {{m_depthResource, Rhi::ResourceState::DepthTarget},
                      {m_countResource, Rhi::ResourceState::IndirectArgs},
                      {m_drawResource, Rhi::ResourceState::IndirectArgs},
                      {color, Rhi::ResourceState::ColorTarget}},
         .record = [this, pipelines, push, colorTexture, depth, blended = m_blendDraws](Rhi::IRhiCommandList& list)
         {
             list.beginRendering(Rhi::RenderTargets{.color = colorTexture, .depth = depth});
             const std::uint64_t listBytes = std::uint64_t{m_desc.instanceCapacity} * sizeof(Shader::IndirectDraw);
             for (std::uint32_t drawList = 0; drawList < MEOW2_DRAW_LIST_COUNT; ++drawList)
             {
                 list.setPipeline(m_pipelines.get(pipelines.lists[drawList]));
                 list.setPushConstants(push);
                 list.drawIndirectCount(m_drawBuffer, m_countBuffer, m_desc.instanceCapacity, drawList * listBytes,
                                        drawList * sizeof(std::uint32_t));
             }
             list.setPipeline(m_pipelines.get(pipelines.blend));
             for (const auto& [instance, indexCount] : blended)
             {
                 Shader::PushConstants draw = push;
                 draw.instance = instance;
                 list.setPushConstants(draw);
                 list.draw(indexCount);
             }
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
