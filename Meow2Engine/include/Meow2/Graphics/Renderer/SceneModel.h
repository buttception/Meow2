/// @file
/// @brief Imported models in the Scene and on the GPU: the MeshRenderer component, uploading a model's meshes,
///        materials and textures to a GpuScene, making its entities, and gathering what to draw.

#pragma once

#include <Meow2/Assets/Gltf/GltfImporter.h>
#include <Meow2/Assets/Texture/TextureCache.h>
#include <Meow2/Core/Id/Uuid.h>
#include <Meow2/Core/Id/UuidGenerator.h>
#include <Meow2/Core/Jobs/JobSystem.h>
#include <Meow2/Core/Log/Logger.h>
#include <Meow2/Core/Result.h>
#include <Meow2/Graphics/Renderer/GpuScene.h>
#include <Meow2/Math/Bounds.h>
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
/// Every mesh gets a new AssetId. Textures the materials don't use aren't uploaded. A texture or material that can't be
/// added is logged and drawn without, so one bad image doesn't lose the model.
/// @param scene  Where to upload.
/// @param model  What to upload.
/// @param ids    Makes the mesh IDs.
/// @param meshes Receives each mesh's parts.
/// @param log    Receives what was skipped.
/// @param cooking How textures are cooked: each image once per kind it's used as (colour, data, normal map).
/// @return The AssetId of each of `model.meshes`, in order; an error if a mesh can't be uploaded (the GPU scene is
///         full, or the device refused).
[[nodiscard]] Result<std::vector<AssetId>> uploadModel(GpuScene& scene, const Assets::ImportedModel& model,
                                                       UuidGenerator& ids, RenderMeshes& meshes, Log::Logger& log,
                                                       const TextureCooking& cooking = {});

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

/// @brief One InstanceData per part of every entity with a MeshRenderer and a WorldTransform.
/// @param scene  The scene, after TransformHierarchy::update(). Not changed (non-const: EnTT views of const components
/// need an
///               allocator with a default constructor, which the scene's hasn't).
/// @param meshes The GPU parts of each mesh.
/// @param out    Cleared, then filled.
/// @return A box around everything gathered, in world space; empty when nothing was.
std::optional<Math::Aabb> gatherInstances(Scene::Scene& scene, const RenderMeshes& meshes,
                                          std::vector<Shader::InstanceData>& out);
} // namespace Meow2::Renderer
