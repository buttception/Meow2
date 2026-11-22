/// @file
/// @brief EditorWorld (EditorWorld.h).

#include "Viewports/EditorWorld.h"

#include <Meow2/Assets/Gltf/GltfImporter.h>
#include <Meow2/Core/Memory/GlobalTracking.h>
#include <Meow2/Math/Quaternion.h>
#include <Meow2/Scene/Camera.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <format>
#include <string>

namespace Meow2::Editor
{
EditorWorld::EditorWorld(const EditorWorldDeps& deps)
    : m_deps(deps)
{
    adopt(makeScene());
}

EditorWorld::Fresh EditorWorld::makeScene() const
{
    Fresh fresh;
    fresh.scene = std::make_unique<Scene::Scene>(m_deps.ids, Memory::globalTracker(), m_deps.jobs.threadCount());
    fresh.hierarchy = std::make_unique<Scene::TransformHierarchy>(*fresh.scene, m_deps.jobs);
    m_deps.components.registerAll(*fresh.scene); // every pool before any job touches the scene (techstack.md §25)
    return fresh;
}

void EditorWorld::adopt(Fresh fresh)
{
    // The old hierarchy goes before its scene (it's connected to the scene's signals).
    m_hierarchy = std::move(fresh.hierarchy);
    m_scene = std::move(fresh.scene);
    m_meshes = {};
    m_meshNames.clear();
    m_models.clear();
    m_instances.clear();
    m_bounds = {.center = {0.0F, 0.0F, 0.0F}, .radius = 5.0F};
    ++m_revision;
}

Result<void> EditorWorld::buildDefaultScene(Renderer::GpuScene* gpu)
{
    // One cube mesh; each colour is its own mesh asset (the same GPU mesh with another material).
    const auto makeMeshAsset = [&](const Math::Color& color, float roughness, std::string name) -> Result<AssetId>
    {
        const AssetId id = m_deps.ids.next<IdKind::Asset>();
        m_meshNames[id] = std::move(name);
        if (gpu == nullptr)
        {
            return id;
        }
        Result<std::uint32_t> mesh = gpu->addMesh(Renderer::makeCube());
        if (!mesh)
        {
            return Unexpected<Error>{mesh.error()};
        }
        Result<std::uint32_t> material =
            gpu->addMaterial({.baseColorFactor = color, .metallic = 0.0F, .roughness = roughness});
        if (!material)
        {
            return Unexpected<Error>{material.error()};
        }
        m_meshes.add(id, {Renderer::GpuSubmesh{.mesh = *mesh,
                                               .material = *material,
                                               .bounds = {.center = {}, .extents = {0.5F, 0.5F, 0.5F}}}});
        return id;
    };
    const auto spawn = [&](const char* name, const Math::Transform& transform, AssetId mesh)
    {
        const entt::entity entity = m_scene->create(name);
        m_hierarchy->add(entity, transform);
        m_scene->registry().emplace<Renderer::MeshRenderer>(entity, Renderer::MeshRenderer{.mesh = mesh});
        return entity;
    };

    Result<AssetId> ground = makeMeshAsset({0.35F, 0.35F, 0.38F, 1.0F}, 0.9F, "Test scene: ground");
    if (!ground)
    {
        return Unexpected<Error>{ground.error()};
    }
    static_cast<void>(
        spawn("Ground", {.position = {0.0F, -0.55F, 0.0F}, .rotation = {}, .scale = {12.0F, 0.1F, 12.0F}}, *ground));
    constexpr std::array<Math::Color, 5> c_colors{Math::Color{0.9F, 0.15F, 0.2F, 1.0F},
                                                  {0.95F, 0.6F, 0.1F, 1.0F},
                                                  {0.2F, 0.75F, 0.3F, 1.0F},
                                                  {0.15F, 0.45F, 0.9F, 1.0F},
                                                  {0.6F, 0.25F, 0.85F, 1.0F}};
    for (std::size_t i = 0; i < c_colors.size(); ++i)
    {
        Result<AssetId> cube = makeMeshAsset(c_colors[i], 0.4F, std::format("Test scene: cube {}", i + 1));
        if (!cube)
        {
            return Unexpected<Error>{cube.error()};
        }
        const float x = (static_cast<float>(i) - 2.0F) * 2.0F;
        static_cast<void>(
            spawn("Cube", {.position = {x, 0.0F, 0.0F}, .rotation = {}, .scale = {1.0F, 1.0F, 1.0F}}, *cube));
    }

    // The game's main camera: 8 m back on -Z, a little up, looking at the cubes.
    const entt::entity camera = m_scene->create("Main camera");
    const Math::Vec3 eye{0.0F, 2.5F, -8.0F};
    const Math::Vec3 toCubes{-eye.x, -eye.y, -eye.z};
    m_hierarchy->add(camera, {.position = eye, .rotation = Math::lookRotation(toCubes)});
    m_scene->registry().emplace<Scene::Camera>(camera, Scene::Camera{.main = true});

    m_bounds = {.center = {0.0F, 0.0F, 0.0F}, .radius = std::sqrt(6.0F * 6.0F * 2.0F)}; // the slab's corners
    return {};
}

void EditorWorld::update(Renderer::GpuScene* gpu, std::span<const EntityId> selected)
{
    m_hierarchy->update();
    if (gpu == nullptr)
    {
        return;
    }
    if (const std::optional<Math::Aabb> box =
            Renderer::gatherInstances(*m_scene, m_meshes, m_instances, &m_instanceEntities))
    {
        m_bounds = Math::Sphere{.center = box->center,
                                .radius = std::sqrt(box->extents.x * box->extents.x + box->extents.y * box->extents.y +
                                                    box->extents.z * box->extents.z)};
    }
    // Selecting a model's root outlines all of it: an instance is selected if its entity or an ancestor is.
    std::vector<entt::entity> roots;
    for (const EntityId id : selected)
    {
        if (const std::optional<entt::entity> entity = m_scene->find(id))
        {
            roots.push_back(*entity);
        }
    }
    m_anySelected = false;
    if (!roots.empty())
    {
        const auto& registry = m_scene->registry();
        for (std::size_t i = 0; i < m_instances.size(); ++i)
        {
            for (entt::entity at = m_instanceEntities[i]; at != entt::null && registry.valid(at);)
            {
                if (std::ranges::find(roots, at) != roots.end())
                {
                    m_instances[i].selected = true;
                    m_anySelected = true;
                    break;
                }
                const auto* parent = registry.try_get<Scene::Parent>(at);
                at = parent != nullptr ? parent->entity : entt::null;
            }
        }
    }
    gpu->setInstances(m_instances); // only what moved (or changed selection) goes to the GPU
}

entt::entity EditorWorld::instanceEntity(std::uint32_t slot) const
{
    return slot < m_instanceEntities.size() ? m_instanceEntities[slot] : entt::entity{entt::null};
}

void EditorWorld::newScene(const FreshGpu& freshGpu)
{
    if (freshGpu)
    {
        static_cast<void>(freshGpu()); // the old scene's meshes go
    }
    Fresh fresh = makeScene();
    // A camera to see the game through, 8 m back on -Z and a little up, looking at the origin; and the sun.
    const entt::entity camera = fresh.scene->create("Main camera");
    const Math::Vec3 eye{0.0F, 2.5F, -8.0F};
    fresh.hierarchy->add(camera, {.position = eye,
                                  .rotation = Math::lookRotation(Math::Vec3{-eye.x, -eye.y, -eye.z}),
                                  .scale = {1.0F, 1.0F, 1.0F}});
    fresh.scene->registry().emplace<Scene::Camera>(camera, Scene::Camera{.lens = {}, .main = true});
    Scene::setEnvironment(*fresh.scene, {}); // the sun and exposure: a scene setting, not an entity
    adopt(std::move(fresh));
}

Result<Assets::SceneReadReport> EditorWorld::openScene(std::string_view path, const FreshGpu& freshGpu)
{
    Fresh fresh = makeScene();
    Result<Assets::SceneReadReport> read =
        Assets::loadScene(m_deps.assets.files(), path, *fresh.scene, *fresh.hierarchy, m_deps.components);
    if (!read)
    {
        return Unexpected<Error>{read.error()};
    }
    for (const std::string& warning : read->warnings)
    {
        MEOW2_LOG_WARN(m_deps.log, "{}", warning);
    }
    adopt(std::move(fresh));
    Renderer::GpuScene* gpu = freshGpu ? freshGpu() : nullptr; // only now: a file that can't be read keeps the old
    // The models its MeshRenderers name, found by their stable mesh IDs in the models' .meta files.
    const Renderer::SceneModels models = Renderer::importSceneModels(*m_scene, m_deps.assets, m_meshes, m_deps.log);
    for (const Renderer::SceneModelImport& model : models.models)
    {
        const std::optional<Assets::AssetRecord> record = m_deps.assets.find(model.model);
        nameMeshes(record ? record->sourcePath : std::string{}, model.data, model.meshIds);
        if (record)
        {
            watchModel(record->sourcePath, model.meshIds);
        }
    }
    for (const std::string& warning : models.warnings)
    {
        MEOW2_LOG_WARN(m_deps.log, "{}: {}", path, warning);
    }
    if (gpu != nullptr)
    {
        if (Result<void> uploaded = Renderer::uploadSceneModels(*gpu, models, m_meshes, m_deps.ids, m_deps.log);
            !uploaded)
        {
            MEOW2_LOG_ERROR(m_deps.log, "{}: its models can't all be drawn: {}", path, uploaded.error());
        }
    }
    return read;
}

Result<void> EditorWorld::saveScene(std::string_view path) const
{
    return Assets::saveScene(m_deps.assets.files(), path, *m_scene, m_deps.components);
}

Result<std::vector<entt::entity>> EditorWorld::importModel(std::string_view source, Renderer::GpuScene* gpu)
{
    Result<Assets::ImportedModel> model = Assets::importGltf(m_deps.assets.files(), source, m_deps.log);
    if (!model)
    {
        return Unexpected<Error>{model.error()};
    }
    // Stable mesh IDs: the model's .meta keeps one per mesh, so a saved scene finds its meshes again.
    Result<AssetId> modelId = m_deps.assets.importSource(source);
    if (!modelId)
    {
        return Unexpected<Error>{modelId.error()};
    }
    Result<std::vector<AssetId>> meshIds = m_deps.assets.meshIds(*modelId, model->meshes.size());
    if (!meshIds)
    {
        return Unexpected<Error>{meshIds.error()};
    }
    // Uploaded once per model: a second import of the same model reuses its meshes.
    const bool uploaded = !meshIds->empty() && !m_meshes.find(meshIds->front()).empty();
    if (gpu != nullptr && !uploaded)
    {
        Result<std::vector<AssetId>> added =
            Renderer::uploadModel(*gpu, *model, m_deps.ids, m_meshes, m_deps.log, {}, *meshIds);
        if (!added)
        {
            return Unexpected<Error>{added.error()};
        }
    }
    nameMeshes(source, *model, *meshIds);
    watchModel(source, *meshIds);
    std::string name{source.substr(source.find_last_of('/') + 1)};
    name = name.substr(0, name.find_last_of('.'));
    const entt::entity root = m_scene->create(name);
    m_hierarchy->add(root);
    std::vector<entt::entity> entities{root};
    const std::vector<entt::entity> nodes = Renderer::instantiateModel(*m_scene, *m_hierarchy, *model, *meshIds, root);
    entities.insert(entities.end(), nodes.begin(), nodes.end());
    ++m_revision;
    return entities;
}

void EditorWorld::destroyEntities(std::span<const entt::entity> entities)
{
    ++m_revision;
    // Last first: nodes come after their parents, so children go before the entities they hang under.
    for (auto entity = entities.rbegin(); entity != entities.rend(); ++entity)
    {
        if (m_scene->contains(*entity))
        {
            m_scene->destroy(*entity);
        }
    }
}

std::optional<Scene::Environment> EditorWorld::environment() const
{
    if (!Scene::hasEnvironment(*m_scene))
    {
        return std::nullopt;
    }
    return Scene::environment(*m_scene);
}

void EditorWorld::nameMeshes(std::string_view source, const Assets::ImportedModel& model,
                             std::span<const AssetId> meshIds)
{
    // "Sponza: mesh_12": the model's file name without its folder and extension, then the mesh's own name.
    std::string stem{source.substr(source.find_last_of('/') + 1)};
    stem = stem.substr(0, stem.find_last_of('.'));
    for (std::size_t i = 0; i < meshIds.size() && i < model.meshes.size(); ++i)
    {
        const std::string& mesh = model.meshes[i].name;
        const std::string part = mesh.empty() ? std::format("mesh {}", i + 1) : mesh;
        m_meshNames[meshIds[i]] = stem.empty() ? part : stem + ": " + part;
    }
}

std::optional<Math::Sphere> EditorWorld::boundsOf(entt::entity entity) const
{
    const auto& registry = m_scene->registry();
    if (!registry.valid(entity))
    {
        return std::nullopt;
    }
    // Every mesh part of the entity and the entities under it, in world space: a model root frames all of it.
    const auto isUnder = [&registry, entity](entt::entity at)
    {
        for (; at != entt::null && registry.valid(at);)
        {
            if (at == entity)
            {
                return true;
            }
            const auto* parent = registry.try_get<Scene::Parent>(at);
            at = parent != nullptr ? parent->entity : entt::null;
        }
        return false;
    };
    bool any = false;
    Math::Vec3 low{};
    Math::Vec3 high{};
    for (const auto [at, renderer] : registry.storage<Renderer::MeshRenderer>()->each())
    {
        const auto* world = registry.try_get<Scene::WorldTransform>(at);
        if (world == nullptr || !isUnder(at))
        {
            continue;
        }
        for (const Renderer::GpuSubmesh& part : m_meshes.find(renderer.mesh))
        {
            const Math::MinMax box = Math::toMinMax(Math::transformAabb(part.bounds, Math::load(world->matrix)));
            low = any ? Math::Vec3{std::min(low.x, box.min.x), std::min(low.y, box.min.y), std::min(low.z, box.min.z)}
                      : box.min;
            high =
                any ? Math::Vec3{std::max(high.x, box.max.x), std::max(high.y, box.max.y), std::max(high.z, box.max.z)}
                    : box.max;
            any = true;
        }
    }
    if (!any)
    {
        // Nothing drawn (an empty, a camera, no GPU): a metre around where it is.
        const auto* world = registry.try_get<Scene::WorldTransform>(entity);
        if (world == nullptr)
        {
            return std::nullopt;
        }
        return Math::Sphere{.center = {world->matrix._41, world->matrix._42, world->matrix._43}, .radius = 1.0F};
    }
    const Math::Vec3 half{(high.x - low.x) * 0.5F, (high.y - low.y) * 0.5F, (high.z - low.z) * 0.5F};
    return Math::Sphere{.center = {low.x + half.x, low.y + half.y, low.z + half.z},
                        .radius = std::max(std::sqrt(half.x * half.x + half.y * half.y + half.z * half.z), 0.01F)};
}

std::string EditorWorld::meshName(AssetId mesh) const
{
    const auto found = m_meshNames.find(mesh);
    return found != m_meshNames.end() ? found->second : std::string{};
}

std::vector<std::pair<AssetId, std::string>> EditorWorld::meshNames() const
{
    std::vector<std::pair<AssetId, std::string>> out(m_meshNames.begin(), m_meshNames.end());
    std::ranges::sort(out, [](const auto& a, const auto& b) { return a.second < b.second; });
    return out;
}

namespace
{
/// @brief The newest write time of the files in a model's folder (its .gltf, .bin and textures), .meta sidecars aside.
std::filesystem::file_time_type newestIn(const std::filesystem::path& folder)
{
    // min(), not {}: libstdc++'s file clock counts from 2174, so today's file times are negative there and a
    // zero start would always win (hot reload never saw a change on Linux).
    std::filesystem::file_time_type newest = std::filesystem::file_time_type::min();
    std::error_code error;
    for (auto it = std::filesystem::recursive_directory_iterator(folder, error);
         !error && it != std::filesystem::recursive_directory_iterator(); it.increment(error))
    {
        if (it->is_regular_file(error) && it->path().extension() != ".meta")
        {
            newest = std::max(newest, it->last_write_time(error));
        }
    }
    return newest;
}
} // namespace

void EditorWorld::watchModel(std::string_view source, std::span<const AssetId> meshIds)
{
    const Result<std::filesystem::path> native = m_deps.assets.files().nativePath(source);
    if (!native)
    {
        return; // not on disk (a test's memory mount): nothing to watch
    }
    if (std::ranges::any_of(m_models, [source](const WatchedModel& model) { return model.source == source; }))
    {
        return;
    }
    const std::filesystem::path folder = native->parent_path();
    m_models.push_back({.source = std::string{source},
                        .folder = folder,
                        .meshIds = {meshIds.begin(), meshIds.end()},
                        .newest = newestIn(folder)});
}

std::size_t EditorWorld::reloadChangedModels(Renderer::GpuScene* gpu, bool now)
{
    // Twice a second: a look at a model folder's file times costs little, but not at every frame.
    const auto clock = std::chrono::steady_clock::now();
    if (!now && clock - m_lastReloadCheck < std::chrono::milliseconds(500))
    {
        return 0;
    }
    m_lastReloadCheck = clock;
    std::size_t reloaded = 0;
    for (WatchedModel& model : m_models)
    {
        const std::filesystem::file_time_type newest = newestIn(model.folder);
        if (newest <= model.newest)
        {
            continue;
        }
        // A file written moments ago may still be being written (an image editor saving): wait for it to settle.
        if (!now && std::filesystem::file_time_type::clock::now() - newest < std::chrono::milliseconds(300))
        {
            continue;
        }
        model.newest = newest;
        Result<Assets::ImportedModel> imported = Assets::importGltf(m_deps.assets.files(), model.source, m_deps.log);
        if (!imported)
        {
            MEOW2_LOG_WARN(m_deps.log, "{} changed but can't be read: {}; the old one stays", model.source,
                           imported.error());
            continue;
        }
        if (gpu != nullptr)
        {
            // The same mesh IDs: the scene's MeshRenderers point at them, so they draw the new meshes, materials and
            // textures from the next update on.
            Result<std::vector<AssetId>> uploaded =
                Renderer::uploadModel(*gpu, *imported, m_deps.ids, m_meshes, m_deps.log, {}, model.meshIds);
            if (!uploaded)
            {
                MEOW2_LOG_WARN(m_deps.log, "{} changed but can't be uploaded: {}", model.source, uploaded.error());
                continue;
            }
        }
        nameMeshes(model.source, *imported, model.meshIds);
        MEOW2_LOG_INFO(m_deps.log, "Reloaded {} (its file or a texture changed)", model.source);
        ++m_revision;
        ++reloaded;
    }
    return reloaded;
}
} // namespace Meow2::Editor
