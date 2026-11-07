/// @file
/// @brief GpuScene: meshes and instances in GPU buffers, culled in compute and drawn indirectly.

#pragma once

#include <Meow2/Assets/Texture/Image.h>
#include <Meow2/Core/Result.h>
#include <Meow2/Graphics/Renderer/PipelineLibrary.h>
#include <Meow2/Graphics/Renderer/RenderDevice.h>
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
#include <string>
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

/// @brief A mesh ready to upload: quantised positions, octahedral normals, half-float UVs, 32-bit indices. Plain data.
struct MeshData
{
    std::vector<Shader::PackedVertex> vertices; ///< Made by makeMesh() or packVertex().
    std::vector<std::uint32_t> indices;         ///< Three per triangle; clockwise faces the viewer.
    Math::Aabb bounds{};                        ///< The box the positions are quantised in.
};

/// @brief Encodes one vertex: its position quantised within `bounds`, an octahedral normal and half-float UVs.
/// @param position Its position, inside `bounds`.
/// @param normal   Its unit normal.
/// @param uv       Its texture coordinates.
/// @param bounds   The mesh's box.
/// @return The packed vertex.
[[nodiscard]] Shader::PackedVertex packVertex(const Math::Vec3& position, const Math::Vec3& normal,
                                              const Math::Vec2& uv, const Math::Aabb& bounds);

/// @brief Packs a mesh for upload.
/// @param positions Positions.
/// @param normals   Unit normals; as many as positions.
/// @param uvs       Texture coordinates; as many as positions.
/// @param indices   Three per triangle, clockwise.
/// @param bounds    A box around the positions.
/// @return The mesh.
/// @pre The three vertex arrays are the same size (asserted).
[[nodiscard]] MeshData makeMesh(std::span<const Math::Vec3> positions, std::span<const Math::Vec3> normals,
                                std::span<const Math::Vec2> uvs, std::span<const std::uint32_t> indices,
                                const Math::Aabb& bounds);

/// @brief A unit cube, for test scenes.
/// @return Its 24 vertices and 36 indices, with UVs 0..1 on each face.
[[nodiscard]] MeshData makeCube();

/// @brief A material to add to a GpuScene. Plain data.
struct MaterialDesc
{
    Math::Color baseColorFactor{1.0F, 1.0F, 1.0F, 1.0F}; ///< Linear RGBA; multiplies the texture.
    std::int32_t baseColorTexture = -1;                  ///< From GpuScene::addTexture() (sRGB), or -1 for none.
    bool alphaMask = false;                              ///< Discard pixels whose alpha is below `alphaCutoff`.
    float alphaCutoff = 0.5F;                            ///< For `alphaMask`.
    float metallic = 1.0F;                               ///< Kept for the PBR pass; 0..1.
    float roughness = 1.0F;                              ///< Kept for the PBR pass; 0..1.
};

/// @brief The GPU-driven path: one compute pass culls every instance, and one indirect call draws what's left.
///
/// There's no per-object work on the CPU, so the cost of a frame doesn't grow with the instance count. Instances and
/// meshes live in buffers the shaders read by heap index, and vertices are pulled by the vertex shader.
///
/// It's a RenderDevice user: it keeps a CPU copy of its meshes and instances, and makes its buffers again from them
/// when the backend changes.
/// @par Thread safety
/// Not thread-safe. The passes it adds run on jobs, which only read it.
class GpuScene
{
public:
    /// @brief Creates the buffers and pipelines.
    /// @param device    The device. Must outlive the scene.
    /// @param pipelines Builds the pipelines; uses the same `device`. Must outlive the scene.
    /// @param desc      Capacity and formats.
    /// @return The scene, or an error from creating them.
    [[nodiscard]] static Result<std::unique_ptr<GpuScene>> create(RenderDevice& device, PipelineLibrary& pipelines,
                                                                  const GpuSceneDesc& desc);

    /// @brief Use create().
    /// @param device    The device.
    /// @param pipelines Builds the pipelines.
    /// @param desc      Capacity and formats.
    GpuScene(RenderDevice& device, PipelineLibrary& pipelines, const GpuSceneDesc& desc);

    /// @brief Destroys the buffers.
    ~GpuScene();

    GpuScene(const GpuScene&) = delete;            ///< Not copyable: owns buffers.
    GpuScene& operator=(const GpuScene&) = delete; ///< Not copyable: owns buffers.

    /// @brief Uploads a mesh, and keeps a copy for the next device.
    /// @param mesh The mesh.
    /// @return Its index, for Shader::InstanceData::mesh, or an error.
    [[nodiscard]] Result<std::uint32_t> addMesh(const MeshData& mesh);

    /// @brief Uploads a texture at the start of the next frame, and keeps a copy for the next device.
    /// @param image The pixels, RGBA8.
    /// @param srgb  True for colour (base colour, emissive), false for data (normals, roughness).
    /// @param name  Shown in debugging tools.
    /// @return Its index, for MaterialDesc, or an error (an empty image, or the device refused).
    [[nodiscard]] Result<std::uint32_t> addTexture(const Assets::Image& image, bool srgb, std::string name = "Texture");

    /// @brief Adds a material, and keeps a copy for the next device.
    /// @param material Its parameters.
    /// @return Its index, for Shader::InstanceData::material, or an error when the scene has too many.
    [[nodiscard]] Result<std::uint32_t> addMaterial(const MaterialDesc& material);

    /// @brief Sets every instance, and keeps a copy for the next device. Between frames: the GPU reads these while
    ///        drawing.
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
    /// @return A buffer holding one `uint`. Changes when the backend does.
    [[nodiscard]] Rhi::BufferHandle countBuffer() const
    {
        return m_countBuffer;
    }

    /// @brief How many instances are drawn, for statistics.
    /// @return The count from the last setInstances().
    [[nodiscard]] std::uint32_t instanceCount() const
    {
        return static_cast<std::uint32_t>(m_instances.size());
    }

    /// @brief How many meshes were added, for statistics.
    /// @return The count.
    [[nodiscard]] std::uint32_t meshCount() const
    {
        return static_cast<std::uint32_t>(m_meshes.size());
    }

    /// @brief How many materials were added, for statistics.
    /// @return The count.
    [[nodiscard]] std::uint32_t materialCount() const
    {
        return static_cast<std::uint32_t>(m_materials.size());
    }

    /// @brief How many textures were added, for statistics.
    /// @return The count.
    [[nodiscard]] std::uint32_t textureCount() const
    {
        return static_cast<std::uint32_t>(m_textures.size());
    }

private:
    /// @brief Creates the scene's own buffers (not the meshes').
    /// @param device The device to create them on.
    /// @return The first error; what was made is destroyed by destroyBuffers().
    [[nodiscard]] Result<void> createBuffers(Rhi::IRhiDevice& device);

    /// @brief Destroys every buffer, the meshes' too, and forgets their handles.
    /// @param device The device they were made on.
    void destroyBuffers(Rhi::IRhiDevice& device);

    /// @brief Makes every buffer again on a new device, from the CPU copies.
    /// @param device The new device.
    /// @return The first error.
    [[nodiscard]] Result<void> restoreBuffers(Rhi::IRhiDevice& device);

    /// @brief Creates a texture and its staging buffer, to be copied by the next frame.
    /// @param device The device.
    /// @param index  Its place in m_textures.
    /// @return An error from creating them.
    [[nodiscard]] Result<void> createTexture(Rhi::IRhiDevice& device, std::uint32_t index);

    /// @brief Writes a material's record, with the current heap index of its texture.
    /// @param device The device.
    /// @param index  Its place in the material buffer.
    void writeMaterial(Rhi::IRhiDevice& device, std::uint32_t index);

    /// @brief Uploads one mesh and writes its record.
    /// @param device The device.
    /// @param mesh   The mesh.
    /// @param index  Its place in the mesh buffer.
    /// @return An error from creating its buffers.
    [[nodiscard]] Result<void> uploadMesh(Rhi::IRhiDevice& device, const MeshData& mesh, std::uint32_t index);

    RenderDevice& m_device;
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
    std::vector<MeshData> m_meshes;                 ///< CPU copies, for the next device.
    std::vector<Shader::InstanceData> m_instances;  ///< CPU copy, for the next device.

    /// @brief A texture, its pixels kept for the next device, and its upload until the GPU has done it.
    struct TextureRecord
    {
        Assets::Image image{};        ///< CPU copy.
        bool srgb = false;            ///< Which format.
        std::string name{};           ///< For debugging tools.
        Rhi::TextureHandle texture{}; ///< On the current device.
        Rhi::BufferHandle staging{};  ///< Holds the pixels until the copy is recorded; unset after.
    };
    std::vector<TextureRecord> m_textures;
    std::vector<std::uint32_t> m_pendingTextures;  ///< Textures whose copy the next frame records.
    std::vector<Rhi::BufferHandle> m_spentStaging; ///< Staging buffers to destroy once their copies are recorded.
    std::vector<MaterialDesc> m_materials;         ///< CPU copies, for the next device.
    Rhi::BufferHandle m_materialBuffer;
    Rhi::ResourceState m_drawState = Rhi::ResourceState::Undefined;
    Rhi::ResourceState m_countState = Rhi::ResourceState::Undefined;
    Rhi::ResourceState m_depthState = Rhi::ResourceState::Undefined;
    Rhi::TextureHandle m_lastDepth;
    GraphResource m_drawResource;
    GraphResource m_countResource;
    GraphResource m_depthResource;
    DeviceUser m_deviceUser; ///< Last: stops its callbacks before the members they use go away.
};
} // namespace Meow2::Renderer
