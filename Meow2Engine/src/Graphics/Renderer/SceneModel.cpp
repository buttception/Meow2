/// @file
/// @brief Imported models in the Scene and on the GPU.

#include "Meow2/Graphics/Renderer/SceneModel.h"

#include "Meow2/Core/Assert.h"
#include "Meow2/Graphics/Renderer/MeshProcessing.h"

#include <DirectXMath.h>

#include <algorithm>
#include <array>
#include <format>
#include <optional>
#include <unordered_set>
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
                                         RenderMeshes& meshes, Log::Logger& log, const TextureCooking& cooking,
                                         std::span<const AssetId> givenIds)
{
    MEOW2_ASSERT(givenIds.empty() || givenIds.size() == model.meshes.size(), "one mesh ID per mesh, or none");
    // Only images a material uses are uploaded, each once per kind: colour (base colour, emissive) is sRGB, data
    // (metallic-roughness, occlusion) linear, normal maps BC5 when compressed. An image used two ways is cooked twice.
    std::vector<std::array<std::int32_t, 3>> textureOf(model.images.size(), {-1, -1, -1});

    // Cooking (mipmaps, and compression when asked) is most of a big model's upload, and each texture is independent:
    // with a job system, every texture the materials use is cooked in parallel first. They're still added to the scene
    // one by one below, in the same order as without jobs, so texture indices don't depend on threads.
    const auto cookIndex = [](std::int32_t image, Assets::TextureKind kind)
    {
        return static_cast<std::size_t>(image) * 3 + static_cast<std::size_t>(kind);
    };
    std::vector<std::optional<Result<Assets::CookedTexture>>> precooked(model.images.size() * 3);
    if (cooking.jobs != nullptr)
    {
        std::vector<std::pair<std::int32_t, Assets::TextureKind>> wanted;
        const auto want = [&](const Assets::ImportedTexture& imported, Assets::TextureKind kind)
        {
            const std::int32_t image = imported.image;
            if (image >= 0 && static_cast<std::size_t>(image) < model.images.size() &&
                !model.images[static_cast<std::size_t>(image)].pixels.empty() &&
                std::ranges::find(wanted, std::pair{image, kind}) == wanted.end())
            {
                wanted.emplace_back(image, kind);
            }
        };
        for (const Assets::ImportedMaterial& material : model.materials)
        {
            want(material.baseColor, Assets::TextureKind::Color);
            want(material.metallicRoughness, Assets::TextureKind::Data);
            want(material.normal, Assets::TextureKind::NormalMap);
            want(material.occlusion, Assets::TextureKind::Data);
            want(material.emissive, Assets::TextureKind::Color);
        }
        const Jobs::Job cookAll = cooking.jobs->parallelFor(
            static_cast<std::uint32_t>(wanted.size()), 1,
            [&](std::uint32_t begin, std::uint32_t end, std::uint32_t /*thread*/)
            {
                for (std::uint32_t i = begin; i < end; ++i)
                {
                    const auto [image, kind] = wanted[i];
                    const Assets::Image& source = model.images[static_cast<std::size_t>(image)];
                    precooked[cookIndex(image, kind)] =
                        cooking.cache != nullptr ? cooking.cache->cook(source, kind, cooking.compress, cooking.jobs)
                                                 : Assets::cookTexture(source, kind, cooking.compress, cooking.jobs);
                }
            });
        cooking.jobs->wait(cookAll);
    }

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
            std::optional<Result<Assets::CookedTexture>>& ready = precooked[cookIndex(image, kind)];
            Result<Assets::CookedTexture> cooked =
                ready                      ? std::move(*ready)
                : cooking.cache != nullptr ? cooking.cache->cook(source, kind, cooking.compress, cooking.jobs)
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
        const AssetId id = givenIds.empty() ? ids.next<AssetId::c_kind>() : givenIds[meshIds.size()];
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
                                          std::vector<InstanceDesc>& out, std::vector<entt::entity>* entities)
{
    using namespace DirectX;
    out.clear();
    if (entities != nullptr)
    {
        entities->clear();
    }
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
        // Movable things are drawn into the sun's shadow every frame; the rest into its cache (until M14 infers it,
        // Auto counts as static).
        const auto* local = registry.try_get<Scene::LocalTransform>(entity);
        const bool movable = local != nullptr && local->mobility == Scene::Mobility::Movable;
        const XMMATRIX matrix = XMLoadFloat4x4(&world->matrix);
        for (const GpuSubmesh& part : meshes.find(renderer.mesh))
        {
            out.push_back({.world = world->matrix, .mesh = part.mesh, .material = part.material, .movable = movable});
            if (entities != nullptr)
            {
                entities->push_back(entity);
            }

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

void registerRenderComponents(Scene::ComponentRegistry& registry)
{
    Scene::ComponentDesc meshRenderer = Scene::component<MeshRenderer>(
        "MeshRenderer", 2, {Scene::field<MeshRenderer>("mesh", [](MeshRenderer& c) -> auto& { return c.mesh; })});
    // Version 2 dropped `movable`: whether something moves is the transform's `mobility`, one field for every
    // system. A flag in an older file isn't carried over (a migration sees one component only); Meow2Renderer marks
    // animated nodes again each time it loads them.
    meshRenderer.migrations.push_back({.from = 1,
                                       .up = [](Scene::FieldTable& table) { table.erase("movable"); },
                                       .down =
                                           [](Scene::FieldTable& table)
                                       {
                                           table.emplace("movable", false);
                                       }});
    registry.add(std::move(meshRenderer));
}

SceneModels importSceneModels(Scene::Scene& scene, Assets::AssetManager& assets, const RenderMeshes& meshes,
                              Log::Logger& log, Jobs::JobSystem* jobs)
{
    SceneModels report;
    // Which models: every mesh a MeshRenderer names that isn't on the GPU yet, grouped by its model, so a model used
    // by many entities is imported once.
    std::vector<AssetId> models;
    std::unordered_set<AssetId> missing;
    for (const auto [entity, renderer] : scene.registry().view<MeshRenderer>().each())
    {
        if (!renderer.mesh.isValid() || !meshes.find(renderer.mesh).empty() || missing.contains(renderer.mesh))
        {
            continue;
        }
        const std::optional<Assets::SubAssetRecord> sub = assets.findSubAsset(renderer.mesh);
        if (!sub)
        {
            missing.insert(renderer.mesh);
            report.warnings.push_back(
                std::format("mesh {} isn't in any model's .meta; it isn't drawn", renderer.mesh.uuid().toString()));
            continue;
        }
        if (std::ranges::find(models, sub->owner) == models.end())
        {
            models.push_back(sub->owner);
        }
    }
    for (const AssetId modelId : models)
    {
        const std::optional<Assets::AssetRecord> record = assets.find(modelId);
        if (!record)
        {
            report.warnings.push_back(std::format("model {} isn't registered", modelId.uuid().toString()));
            continue;
        }
        Result<Assets::ImportedModel> model = Assets::importGltf(assets.files(), record->sourcePath, log, jobs);
        if (!model)
        {
            report.warnings.push_back(std::format("{}: {}", record->sourcePath, model.error().message));
            continue;
        }
        Result<std::vector<AssetId>> meshIds = assets.meshIds(modelId, model->meshes.size());
        if (!meshIds)
        {
            report.warnings.push_back(std::format("{}: {}", record->sourcePath, meshIds.error().message));
            continue;
        }
        report.models.push_back({.model = modelId, .data = std::move(*model), .meshIds = std::move(*meshIds)});
    }
    return report;
}

std::uint32_t sceneInstanceCount(Scene::Scene& scene, const SceneModels& models, const RenderMeshes& meshes)
{
    std::unordered_map<AssetId, std::uint32_t> parts;
    for (const SceneModelImport& model : models.models)
    {
        for (std::size_t i = 0; i < model.meshIds.size(); ++i)
        {
            parts[model.meshIds[i]] = static_cast<std::uint32_t>(model.data.meshes[i].primitives.size());
        }
    }
    std::uint32_t count = 0;
    for (const auto [entity, renderer] : scene.registry().view<MeshRenderer>().each())
    {
        const auto known = parts.find(renderer.mesh);
        count += known != parts.end() ? known->second : static_cast<std::uint32_t>(meshes.find(renderer.mesh).size());
    }
    return count;
}

Result<void> uploadSceneModels(GpuScene& gpu, const SceneModels& models, RenderMeshes& meshes, UuidGenerator& ids,
                               Log::Logger& log, const TextureCooking& cooking)
{
    for (const SceneModelImport& model : models.models)
    {
        Result<std::vector<AssetId>> uploaded = uploadModel(gpu, model.data, ids, meshes, log, cooking, model.meshIds);
        if (!uploaded)
        {
            return Unexpected<Error>{uploaded.error()};
        }
    }
    return {};
}

Result<SceneModelsReport> loadSceneModels(GpuScene& gpu, Scene::Scene& scene, Assets::AssetManager& assets,
                                          RenderMeshes& meshes, UuidGenerator& ids, Log::Logger& log,
                                          const TextureCooking& cooking)
{
    SceneModels models = importSceneModels(scene, assets, meshes, log, cooking.jobs);
    if (Result<void> uploaded = uploadSceneModels(gpu, models, meshes, ids, log, cooking); !uploaded)
    {
        return Unexpected<Error>{uploaded.error()};
    }
    return SceneModelsReport{
        .models = models.models.size(), .meshes = meshes.size(), .warnings = std::move(models.warnings)};
}
} // namespace Meow2::Renderer
