/// @file
/// @brief Imported models in the Scene and on the GPU.

#include "Meow2/Graphics/Renderer/SceneModel.h"

#include "Meow2/Core/Assert.h"
#include "Meow2/Graphics/Renderer/MeshProcessing.h"

#include <DirectXMath.h>

#include <algorithm>
#include <array>
#include <format>
#include <utility>

namespace Meow2::Renderer
{
namespace
{
constexpr float c_anisotropy = 8.0F; // clamped to what the GPU supports by the RHI
} // namespace

void RenderMeshes::add(AssetId mesh, std::vector<GpuSubmesh> parts)
{
    m_meshes[mesh] = std::move(parts);
}

std::span<const GpuSubmesh> RenderMeshes::find(AssetId mesh) const
{
    const auto found = m_meshes.find(mesh);
    return found == m_meshes.end() ? std::span<const GpuSubmesh>{} : std::span<const GpuSubmesh>{found->second};
}

Result<std::vector<AssetId>> uploadModel(GpuScene& scene, const Assets::ImportedModel& model, UuidGenerator& ids,
                                         RenderMeshes& meshes, Log::Logger& log, const TextureCooking& cooking)
{
    // Only images a material uses are uploaded, each once per kind: colour (base colour, emissive) is sRGB, data
    // (metallic-roughness, occlusion) linear, normal maps BC5 when compressed. An image used two ways is cooked twice.
    std::vector<std::array<std::int32_t, 3>> textureOf(model.images.size(), {-1, -1, -1});
    const auto texture = [&](std::int32_t image, Assets::TextureKind kind) -> std::int32_t
    {
        if (image < 0 || static_cast<std::size_t>(image) >= model.images.size())
        {
            return -1;
        }
        std::int32_t& slot = textureOf[static_cast<std::size_t>(image)][static_cast<std::size_t>(kind)];
        if (slot < 0 && !model.images[static_cast<std::size_t>(image)].pixels.empty())
        {
            const Assets::Image& source = model.images[static_cast<std::size_t>(image)];
            Result<Assets::CookedTexture> cooked =
                cooking.cache != nullptr ? cooking.cache->cook(source, kind, cooking.compress, cooking.jobs)
                                         : Assets::cookTexture(source, kind, cooking.compress, cooking.jobs);
            Result<std::uint32_t> added = cooked ? scene.addTexture(std::move(*cooked), std::format("Image {}", image))
                                                 : Result<std::uint32_t>{Unexpected<Error>{cooked.error()}};
            if (added)
            {
                slot = static_cast<std::int32_t>(*added);
            }
            else
            {
                MEOW2_LOG_WARN(log, "Image {} not uploaded: {}", image, added.error());
            }
        }
        return slot;
    };

    // The file's samplers; the scene shares identical ones. Linear filtering gets anisotropy, which keeps textures
    // sharp on surfaces seen at a grazing angle (floors, Sponza's walls).
    std::vector<std::int32_t> samplerOf;
    for (const Assets::ImportedSampler& imported : model.samplers)
    {
        const auto address = [](Assets::ImportedWrap wrap)
        {
            switch (wrap)
            {
                case Assets::ImportedWrap::Clamp:
                    return Rhi::SamplerAddress::Clamp;
                case Assets::ImportedWrap::Mirror:
                    return Rhi::SamplerAddress::Mirror;
                case Assets::ImportedWrap::Repeat:
                    break;
            }
            return Rhi::SamplerAddress::Repeat;
        };
        Result<std::uint32_t> added =
            scene.addSampler({.filter = imported.linear ? Rhi::SamplerFilter::Linear : Rhi::SamplerFilter::Nearest,
                              .addressU = address(imported.wrapU),
                              .addressV = address(imported.wrapV),
                              .maxAnisotropy = imported.linear ? c_anisotropy : 1.0F,
                              .name = "glTF sampler"});
        if (!added)
        {
            MEOW2_LOG_WARN(log, "Sampler not created, using the default: {}", added.error());
        }
        samplerOf.push_back(added ? static_cast<std::int32_t>(*added) : -1);
    }
    const auto slot = [&](const Assets::ImportedTexture& imported, Assets::TextureKind kind) -> MaterialTexture
    {
        const bool hasSampler = imported.sampler >= 0 && static_cast<std::size_t>(imported.sampler) < samplerOf.size();
        return {.texture = texture(imported.image, kind),
                .sampler = hasSampler ? samplerOf[static_cast<std::size_t>(imported.sampler)] : -1,
                .uvOffset = imported.offset,
                .uvRotation = imported.rotation,
                .uvScale = imported.scale};
    };

    // The model's materials, then one default (white, opaque) for primitives that name none.
    std::vector<std::uint32_t> materialOf;
    for (const Assets::ImportedMaterial& material : model.materials)
    {
        Result<std::uint32_t> added =
            scene.addMaterial({.baseColorFactor = material.baseColorFactor,
                               .baseColor = slot(material.baseColor, Assets::TextureKind::Color),
                               .metallic = material.metallicFactor,
                               .roughness = material.roughnessFactor,
                               .metallicRoughness = slot(material.metallicRoughness, Assets::TextureKind::Data),
                               .normal = slot(material.normal, Assets::TextureKind::NormalMap),
                               .normalScale = material.normalScale,
                               .occlusion = slot(material.occlusion, Assets::TextureKind::Data),
                               .occlusionStrength = material.occlusionStrength,
                               .emissiveFactor = material.emissiveFactor,
                               .emissive = slot(material.emissive, Assets::TextureKind::Color),
                               .alphaMask = material.alphaMode == Assets::AlphaMode::Mask,
                               .alphaCutoff = material.alphaCutoff,
                               .blend = material.alphaMode == Assets::AlphaMode::Blend,
                               .doubleSided = material.doubleSided});
        if (!added)
        {
            return Unexpected<Error>{added.error()};
        }
        materialOf.push_back(*added);
    }
    Result<std::uint32_t> fallback = scene.addMaterial({});
    if (!fallback)
    {
        return Unexpected<Error>{fallback.error()};
    }

    std::vector<AssetId> meshIds;
    meshIds.reserve(model.meshes.size());
    for (const Assets::ImportedMesh& mesh : model.meshes)
    {
        std::vector<GpuSubmesh> parts;
        for (const Assets::ImportedPrimitive& primitive : mesh.primitives)
        {
            MeshData packed = makeMesh({.positions = primitive.positions,
                                        .normals = primitive.normals,
                                        .tangents = primitive.tangents,
                                        .uvs = primitive.uvs,
                                        .colors = primitive.colors,
                                        .indices = primitive.indices},
                                       primitive.bounds);
            // Ordered for the GPU's caches and split into meshlets once, here, rather than on every backend switch.
            optimizeMesh(packed);
            buildMeshlets(packed);
            Result<std::uint32_t> uploaded = scene.addMesh(packed);
            if (!uploaded)
            {
                return Unexpected<Error>{uploaded.error()};
            }
            const bool named =
                primitive.material >= 0 && static_cast<std::size_t>(primitive.material) < materialOf.size();
            parts.push_back({.mesh = *uploaded,
                             .material = named ? materialOf[static_cast<std::size_t>(primitive.material)] : *fallback,
                             .bounds = primitive.bounds});
        }
        const AssetId id = ids.next<AssetId::c_kind>();
        meshes.add(id, std::move(parts));
        meshIds.push_back(id);
    }
    return meshIds;
}

std::vector<entt::entity> instantiateModel(Scene::Scene& scene, Scene::TransformHierarchy& hierarchy,
                                           const Assets::ImportedModel& model, std::span<const AssetId> meshIds,
                                           entt::entity parent)
{
    MEOW2_ASSERT(meshIds.size() == model.meshes.size(), "One AssetId per mesh, from uploadModel()");
    scene.registerComponent<MeshRenderer>();
    std::vector<entt::entity> entities;
    entities.reserve(model.nodes.size());
    for (const Assets::ImportedNode& node : model.nodes)
    {
        const entt::entity entity = scene.create(node.name);
        // Parents come before their children, so the parent's entity already exists.
        const entt::entity nodeParent = node.parent >= 0 ? entities[static_cast<std::size_t>(node.parent)] : parent;
        hierarchy.add(entity, node.local, nodeParent);
        if (node.mesh >= 0)
        {
            scene.registry().emplace<MeshRenderer>(entity, MeshRenderer{meshIds[static_cast<std::size_t>(node.mesh)]});
        }
        entities.push_back(entity);
    }
    return entities;
}

std::optional<Math::Aabb> gatherInstances(Scene::Scene& scene, const RenderMeshes& meshes,
                                          std::vector<InstanceDesc>& out)
{
    using namespace DirectX;
    out.clear();
    bool any = false;
    XMVECTOR low = XMVectorReplicate(0.0F);
    XMVECTOR high = XMVectorReplicate(0.0F);
    // One storage walked, the other looked up: a two-type EnTT view needs a default-constructible allocator for its
    // placeholder storage, which the scene's ResourceAllocator isn't.
    Scene::Scene::Registry& registry = scene.registry();
    for (const auto [entity, renderer] : registry.view<MeshRenderer>().each())
    {
        const Scene::WorldTransform* world = registry.try_get<Scene::WorldTransform>(entity);
        if (world == nullptr)
        {
            continue;
        }
        const XMMATRIX matrix = XMLoadFloat4x4(&world->matrix);
        for (const GpuSubmesh& part : meshes.find(renderer.mesh))
        {
            out.push_back(
                {.world = world->matrix, .mesh = part.mesh, .material = part.material, .movable = renderer.movable});

            const Math::MinMax box = Math::toMinMax(Math::transformAabb(part.bounds, matrix));
            const XMVECTOR boxLow = XMLoadFloat3(&box.min);
            const XMVECTOR boxHigh = XMLoadFloat3(&box.max);
            low = any ? XMVectorMin(low, boxLow) : boxLow;
            high = any ? XMVectorMax(high, boxHigh) : boxHigh;
            any = true;
        }
    }
    if (!any)
    {
        return std::nullopt;
    }
    Math::Vec3 lowCorner;
    Math::Vec3 highCorner;
    XMStoreFloat3(&lowCorner, low);
    XMStoreFloat3(&highCorner, high);
    return Math::aabbFromMinMax(lowCorner, highCorner);
}
} // namespace Meow2::Renderer
