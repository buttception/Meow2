/// @file
/// @brief GpuScene: the culling and drawing passes.

#include "Meow2/Graphics/Renderer/GpuScene.h"

#include "Meow2/Core/Assert.h"
#include "Meow2/Graphics/Renderer/MeshProcessing.h"
#include "Meow2/Math/Packing.h"

#include <DirectXMath.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <format>
#include <optional>
#include <span>
#include <tuple>
#include <utility>
#include <vector>

namespace Meow2::Renderer
{
namespace
{
constexpr std::uint32_t c_maxMeshes = 4096;
constexpr std::uint32_t c_maxMaterials = 4096;
constexpr std::uint32_t c_cullGroupSize = 64; // [numthreads] in GpuDriven.hlsl
// The G-buffer, in the order GpuDriven.hlsl's GBufferOutput writes it: albedo (8-bit sRGB is enough for a colour),
// normal (16-bit float: 8 bits per axis would band smooth highlights), material (metallic, roughness, occlusion: 8 bits
// each), emissive (16-bit float: emitted light goes above 1).
constexpr std::array<Rhi::RhiFormat, 4> c_gBufferFormats{Rhi::RhiFormat::Rgba8Srgb, Rhi::RhiFormat::Rgba16Float,
                                                         Rhi::RhiFormat::Rgba8Unorm, Rhi::RhiFormat::Rgba16Float};
constexpr std::array<const char*, 4> c_gBufferNames{"G-buffer albedo", "G-buffer normal", "G-buffer material",
                                                    "G-buffer emissive"};
/// @brief Bytes of one draw list's mesh dispatch arguments: x, y, z and a pad (csTaskArgs).
constexpr std::uint32_t c_taskArgBytes = 16;
/// @brief The most TaskItems a list can use: one task group each, and a dispatch has at most 2^22 groups.
constexpr std::uint32_t c_maxTaskItems = MEOW2_TASK_GROUPS_X * 128;

/// @brief The draw lists' cull modes and names, the same for both paths (see MEOW2_DRAW_LIST_*).
constexpr std::array<std::pair<Rhi::CullMode, const char*>, MEOW2_DRAW_LIST_COUNT> c_lists{
    std::pair{Rhi::CullMode::Back, "instances"}, std::pair{Rhi::CullMode::Front, "mirrored instances"},
    std::pair{Rhi::CullMode::None, "double-sided instances"}};

/// @brief Where a frustum's side planes meet: the camera of a perspective frustum, the origin of the meshlets' cone
///        test. It's taken from the frustum rather than the camera, so frozen culling freezes this point too.
/// @param frustum The frustum.
/// @return The point; empty for an orthographic frustum, whose sides are parallel.
std::optional<Math::Vec3> frustumApex(const Math::Frustum& frustum)
{
    using namespace DirectX;
    // The left, right and bottom planes: n · p + d = 0 for each, solved with Cramer's rule.
    const XMVECTOR n0 = XMVectorSet(frustum.planes[0].x, frustum.planes[0].y, frustum.planes[0].z, 0.0F);
    const XMVECTOR n1 = XMVectorSet(frustum.planes[1].x, frustum.planes[1].y, frustum.planes[1].z, 0.0F);
    const XMVECTOR n2 = XMVectorSet(frustum.planes[2].x, frustum.planes[2].y, frustum.planes[2].z, 0.0F);
    const XMVECTOR n1n2 = XMVector3Cross(n1, n2);
    const float determinant = XMVectorGetX(XMVector3Dot(n0, n1n2));
    if (std::abs(determinant) < 1e-5F)
    {
        return std::nullopt;
    }
    const XMVECTOR sum = XMVectorAdd(XMVectorAdd(XMVectorScale(n1n2, -frustum.planes[0].w),
                                                 XMVectorScale(XMVector3Cross(n2, n0), -frustum.planes[1].w)),
                                     XMVectorScale(XMVector3Cross(n0, n1), -frustum.planes[2].w));
    Math::Vec3 apex;
    XMStoreFloat3(&apex, XMVectorScale(sum, 1.0F / determinant));
    return apex;
}

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
    for (std::uint32_t list = 0; list < MEOW2_DRAW_LIST_COUNT; ++list)
    {
        Result<PipelineId> draw =
            pipelines.add(GraphicsPipelineRequest{.shader = desc.shader,
                                                  .colorFormat = desc.colorFormat,
                                                  .depthFormat = Rhi::RhiFormat::D32Float,
                                                  .cull = c_lists[list].first,
                                                  .name = std::format("Draw {}", c_lists[list].second)});
        if (!draw)
        {
            return Unexpected<Error>{draw.error()};
        }
        scene->m_drawPipelines[list] = *draw;
    }
    // The meshlet path: the same lists through task and mesh shaders, and the pass that sizes their dispatches.
    if (device.device().adapter().meshShaders)
    {
        Result<PipelineId> taskArgs = pipelines.add(
            ComputePipelineRequest{.shader = desc.shader, .entry = "csTaskArgs", .name = "Task arguments"});
        if (!taskArgs)
        {
            return Unexpected<Error>{taskArgs.error()};
        }
        scene->m_taskArgsPipeline = *taskArgs;
        for (std::uint32_t list = 0; list < MEOW2_DRAW_LIST_COUNT; ++list)
        {
            Result<PipelineId> draw =
                pipelines.add(GraphicsPipelineRequest{.shader = desc.shader,
                                                      .taskEntry = "asMain",
                                                      .meshEntry = "msMain",
                                                      .colorFormat = desc.colorFormat,
                                                      .depthFormat = Rhi::RhiFormat::D32Float,
                                                      .cull = c_lists[list].first,
                                                      .name = std::format("Draw {} (meshlets)", c_lists[list].second)});
            if (!draw)
            {
                return Unexpected<Error>{draw.error()};
            }
            scene->m_meshPipelines[list] = *draw;
        }
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

    // The deferred path: the same draw lists writing the G-buffer (GBUFFER = 1), and the pass that lights it.
    if (desc.deferredShader.isValid())
    {
        Result<DrawPipelines> gBuffer = scene->makeDrawPipelines(
            {.options = {{"GBUFFER", "1"}},
             .colorFormat = c_gBufferFormats[0],
             .extraColorFormats = {c_gBufferFormats[1], c_gBufferFormats[2], c_gBufferFormats[3]},
             .blended = false,
             .name = "G-buffer"});
        if (!gBuffer)
        {
            return Unexpected<Error>{gBuffer.error()};
        }
        scene->m_gBufferPipelines = *gBuffer;
        Result<PipelineId> lighting = pipelines.add(GraphicsPipelineRequest{.shader = desc.deferredShader,
                                                                            .vertexEntry = "vsFullscreen",
                                                                            .pixelEntry = "psLighting",
                                                                            .colorFormat = desc.colorFormat,
                                                                            .depthFormat = Rhi::RhiFormat::Unknown,
                                                                            .depthWrite = false,
                                                                            .name = "Deferred lighting"});
        if (!lighting)
        {
            return Unexpected<Error>{lighting.error()};
        }
        scene->m_lightingPipeline = *lighting;
    }

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
                     Rhi::RhiMemory::Upload, "Materials"),
          // The meshlet path's: made on every GPU (they're small), used only with mesh shaders.
          makeBuffer(m_taskBuffer, std::uint64_t{m_taskCapacity} * MEOW2_DRAW_LIST_COUNT * sizeof(Shader::TaskItem),
                     Rhi::RhiMemory::GpuOnly, "Task items"),
          makeBuffer(m_taskCountBuffer, MEOW2_TASK_COUNT_SIZE * sizeof(std::uint32_t), Rhi::RhiMemory::GpuOnly,
                     "Meshlet counts"),
          makeBuffer(m_taskArgsBuffer, std::uint64_t{MEOW2_DRAW_LIST_COUNT} * c_taskArgBytes, Rhi::RhiMemory::GpuOnly,
                     "Task arguments")})
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
        if (Result<void> made = makeBuffer(m_countReadback[slot], MEOW2_TASK_COUNT_SIZE * sizeof(std::uint32_t),
                                           Rhi::RhiMemory::Readback, "Meshlet counts readback");
            !made)
        {
            return made;
        }
        if (Result<void> made = makeBuffer(m_deferredInputs[slot], sizeof(Shader::DeferredInputs),
                                           Rhi::RhiMemory::Upload, "Deferred inputs");
            !made)
        {
            return made;
        }
    }
    return {};
}

Result<void> GpuScene::sizeTaskBuffer(Rhi::IRhiDevice& device)
{
    // Each opaque instance can write one TaskItem per 32 meshlets, all into one list (which isn't known up front).
    std::uint64_t items = 0;
    m_meshletStats.meshlets = 0;
    m_meshletStats.triangles = 0;
    for (const Shader::InstanceData& instance : m_instances)
    {
        if (instance.mesh >= m_meshes.size() ||
            (instance.material < m_materials.size() && m_materials[instance.material].blend))
        {
            continue;
        }
        const MeshData& mesh = m_meshes[instance.mesh];
        items += (mesh.meshlets.size() + MEOW2_TASK_MESHLETS - 1) / MEOW2_TASK_MESHLETS;
        m_meshletStats.meshlets += static_cast<std::uint32_t>(mesh.meshlets.size());
        m_meshletStats.triangles += mesh.indices.size() / 3;
    }
    const auto needed = static_cast<std::uint32_t>(std::clamp<std::uint64_t>(items, 1, c_maxTaskItems));
    if (needed <= m_taskCapacity && m_taskBuffer.isSet())
    {
        return {};
    }
    device.destroy(m_taskBuffer); // the GPU is idle (setInstances()' precondition)
    m_taskBuffer = {};
    Result<Rhi::BufferHandle> buffer =
        device.createBuffer({.size = std::uint64_t{needed} * MEOW2_DRAW_LIST_COUNT * sizeof(Shader::TaskItem),
                             .memory = Rhi::RhiMemory::GpuOnly,
                             .tag = Memory::MemTag::Renderer,
                             .name = "Task items"});
    if (!buffer)
    {
        return Unexpected<Error>{buffer.error()};
    }
    m_taskBuffer = *buffer;
    m_taskCapacity = needed;
    m_taskState = Rhi::ResourceState::Undefined;
    return {};
}

void GpuScene::destroyBuffers(Rhi::IRhiDevice& device)
{
    // Unset handles are ignored, so this also cleans up after a creation that stopped halfway.
    for (Rhi::BufferHandle* buffer : {&m_instanceBuffer, &m_meshBuffer, &m_drawBuffer, &m_countBuffer,
                                      &m_materialBuffer, &m_taskBuffer, &m_taskCountBuffer, &m_taskArgsBuffer})
    {
        device.destroy(*buffer);
        *buffer = {};
    }
    for (std::uint32_t slot = 0; slot < Rhi::c_maxFramesInFlight; ++slot)
    {
        device.destroy(m_frameBuffers[slot]);
        device.destroy(m_cullBuffers[slot]);
        device.destroy(m_countReadback[slot]);
        device.destroy(m_deferredInputs[slot]);
        m_deferredInputs[slot] = {};
        m_frameBuffers[slot] = {};
        m_cullBuffers[slot] = {};
        m_countReadback[slot] = {};
        m_slotReadsBack[slot] = false;
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
    // The G-buffer is made again at the next deferred frame, at the targets' size.
    for (Rhi::TextureHandle& texture : m_gBuffer)
    {
        device.destroy(texture);
        texture = {};
    }
    m_gBufferWidth = 0;
    m_gBufferHeight = 0;
    m_gBufferState = Rhi::ResourceState::Undefined;
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
    m_taskState = Rhi::ResourceState::Undefined;
    m_taskCountState = Rhi::ResourceState::Undefined;
    m_taskArgsState = Rhi::ResourceState::Undefined;
    m_lastDepth = {};
    return {};
}

Result<void> GpuScene::uploadMesh(Rhi::IRhiDevice& device, const MeshData& mesh, std::uint32_t index)
{
    // One upload buffer per array, copied in and named in the mesh's record. An empty array gets no buffer (heap index
    // 0): a mesh without triangles has no meshlets, so nothing reads it.
    const auto upload = [&](std::span<const std::byte> bytes, const char* name) -> Result<std::uint32_t>
    {
        if (bytes.empty())
        {
            return Rhi::c_invalidBindlessIndex;
        }
        Result<Rhi::BufferHandle> buffer = device.createBuffer(
            {.size = bytes.size(), .memory = Rhi::RhiMemory::Upload, .tag = Memory::MemTag::Renderer, .name = name});
        if (!buffer)
        {
            return Unexpected<Error>{buffer.error()};
        }
        m_meshResources.push_back(*buffer);
        std::memcpy(device.mappedMemory(*buffer).data(), bytes.data(), bytes.size());
        return device.bindlessIndex(*buffer);
    };
    Shader::MeshDraw draw{};
    const std::array<std::tuple<std::span<const std::byte>, const char*, std::uint32_t*>, 5> arrays{
        std::tuple{std::as_bytes(std::span{mesh.vertices}), "Mesh vertices", &draw.vertexBuffer},
        std::tuple{std::as_bytes(std::span{mesh.indices}), "Mesh indices", &draw.indexBuffer},
        std::tuple{std::as_bytes(std::span{mesh.meshlets}), "Meshlets", &draw.meshletBuffer},
        std::tuple{std::as_bytes(std::span{mesh.meshletVertices}), "Meshlet vertices", &draw.meshletVertexBuffer},
        std::tuple{std::as_bytes(std::span{mesh.meshletTriangles}), "Meshlet triangles", &draw.meshletTriangleBuffer}};
    for (const auto& [bytes, name, heapIndex] : arrays)
    {
        Result<std::uint32_t> uploaded = upload(bytes, name);
        if (!uploaded)
        {
            return Unexpected<Error>{uploaded.error()};
        }
        *heapIndex = *uploaded;
    }

    const Math::Vec3& extents = mesh.bounds.extents;
    draw.meshletCount = static_cast<std::uint32_t>(mesh.meshlets.size());
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
    // KHR_texture_transform as two rows of translation × rotation × scale. The rotation's sign is the one Khronos'
    // TextureTransformTest sample checks (its arrows point at the green markers): rows (cos, sin) and (-sin, cos).
    const auto uvRows = [](const MaterialTexture& texture, Shader::float4& row0, Shader::float4& row1)
    {
        const float c = std::cos(texture.uvRotation);
        const float s = std::sin(texture.uvRotation);
        row0 = {c * texture.uvScale.x, s * texture.uvScale.y, texture.uvOffset.x, 0.0F};
        row1 = {-s * texture.uvScale.x, c * texture.uvScale.y, texture.uvOffset.y, 0.0F};
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
    MeshData stored = mesh; // kept for the next device, after a backend switch
    if (stored.meshlets.empty())
    {
        buildMeshlets(stored);
    }
    if (Result<void> uploaded = uploadMesh(m_device.device(), stored, index); !uploaded)
    {
        return Unexpected<Error>{uploaded.error()};
    }
    m_meshes.push_back(std::move(stored));
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
    // Out of GPU memory for the lists leaves them unset: meshletsAvailable() turns false, and the indirect path draws.
    [[maybe_unused]] const Result<void> sized = sizeTaskBuffer(m_device.device());
}

bool GpuScene::meshletsAvailable() const
{
    return m_device.device().adapter().meshShaders && m_taskArgsPipeline.has_value() &&
           std::ranges::all_of(m_meshPipelines, [](const std::optional<PipelineId>& id) { return id.has_value(); }) &&
           m_taskBuffer.isSet();
}

bool GpuScene::usesMeshlets() const
{
    return m_meshletsWanted && meshletsAvailable() && (!m_override || m_override->hasMeshLists);
}

void GpuScene::readMeshletStats(Rhi::IRhiDevice& device, std::uint32_t frameSlot)
{
    if (!m_slotReadsBack[frameSlot])
    {
        return;
    }
    std::array<std::uint32_t, MEOW2_TASK_COUNT_SIZE> counts{};
    std::memcpy(counts.data(), device.mappedMemory(m_countReadback[frameSlot]).data(), sizeof(counts));
    m_meshletStats.meshletsDrawn = counts[MEOW2_TASK_COUNT_MESHLETS];
    m_meshletStats.trianglesDrawn = counts[MEOW2_TASK_COUNT_TRIANGLES];
    m_meshletStats.valid = true;
}

void GpuScene::setView(std::uint32_t frameSlot, const Shader::FrameConstants& frame, const Math::Frustum& frustum)
{
    using namespace DirectX;
    Rhi::IRhiDevice& device = m_device.device();
    readMeshletStats(device, frameSlot); // this slot's last frame is done
    Shader::FrameConstants constants = frame;
    constants.materialBuffer = device.bindlessIndex(m_materialBuffer);
    constants.materialCount = static_cast<std::uint32_t>(m_materials.size());
    constants.taskBuffer = device.bindlessIndex(m_taskBuffer);
    constants.taskCounts = device.bindlessStorageIndex(m_taskCountBuffer);
    constants.taskCapacity = m_taskCapacity;
    constants.cullBuffer = device.bindlessIndex(m_cullBuffers[frameSlot]);
    // Deferred lighting rebuilds each pixel's position from its depth with this.
    XMStoreFloat4x4(&constants.inverseViewProjection, XMMatrixInverse(nullptr, XMLoadFloat4x4(&frame.viewProjection)));
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
    const std::optional<Math::Vec3> apex = frustumApex(frustum);
    cull.origin = apex ? Shader::float4{apex->x, apex->y, apex->z, 1.0F} : Shader::float4{0.0F, 0.0F, 0.0F, 0.0F};
    cull.meshlets = 0; // addPasses() decides
    cull.taskBuffer = device.bindlessStorageIndex(m_taskBuffer);
    cull.taskCounts = device.bindlessStorageIndex(m_taskCountBuffer);
    cull.taskCapacity = m_taskCapacity;
    cull.taskArguments = device.bindlessStorageIndex(m_taskArgsBuffer);
    std::memcpy(device.mappedMemory(m_cullBuffers[frameSlot]).data(), &cull, sizeof(cull));
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
    // The path is fixed for the frame here: culling writes one kind of list, and the draws read it.
    const bool meshlets = usesMeshlets();
    m_frameUsesMeshlets = meshlets;
    m_slotReadsBack[frameSlot] = meshlets;
    const std::uint32_t meshletFlag = meshlets ? 1U : 0U;
    std::memcpy(m_device.device().mappedMemory(m_cullBuffers[frameSlot]).data() +
                    offsetof(Shader::CullConstants, meshlets),
                &meshletFlag, sizeof(meshletFlag));
    if (meshlets)
    {
        m_taskResource = graph.import(m_taskBuffer, m_taskState, "Task items");
        m_taskCountResource = graph.import(m_taskCountBuffer, m_taskCountState, "Meshlet counts");
        m_taskArgsResource = graph.import(m_taskArgsBuffer, m_taskArgsState, "Task arguments");
    }

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
    if (meshlets)
    {
        graph.addPass({.name = "Clear meshlet counts",
                       .accesses = {{m_taskCountResource, ResourceState::CopyTarget}},
                       .record = [this](Rhi::IRhiCommandList& list) { list.fillBuffer(m_taskCountBuffer, 0); },
                       .small = true});
    }
    std::vector<GraphAccess> cullAccesses{{m_countResource, ResourceState::ShaderWrite},
                                          {m_drawResource, ResourceState::ShaderWrite}};
    if (meshlets)
    {
        cullAccesses.push_back({m_taskResource, ResourceState::ShaderWrite});
        cullAccesses.push_back({m_taskCountResource, ResourceState::ShaderWrite});
    }
    graph.addPass({.name = "Cull instances",
                   .accesses = std::move(cullAccesses),
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
    if (meshlets)
    {
        graph.addPass({.name = "Task arguments",
                       .accesses = {{m_taskCountResource, ResourceState::ShaderWrite},
                                    {m_taskArgsResource, ResourceState::ShaderWrite}},
                       .record =
                           [this, cullIndex](Rhi::IRhiCommandList& list)
                       {
                           Shader::PushConstants push{};
                           push.userData = cullIndex;
                           list.setPipeline(m_pipelines.get(*m_taskArgsPipeline));
                           list.setPushConstants(push);
                           list.dispatch(1);
                       },
                       .small = true});
    }

    // Three passes, so debugging tools can show each step's output: everything opaque (clearing the targets), the
    // sky behind it (where depth is still far), then what's blended over both. Each keeps what the one before drew.
    GraphResource colorResource{};
    if (swapchain == nullptr)
    {
        colorResource = graph.import(color, ResourceState::Undefined, "Scene colour");
    }
    const auto targets = [&](bool lists, bool withColor = true)
    {
        std::vector<GraphAccess> accesses{{m_depthResource, ResourceState::DepthTarget}};
        if (lists && meshlets)
        {
            accesses.push_back({m_taskArgsResource, ResourceState::IndirectArgs});
            accesses.push_back({m_taskResource, ResourceState::ShaderRead});
            accesses.push_back({m_taskCountResource, ResourceState::ShaderWrite}); // the task shader counts survivors
        }
        else if (lists)
        {
            accesses.push_back({m_countResource, ResourceState::IndirectArgs});
            accesses.push_back({m_drawResource, ResourceState::IndirectArgs});
        }
        if (swapchain == nullptr && withColor)
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

    // This frame's pipelines for each list, on its path.
    DrawPipelines current{};
    for (std::uint32_t drawList = 0; drawList < MEOW2_DRAW_LIST_COUNT; ++drawList)
    {
        current.lists[drawList] = m_override ? m_override->lists[drawList] : *m_drawPipelines[drawList];
        if (meshlets)
        {
            current.meshLists[drawList] = m_override ? m_override->meshLists[drawList] : *m_meshPipelines[drawList];
        }
    }
    current.hasMeshLists = meshlets;
    m_frameDeferred = m_deferred && !m_override && m_gBufferPipelines && m_lightingPipeline && swapchain == nullptr &&
                      ensureGBuffer(device, color);
    if (m_frameDeferred)
    {
        // Deferred: the draw lists write each surface's properties into the G-buffer (and depth), then one full-screen
        // pass lights every pixel from them. Same lists, same culling, same pipelines per list, other outputs.
        DrawPipelines gBuffer{};
        for (std::uint32_t drawList = 0; drawList < MEOW2_DRAW_LIST_COUNT; ++drawList)
        {
            gBuffer.lists[drawList] = m_gBufferPipelines->lists[drawList];
            gBuffer.meshLists[drawList] = m_gBufferPipelines->meshLists[drawList];
        }
        gBuffer.hasMeshLists = meshlets && m_gBufferPipelines->hasMeshLists;
        std::vector<GraphAccess> gBufferAccesses = targets(true, false);
        for (std::size_t i = 0; i < m_gBuffer.size(); ++i)
        {
            m_gBufferResources[i] = graph.import(m_gBuffer[i], m_gBufferState, c_gBufferNames[i]);
            gBufferAccesses.push_back({m_gBufferResources[i], ResourceState::ColorTarget});
        }
        graph.addPass({.name = "G-buffer",
                       .accesses = std::move(gBufferAccesses),
                       .record = [this, depth, push, gBuffer](Rhi::IRhiCommandList& list)
                       {
                           // Cleared to zero: a pixel nothing covers stays empty (its depth too, for the sky).
                           list.beginRendering(
                               Rhi::RenderTargets{.color = m_gBuffer[0],
                                                  .extraColors = {m_gBuffer[1], m_gBuffer[2], m_gBuffer[3]},
                                                  .clearColor = Math::Color{.a = 0.0F},
                                                  .depth = depth,
                                                  .clearDepth = 0.0F});
                           recordLists(list, gBuffer, push);
                           list.endRendering();
                       }});

        // The lighting pass reads the G-buffer and depth through heap indices, from this record.
        const Shader::DeferredInputs inputs{.albedo = device.bindlessIndex(m_gBuffer[0]),
                                            .normal = device.bindlessIndex(m_gBuffer[1]),
                                            .material = device.bindlessIndex(m_gBuffer[2]),
                                            .emissive = device.bindlessIndex(m_gBuffer[3]),
                                            .depth = device.bindlessIndex(depth),
                                            .width = m_gBufferWidth,
                                            .height = m_gBufferHeight,
                                            .padding0 = 0};
        std::memcpy(device.mappedMemory(m_deferredInputs[frameSlot]).data(), &inputs, sizeof(inputs));
        Shader::PushConstants lightingPush = push;
        lightingPush.userData = device.bindlessIndex(m_deferredInputs[frameSlot]);
        std::vector<GraphAccess> lightingAccesses{{m_depthResource, ResourceState::ShaderRead},
                                                  {colorResource, ResourceState::ColorTarget}};
        for (const GraphResource resource : m_gBufferResources)
        {
            lightingAccesses.push_back({resource, ResourceState::ShaderRead});
        }
        graph.addPass({.name = "Lighting",
                       .accesses = std::move(lightingAccesses),
                       .record =
                           [this, color, clear, lightingPush](Rhi::IRhiCommandList& list)
                       {
                           // Clears the scene colour, then shades every pixel the G-buffer covers; the rest is the
                           // clear colour until the sky pass.
                           list.beginRendering(Rhi::RenderTargets{.color = color, .clearColor = clear});
                           list.setPipeline(m_pipelines.get(*m_lightingPipeline));
                           list.setPushConstants(lightingPush);
                           list.draw(3);
                           list.endRendering();
                       },
                       .small = true});
    }
    else
    {
        graph.addPass({.name = "Opaque",
                       .accesses = targets(true),
                       .record = [this, begin, clear, push, current](Rhi::IRhiCommandList& list)
                       {
                           begin(list, clear, 0.0F);
                           recordLists(list, current, push);
                           list.endRendering();
                       }});
    }
    if (meshlets)
    {
        // The counts come back to the CPU a few frames later, without waiting (meshletStats()).
        graph.addPass({.name = "Read meshlet counts",
                       .accesses = {{m_taskCountResource, ResourceState::CopySource}},
                       .record =
                           [this, frameSlot](Rhi::IRhiCommandList& list)
                       {
                           // The CPU last read this buffer before the slot's fence: no barrier needed before the copy.
                           list.copyBuffer(m_taskCountBuffer, m_countReadback[frameSlot],
                                           MEOW2_TASK_COUNT_SIZE * sizeof(std::uint32_t));
                           list.barrier(m_countReadback[frameSlot], ResourceState::CopyTarget, ResourceState::HostRead);
                       },
                       .small = true});
    }

    if (m_sky != nullptr && m_skyPass)
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
    DrawPipelines out;
    for (std::uint32_t list = 0; list < MEOW2_DRAW_LIST_COUNT; ++list)
    {
        Result<PipelineId> draw =
            m_pipelines.add(GraphicsPipelineRequest{.shader = m_desc.shader,
                                                    .options = options.options,
                                                    .colorFormat = options.colorFormat.value_or(m_desc.colorFormat),
                                                    .extraColorFormats = options.extraColorFormats,
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
    if (!options.blended)
    {
        out.blend = out.lists[0]; // not used: the caller draws no blended surfaces with these
    }
    else if (Result<PipelineId> blend = m_pipelines.add(GraphicsPipelineRequest{
                 .shader = m_desc.shader,
                 .options = options.options,
                 .vertexEntry = "vsBlend",
                 .colorFormat = options.colorFormat.value_or(m_desc.colorFormat),
                 .extraColorFormats = options.extraColorFormats,
                 .depthFormat = Rhi::RhiFormat::D32Float,
                 .depthTest = options.depthTest,
                 .depthWrite = false,
                 .cull = Rhi::CullMode::None,
                 .blend = options.blend == Rhi::BlendMode::Additive ? Rhi::BlendMode::Additive : Rhi::BlendMode::Alpha,
                 .fill = options.fill,
                 .name = std::format("{}: blended instances", options.name)});
             !blend)
    {
        return Unexpected<Error>{blend.error()};
    }
    else
    {
        out.blend = *blend;
    }
    if (options.meshlets && m_device.device().adapter().meshShaders)
    {
        if (Result<void> made = addMeshPipelines(options, out); !made)
        {
            return Unexpected<Error>{made.error()};
        }
    }
    return out;
}

Result<void> GpuScene::addMeshPipelines(const DrawPipelineOptions& options, DrawPipelines& out)
{
    for (std::uint32_t list = 0; list < MEOW2_DRAW_LIST_COUNT; ++list)
    {
        Result<PipelineId> draw = m_pipelines.add(
            GraphicsPipelineRequest{.shader = m_desc.shader,
                                    .options = options.options,
                                    .taskEntry = "asMain",
                                    .meshEntry = "msMain",
                                    .colorFormat = options.colorFormat.value_or(m_desc.colorFormat),
                                    .extraColorFormats = options.extraColorFormats,
                                    .depthFormat = Rhi::RhiFormat::D32Float,
                                    .depthTest = options.depthTest,
                                    .depthWrite = options.depthWrite,
                                    .cull = c_lists[list].first,
                                    .blend = options.blend,
                                    .fill = options.fill,
                                    .name = std::format("{}: {} (meshlets)", options.name, c_lists[list].second)});
        if (!draw)
        {
            return Unexpected<Error>{draw.error()};
        }
        out.meshLists[list] = *draw;
    }
    out.hasMeshLists = true;
    return {};
}

void GpuScene::recordLists(Rhi::IRhiCommandList& list, const DrawPipelines& pipelines,
                           const Shader::PushConstants& push) const
{
    if (pipelines.hasMeshLists)
    {
        // One mesh dispatch per list, sized by csTaskArgs; the task shader learns its list from g_push.instance.
        for (std::uint32_t drawList = 0; drawList < MEOW2_DRAW_LIST_COUNT; ++drawList)
        {
            Shader::PushConstants listPush = push;
            listPush.instance = drawList;
            list.setPipeline(m_pipelines.get(pipelines.meshLists[drawList]));
            list.setPushConstants(listPush);
            list.dispatchMeshIndirect(m_taskArgsBuffer, std::uint64_t{drawList} * c_taskArgBytes);
        }
        return;
    }
    const std::uint64_t listBytes = std::uint64_t{m_desc.instanceCapacity} * sizeof(Shader::IndirectDraw);
    for (std::uint32_t drawList = 0; drawList < MEOW2_DRAW_LIST_COUNT; ++drawList)
    {
        list.setPipeline(m_pipelines.get(pipelines.lists[drawList]));
        list.setPushConstants(push);
        list.drawIndirectCount(m_drawBuffer, m_countBuffer, m_desc.instanceCapacity, drawList * listBytes,
                               drawList * sizeof(std::uint32_t));
    }
}

void GpuScene::addOverlayPass(RenderGraph& graph, std::string name, const DrawPipelines& pipelines, GraphResource color,
                              Rhi::TextureHandle colorTexture, Rhi::TextureHandle depth, std::uint32_t frameSlot)
{
    Rhi::IRhiDevice& device = m_device.device();
    Shader::PushConstants push{};
    push.frameBuffer = device.bindlessIndex(m_frameBuffers[frameSlot]);
    push.instanceBuffer = device.bindlessIndex(m_instanceBuffer);
    push.userData = device.bindlessIndex(m_meshBuffer);
    // The lists culling filled this frame: TaskItems on the meshlet path, indirect draws on the other. Overlay
    // pipelines without meshlet pipelines can't draw TaskItems, so on a meshlet frame they redraw only blended
    // surfaces.
    const bool lists = !m_frameUsesMeshlets || pipelines.hasMeshLists;
    DrawPipelines used = pipelines;
    used.hasMeshLists = m_frameUsesMeshlets;
    std::vector<GraphAccess> accesses{{m_depthResource, Rhi::ResourceState::DepthTarget},
                                      {color, Rhi::ResourceState::ColorTarget}};
    if (lists && m_frameUsesMeshlets)
    {
        accesses.push_back({m_taskArgsResource, Rhi::ResourceState::IndirectArgs});
        accesses.push_back({m_taskResource, Rhi::ResourceState::ShaderRead});
        accesses.push_back({m_taskCountResource, Rhi::ResourceState::ShaderWrite});
    }
    else if (lists)
    {
        accesses.push_back({m_countResource, Rhi::ResourceState::IndirectArgs});
        accesses.push_back({m_drawResource, Rhi::ResourceState::IndirectArgs});
    }
    graph.addPass(
        {.name = std::move(name),
         .accesses = std::move(accesses),
         .record = [this, used, lists, push, colorTexture, depth, blended = m_blendDraws](Rhi::IRhiCommandList& list)
         {
             list.beginRendering(Rhi::RenderTargets{.color = colorTexture, .depth = depth});
             if (lists)
             {
                 recordLists(list, used, push);
             }
             list.setPipeline(m_pipelines.get(used.blend));
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

bool GpuScene::ensureGBuffer(Rhi::IRhiDevice& device, Rhi::TextureHandle color)
{
    const Rhi::TextureInfo target = device.textureInfo(color);
    if (m_gBuffer[0].isSet() && m_gBufferWidth == target.width && m_gBufferHeight == target.height)
    {
        return true;
    }
    for (Rhi::TextureHandle& texture : m_gBuffer)
    {
        device.destroy(texture); // deferred until the GPU is done with it
        texture = {};
    }
    for (std::size_t i = 0; i < m_gBuffer.size(); ++i)
    {
        Result<Rhi::TextureHandle> made = device.createTexture({.width = target.width,
                                                                .height = target.height,
                                                                .format = c_gBufferFormats[i],
                                                                .renderTarget = true,
                                                                .tag = Memory::MemTag::Renderer,
                                                                .name = c_gBufferNames[i]});
        if (!made)
        {
            return false; // this frame draws forward
        }
        m_gBuffer[i] = *made;
    }
    m_gBufferWidth = target.width;
    m_gBufferHeight = target.height;
    m_gBufferState = Rhi::ResourceState::Undefined;
    return true;
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
    if (m_frameDeferred)
    {
        m_gBufferState = graph.finalState(m_gBufferResources[0]); // all four end where Lighting read them
    }
    if (m_frameUsesMeshlets)
    {
        m_taskState = graph.finalState(m_taskResource);
        m_taskCountState = graph.finalState(m_taskCountResource);
        m_taskArgsState = graph.finalState(m_taskArgsResource);
    }
}
} // namespace Meow2::Renderer
