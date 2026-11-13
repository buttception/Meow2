/// @file
/// @brief SceneView: opening a model or the cube grid.

#include "SceneView.h"

#include <Meow2/Assets/Gltf/GltfImporter.h>
#include <Meow2/Core/Memory/GlobalTracking.h>
#include <Meow2/Math/Transform.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <format>
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
    // Each model gets the `model` mount, so its buffers and images are found next to it.
    if (deps.files.isMounted("model"))
    {
        static_cast<void>(deps.files.unmount("model"));
    }
    if (Result<void> mounted = deps.files.mountDirectory("model", file.parent_path()); !mounted)
    {
        return Unexpected<Error>{mounted.error()};
    }
    Result<Assets::ImportedModel> model = Assets::importGltf(deps.files, "model/" + file.filename().string(), deps.log);
    if (!model)
    {
        return Unexpected<Error>{model.error()};
    }

    auto view = std::make_unique<SceneView>(deps, file.filename().string());
    std::uint32_t parts = 0;
    for (const Assets::ImportedNode& node : model->nodes)
    {
        parts += node.mesh >= 0
                     ? static_cast<std::uint32_t>(model->meshes[static_cast<std::size_t>(node.mesh)].primitives.size())
                     : 0;
    }
    Result<std::unique_ptr<Renderer::GpuScene>> gpu = Renderer::GpuScene::create(
        deps.device, deps.pipelines,
        {.shader = deps.shader, .instanceCapacity = std::max(parts, 1U), .colorFormat = deps.colorFormat});
    if (!gpu)
    {
        return Unexpected<Error>{gpu.error()};
    }
    view->m_gpu = std::move(*gpu);
    view->m_gpu->setSky(deps.sky);
    Result<std::vector<AssetId>> meshIds =
        Renderer::uploadModel(*view->m_gpu, *model, deps.ids, view->m_meshes, deps.log, deps.textures);
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
    view->m_animations = std::move(model->animations);
    view->m_hierarchy->update();
    std::vector<Shader::InstanceData> instances;
    if (const std::optional<Math::Aabb> bounds = Renderer::gatherInstances(*view->m_world, view->m_meshes, instances))
    {
        view->m_center = bounds->center;
        const Math::Vec3& e = bounds->extents;
        // A floor only against an empty or flat-point model: some samples are millimetres across (MetalRoughSpheres-
        // NoTextures is 7 mm), and a larger floor would frame them from too far away.
        view->m_radius = std::max(std::sqrt(e.x * e.x + e.y * e.y + e.z * e.z), 1e-5F);
    }
    view->m_gpu->setInstances(instances);
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
    std::vector<Shader::InstanceData> instances;
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
    Result<std::unique_ptr<Renderer::GpuScene>> gpu =
        Renderer::GpuScene::create(deps.device, deps.pipelines,
                                   {.shader = deps.shader, .instanceCapacity = count, .colorFormat = deps.colorFormat});
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
    std::vector<Shader::InstanceData> instances(count);
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
