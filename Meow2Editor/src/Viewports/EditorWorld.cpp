/// @file
/// @brief EditorWorld (EditorWorld.h).

#include "Viewports/EditorWorld.h"

#include <Meow2/Core/Memory/GlobalTracking.h>
#include <Meow2/Math/Quaternion.h>
#include <Meow2/Scene/Camera.h>

#include <array>
#include <cmath>

namespace Meow2::Editor
{
EditorWorld::EditorWorld(UuidGenerator& ids, Jobs::JobSystem& jobs)
    : m_ids(ids)
    , m_scene(std::make_unique<Scene::Scene>(ids, Memory::globalTracker(), jobs.threadCount()))
    , m_hierarchy(std::make_unique<Scene::TransformHierarchy>(*m_scene, jobs))
{
    // Every pool before any job touches the scene (techstack.md §25).
    m_scene->registerComponent<Scene::Camera>();
    m_scene->registerComponent<Renderer::MeshRenderer>();
}

Result<void> EditorWorld::buildDefaultScene(Renderer::GpuScene* gpu)
{
    // One cube mesh; each colour is its own mesh asset (the same GPU mesh with another material).
    const auto makeMeshAsset = [&](const Math::Color& color, float roughness) -> Result<AssetId>
    {
        const AssetId id = m_ids.next<IdKind::Asset>();
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

    Result<AssetId> ground = makeMeshAsset({0.35F, 0.35F, 0.38F, 1.0F}, 0.9F);
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
        Result<AssetId> cube = makeMeshAsset(c_colors[i], 0.4F);
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

void EditorWorld::update(Renderer::GpuScene* gpu)
{
    m_hierarchy->update();
    if (gpu == nullptr)
    {
        return;
    }
    if (const std::optional<Math::Aabb> box = Renderer::gatherInstances(*m_scene, m_meshes, m_instances))
    {
        m_bounds = Math::Sphere{.center = box->center,
                                .radius = std::sqrt(box->extents.x * box->extents.x + box->extents.y * box->extents.y +
                                                    box->extents.z * box->extents.z)};
    }
    gpu->setInstances(m_instances); // only what moved goes to the GPU
}
} // namespace Meow2::Editor
