/// @file
/// @brief GpuScene: meshes and instances in GPU buffers, culled in compute and drawn indirectly.

#pragma once

#include <Meow2/Assets/Texture/CookedTexture.h>
#include <Meow2/Assets/Texture/Image.h>
#include <Meow2/Core/Result.h>
#include <Meow2/Graphics/Renderer/PipelineLibrary.h>
#include <Meow2/Graphics/Renderer/RenderDevice.h>
#include <Meow2/Graphics/Renderer/RenderGraph.h>
#include <Meow2/Graphics/Renderer/SkyAtmosphere.h>
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
/// @brief How to build a variant set of a GpuScene's draw pipelines (makeDrawPipelines()). Plain data.
struct DrawPipelineOptions
{
    std::vector<Assets::ShaderOptionChoice> options{}; ///< The shader variant, e.g. a debug view.
    Rhi::FillMode fill = Rhi::FillMode::Solid;         ///< Filled, or only edges.
    Rhi::BlendMode blend = Rhi::BlendMode::None; ///< How the draw lists combine with the target (blended surfaces keep
                                                 ///< alpha blending unless this is Additive).
    bool depthTest = true;                       ///< Whether depth is tested.
    bool depthWrite = true;                      ///< Whether the draw lists write depth.
    std::string name = "Variant";                ///< Prefix of the pipelines' names.
};

/// @brief A GpuScene's draw pipelines: one per draw list and one for blended surfaces. Plain data.
struct DrawPipelines
{
    std::array<PipelineId, MEOW2_DRAW_LIST_COUNT> lists{}; ///< One per MEOW2_DRAW_LIST_*.
    PipelineId blend{};                                    ///< For blended surfaces.
};

/// @brief What to create a GpuScene with. Plain data.
struct GpuSceneDesc
{
    AssetId shader{};                                     ///< `shaders/GpuDriven.hlsl`.
    std::uint32_t instanceCapacity = 1;                   ///< Most instances; > 0.
    Rhi::RhiFormat colorFormat = Rhi::RhiFormat::Unknown; ///< Format of the colour target.
};

/// @brief A mesh ready to upload: quantised positions, octahedral normals and tangents, half-float UVs, RGBA8 colours,
///        32-bit indices. Plain data.
struct MeshData
{
    std::vector<Shader::PackedVertex> vertices; ///< Made by makeMesh() or packVertex().
    std::vector<std::uint32_t> indices;         ///< Three per triangle; clockwise faces the viewer.
    Math::Aabb bounds{};                        ///< The box the positions are quantised in.
};

/// @brief One vertex before packing. Plain data.
struct VertexAttributes
{
    Math::Vec3 position{};                      ///< Inside the mesh's bounds.
    Math::Vec3 normal{0.0F, 1.0F, 0.0F};        ///< Unit normal.
    Math::Vec4 tangent{1.0F, 0.0F, 0.0F, 1.0F}; ///< xyz along +U, unit; w the bitangent's sign (±1).
    Math::Vec2 uv{};                            ///< Texture coordinates.
    Math::Color color{1.0F, 1.0F, 1.0F, 1.0F};  ///< Linear vertex colour.
};

/// @brief Encodes one vertex: position quantised within `bounds`, octahedral normal and tangent, half-float UVs, RGBA8
///        colour.
/// @param vertex The vertex.
/// @param bounds The mesh's box.
/// @return The packed vertex.
[[nodiscard]] Shader::PackedVertex packVertex(const VertexAttributes& vertex, const Math::Aabb& bounds);

/// @brief The vertex arrays of a mesh, for makeMesh(). Plain data.
struct MeshArrays
{
    std::span<const Math::Vec3> positions{};  ///< Positions.
    std::span<const Math::Vec3> normals{};    ///< Unit normals; as many as positions.
    std::span<const Math::Vec4> tangents{};   ///< As many as positions, or empty (tangents along +X).
    std::span<const Math::Vec2> uvs{};        ///< As many as positions.
    std::span<const Math::Color> colors{};    ///< As many as positions, or empty (white).
    std::span<const std::uint32_t> indices{}; ///< Three per triangle, clockwise.
};

/// @brief Packs a mesh for upload.
/// @param arrays Its vertex arrays and indices.
/// @param bounds A box around the positions.
/// @return The mesh.
/// @pre The vertex arrays that are given have as many elements as `positions` (asserted).
[[nodiscard]] MeshData makeMesh(const MeshArrays& arrays, const Math::Aabb& bounds);

/// @brief A unit cube, for test scenes.
/// @return Its 24 vertices and 36 indices, with UVs 0..1 on each face.
[[nodiscard]] MeshData makeCube();

/// @brief One texture slot of a material. Plain data.
struct MaterialTexture
{
    std::int32_t texture = -1;       ///< From GpuScene::addTexture(), or -1 for none.
    std::int32_t sampler = -1;       ///< From GpuScene::addSampler(), or -1 for linear, repeating, mipmapped.
    Math::Vec2 uvOffset{0.0F, 0.0F}; ///< KHR_texture_transform: added to the UV last.
    float uvRotation = 0.0F;         ///< KHR_texture_transform: radians, counter-clockwise in UV space.
    Math::Vec2 uvScale{1.0F, 1.0F};  ///< KHR_texture_transform: multiplies the UV first.
};

/// @brief A metallic-roughness material to add to a GpuScene (glTF's model). Plain data.
struct MaterialDesc
{
    Math::Color baseColorFactor{1.0F, 1.0F, 1.0F, 1.0F}; ///< Linear RGBA; multiplies the texture and vertex colour.
    MaterialTexture baseColor{};                         ///< sRGB texture.
    float metallic = 1.0F;                               ///< 0..1; multiplies the texture's B.
    float roughness = 1.0F;                              ///< 0..1; multiplies the texture's G.
    MaterialTexture metallicRoughness{};                 ///< Linear texture.
    MaterialTexture normal{};                            ///< Linear tangent-space normal map, +Y up.
    float normalScale = 1.0F;                            ///< Scales the normal map's X and Y.
    MaterialTexture occlusion{};                         ///< Linear texture, occlusion in R.
    float occlusionStrength = 1.0F;                      ///< 0..1.
    Math::Color emissiveFactor{0.0F, 0.0F, 0.0F, 1.0F};  ///< Linear RGB; multiplies the texture.
    MaterialTexture emissive{};                          ///< sRGB texture.
    bool alphaMask = false;                              ///< Discard pixels whose alpha is below `alphaCutoff`.
    float alphaCutoff = 0.5F;                            ///< For `alphaMask`.
    bool blend = false;                                  ///< Blended over what's behind, sorted, no depth writes.
    bool doubleSided = false;                            ///< Both sides drawn, each lit from its own side.
};

/// @brief The scene's light until lights are entities: one sun and a sky/ground ambient. Plain data.
struct SceneLighting
{
    Math::Vec3 sunDirection{0.4F, 0.8F, 0.6F};          ///< Towards the sun; normalised. In front of glTF models (+Z).
    Math::Color sunColor{1.0F, 1.0F, 1.0F, 1.0F};       ///< Linear colour; with a sky, the atmosphere tints it.
    float sunIntensity = 3.0F;                          ///< Multiplies sunColor.
    Math::Color skyColor{0.45F, 0.55F, 0.7F, 1.0F};     ///< Ambient light from above, linear.
    Math::Color groundColor{0.15F, 0.13F, 0.11F, 1.0F}; ///< Ambient light from below, linear.
    float exposure = 1.0F;                              ///< Scales the final colour before the tone curve.
};

/// @brief The GPU-driven path: one compute pass culls every instance and sorts the survivors into draw lists, and one
///        indirect call per list draws them with metallic-roughness PBR.
///
/// There's no per-object work on the CPU for opaque and alpha-masked surfaces, so the cost of a frame doesn't grow with
/// the instance count. Instances and meshes live in buffers the shaders read by heap index, and vertices are pulled by
/// the vertex shader.
///
/// - **Draw lists** (MEOW2_DRAW_LIST_*): back faces culled (most meshes), mirrored instances (negative scale reverses
///   the winding, so front faces are culled instead), and double-sided materials (nothing culled).
/// - **Blended materials** are left out of the lists. They're drawn after them, one direct draw per instance, sorted
///   back to front on the CPU, without writing depth.
/// - **Light:** one sun and a sky/ground ambient (SceneLighting), then exposure and a tone curve.
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
    ///
    /// The image is cooked uncompressed (RGBA8 with mips); cook it with Assets::cookTexture() for BC7 or BC5.
    /// @param image The pixels, RGBA8.
    /// @param srgb  True for colour (base colour, emissive), false for data (normals, roughness).
    /// @param name  Shown in debugging tools.
    /// @return Its index, for MaterialDesc, or an error (an empty image, or the device refused).
    [[nodiscard]] Result<std::uint32_t> addTexture(const Assets::Image& image, bool srgb, std::string name = "Texture");

    /// @brief Uploads a cooked texture (BC7, BC5 or RGBA8, every level) at the start of the next frame, and keeps it
    ///        for the next device.
    /// @param texture The texture, from Assets::cookTexture() or a `.m2texture` file.
    /// @param name    Shown in debugging tools.
    /// @return Its index, for MaterialDesc, or an error (no levels, or the device refused).
    [[nodiscard]] Result<std::uint32_t> addTexture(Assets::CookedTexture texture, std::string name = "Texture");

    /// @brief Adds a sampler, or finds an identical one already added; kept for the next device.
    /// @param desc How it filters and wraps.
    /// @return Its index, for MaterialTexture::sampler, or an error from the device.
    [[nodiscard]] Result<std::uint32_t> addSampler(const Rhi::SamplerDesc& desc);

    /// @brief Sets the sun and ambient light, from the next setView().
    /// @param lighting The light.
    void setLighting(const SceneLighting& lighting)
    {
        m_lighting = lighting;
    }

    /// @brief Lights the scene with a sky and draws it behind the scene, or goes back to the flat sky and ground
    ///        colours of SceneLighting.
    /// @param sky The sky, or null for none. Must outlive the scene, or be unset first; its addPasses() goes before
    ///            this scene's each frame.
    void setSky(const SkyAtmosphere* sky)
    {
        m_sky = sky;
    }

    /// @brief Whether the sky is drawn behind the scene (it still lights it). Debugging views turn it off, so their
    ///        background is plain and an overdraw count isn't raised by it.
    /// @param draw True to draw it (the default).
    void setSkyPass(bool draw)
    {
        m_skyPass = draw;
    }

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

    /// @brief Builds a set of the scene's draw pipelines from another variant of its shader, or with other states.
    ///
    /// The pipelines live in the PipelineLibrary, so they survive backend switches and shader reloads. Debugging tools
    /// use it for view modes and wireframes; an editor could use it for selection outlines.
    /// @param options The variant and states.
    /// @return The pipelines, or the error from building one.
    [[nodiscard]] Result<DrawPipelines> makeDrawPipelines(const DrawPipelineOptions& options);

    /// @brief Draws with other pipelines instead of the scene's own, until cleared.
    /// @param pipelines From makeDrawPipelines(); std::nullopt for the scene's own.
    void setPipelineOverride(std::optional<DrawPipelines> pipelines)
    {
        m_override = pipelines;
    }

    /// @brief Adds a pass that draws this frame's culled instances again with other pipelines, over what's in the
    ///        targets (e.g. a wireframe on top of the shaded scene). Blended surfaces are drawn too.
    /// @param graph     The frame's graph, after addPasses().
    /// @param name      The pass's name.
    /// @param pipelines From makeDrawPipelines().
    /// @param color     The colour target in the graph (addPasses()' result).
    /// @param colorTexture The same texture.
    /// @param depth     The depth target given to addPasses().
    /// @param frameSlot IRhiDevice::frameSlot().
    void addOverlayPass(RenderGraph& graph, std::string name, const DrawPipelines& pipelines, GraphResource color,
                        Rhi::TextureHandle colorTexture, Rhi::TextureHandle depth, std::uint32_t frameSlot);

    /// @brief Adds the culling and drawing passes.
    /// @param graph     The frame's graph.
    /// @param frameSlot IRhiDevice::frameSlot().
    /// @param swapchain Draws to this swapchain; null to use `color`.
    /// @param color     The colour target when `swapchain` is null.
    /// @param depth     The depth target, D32Float.
    /// @param clear     The colour to clear to.
    /// @return The colour target in the graph, for passes after these (tone mapping, debugging); an unset resource
    ///         when drawing to a swapchain.
    GraphResource addPasses(RenderGraph& graph, std::uint32_t frameSlot, Rhi::IRhiSwapchain* swapchain,
                            Rhi::TextureHandle color, Rhi::TextureHandle depth, const Math::Color& clear);

    /// @brief Records the state of the graph's resources, once it's compiled. Call before the next addPasses().
    /// @param graph The graph.
    void endFrame(const RenderGraph& graph);

    /// @brief Where the surviving draw counts land, for tests and statistics.
    /// @return A buffer holding one `uint` per draw list (MEOW2_DRAW_LIST_*), the back-culled list first. Changes when
    ///         the backend does.
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
    std::array<std::optional<PipelineId>, MEOW2_DRAW_LIST_COUNT> m_drawPipelines{}; ///< One per draw list.
    std::optional<PipelineId> m_blendPipeline;
    std::optional<DrawPipelines> m_override; ///< Used instead of the pipelines above when set.
    std::vector<std::pair<std::uint32_t, std::uint32_t>>
        m_blendDraws; ///< This frame's blended draws: instance, indices.
    Rhi::BufferHandle m_instanceBuffer;
    Rhi::BufferHandle m_meshBuffer;
    Rhi::BufferHandle m_drawBuffer;
    Rhi::BufferHandle m_countBuffer;
    std::array<Rhi::BufferHandle, Rhi::c_maxFramesInFlight> m_frameBuffers{};
    std::array<Rhi::BufferHandle, Rhi::c_maxFramesInFlight> m_cullBuffers{};
    std::vector<Rhi::BufferHandle> m_meshResources; ///< Vertex and index buffers.
    std::vector<MeshData> m_meshes;                 ///< CPU copies, for the next device.
    std::vector<Shader::InstanceData> m_instances;  ///< CPU copy, for the next device.

    /// @brief A texture, its levels kept for the next device, and its upload until the GPU has done it.
    struct TextureRecord
    {
        Assets::CookedTexture cooked{}; ///< CPU copy of every level, full size first.
        std::string name{};             ///< For debugging tools.
        Rhi::TextureHandle texture{};   ///< On the current device.
        Rhi::BufferHandle staging{};    ///< Holds the pixels until the copy is recorded; unset after.
    };
    std::vector<TextureRecord> m_textures;
    std::vector<std::uint32_t> m_pendingTextures;  ///< Textures whose copy the next frame records.
    std::vector<Rhi::BufferHandle> m_spentStaging; ///< Staging buffers to destroy once their copies are recorded.
    std::vector<MaterialDesc> m_materials;         ///< CPU copies, for the next device.

    /// @brief A sampler and how to make it again.
    struct SamplerRecord
    {
        Rhi::SamplerDesc desc{};      ///< How it samples.
        Rhi::SamplerHandle sampler{}; ///< On the current device.
    };
    std::vector<SamplerRecord> m_samplers;
    std::vector<std::uint32_t> m_blended;    ///< Instances with blended materials, drawn sorted.
    std::vector<std::uint32_t> m_blendOrder; ///< This frame's back-to-front order of m_blended.
    SceneLighting m_lighting{};
    const SkyAtmosphere* m_sky = nullptr; ///< Optional: lights the scene and is drawn behind it.
    bool m_skyPass = true;                ///< Whether the sky's pass is added (setSkyPass()).
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
