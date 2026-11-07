/// @file
/// @brief SceneView: one scene open in Meow2Renderer, its entities and GPU scene, and where the camera looks.

#pragma once

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
#include <Meow2/Scene/Scene.h>
#include <Meow2/Scene/TransformHierarchy.h>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace Meow2::RendererApp
{
/// @brief What a SceneView uses. Plain data; everything must outlive the views made with it.
struct SceneViewDeps
{
    Renderer::RenderDevice& device;       ///< Holds the GPU scene's objects.
    Renderer::PipelineLibrary& pipelines; ///< Builds the GPU-driven pipelines (shared between views).
    FileSystem::FileSystem& files;        ///< Reads models; mount `model` is (re)made for each one.
    UuidGenerator& ids;                   ///< Makes entity and mesh IDs.
    Jobs::JobSystem& jobs;                ///< Runs the transform hierarchy.
    Log::Logger& log;                     ///< Receives import warnings.
    AssetId shader;                       ///< `shaders/GpuDriven.hlsl`.
    Rhi::RhiFormat colorFormat;           ///< The swapchain's format.
};

/// @brief Where a Khronos sample lives in the fetched samples folder.
/// @param sampleRoot `assets/gltf-sample-assets` (renderer.sampleAssets).
/// @param name       The model's folder name, e.g. `DamagedHelmet`.
/// @return Its `glTF/<name>.gltf`, or `glTF-Binary/<name>.glb` if only that exists; empty if neither does.
[[nodiscard]] std::filesystem::path findSample(const std::filesystem::path& sampleRoot, const std::string& name);

/// @brief One scene open in Meow2Renderer: a glTF model, or the grid of cubes.
///
/// Owns the `Scene` of entities, its transform hierarchy and the GpuScene that draws them. Opening another scene makes
/// a new view and destroys the old one between frames.
/// @par Thread safety
/// Not thread-safe; use on the main thread.
class SceneView
{
public:
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

    /// @brief How long importing took.
    /// @return Milliseconds; 0 for the cube grid.
    [[nodiscard]] double importMilliseconds() const
    {
        return m_importMilliseconds;
    }

private:
    SceneViewDeps m_deps;
    std::string m_name;
    std::unique_ptr<Scene::Scene> m_world;
    std::unique_ptr<Scene::TransformHierarchy> m_hierarchy;
    Renderer::RenderMeshes m_meshes;
    std::unique_ptr<Renderer::GpuScene> m_gpu;
    Math::Vec3 m_center{0.0F, 0.0F, 0.0F};
    float m_radius = 1.0F;
    double m_importMilliseconds = 0.0;
};
} // namespace Meow2::RendererApp
