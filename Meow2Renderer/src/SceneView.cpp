/// @file
/// @brief SceneView: opening a model or the cube grid.

#include "SceneView.h"

#include <Meow2/Assets/Gltf/GltfImporter.h>
#include <Meow2/Assets/SceneFile.h>
#include <Meow2/Core/Memory/GlobalTracking.h>
#include <Meow2/Math/Transform.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <format>
#include <functional>
#include <optional>
#include <utility>

namespace Meow2::RendererApp
{
std::filesystem::path findSample(const std::filesystem::path& sampleRoot, const std::string& name)
{
    std::error_code error;
    const std::filesystem::path folder = sampleRoot / "Models" / name;
    for (const std::filesystem::path& file :
         {folder / "glTF" / (name + ".gltf"), folder / "glTF-Binary" / (name + ".glb")})
    {
        if (std::filesystem::exists(file, error))
        {
            return file;
        }
    }
    return {};
}

std::filesystem::path findSampleFile(const std::filesystem::path& sampleRoot, const std::string& name, bool binary)
{
    std::error_code error;
    const std::filesystem::path file = binary ? sampleRoot / "Models" / name / "glTF-Binary" / (name + ".glb")
                                              : sampleRoot / "Models" / name / "glTF" / (name + ".gltf");
    return std::filesystem::exists(file, error) ? file : std::filesystem::path{};
}

SceneView::SceneView(const SceneViewDeps& deps, std::string name)
    : m_deps(deps)
    , m_name(std::move(name))
    , m_world(std::make_unique<Scene::Scene>(deps.ids, Memory::globalTracker(), deps.jobs.threadCount()))
    , m_hierarchy(std::make_unique<Scene::TransformHierarchy>(*m_world, deps.jobs))
{
    deps.components.registerAll(*m_world); // every pool before anything touches it (techstack §25)
}

std::string folderMountName(const std::filesystem::path& folder)
{
    // A hash of the folder's path: the same folder always gets the same mount, so an asset's virtual path never
    // changes meaning (the asset manager keeps its records), and two folders never share one.
    const std::size_t hash = std::hash<std::string>{}(folder.lexically_normal().generic_string());
    return std::format("f{:016x}", static_cast<std::uint64_t>(hash));
}

std::filesystem::path scenePathFor(const std::filesystem::path& model)
{
    std::filesystem::path scene = model;
    scene.replace_extension(Assets::c_sceneExtension);
    return scene;
}

bool isSceneFile(const std::filesystem::path& file)
{
    return file.extension() == Assets::c_sceneExtension;
}

Result<std::string> SceneView::mountFolder(const SceneViewDeps& deps, const std::filesystem::path& folder)
{
    const std::string name = folderMountName(folder);
    if (!deps.files.isMounted(name))
    {
        if (Result<void> mounted = deps.files.mountDirectory(name, folder, FileSystem::MountAccess::ReadWrite);
            !mounted)
        {
            return Unexpected<Error>{mounted.error()};
        }
    }
    return name;
}

Result<std::unique_ptr<SceneView>> SceneView::open(const SceneViewDeps& deps, const std::filesystem::path& file)
{
    return isSceneFile(file) ? openScene(deps, file) : openModel(deps, file);
}

Result<std::unique_ptr<SceneView>> SceneView::openScene(const SceneViewDeps& deps, const std::filesystem::path& file)
{
    const auto start = std::chrono::steady_clock::now();
    Result<std::string> mount = mountFolder(deps, file.parent_path());
    if (!mount)
    {
        return Unexpected<Error>{mount.error()};
    }
    // The models' .meta files in the scene's folder and below say which model each mesh ID belongs to.
    if (Result<std::size_t> scanned = deps.assets.scan(*mount); !scanned)
    {
        return Unexpected<Error>{scanned.error()};
    }
    auto view = std::make_unique<SceneView>(deps, file.filename().string());
    const std::string path = *mount + "/" + file.filename().string();
    Result<Assets::SceneReadReport> read =
        Assets::loadScene(deps.files, path, *view->m_world, *view->m_hierarchy, deps.components);
    if (!read)
    {
        return Unexpected<Error>{read.error()};
    }
    for (const std::string& warning : read->warnings)
    {
        MEOW2_LOG_WARN(deps.log, "{}", warning);
    }
    const Renderer::SceneModels models =
        Renderer::importSceneModels(*view->m_world, deps.assets, view->m_meshes, deps.log);
    for (const std::string& warning : models.warnings)
    {
        MEOW2_LOG_WARN(deps.log, "{}: {}", view->m_name, warning);
    }
    const std::uint32_t instances = Renderer::sceneInstanceCount(*view->m_world, models, view->m_meshes);
    Result<std::unique_ptr<Renderer::GpuScene>> gpu =
        Renderer::GpuScene::create(deps.device, deps.pipelines,
                                   {.shader = deps.shader,
                                    .deferredShader = deps.deferredShader,
                                    .shadowShader = deps.shadowShader,
                                    .instanceCapacity = std::max(instances, 1U),
                                    .colorFormat = deps.colorFormat});
    if (!gpu)
    {
        return Unexpected<Error>{gpu.error()};
    }
    view->m_gpu = std::move(*gpu);
    view->m_gpu->setSky(deps.sky);
    if (Result<void> uploaded =
            Renderer::uploadSceneModels(*view->m_gpu, models, view->m_meshes, deps.ids, deps.log, deps.textures);
        !uploaded)
    {
        return Unexpected<Error>{uploaded.error()};
    }
    view->m_savePath = file;
    view->m_saveVirtualPath = path;
    view->finishLoading();
    view->m_importMilliseconds =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    MEOW2_LOG_INFO(deps.log, "Opened {}: {} entities ({} migrated, {} unknown components kept), {} models in {:.0f} ms",
                   view->m_name, read->entities, read->migrated, read->unknown, models.models.size(),
                   view->m_importMilliseconds);
    return view;
}

void SceneView::finishLoading()
{
    m_hierarchy->update();
    std::vector<Renderer::InstanceDesc> instances;
    if (const std::optional<Math::Aabb> bounds = Renderer::gatherInstances(*m_world, m_meshes, instances))
    {
        m_center = bounds->center;
        const Math::Vec3& e = bounds->extents;
        // A floor only against an empty or flat-point model: some samples are millimetres across (MetalRoughSpheres-
        // NoTextures is 7 mm), and a larger floor would frame them from too far away.
        m_radius = std::max(std::sqrt(e.x * e.x + e.y * e.y + e.z * e.z), 1e-5F);
    }
    m_gpu->setInstances(instances);
}

std::optional<Scene::Environment> SceneView::environment() const
{
    const std::optional<entt::entity> entity = Scene::findEnvironment(*m_world);
    if (!entity)
    {
        return std::nullopt;
    }
    return m_world->registry().get<Scene::Environment>(*entity);
}

Result<std::filesystem::path> SceneView::saveScene(const Scene::Environment& environment)
{
    if (m_savePath.empty())
    {
        return fail(ErrorCode::Unsupported,
                    m_saveProblem.empty() ? std::string{"this scene can't be saved"} : m_saveProblem);
    }
    entt::entity holder = Scene::findEnvironment(*m_world).value_or(entt::null);
    if (holder == entt::null)
    {
        holder = m_world->create("Environment");
    }
    m_world->registry().emplace_or_replace<Scene::Environment>(holder, environment);
    if (Result<void> saved = Assets::saveScene(m_deps.files, m_saveVirtualPath, *m_world, m_deps.components); !saved)
    {
        return Unexpected<Error>{saved.error()};
    }
    MEOW2_LOG_INFO(m_deps.log, "Saved {}", m_savePath.string());
    return m_savePath;
}

SceneView::~SceneView()
{
    // The GPU scene first: it's a device user, and its buffers go back to the device it was made on.
    m_gpu.reset();
    m_hierarchy.reset();
    m_world.reset();
}

Result<std::unique_ptr<SceneView>> SceneView::openModel(const SceneViewDeps& deps, const std::filesystem::path& file)
{
    const auto start = std::chrono::steady_clock::now();
    // The model's folder gets a read-write mount of its own, so its buffers and images are found next to it and its
    // .meta (stable mesh IDs) and scene file can be written there.
    Result<std::string> mount = mountFolder(deps, file.parent_path());
    if (!mount)
    {
        return Unexpected<Error>{mount.error()};
    }
    const std::string source = *mount + "/" + file.filename().string();
    Result<Assets::ImportedModel> model = Assets::importGltf(deps.files, source, deps.log);
    if (!model)
    {
        return Unexpected<Error>{model.error()};
    }

    auto view = std::make_unique<SceneView>(deps, file.filename().string());
    // Stable mesh IDs from the model's .meta, so a saved scene can name its meshes. A folder that can't be written
    // (a read-only share) still opens, with throw-away IDs; it just can't be saved as a scene.
    std::vector<AssetId> stableIds;
    Result<AssetId> modelId = deps.assets.importSource(source);
    if (modelId)
    {
        if (Result<std::vector<AssetId>> ids = deps.assets.meshIds(*modelId, model->meshes.size()))
        {
            stableIds = std::move(*ids);
        }
        else
        {
            view->m_saveProblem = std::format("can't keep the mesh IDs: {}", ids.error().message);
        }
    }
    else
    {
        view->m_saveProblem = std::format("can't write the model's .meta: {}", modelId.error().message);
    }
    if (view->m_saveProblem.empty())
    {
        view->m_savePath = scenePathFor(file);
        view->m_saveVirtualPath = *mount + "/" + view->m_savePath.filename().string();
    }
    else
    {
        MEOW2_LOG_WARN(deps.log, "{}: {}; it can't be saved as a scene", view->m_name, view->m_saveProblem);
    }
    std::uint32_t parts = 0;
    for (const Assets::ImportedNode& node : model->nodes)
    {
        parts += node.mesh >= 0
                     ? static_cast<std::uint32_t>(model->meshes[static_cast<std::size_t>(node.mesh)].primitives.size())
                     : 0;
    }
    Result<std::unique_ptr<Renderer::GpuScene>> gpu =
        Renderer::GpuScene::create(deps.device, deps.pipelines,
                                   {.shader = deps.shader,
                                    .deferredShader = deps.deferredShader,
                                    .shadowShader = deps.shadowShader,
                                    .instanceCapacity = std::max(parts, 1U),
                                    .colorFormat = deps.colorFormat});
    if (!gpu)
    {
        return Unexpected<Error>{gpu.error()};
    }
    view->m_gpu = std::move(*gpu);
    view->m_gpu->setSky(deps.sky);
    Result<std::vector<AssetId>> meshIds =
        Renderer::uploadModel(*view->m_gpu, *model, deps.ids, view->m_meshes, deps.log, deps.textures, stableIds);
    if (!meshIds)
    {
        return Unexpected<Error>{meshIds.error()};
    }
    view->m_nodeEntities = Renderer::instantiateModel(*view->m_world, *view->m_hierarchy, *model, *meshIds);
    for (std::size_t i = 0; i < model->nodes.size(); ++i)
    {
        const Assets::ImportedNode& node = model->nodes[i];
        view->m_restTransforms.push_back(node.local);
        if (node.camera >= 0)
        {
            const Assets::ImportedCamera& camera = model->cameras[static_cast<std::size_t>(node.camera)];
            std::string name = !camera.name.empty() ? camera.name : node.name;
            view->m_cameras.push_back({.name = name.empty() ? std::format("Camera {}", view->m_cameras.size()) : name,
                                       .node = static_cast<std::int32_t>(i),
                                       .lens = camera});
        }
    }
    // What the file's animations move casts into the sun's shadow every frame; the rest is cached. A node moves if a
    // channel drives it or any of its parents (parents come first, so one pass settles it). The editor will infer this
    // from components instead (M13).
    std::vector<bool> movable(model->nodes.size(), false);
    for (const Assets::ImportedAnimation& animation : model->animations)
    {
        for (const Assets::ImportedChannel& channel : animation.channels)
        {
            if (channel.node >= 0 && static_cast<std::size_t>(channel.node) < movable.size())
            {
                movable[static_cast<std::size_t>(channel.node)] = true;
            }
        }
    }
    for (std::size_t i = 0; i < model->nodes.size(); ++i)
    {
        const std::int32_t parent = model->nodes[i].parent;
        if (parent >= 0 && movable[static_cast<std::size_t>(parent)])
        {
            movable[i] = true;
        }
        if (movable[i])
        {
            if (auto* renderer = view->m_world->registry().try_get<Renderer::MeshRenderer>(view->m_nodeEntities[i]))
            {
                renderer->movable = true;
            }
        }
    }
    view->m_animations = std::move(model->animations);
    view->finishLoading();
    view->m_importMilliseconds =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    MEOW2_LOG_INFO(deps.log, "Opened {}: {} nodes, {} meshes, {} materials, {} images in {:.0f} ms", view->m_name,
                   model->nodes.size(), model->meshes.size(), model->materials.size(), model->images.size(),
                   view->m_importMilliseconds);
    return view;
}

void SceneView::pose(int animation, float seconds)
{
    const bool animated = animation >= 0 && static_cast<std::size_t>(animation) < m_animations.size();
    for (std::size_t i = 0; i < m_nodeEntities.size(); ++i)
    {
        const Math::Transform local =
            animated ? Assets::animatedTransform(m_animations[static_cast<std::size_t>(animation)],
                                                 static_cast<std::int32_t>(i), m_restTransforms[i], seconds)
                     : m_restTransforms[i];
        m_hierarchy->setLocal(m_nodeEntities[i], local);
    }
    m_hierarchy->update();
    std::vector<Renderer::InstanceDesc> instances;
    static_cast<void>(Renderer::gatherInstances(*m_world, m_meshes, instances));
    m_gpu->setInstances(instances);
}

Math::Mat4 SceneView::nodeWorld(std::int32_t node) const
{
    if (node < 0 || static_cast<std::size_t>(node) >= m_nodeEntities.size())
    {
        return Math::toMat4(Math::Transform{});
    }
    const auto* world =
        m_world->registry().try_get<Scene::WorldTransform>(m_nodeEntities[static_cast<std::size_t>(node)]);
    return world != nullptr ? world->matrix : Math::toMat4(Math::Transform{});
}

Result<std::unique_ptr<SceneView>> SceneView::cubeGrid(const SceneViewDeps& deps, std::uint32_t count)
{
    auto view = std::make_unique<SceneView>(deps, "Cube grid");
    Result<std::unique_ptr<Renderer::GpuScene>> gpu = Renderer::GpuScene::create(deps.device, deps.pipelines,
                                                                                 {.shader = deps.shader,
                                                                                  .deferredShader = deps.deferredShader,
                                                                                  .shadowShader = deps.shadowShader,
                                                                                  .instanceCapacity = count,
                                                                                  .colorFormat = deps.colorFormat});
    if (!gpu)
    {
        return Unexpected<Error>{gpu.error()};
    }
    view->m_gpu = std::move(*gpu);
    view->m_gpu->setSky(deps.sky);
    if (Result<std::uint32_t> cube = view->m_gpu->addMesh(Renderer::makeCube()); !cube)
    {
        return Unexpected<Error>{cube.error()};
    }
    // A cube 47 on a side, 3 units apart; no materials, so each cube's colour comes from its index.
    constexpr std::uint32_t c_side = 47;
    std::vector<Renderer::InstanceDesc> instances(count);
    for (std::uint32_t i = 0; i < count; ++i)
    {
        Math::Transform transform;
        transform.position = Math::WorldPosition{(static_cast<float>(i % c_side) - 23.0F) * 3.0F,
                                                 (static_cast<float>((i / c_side) % c_side) - 23.0F) * 3.0F,
                                                 (static_cast<float>(i / (c_side * c_side)) - 23.0F) * 3.0F};
        instances[i].world = Math::toMat4(transform);
        instances[i].material = i + 1;
    }
    view->m_gpu->setInstances(instances);
    view->m_radius = 70.0F;
    return view;
}
} // namespace Meow2::RendererApp
