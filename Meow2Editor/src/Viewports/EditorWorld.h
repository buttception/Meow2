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
#include <Meow2/Physics/CollisionMeshes.h>
#include <Meow2/Scene/ComponentRegistry.h>
#include <Meow2/Scene/Environment.h>
#include <Meow2/Scene/Scene.h>
#include <Meow2/Scene/TransformHierarchy.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Meow2::Editor
{
/// @brief The light a new scene starts with (`scene.new`): a sun 55° high, behind the default main camera (which
///        stands on −Z looking +Z, so the sun's azimuth is about 180°), and an exposure for daylight. The engine's
///        own Environment defaults (35° high, in front of that camera, exposure 1) read as dusk in the editor.
///        Changing these doesn't touch saved scenes: each file keeps its own Environment.
inline constexpr Scene::Environment c_newSceneEnvironment{.sunElevation = 55.0F,
                                                          .sunAzimuth = 200.0F,
                                                          .sunColor = {1.0F, 1.0F, 1.0F, 1.0F},
                                                          .sunIntensity = 3.0F,
                                                          .exposure = 2.5F};

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

    /// @brief Replaces the scene with a new one: a main camera looking at the origin, and c_newSceneEnvironment.
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
    /// @param gpu      The GPU scene; null to update the transforms only.
    /// @param selected The selection: their instances, and their children's, are marked InstanceDesc::selected (the
    ///                 outline); IDs not in the scene are ignored.
    void update(Renderer::GpuScene* gpu, std::span<const EntityId> selected = {});

    /// @brief The entity a GPU instance slot was drawn for by the last update() (a pick names slots).
    /// @param slot The slot.
    /// @return The entity, or entt::null for a slot out of range; it may have been destroyed since (check it).
    [[nodiscard]] entt::entity instanceEntity(std::uint32_t slot) const;

    /// @brief Whether the last update() marked any instance selected.
    /// @return True if one is.
    [[nodiscard]] bool anySelectedInstance() const
    {
        return m_anySelected;
    }

    /// @brief A number that changes whenever the scene does (a new or opened scene, an import, an edit command), so
    ///        panels rebuild only when there's something new to show.
    /// @return The revision.
    [[nodiscard]] std::uint64_t revision() const
    {
        return m_revision;
    }

    /// @brief A new entity ID from the editor's generator (for copies made from snapshots).
    /// @return The ID.
    [[nodiscard]] EntityId newEntityId()
    {
        return m_deps.ids.next<IdKind::Entity>();
    }

    /// @brief Says the scene changed (edit commands call it).
    void markChanged()
    {
        ++m_revision;
    }

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

    /// @brief Reloads the models whose files changed on disk since they were loaded (the .gltf, its .bin or a
    ///        texture), at most twice a second: re-imported and uploaded again under the same mesh IDs, so every
    ///        entity drawing them shows the new meshes, materials and textures. The model's node tree isn't
    ///        changed (entities keep their edits); a file that can't be read keeps the old model.
    /// @param gpu Where to upload; null re-imports only (tests, no GPU).
    /// @param now Look at once, without waiting for the half second or for a fresh file to settle (tests).
    /// @return How many models were reloaded.
    std::size_t reloadChangedModels(Renderer::GpuScene* gpu, bool now = false);

    /// @brief A sphere around an entity and everything under it, as drawn (its meshes' boxes in world space), for
    ///        framing it (double-click in the Hierarchy, F).
    /// @param entity The entity.
    /// @return The sphere; a 1 m one at its position when nothing under it is drawn; nothing for an entity that's gone
    ///         or has no transform.
    [[nodiscard]] std::optional<Math::Sphere> boundsOf(entt::entity entity) const;

    /// @brief The scene, read-only.
    /// @return It.
    [[nodiscard]] const Scene::Scene& scene() const
    {
        return *m_scene;
    }

    /// @brief A mesh's name for people: "<model file>: <mesh>" for imported models, as the Inspector shows it.
    /// @param mesh The mesh's AssetId.
    /// @return The name, or empty for a mesh this scene hasn't loaded.
    [[nodiscard]] std::string meshName(AssetId mesh) const;

    /// @brief Every mesh the scene has loaded, with its name, sorted by name (the Inspector's mesh drop-down).
    /// @return The meshes.
    [[nodiscard]] std::vector<std::pair<AssetId, std::string>> meshNames() const;

    /// @brief Its transform hierarchy.
    /// @return It.
    [[nodiscard]] Scene::TransformHierarchy& hierarchy()
    {
        return *m_hierarchy;
    }

    /// @brief The job system it was made with (the transform update runs on it; so does Play's physics).
    /// @return It.
    [[nodiscard]] Jobs::JobSystem& jobs()
    {
        return m_deps.jobs;
    }

    /// @brief The triangles of the scene's meshes, for mesh Colliders (kept whenever a model loads or reloads).
    /// @return Them.
    [[nodiscard]] const Physics::CollisionMeshes& collisionMeshes() const
    {
        return m_collisionMeshes;
    }

    /// @brief The scene as `.m2scene` text: Play's snapshot.
    /// @return The text, or the writer's error.
    [[nodiscard]] Result<std::string> snapshot() const;

    /// @brief Replaces the scene with a snapshot's (Stop). Unlike openScene(), the loaded meshes stay: the snapshot is
    ///        this scene from before Play, so it uses the same models.
    /// @param text From snapshot().
    /// @return Nothing, or the reader's error (the current scene is kept then).
    [[nodiscard]] Result<void> restore(std::string_view text);

    /// @brief Whether the scene is being simulated (Play): the scene file commands are off meanwhile, since saving
    ///        would save the simulation's state.
    /// @return True between Play and Stop.
    [[nodiscard]] bool playing() const
    {
        return m_playing;
    }

    /// @brief Marks the scene as being simulated or not; PlayMode sets it.
    /// @param playing True from Play to Stop.
    void setPlaying(bool playing)
    {
        m_playing = playing;
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

    /// @brief Starts watching a model's folder for reloadChangedModels(); once per model.
    /// @param source  The model's virtual path (a folder mount, so it has a native path).
    /// @param meshIds Its stable mesh IDs.
    void watchModel(std::string_view source, std::span<const AssetId> meshIds);

    /// @brief Keeps a model's meshes' triangles for mesh Colliders (collisionMeshes()).
    /// @param model   The imported model.
    /// @param meshIds Its stable mesh IDs, one per mesh.
    void keepCollisionMeshes(const Assets::ImportedModel& model, std::span<const AssetId> meshIds);

    /// @brief Remembers the names of a model's meshes (meshName()).
    /// @param source  The model's virtual path; its file name prefixes the names.
    /// @param model   The imported model (its meshes' names).
    /// @param meshIds Its stable mesh IDs, one per mesh.
    void nameMeshes(std::string_view source, const Assets::ImportedModel& model, std::span<const AssetId> meshIds);

    EditorWorldDeps m_deps;
    std::unique_ptr<Scene::Scene> m_scene;                  ///< Behind a pointer: Scene can't move.
    std::unique_ptr<Scene::TransformHierarchy> m_hierarchy; ///< Likewise; destroyed before the scene.
    Renderer::RenderMeshes m_meshes;                        ///< Mesh AssetId → GPU mesh and material.
    std::unordered_map<AssetId, std::string> m_meshNames;   ///< meshName().
    Physics::CollisionMeshes m_collisionMeshes;             ///< collisionMeshes().
    bool m_playing = false;                                 ///< playing().
    /// @brief A loaded model reloadChangedModels() watches. Plain data.
    struct WatchedModel
    {
        std::string source;                     ///< Its virtual path.
        std::filesystem::path folder;           ///< Its folder on disk.
        std::vector<AssetId> meshIds;           ///< Its stable mesh IDs.
        std::filesystem::file_time_type newest; ///< The newest file time in the folder when last loaded.
    };
    std::vector<WatchedModel> m_models;                        ///< The scene's models.
    std::chrono::steady_clock::time_point m_lastReloadCheck{}; ///< reloadChangedModels()'s last look.
    std::vector<Renderer::InstanceDesc> m_instances;           ///< Reused each update.
    std::vector<entt::entity> m_instanceEntities;              ///< The entity of each of m_instances (its slot).
    bool m_anySelected = false;                                ///< anySelectedInstance().
    Math::Sphere m_bounds{.center = {0.0F, 0.0F, 0.0F}, .radius = 5.0F};
    std::uint64_t m_revision = 0; ///< revision().
};
} // namespace Meow2::Editor
