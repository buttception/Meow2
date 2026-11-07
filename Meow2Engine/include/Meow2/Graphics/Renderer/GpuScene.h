/// @file
/// @brief GpuScene: meshes and instances in GPU buffers, culled in compute and drawn indirectly.

#pragma once

#include <Meow2/Core/Result.h>
#include <Meow2/Graphics/Renderer/PipelineLibrary.h>
#include <Meow2/Graphics/Renderer/RenderGraph.h>
#include <Meow2/Graphics/Rhi/IRhiDevice.h>
#include <Meow2/Graphics/Shader/BindlessLayout.h>
#include <Meow2/Math/Bounds.h>
#include <Meow2/Math/Color.h>

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace Meow2::Renderer
{
/// @brief What to create a GpuScene with. Plain data.
struct GpuSceneDesc
{
    AssetId shader{};                                     ///< `shaders/GpuDriven.hlsl`.
    std::uint32_t instanceCapacity = 1;                   ///< Most instances; > 0.
    Rhi::RhiFormat colorFormat = Rhi::RhiFormat::Unknown; ///< Format of the colour target.
};

/// @brief A mesh ready to upload: quantised positions, octahedral normals, 32-bit indices. Plain data.
struct MeshData
{
    std::vector<Shader::PackedVertex> vertices; ///< Math::quantizePosition() within `bounds`, Math::packOctahedral().
    std::vector<std::uint32_t> indices;         ///< Three per triangle; clockwise faces the viewer.
    Math::Aabb bounds{};                        ///< The box the positions are quantised in.
};

/// @brief A unit cube, for test scenes.
/// @return Its 24 vertices and 36 indices.
[[nodiscard]] MeshData makeCube();

/// @brief The GPU-driven path: one compute pass culls every instance, and one indirect call draws what's left.
///
/// There's no per-object work on the CPU, so the cost of a frame doesn't grow with the instance count. Instances and
/// meshes live in buffers the shaders read by heap index, and vertices are pulled by the vertex shader.
/// @par Thread safety
/// Not thread-safe. The passes it adds run on jobs, which only read it.
class GpuScene
{
public:
    /// @brief Creates the buffers and pipelines.
    /// @param device    The device. Must outlive the scene.
    /// @param pipelines Builds the pipelines. Must outlive the scene.
    /// @param desc      Capacity and formats.
    /// @return The scene, or an error from creating them.
    [[nodiscard]] static Result<std::unique_ptr<GpuScene>> create(Rhi::IRhiDevice& device, PipelineLibrary& pipelines,
                                                                  const GpuSceneDesc& desc);

    /// @brief Use create().
    /// @param device    The device.
    /// @param pipelines Builds the pipelines.
    /// @param desc      Capacity and formats.
    GpuScene(Rhi::IRhiDevice& device, PipelineLibrary& pipelines, const GpuSceneDesc& desc);

    /// @brief Destroys the buffers.
    ~GpuScene();

    GpuScene(const GpuScene&) = delete;            ///< Not copyable: owns buffers.
    GpuScene& operator=(const GpuScene&) = delete; ///< Not copyable: owns buffers.

    /// @brief Uploads a mesh.
    /// @param mesh The mesh.
    /// @return Its index, for Shader::InstanceData::mesh, or an error.
    [[nodiscard]] Result<std::uint32_t> addMesh(const MeshData& mesh);

    /// @brief Sets every instance. Between frames: the GPU reads these while drawing.
    /// @param instances The instances; at most GpuSceneDesc::instanceCapacity.
    /// @pre No frame is recording, and the GPU is idle (asserted for the count only).
    void setInstances(std::span<const Shader::InstanceData> instances);

    /// @brief Sets this frame's camera.
    /// @param frameSlot IRhiDevice::frameSlot().
    /// @param frame     What shaders see.
    /// @param frustum   What's visible, camera relative like the instances.
    void setView(std::uint32_t frameSlot, const Shader::FrameConstants& frame, const Math::Frustum& frustum);

    /// @brief Adds the culling and drawing passes.
    /// @param graph     The frame's graph.
    /// @param frameSlot IRhiDevice::frameSlot().
    /// @param swapchain Draws to this swapchain; null to use `color`.
    /// @param color     The colour target when `swapchain` is null.
    /// @param depth     The depth target, D32Float.
    /// @param clear     The colour to clear to.
    void addPasses(RenderGraph& graph, std::uint32_t frameSlot, Rhi::IRhiSwapchain* swapchain, Rhi::TextureHandle color,
                   Rhi::TextureHandle depth, const Math::Color& clear);

    /// @brief Records the state of the graph's resources, once it's compiled. Call before the next addPasses().
    /// @param graph The graph.
    void endFrame(const RenderGraph& graph);

    /// @brief Where the surviving draw count lands, for tests and statistics.
    /// @return A buffer holding one `uint`.
    [[nodiscard]] Rhi::BufferHandle countBuffer() const
    {
        return m_countBuffer;
    }

private:
    Rhi::IRhiDevice& m_device;
    PipelineLibrary& m_pipelines;
    GpuSceneDesc m_desc;
    std::optional<PipelineId> m_cullPipeline;
    std::optional<PipelineId> m_drawPipeline;
    Rhi::BufferHandle m_instanceBuffer;
    Rhi::BufferHandle m_meshBuffer;
    Rhi::BufferHandle m_drawBuffer;
    Rhi::BufferHandle m_countBuffer;
    std::array<Rhi::BufferHandle, Rhi::c_maxFramesInFlight> m_frameBuffers{};
    std::array<Rhi::BufferHandle, Rhi::c_maxFramesInFlight> m_cullBuffers{};
    std::vector<Rhi::BufferHandle> m_meshResources; ///< Vertex and index buffers.
    std::uint32_t m_meshCount = 0;
    std::uint32_t m_instanceCount = 0;
    Rhi::ResourceState m_drawState = Rhi::ResourceState::Undefined;
    Rhi::ResourceState m_countState = Rhi::ResourceState::Undefined;
    Rhi::ResourceState m_depthState = Rhi::ResourceState::Undefined;
    Rhi::TextureHandle m_lastDepth;
    GraphResource m_drawResource;
    GraphResource m_countResource;
    GraphResource m_depthResource;
};
} // namespace Meow2::Renderer
