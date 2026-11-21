/// @file
/// @brief Imported models in the Scene and on the GPU: the MeshRenderer component, uploading a model's meshes,
///        materials and textures to a GpuScene, making its entities, and gathering what to draw.

#pragma once

#include <Meow2/Assets/AssetManager.h>
#include <Meow2/Assets/Gltf/GltfImporter.h>
#include <Meow2/Assets/Texture/TextureCache.h>
#include <Meow2/Core/Id/Uuid.h>
#include <Meow2/Core/Id/UuidGenerator.h>
#include <Meow2/Core/Jobs/JobSystem.h>
#include <Meow2/Core/Log/Logger.h>
#include <Meow2/Core/Result.h>
#include <Meow2/Graphics/Renderer/GpuScene.h>
#include <Meow2/Math/Bounds.h>
#include <Meow2/Scene/ComponentRegistry.h>
#include <Meow2/Scene/Scene.h>
#include <Meow2/Scene/TransformHierarchy.h>

#include <cstdint>
#include <optional>
#include <span>
#include <unordered_map>
#include <vector>

namespace Meow2::Renderer
{
/// @brief Draws an entity with a mesh at its WorldTransform. Plain data.
///
/// Refers to the mesh by AssetId, never by a GPU index, so it means the same in every app and after a backend switch.
struct MeshRenderer
{
    AssetId mesh{}; ///< Which mesh; looked up in RenderMeshes.
    bool movable =
        false; ///< Moves at runtime (animated): casts into the sun's shadow every frame rather than into
               ///< the cached map of static casters (MEOW2_INSTANCE_MOVABLE). Until the editor infers it (M13).
};

/// @brief One part of a mesh on the GPU: a GpuScene mesh and the material it's drawn with. Plain data.
struct GpuSubmesh
{
    std::uint32_t mesh = 0;     ///< From GpuScene::addMesh().
    std::uint32_t material = 0; ///< From GpuScene::addMaterial().
    Math::Aabb bounds{};        ///< Its box in model space.
};

/// @brief Which GPU meshes draw each mesh AssetId.
/// @par Thread safety
/// Not thread-safe while it changes; any thread may read it while it doesn't.
class RenderMeshes
{
public:
    /// @brief Adds (or replaces) a mesh's parts.
    /// @param mesh  The mesh.
    /// @param parts Its parts.
    void add(AssetId mesh, std::vector<GpuSubmesh> parts);

    /// @brief A mesh's parts.
    /// @param mesh The mesh.
    /// @return Its parts; empty if it isn't known.
    [[nodiscard]] std::span<const GpuSubmesh> find(AssetId mesh) const;

    /// @brief How many meshes it knows.
    /// @return The count.
    [[nodiscard]] std::size_t size() const
    {
        return m_meshes.size();
    }

private:
    std::unordered_map<AssetId, std::vector<GpuSubmesh>> m_meshes;
};

/// @brief How uploadModel() cooks a model's textures. Plain data.
struct TextureCooking
{
    bool compress = false;                 ///< BC7 for colour and data, BC5 for normal maps; false keeps RGBA8.
    Assets::TextureCache* cache = nullptr; ///< Keeps cooked textures between runs; null cooks every time.
    Jobs::JobSystem* jobs = nullptr;       ///< Compresses in parallel; null cooks on the calling thread.
};

/// @brief Uploads a model's meshes, materials and the textures they use to a GpuScene.
///
/// Each mesh gets the ID given in `meshIds` (stable ones from AssetManager::meshIds(), so scenes can refer to them), or
/// a new AssetId when `meshIds` is empty. Textures the materials don't use aren't uploaded. A texture or material that
/// can't be added is logged and drawn without, so one bad image doesn't lose the model.
/// @param scene  Where to upload.
/// @param model  What to upload.
/// @param ids    Makes the mesh IDs.
/// @param meshes Receives each mesh's parts.
/// @param log    Receives what was skipped.
/// @param cooking How textures are cooked: each image once per kind it's used as (colour, data, normal map).
/// @param meshIds One ID per mesh of `model`, or empty to make new ones.
/// @return The AssetId of each of `model.meshes`, in order; an error if a mesh can't be uploaded (the GPU scene is
///         full, or the device refused).
[[nodiscard]] Result<std::vector<AssetId>> uploadModel(GpuScene& scene, const Assets::ImportedModel& model,
                                                       UuidGenerator& ids, RenderMeshes& meshes, Log::Logger& log,
                                                       const TextureCooking& cooking = {},
                                                       std::span<const AssetId> meshIds = {});

/// @brief Makes an entity per node of a model, with its local transform, parent and MeshRenderer.
/// @param scene     The scene.
/// @param hierarchy Its transform hierarchy.
/// @param model     The model.
/// @param meshIds   From uploadModel().
/// @param parent    The entity the model's roots hang under; entt::null for none.
/// @return The entities, one per `model.nodes`, in order.
/// @pre `meshIds` has one ID per mesh of `model` (asserted).
std::vector<entt::entity> instantiateModel(Scene::Scene& scene, Scene::TransformHierarchy& hierarchy,
                                           const Assets::ImportedModel& model, std::span<const AssetId> meshIds,
                                           entt::entity parent = entt::null);

/// @brief One InstanceDesc per part of every entity with a MeshRenderer and a WorldTransform, in a stable order (the
///        component storage's), so GpuScene::setInstances() keeps each part in the same slot from call to call.
/// @param scene  The scene, after TransformHierarchy::update(). Not changed (non-const: EnTT views of const
///               components need an allocator with a default constructor, which the scene's hasn't).
/// @param meshes The GPU parts of each mesh.
/// @param out    Cleared, then filled.
/// @param entities Cleared, then filled with each instance's entity, in the same order (several parts of one mesh give
///                 several entries); null to skip. The editor maps a picked instance back to its entity with it.
/// @return A box around everything gathered, in world space; empty when nothing was.
std::optional<Math::Aabb> gatherInstances(Scene::Scene& scene, const RenderMeshes& meshes,
                                          std::vector<InstanceDesc>& out,
                                          std::vector<entt::entity>* entities = nullptr);
/// @brief Registers the renderer's scene components (`MeshRenderer`) for scene files.
/// @param registry Where.
void registerRenderComponents(Scene::ComponentRegistry& registry);

/// @brief A model a scene uses, imported and ready to upload. Plain data.
struct SceneModelImport
{
    AssetId model;                ///< The model asset.
    Assets::ImportedModel data;   ///< Its contents.
    std::vector<AssetId> meshIds; ///< Its stable mesh IDs (AssetManager::meshIds()), one per `data.meshes`.
};

/// @brief The models a scene's MeshRenderers need that aren't on the GPU yet. Plain data.
struct SceneModels
{
    std::vector<SceneModelImport> models; ///< Each model once.
    std::vector<std::string> warnings;    ///< Meshes or models that couldn't be found or imported, one line each.
};

/// @brief What loadSceneModels() did. Plain data.
struct SceneModelsReport
{
    std::size_t models = 0;            ///< Model files imported and uploaded.
    std::size_t meshes = 0;            ///< Meshes now known to RenderMeshes.
    std::vector<std::string> warnings; ///< Meshes or models that couldn't be found or loaded, one line each.
};

/// @brief Imports every model a loaded scene's MeshRenderers refer to and that `meshes` doesn't hold yet.
///
/// Each mesh ID is looked up in the asset manager (a model's `.meta` lists its mesh IDs, AssetManager::meshIds()), and
/// its model imported once. A mesh whose model isn't found is reported and won't be drawn. No GPU work: count the
/// instances with sceneInstanceCount() to size a GpuScene, then call uploadSceneModels().
/// @param scene  The scene (its MeshRenderers).
/// @param assets Finds models by mesh ID; their `.meta` files must be registered (AssetManager::scan()).
/// @param meshes What's on the GPU already.
/// @param log    Receives import warnings.
/// @return The models.
[[nodiscard]] SceneModels importSceneModels(Scene::Scene& scene, Assets::AssetManager& assets,
                                            const RenderMeshes& meshes, Log::Logger& log);

/// @brief How many GPU instances the scene's MeshRenderers make: the parts of each one's mesh, from `models` or from
///        `meshes` (what's uploaded already). For GpuSceneDesc::instanceCapacity.
/// @param scene  The scene.
/// @param models From importSceneModels().
/// @param meshes What's on the GPU already.
/// @return The count.
[[nodiscard]] std::uint32_t sceneInstanceCount(Scene::Scene& scene, const SceneModels& models,
                                               const RenderMeshes& meshes);

/// @brief Uploads imported models with their stable mesh IDs.
/// @param gpu     Where to upload.
/// @param models  From importSceneModels().
/// @param meshes  Receives the meshes.
/// @param ids     For uploadModel().
/// @param log     Receives what was skipped.
/// @param cooking How textures are cooked.
/// @return Nothing, or the first upload error (the GPU scene is full, or the device refused).
[[nodiscard]] Result<void> uploadSceneModels(GpuScene& gpu, const SceneModels& models, RenderMeshes& meshes,
                                             UuidGenerator& ids, Log::Logger& log, const TextureCooking& cooking = {});

/// @brief importSceneModels() then uploadSceneModels(), so a scene file opens with its meshes. Meshes already in
///        `meshes` are skipped, so calling it again after adding entities uploads only what's new.
/// @param gpu     Where to upload.
/// @param scene   The scene (its MeshRenderers).
/// @param assets  Finds models by mesh ID; their `.meta` files must be registered (AssetManager::scan()).
/// @param meshes  Receives the uploaded meshes.
/// @param ids     For uploadModel().
/// @param log     Receives import warnings.
/// @param cooking How textures are cooked.
/// @return What was loaded, or the first upload error (the GPU scene is full, or the device refused).
[[nodiscard]] Result<SceneModelsReport> loadSceneModels(GpuScene& gpu, Scene::Scene& scene,
                                                        Assets::AssetManager& assets, RenderMeshes& meshes,
                                                        UuidGenerator& ids, Log::Logger& log,
                                                        const TextureCooking& cooking = {});
} // namespace Meow2::Renderer
