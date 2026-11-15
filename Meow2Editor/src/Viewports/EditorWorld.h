#pragma once

/// @file
/// @brief EditorWorld: the scene the editor shows: its entities and transforms, the GPU meshes they draw, and opening,
///        saving and starting scenes.

#include <Meow2/Assets/AssetManager.h>
#include <Meow2/Assets/SceneFile.h>
#include <Meow2/Core/Id/UuidGenerator.h>
#include <Meow2/Core/Jobs/JobSystem.h>
#include <Meow2/Core/Log/Logger.h>
#include <Meow2/Core/Result.h>
#include <Meow2/Graphics/Renderer/GpuScene.h>
#include <Meow2/Graphics/Renderer/SceneModel.h>
#include <Meow2/Math/Bounds.h>
#include <Meow2/Scene/ComponentRegistry.h>
#include <Meow2/Scene/Environment.h>
#include <Meow2/Scene/Scene.h>
#include <Meow2/Scene/TransformHierarchy.h>

#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace Meow2::Editor
{
/// @brief What an EditorWorld uses. Plain data; everything must outlive the world.
struct EditorWorldDeps
{
    UuidGenerator& ids;                         ///< Entity and asset IDs.
    Jobs::JobSystem& jobs;                      ///< Runs the transform hierarchy's update.
    const Scene::ComponentRegistry& components; ///< What scene files save and load.
    Assets::AssetManager& assets;               ///< Models' stable mesh IDs; a scene's models by mesh ID.
    Log::Logger& log;                           ///< Import and scene-file warnings.
};

/// @brief The editor's scene: a `Scene` with its transform hierarchy and the registry's components, and the GPU meshes
///        its entities draw.
///
/// newScene() starts an empty one (a main camera and an Environment), openScene() and saveScene() read and write
/// `.m2scene` files (docs/wiki/SceneFiles.md), importModel() brings a glTF model in. update() keeps the GPU scene in
/// step: it updates the world transforms and hands every instance to GpuScene::setInstances(), which uploads only what
/// moved. Without a GPU (tests, the offscreen platform) everything but the upload still happens.
/// @par Thread safety
/// Not thread-safe; the main thread (the transform update runs jobs internally).
/// @par Lifetime
/// Opening or starting a scene replaces the Scene: references from scene() don't outlive that.
class EditorWorld
{
public:
    /// @brief A world with an empty scene.
    /// @param deps What it uses.
    explicit EditorWorld(const EditorWorldDeps& deps);

    /// @brief Builds the test scene into the current one: a 12 × 12 m ground slab, five coloured cubes and a main
    ///        camera 8 m back. Its meshes are made on the fly (no model files), so it's for tests, not for saving.
    /// @param gpu Where the meshes and materials go; null without a GPU.
    /// @return Nothing, or the GPU scene's error.
    Result<void> buildDefaultScene(Renderer::GpuScene* gpu);

    /// @brief Gives an empty GPU scene for a new or opened scene (EditorRenderer::resetScene()), or null without a GPU.
    using FreshGpu = std::function<Renderer::GpuScene*()>;

    /// @brief Replaces the scene with a new one: a main camera looking at the origin and an Environment.
    /// @param freshGpu Empties the GPU scene; unset without a GPU.
    void newScene(const FreshGpu& freshGpu = {});

    /// @brief Replaces the scene with a scene file's, and uploads the models it uses.
    /// @param path Virtual path of the `.m2scene` (its models' `.meta` files must be registered with the assets).
    /// @param freshGpu Empties the GPU scene, called only once the file was read; unset without a GPU.
    /// @return What was read (warnings are logged too), or the error; on an error the current scene and the GPU
    ///         scene are kept as they were.
    Result<Assets::SceneReadReport> openScene(std::string_view path, const FreshGpu& freshGpu = {});

    /// @brief Writes the scene to a `.m2scene` file.
    /// @param path Virtual path (a writable mount).
    /// @return Nothing, or the error.
    Result<void> saveScene(std::string_view path) const;

    /// @brief Brings a glTF model into the scene: a root entity named after the file, the model's nodes under it, its
    ///        meshes with stable IDs from its `.meta` (made on first import).
    /// @param source Virtual path of the `.gltf`/`.glb` (a writable mount, for its `.meta`).
    /// @param gpu    Where its meshes go (once per model); null without a GPU.
    /// @return The entities made, the root first; or the error (nothing is added then).
    Result<std::vector<entt::entity>> importModel(std::string_view source, Renderer::GpuScene* gpu);

    /// @brief Destroys entities (an undone import), children before parents.
    /// @param entities The entities; ones already gone are skipped.
    void destroyEntities(std::span<const entt::entity> entities);

    /// @brief Updates world transforms, and the GPU scene's instances from them.
    /// @param gpu The GPU scene; null to update the transforms only.
    void update(Renderer::GpuScene* gpu);

    /// @brief The scene's Environment, if it has one.
    /// @return A copy, or nothing.
    [[nodiscard]] std::optional<Scene::Environment> environment() const;

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
    /// @brief A scene with the registry's components and a hierarchy, ready to fill.
    struct Fresh
    {
        std::unique_ptr<Scene::Scene> scene;
        std::unique_ptr<Scene::TransformHierarchy> hierarchy;
    };

    [[nodiscard]] Fresh makeScene() const;

    /// @brief Makes `fresh` the current scene; its meshes start empty.
    void adopt(Fresh fresh);

    EditorWorldDeps m_deps;
    std::unique_ptr<Scene::Scene> m_scene;                  ///< Behind a pointer: Scene can't move.
    std::unique_ptr<Scene::TransformHierarchy> m_hierarchy; ///< Likewise; destroyed before the scene.
    Renderer::RenderMeshes m_meshes;                        ///< Mesh AssetId → GPU mesh and material.
    std::vector<Renderer::InstanceDesc> m_instances;        ///< Reused each update.
    Math::Sphere m_bounds{.center = {0.0F, 0.0F, 0.0F}, .radius = 5.0F};
};
} // namespace Meow2::Editor
