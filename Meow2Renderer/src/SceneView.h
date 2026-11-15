/// @file
/// @brief SceneView: one scene open in Meow2Renderer, its entities and GPU scene, and where the camera looks.

#pragma once

#include "CameraControls.h"

#include <Meow2/Assets/AssetManager.h>
#include <Meow2/Core/Id/Uuid.h>
#include <Meow2/Core/Id/UuidGenerator.h>
#include <Meow2/Core/Jobs/JobSystem.h>
#include <Meow2/Core/Log/Logger.h>
#include <Meow2/Core/Result.h>
#include <Meow2/Graphics/Renderer/GpuScene.h>
#include <Meow2/Graphics/Renderer/PipelineLibrary.h>
#include <Meow2/Graphics/Renderer/RenderDevice.h>
#include <Meow2/Graphics/Renderer/SceneModel.h>
#include <Meow2/Math/Types.h>
#include <Meow2/Platform/FileSystem/FileSystem.h>
#include <Meow2/Scene/ComponentRegistry.h>
#include <Meow2/Scene/Environment.h>
#include <Meow2/Scene/Scene.h>
#include <Meow2/Scene/TransformHierarchy.h>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace Meow2::RendererApp
{
/// @brief What a SceneView uses. Plain data; everything must outlive the views made with it.
struct SceneViewDeps
{
    Renderer::RenderDevice& device;               ///< Holds the GPU scene's objects.
    Renderer::PipelineLibrary& pipelines;         ///< Builds the GPU-driven pipelines (shared between views).
    FileSystem::FileSystem& files;                ///< Reads models and scenes; each folder gets a read-write mount.
    Assets::AssetManager& assets;                 ///< Gives models stable mesh IDs (`.meta`); finds a scene's models.
    const Scene::ComponentRegistry& components;   ///< What scene files save and load.
    UuidGenerator& ids;                           ///< Makes entity and mesh IDs.
    Jobs::JobSystem& jobs;                        ///< Runs the transform hierarchy.
    Log::Logger& log;                             ///< Receives import warnings.
    AssetId shader;                               ///< `shaders/GpuDriven.hlsl`.
    AssetId deferredShader;                       ///< `shaders/Deferred.hlsl`: the deferred path (DEFERRED setting).
    AssetId shadowShader;                         ///< `shaders/Shadows.hlsl`: the sun's shadow (SHADOWS setting).
    Rhi::RhiFormat colorFormat;                   ///< The swapchain's format.
    const Renderer::SkyAtmosphere* sky = nullptr; ///< Lights every view and is drawn behind it; null for none.
    Renderer::TextureCooking textures{};          ///< How models' textures are cooked (BC7/BC5, the cache).
};

/// @brief Where a Khronos sample lives in the fetched samples folder.
/// @param sampleRoot `assets/gltf-sample-assets` (renderer.sampleAssets).
/// @param name       The model's folder name, e.g. `DamagedHelmet`.
/// @return Its `glTF/<name>.gltf`, or `glTF-Binary/<name>.glb` if only that exists; empty if neither does.
[[nodiscard]] std::filesystem::path findSample(const std::filesystem::path& sampleRoot, const std::string& name);

/// @brief One version of a Khronos sample.
/// @param sampleRoot `assets/gltf-sample-assets`.
/// @param name       The model's folder name.
/// @param binary     True for `glTF-Binary/<name>.glb`, false for `glTF/<name>.gltf`.
/// @return The file, or empty if that version isn't downloaded (or the model has none).
[[nodiscard]] std::filesystem::path findSampleFile(const std::filesystem::path& sampleRoot, const std::string& name,
                                                   bool binary);

/// @brief The virtual folder a native folder is mounted as (read-write), the same name every time for one folder, so
///        the asset manager's records never point at another folder's files.
/// @param folder The native folder.
/// @return A mount name such as `f3a9…` (16 hex digits).
[[nodiscard]] std::string folderMountName(const std::filesystem::path& folder);

/// @brief Where a model's scene file goes: next to it, with the extension `.m2scene`.
/// @param model The model, a native path.
/// @return The scene file's native path.
[[nodiscard]] std::filesystem::path scenePathFor(const std::filesystem::path& model);

/// @brief Whether a file is a scene file (`.m2scene`) rather than a model.
/// @param file A native path.
/// @return True for `.m2scene`.
[[nodiscard]] bool isSceneFile(const std::filesystem::path& file);

/// @brief One scene open in Meow2Renderer: a glTF model, a scene file, or the grid of cubes.
///
/// Owns the `Scene` of entities, its transform hierarchy and the GpuScene that draws them. Opening another scene makes
/// a new view and destroys the old one between frames.
/// @par Thread safety
/// Not thread-safe; use on the main thread.
class SceneView
{
public:
    /// @brief openScene() for a `.m2scene` file, openModel() for anything else.
    /// @param deps What it uses.
    /// @param file A native path.
    /// @return The view, or the error.
    [[nodiscard]] static Result<std::unique_ptr<SceneView>> open(const SceneViewDeps& deps,
                                                                 const std::filesystem::path& file);

    /// @brief Opens a scene file: its entities, and the models its meshes come from (found by the stable mesh IDs in
    ///        the models' `.meta` files, in the scene's folder and below).
    /// @param deps What it uses.
    /// @param file The `.m2scene` file, a native path.
    /// @return The view, or the error from reading the file or uploading.
    [[nodiscard]] static Result<std::unique_ptr<SceneView>> openScene(const SceneViewDeps& deps,
                                                                      const std::filesystem::path& file);

    /// @brief Imports a `.gltf` or `.glb` file and uploads it.
    /// @param deps What it uses.
    /// @param file The model, a native path.
    /// @return The view, or the error from importing or uploading.
    [[nodiscard]] static Result<std::unique_ptr<SceneView>> openModel(const SceneViewDeps& deps,
                                                                      const std::filesystem::path& file);

    /// @brief The test scene: a grid of cubes, each with its own colour.
    /// @param deps  What it uses.
    /// @param count How many cubes.
    /// @return The view, or the error from creating the GPU scene.
    [[nodiscard]] static Result<std::unique_ptr<SceneView>> cubeGrid(const SceneViewDeps& deps, std::uint32_t count);

    /// @brief Use openModel() or cubeGrid().
    /// @param deps What it uses.
    /// @param name What it's called in the UI.
    SceneView(const SceneViewDeps& deps, std::string name);

    /// @brief Destroys the GPU scene before the entities.
    ~SceneView();

    SceneView(const SceneView&) = delete;            ///< Not copyable: owns a GPU scene.
    SceneView& operator=(const SceneView&) = delete; ///< Not copyable: owns a GPU scene.

    /// @brief The GPU scene to draw.
    /// @return It.
    [[nodiscard]] Renderer::GpuScene& gpu()
    {
        return *m_gpu;
    }

    /// @brief What the scene is called: the model's file name, or "Cube grid".
    /// @return The name.
    [[nodiscard]] const std::string& name() const
    {
        return m_name;
    }

    /// @brief The centre of everything drawn, for the camera.
    /// @return World-space centre.
    [[nodiscard]] const Math::Vec3& center() const
    {
        return m_center;
    }

    /// @brief The radius of a sphere around everything drawn, for the camera.
    /// @return Radius in world units; > 0.
    [[nodiscard]] float radius() const
    {
        return m_radius;
    }

    /// @brief How many entities the scene has.
    /// @return The count.
    [[nodiscard]] std::size_t entityCount() const
    {
        return m_world->entityCount();
    }

    /// @brief The glTF file's cameras.
    /// @return One entry per camera node; empty for the cube grid.
    [[nodiscard]] const std::vector<SceneCameraEntry>& cameras() const
    {
        return m_cameras;
    }

    /// @brief The glTF file's animations.
    /// @return Them; empty for the cube grid.
    [[nodiscard]] const std::vector<Assets::ImportedAnimation>& animations() const
    {
        return m_animations;
    }

    /// @brief Poses the scene: every animated node at a time in an animation, or every node at rest. Updates the world
    ///        transforms and what the GPU draws.
    /// @param animation Index into animations(), or -1 for the rest pose.
    /// @param seconds   The time in the animation.
    void pose(int animation, float seconds);

    /// @brief A node's world matrix, after the last pose().
    /// @param node Index into the model's nodes.
    /// @return Its WorldTransform; the identity for an unknown node.
    [[nodiscard]] Math::Mat4 nodeWorld(std::int32_t node) const;

    /// @brief How long importing took.
    /// @return Milliseconds; 0 for the cube grid.
    [[nodiscard]] double importMilliseconds() const
    {
        return m_importMilliseconds;
    }

    /// @brief The scene's Environment (sun and exposure), if it has one: a scene file opened, or one saved before.
    /// @return A copy, or nothing (a model or the cube grid uses the app's own settings).
    [[nodiscard]] std::optional<Scene::Environment> environment() const;

    /// @brief Saves the scene as `.m2scene`: a model next to its file (scenePathFor()), a scene file over itself. The
    ///        environment is stored on an entity named Environment.
    /// @param environment The sun and exposure to save with it.
    /// @return The native path written, or why not: the cube grid can't be saved, nor a model whose `.meta` couldn't
    ///         be written (its mesh IDs wouldn't last).
    [[nodiscard]] Result<std::filesystem::path> saveScene(const Scene::Environment& environment);

private:
    /// @brief Mounts a native folder read-write under folderMountName() (once), for models, `.meta` files and scenes.
    /// @param deps   What it uses.
    /// @param folder The folder.
    /// @return The mount name, or the error.
    [[nodiscard]] static Result<std::string> mountFolder(const SceneViewDeps& deps,
                                                         const std::filesystem::path& folder);

    /// @brief Uploads the instances and frames the scene, after its entities and meshes are in place.
    void finishLoading();

    SceneViewDeps m_deps;
    std::string m_name;
    std::unique_ptr<Scene::Scene> m_world;
    std::unique_ptr<Scene::TransformHierarchy> m_hierarchy;
    Renderer::RenderMeshes m_meshes;
    std::unique_ptr<Renderer::GpuScene> m_gpu;
    Math::Vec3 m_center{0.0F, 0.0F, 0.0F};
    float m_radius = 1.0F;
    double m_importMilliseconds = 0.0;
    std::vector<entt::entity> m_nodeEntities;            ///< One per model node, in order.
    std::vector<Math::Transform> m_restTransforms;       ///< Each node's local transform without animation.
    std::vector<SceneCameraEntry> m_cameras;             ///< The file's cameras.
    std::vector<Assets::ImportedAnimation> m_animations; ///< The file's animations.
    std::filesystem::path m_savePath;                    ///< Where saveScene() writes; empty: can't be saved.
    std::string m_saveVirtualPath;                       ///< The same, as a virtual path in the folder's mount.
    std::string m_saveProblem;                           ///< Why it can't be saved, when m_savePath is empty.
};
} // namespace Meow2::RendererApp
