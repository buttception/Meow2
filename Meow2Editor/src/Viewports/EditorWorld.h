#pragma once

/// @file
/// @brief EditorWorld: the scene the editor shows, until scene files (M7 PR 3): entities, transforms and the GPU meshes
///        they draw.

#include <Meow2/Core/Id/UuidGenerator.h>
#include <Meow2/Core/Jobs/JobSystem.h>
#include <Meow2/Core/Result.h>
#include <Meow2/Graphics/Renderer/GpuScene.h>
#include <Meow2/Graphics/Renderer/SceneModel.h>
#include <Meow2/Math/Bounds.h>
#include <Meow2/Scene/Scene.h>
#include <Meow2/Scene/TransformHierarchy.h>

#include <memory>
#include <vector>

namespace Meow2::Editor
{
/// @brief The editor's scene: a `Scene` with its transform hierarchy, the `Camera` and `MeshRenderer` components
///        registered, and the GPU meshes its entities draw.
///
/// Until scene files exist (M7 PR 3) it holds a small test scene: a ground slab, a row of coloured cubes and a main
/// camera looking at them, so both viewports have something to show. update() keeps the GPU scene in step: it
/// updates the world transforms and hands every instance to GpuScene::setInstances(), which uploads only what moved.
/// @par Thread safety
/// Not thread-safe; the main thread (the transform update runs jobs internally).
/// @par Lifetime
/// The UuidGenerator and the JobSystem must outlive it.
class EditorWorld
{
public:
    /// @brief An empty world.
    /// @param ids  Makes entity and asset IDs.
    /// @param jobs Runs the transform hierarchy's update.
    EditorWorld(UuidGenerator& ids, Jobs::JobSystem& jobs);

    /// @brief Builds the test scene: a 12 × 12 m ground slab, five coloured cubes and a main camera 8 m back.
    /// @param gpu Where the meshes and materials go; null without a GPU (the entities are still made, so cameras and
    ///            commands work in tests and on the offscreen platform).
    /// @return Nothing, or the GPU scene's error.
    Result<void> buildDefaultScene(Renderer::GpuScene* gpu);

    /// @brief Updates world transforms, and the GPU scene's instances from them.
    /// @param gpu The GPU scene; null to update the transforms only.
    void update(Renderer::GpuScene* gpu);

    /// @brief A sphere around everything in the scene, for framing it (F).
    /// @return The sphere; a 5 m one at the origin for an empty scene.
    [[nodiscard]] Math::Sphere bounds() const
    {
        return m_bounds;
    }

    /// @brief The scene.
    /// @return It.
    [[nodiscard]] Scene::Scene& scene()
    {
        return *m_scene;
    }

    /// @brief Its transform hierarchy.
    /// @return It.
    [[nodiscard]] Scene::TransformHierarchy& hierarchy()
    {
        return *m_hierarchy;
    }

private:
    UuidGenerator& m_ids;
    std::unique_ptr<Scene::Scene> m_scene;                  ///< Behind a pointer: Scene can't move.
    std::unique_ptr<Scene::TransformHierarchy> m_hierarchy; ///< Likewise; destroyed before the scene.
    Renderer::RenderMeshes m_meshes;                        ///< Mesh AssetId → GPU mesh and material.
    std::vector<Renderer::InstanceDesc> m_instances;        ///< Reused each update.
    Math::Sphere m_bounds{.center = {0.0F, 0.0F, 0.0F}, .radius = 5.0F};
};
} // namespace Meow2::Editor
