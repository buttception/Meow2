/// @file
/// @brief EditorWorld (EditorWorld.h).

#include "Viewports/EditorWorld.h"

#include <Meow2/Assets/Gltf/GltfImporter.h>
#include <Meow2/Core/Memory/GlobalTracking.h>
#include <Meow2/Math/Quaternion.h>
#include <Meow2/Scene/Camera.h>

#include <algorithm>
#include <array>
#include <cmath>
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
    const entt::entity environment = fresh.scene->create("Environment");
    fresh.scene->registry().emplace<Scene::Environment>(environment);
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
    const std::optional<entt::entity> entity = Scene::findEnvironment(*m_scene);
    if (!entity)
    {
        return std::nullopt;
    }
    return m_scene->registry().get<Scene::Environment>(*entity);
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
} // namespace Meow2::Editor
