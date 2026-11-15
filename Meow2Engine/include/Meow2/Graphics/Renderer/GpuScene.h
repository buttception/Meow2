/// @file
/// @brief GpuScene: meshes and instances in GPU buffers, culled in compute and drawn indirectly.

#pragma once

#include <Meow2/Assets/Texture/CookedTexture.h>
#include <Meow2/Assets/Texture/Image.h>
#include <Meow2/Core/Result.h>
#include <Meow2/Graphics/Renderer/PipelineLibrary.h>
#include <Meow2/Graphics/Renderer/RenderDevice.h>
#include <Meow2/Graphics/Renderer/RenderGraph.h>
#include <Meow2/Graphics/Renderer/ShadowCascades.h>
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
#include <utility>
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
    bool meshlets = true; ///< Also build the meshlet path's pipelines, where the GPU has mesh shaders. False keeps
                          ///< frames drawn with this set on the indirect path.
    /// @brief The (first) colour target's format; empty: the scene's (GpuSceneDesc::colorFormat).
    std::optional<Rhi::RhiFormat> colorFormat{};
    std::array<Rhi::RhiFormat, Rhi::c_maxColorTargets - 1> extraColorFormats{}; ///< More targets, e.g. a G-buffer's.
    bool blended = true;               ///< Also build the pipeline for blended surfaces (DrawPipelines::blend).
    std::string pixelEntry = "psMain"; ///< The pixel shader (`psShadow` for the sun's shadow casters).
    std::string name = "Variant";      ///< Prefix of the pipelines' names.
};

/// @brief A GpuScene's draw pipelines: one per draw list for each path, and one for blended surfaces. Plain data.
struct DrawPipelines
{
    std::array<PipelineId, MEOW2_DRAW_LIST_COUNT> lists{};     ///< Indirect path: one per MEOW2_DRAW_LIST_*.
    PipelineId blend{};                                        ///< For blended surfaces (both paths).
    std::array<PipelineId, MEOW2_DRAW_LIST_COUNT> meshLists{}; ///< Meshlet path: one per MEOW2_DRAW_LIST_*.
    bool hasMeshLists = false;                                 ///< Whether `meshLists` were built.
};

/// @brief What the meshlet path drew, for statistics. Plain data.
struct MeshletStats
{
    std::uint32_t meshlets = 0;       ///< Meshlets of every opaque instance (before any culling).
    std::uint64_t triangles = 0;      ///< Their triangles.
    std::uint32_t meshletsDrawn = 0;  ///< Meshlets left after culling instances and then meshlets, a few frames ago.
    std::uint32_t trianglesDrawn = 0; ///< Their triangles.
    bool valid = false;               ///< False until a meshlet-path frame has come back from the GPU.
};

/// @brief What to create a GpuScene with. Plain data.
struct GpuSceneDesc
{
    AssetId shader{};         ///< `shaders/GpuDriven.hlsl`.
    AssetId deferredShader{}; ///< `shaders/Deferred.hlsl`, for the deferred path (setDeferred()); unset: forward only.
    AssetId shadowShader{};   ///< `shaders/Shadows.hlsl`: the sun's shadow (setShadows()); unset: no shadows.
    std::uint32_t instanceCapacity = 1;                   ///< Most instances; > 0.
    Rhi::RhiFormat colorFormat = Rhi::RhiFormat::Unknown; ///< Format of the colour target.
};

/// @brief How the sun's shadow is drawn. Plain data.
struct SunShadowSettings
{
    bool enabled = true;        ///< Whether the sun casts shadows.
    CascadeSettings cascades{}; ///< Cascades and resolution; `distance` is worked out by the scene (see setShadows()).
    float maxDistance = 200.0F; ///< The farthest shadows reach, in metres.
};

/// @brief A mesh ready to upload: quantised positions, octahedral normals and tangents, half-float UVs, RGBA8 colours,
///        32-bit indices, and its meshlets. Plain data.
/// @see MeshProcessing.h: optimizeMesh() and buildMeshlets().
struct MeshData
{
    std::vector<Shader::PackedVertex> vertices{};  ///< Made by makeMesh() or packVertex().
    std::vector<std::uint32_t> indices{};          ///< Three per triangle; clockwise faces the viewer.
    Math::Aabb bounds{};                           ///< The box the positions are quantised in.
    std::vector<Shader::Meshlet> meshlets{};       ///< From buildMeshlets(); GpuScene::addMesh() builds them if empty.
    std::vector<std::uint32_t> meshletVertices{};  ///< Each meshlet's vertices: indices into `vertices`.
    std::vector<std::uint32_t> meshletTriangles{}; ///< Each meshlet's triangles: three 8-bit indices into its
                                                   ///< vertices, low byte first.
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

/// @brief One instance to draw: a mesh with a material at a place in the world. Plain data.
struct InstanceDesc
{
    Math::Mat4 world{};         ///< Local → world, row vectors (Math conventions); any scale, mirroring included.
    std::uint32_t mesh = 0;     ///< From GpuScene::addMesh().
    std::uint32_t material = 0; ///< From GpuScene::addMaterial(); a higher index gets a colour from its index.
    bool movable = false;       ///< Moves at runtime: casts into the sun's shadow every frame, not into its cache.
};

/// @brief Identifies one of a GpuScene's views (cameras drawn each frame), from GpuScene::addView().
using ViewId = std::uint32_t;

/// @brief The view every GpuScene starts with: the one the overloads without a ViewId use.
inline constexpr ViewId c_mainView = 0;

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
///        GPU-driven call per list draws them with metallic-roughness PBR.
///
/// There's no per-object work on the CPU for opaque and alpha-masked surfaces, so the cost of a frame doesn't grow with
/// the instance count. Instances and meshes live in buffers the shaders read by heap index, and vertices are pulled by
/// the vertex or mesh shader.
///
/// - **Draw lists** (MEOW2_DRAW_LIST_*): back faces culled (most meshes), mirrored instances (negative scale reverses
///   the winding, so front faces are culled instead), and double-sided materials (nothing culled).
/// - **Two paths** for the lists. The *indirect path* draws each surviving instance whole (an indirect draw each). The
///   *meshlet path*, on GPUs with mesh shaders, culls each instance's meshlets too: a task shader tests 32 at a time
///   against the frustum and by their normal cones, and a mesh shader draws the ones left. It's on by default where it
///   can run (setMeshlets()); both draw the same image.
/// - **Blended materials** are left out of the lists. They're drawn after them, one direct draw per instance, sorted
///   back to front on the CPU, without writing depth.
/// - **Light:** one sun and a sky/ground ambient (SceneLighting), then exposure and a tone curve.
/// - **Instances** live in a persistent GPU buffer with stable slots, Unreal's "GPU Scene" model: a 3 × 4 transform
///   and last frame's (InstanceTransform), and what's drawn (InstanceInfo) apart from it. Each frame only the changed
///   slots are uploaded, and a compute pass (`csScatter`) copies them into place before anything culls, so the cost of
///   a frame follows what moved, not the scene's size. setInstances() finds the changes itself; addInstance(),
///   updateInstance() and removeInstance() work slot by slot.
/// - **Views:** each camera drawn in a frame is a view (addView()) with its own culling lists, G-buffer, shadow
///   cascades and caches; the scene's meshes, materials, textures and instances are shared. The overloads without a
///   ViewId draw the main view.
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
    /// @param mesh The mesh. Without meshlets, buildMeshlets() makes them (optimizeMesh() is the caller's choice: it
    ///             reorders the triangles).
    /// @return Its index, for Shader::InstanceData::mesh, or an error.
    [[nodiscard]] Result<std::uint32_t> addMesh(const MeshData& mesh);

    /// @brief Chooses the meshlet path or the indirect path for the next frames.
    /// @param on True for the meshlet path where the GPU has mesh shaders (the default); false for the indirect path.
    void setMeshlets(bool on)
    {
        m_meshletsWanted = on;
    }

    /// @brief Whether the meshlet path can run: the GPU has mesh shaders and its pipelines are built.
    /// @return True if setMeshlets(true) draws with meshlets.
    [[nodiscard]] bool meshletsAvailable() const;

    /// @brief Whether the next frame draws with meshlets: asked for, available, and the pipeline override (if any) has
    ///        meshlet pipelines.
    /// @return True for the meshlet path.
    [[nodiscard]] bool usesMeshlets() const;

    /// @brief Adds a view: a camera drawn each frame with its own culling lists, G-buffer and shadow maps (an editor's
    ///        second viewport). Its buffers are made at once; its textures at its first frame.
    /// @return Its id, for the ViewId overloads; or an error from creating its buffers.
    [[nodiscard]] Result<ViewId> addView();

    /// @brief Removes a view added with addView() and frees its GPU objects (once the GPU is done with them).
    /// @param view The view; not c_mainView.
    void removeView(ViewId view);

    /// @brief How many views draw (the main view included).
    /// @return The count.
    [[nodiscard]] std::uint32_t viewCount() const;

    /// @brief What the meshlet path drew. The GPU's counts arrive a few frames late (they're read back without
    ///        waiting).
    /// @param view Which view.
    /// @return The counts.
    [[nodiscard]] MeshletStats meshletStats(ViewId view = c_mainView) const;

    /// @brief Where the meshlet path's counters land, for tests: MEOW2_TASK_COUNT_SIZE `uint`s, laid out as
    ///        MEOW2_TASK_COUNT_*.
    /// @param view Which view.
    /// @return The buffer; changes when the backend does.
    [[nodiscard]] Rhi::BufferHandle meshletCountBuffer(ViewId view = c_mainView) const;

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

    /// @brief How the sun's shadow is drawn: cascaded shadow maps, needing GpuSceneDesc::shadowShader.
    ///
    /// Each cascade has a cached map of the static casters, drawn again only when the cascade moved, the sun turned or
    /// a static instance changed, and a map drawn every frame: a copy of the cached one, with the movable casters
    /// (MEOW2_INSTANCE_MOVABLE) drawn over it. Lighting (forward and deferred) reads that map.
    /// How far shadows reach is worked out from the instances' bounds and the camera, in powers of two (so it rarely
    /// changes), at most `maxDistance`.
    /// @param settings The settings; a change of cascades or resolution draws every static map again.
    void setShadows(const SunShadowSettings& settings);

    /// @brief How the sun's shadow is drawn.
    /// @return The settings.
    [[nodiscard]] const SunShadowSettings& shadows() const
    {
        return m_shadowSettings;
    }

    /// @brief How many times a cascade's cached map of static casters has been drawn since the scene was made: for
    ///        statistics and tests (it should rise only when the camera moves far, the sun turns or the scene changes).
    /// @param view Which view.
    /// @return The count.
    [[nodiscard]] std::uint32_t staticShadowRedraws(ViewId view = c_mainView) const;

    /// @brief The cascades of the last setView() with shadows.
    /// @param view Which view.
    /// @return Them; empty without shadows.
    [[nodiscard]] std::span<const ShadowCascade> shadowCascades(ViewId view = c_mainView) const;

    /// @brief Each cascade's shadow map (static and movable casters), as lighting reads it.
    /// @param view Which view.
    /// @return The maps; unset handles before the first frame with shadows.
    [[nodiscard]] const std::array<Rhi::TextureHandle, c_maxShadowCascades>& shadowMaps(ViewId view = c_mainView) const;

    /// @brief Draws opaque surfaces deferred: a **G-buffer** pass writes each surface's properties (the textures
    ///        gBuffer() lists), then a **Lighting** pass shades every pixel once from them, rebuilding its position
    ///        from depth. Off: one **Opaque** pass shades as it draws (forward). Both give the same image.
    ///
    /// Only when the scene was made with GpuSceneDesc::deferredShader and draws into its own colour texture (not a
    /// swapchain); a pipeline override (debug views) always draws forward. Blended surfaces are always forward.
    /// @param deferred True for the deferred path.
    void setDeferred(bool deferred)
    {
        m_deferred = deferred;
    }

    /// @brief Whether this frame drew deferred (after addPasses()).
    /// @param view Which view.
    /// @return True if the G-buffer and Lighting passes were added.
    [[nodiscard]] bool drewDeferred(ViewId view = c_mainView) const;

    /// @brief The G-buffer textures (created on the first deferred frame, at the colour target's size): albedo
    ///        (RGBA8 sRGB), normal (RGBA16F), material (RGBA8: metallic, roughness, occlusion), emissive (RGBA16F).
    /// @param view Which view.
    /// @return Them; unset handles before the first deferred frame.
    [[nodiscard]] const std::array<Rhi::TextureHandle, 4>& gBuffer(ViewId view = c_mainView) const;

    /// @brief Adds a material, and keeps a copy for the next device.
    /// @param material Its parameters.
    /// @return Its index, for InstanceDesc::material, or an error when the scene has too many.
    [[nodiscard]] Result<std::uint32_t> addMaterial(const MaterialDesc& material);

    /// @brief Sets every instance: instance i goes in slot i, and slots past the end are freed. Only what differs from
    ///        the slots' current contents is uploaded at the next frame, so calling it every frame with a scene where
    ///        one object moved uploads one record. Any time between frames; the GPU never has to be idle.
    /// @param instances The instances; at most GpuSceneDesc::instanceCapacity.
    void setInstances(std::span<const InstanceDesc> instances);

    /// @brief Adds one instance in a free slot (the lowest), uploaded at the next frame.
    /// @param instance What to draw where.
    /// @return Its slot, which stays its own until removeInstance(); or ErrorCode::OutOfMemory when
    ///         GpuSceneDesc::instanceCapacity slots are in use.
    [[nodiscard]] Result<std::uint32_t> addInstance(const InstanceDesc& instance);

    /// @brief Changes one instance, uploaded at the next frame (nothing is uploaded if it didn't change).
    /// @param slot     From addInstance(), or an index given to setInstances().
    /// @param instance Its new description.
    /// @pre The slot is in use (asserted).
    void updateInstance(std::uint32_t slot, const InstanceDesc& instance);

    /// @brief Frees a slot: the instance isn't drawn from the next frame, and the slot can be reused.
    /// @param slot The slot; an unused one is ignored.
    void removeInstance(std::uint32_t slot);

    /// @brief How many instance records the last frame uploaded (changed slots plus the ones settling: a slot that
    ///        moved is sent once more the frame after, so its previous transform catches up). For statistics and tests.
    /// @return The count.
    [[nodiscard]] std::uint32_t instanceUploads() const
    {
        return m_lastUploads;
    }

    /// @brief The GPU's instance records, for tests: InstanceTransform per slot.
    /// @return The buffer; changes when the backend does.
    [[nodiscard]] Rhi::BufferHandle instanceTransformBuffer() const
    {
        return m_transformBuffer;
    }

    /// @brief Sets a view's camera for this frame. FrameConstants::viewProjectionRelative is worked out here from
    ///        `viewProjection` and `cameraPosition`.
    /// @param frameSlot IRhiDevice::frameSlot().
    /// @param frame     What shaders see.
    /// @param frustum   What's visible, in world space like the instances.
    /// @param view      Which view.
    void setView(std::uint32_t frameSlot, const Shader::FrameConstants& frame, const Math::Frustum& frustum,
                 ViewId view = c_mainView);

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
    /// @param pipelines From makeDrawPipelines(). On a meshlet-path frame they need meshlet pipelines
    ///                  (DrawPipelineOptions::meshlets), or only the blended surfaces are drawn again.
    /// @param color     The colour target in the graph (addPasses()' result).
    /// @param colorTexture The same texture.
    /// @param depth     The depth target given to addPasses().
    /// @param frameSlot IRhiDevice::frameSlot().
    /// @param debug     Marks the pass as a debugging tool's (GraphPass::debug), e.g. a wireframe.
    /// @param view      Which view's lists.
    void addOverlayPass(RenderGraph& graph, std::string name, const DrawPipelines& pipelines, GraphResource color,
                        Rhi::TextureHandle colorTexture, Rhi::TextureHandle depth, std::uint32_t frameSlot,
                        bool debug = false, ViewId view = c_mainView);

    /// @brief Adds a view's culling and drawing passes. The first call of a frame also adds the passes every view
    ///        shares: new textures copied, and the changed instances uploaded and scattered into place.
    /// @param graph     The frame's graph; the same one for every view drawn this frame.
    /// @param frameSlot IRhiDevice::frameSlot().
    /// @param swapchain Draws to this swapchain; null to use `color`.
    /// @param color     The colour target when `swapchain` is null.
    /// @param depth     The depth target, D32Float.
    /// @param clear     The colour to clear to.
    /// @param view      Which view; setView() for it came first this frame.
    /// @return The colour target in the graph, for passes after these (tone mapping, debugging); an unset resource
    ///         when drawing to a swapchain.
    GraphResource addPasses(RenderGraph& graph, std::uint32_t frameSlot, Rhi::IRhiSwapchain* swapchain,
                            Rhi::TextureHandle color, Rhi::TextureHandle depth, const Math::Color& clear,
                            ViewId view = c_mainView);

    /// @brief Records the state of the graph's resources (every view drawn in it), once it's compiled. Call once per
    ///        frame, before the next addPasses().
    /// @param graph The graph.
    void endFrame(const RenderGraph& graph);

    /// @brief The depth target in this frame's graph, for passes after addPasses() that read or test it (an infinite
    ///        grid hidden behind the scene). Importing the texture again would track its state twice.
    /// @param view Which view.
    /// @return The resource the view's last addPasses() imported.
    [[nodiscard]] GraphResource depthResource(ViewId view = c_mainView) const;

    /// @brief Where the surviving draw counts land, for tests and statistics.
    /// @return A buffer holding one `uint` per draw list (MEOW2_DRAW_LIST_*), the back-culled list first. Changes when
    ///         the backend does.
    /// @param view Which view.
    [[nodiscard]] Rhi::BufferHandle countBuffer(ViewId view = c_mainView) const;

    /// @brief How many instances are drawn, for statistics.
    /// @return Slots in use.
    [[nodiscard]] std::uint32_t instanceCount() const
    {
        return m_liveInstances;
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
    /// @brief Everything one camera needs that another doesn't: its constants, culling lists, meshlet task lists,
    ///        G-buffer, shadow cascades and caches, and this frame's blended order. One per view.
    struct View
    {
        std::array<Rhi::BufferHandle, Rhi::c_maxFramesInFlight> frameBuffers{};   ///< FrameConstants per frame slot.
        std::array<Rhi::BufferHandle, Rhi::c_maxFramesInFlight> cullBuffers{};    ///< CullConstants per frame slot.
        std::array<Rhi::BufferHandle, Rhi::c_maxFramesInFlight> countReadback{};  ///< Meshlet counters, for the CPU.
        std::array<Rhi::BufferHandle, Rhi::c_maxFramesInFlight> deferredInputs{}; ///< DeferredInputs per frame slot.
        std::array<bool, Rhi::c_maxFramesInFlight> slotReadsBack{}; ///< A slot's last frame copied meshlet counts.
        Rhi::BufferHandle drawBuffer;                               ///< IndirectDraw lists.
        Rhi::BufferHandle countBuffer;                              ///< One count per list.
        Rhi::BufferHandle taskBuffer;                               ///< TaskItem lists, one per draw list.
        Rhi::BufferHandle taskCountBuffer;                          ///< MEOW2_TASK_COUNT_* counters.
        Rhi::BufferHandle taskArgsBuffer;                           ///< Mesh dispatch group counts, per draw list.
        std::uint32_t taskCapacity = 0;                             ///< TaskItems each list has room for.
        Rhi::ResourceState drawState = Rhi::ResourceState::Undefined;
        Rhi::ResourceState countState = Rhi::ResourceState::Undefined;
        Rhi::ResourceState depthState = Rhi::ResourceState::Undefined;
        Rhi::ResourceState taskState = Rhi::ResourceState::Undefined;
        Rhi::ResourceState taskCountState = Rhi::ResourceState::Undefined;
        Rhi::ResourceState taskArgsState = Rhi::ResourceState::Undefined;
        Rhi::TextureHandle lastDepth; ///< The depth target of the last frame.
        GraphResource drawResource;
        GraphResource countResource;
        GraphResource depthResource;
        GraphResource taskResource;
        GraphResource taskCountResource;
        GraphResource taskArgsResource;
        bool drawn = false;                    ///< addPasses() ran for it in the frame being built (endFrame() resets).
        bool frameUsesMeshlets = false;        ///< The path its last addPasses() chose, for overlay passes.
        bool frameDeferred = false;            ///< Whether its last addPasses() drew deferred.
        bool frameShadows = false;             ///< Whether its last setView() set up the sun's shadow.
        MeshletStats meshletStats{};           ///< What it drew on the meshlet path.
        std::vector<std::uint32_t> blendOrder; ///< Blended slots, farthest first.
        std::vector<std::pair<std::uint32_t, std::uint32_t>> blendDraws; ///< This frame's: slot, index count.
        std::array<Rhi::TextureHandle, 4> gBuffer{};                     ///< Albedo, normal, material, emissive.
        std::uint32_t gBufferWidth = 0;                                  ///< Their size.
        std::uint32_t gBufferHeight = 0;
        Rhi::ResourceState gBufferState = Rhi::ResourceState::Undefined;
        std::array<GraphResource, 4> gBufferResources{};
        ShadowCascades cascades;                                          ///< Fitted to this view's camera.
        std::array<Rhi::TextureHandle, c_maxShadowCascades> staticMaps{}; ///< Cached static casters, per cascade.
        std::array<Rhi::TextureHandle, c_maxShadowCascades> shadowMaps{}; ///< Static + movable, per cascade.
        std::array<Rhi::ResourceState, c_maxShadowCascades> staticStates{};
        std::array<Rhi::ResourceState, c_maxShadowCascades> shadowStates{};
        std::array<GraphResource, c_maxShadowCascades> staticResources{};
        std::array<GraphResource, c_maxShadowCascades> shadowResources{};
        std::uint32_t shadowMapResolution = 0; ///< The maps' size; 0 before they're made.
        std::uint32_t shadowMapCount = 0;      ///< How many cascades have maps.
        Rhi::BufferHandle shadowDrawBuffer;    ///< The shadow passes' culled draws (reused cascade after cascade).
        Rhi::BufferHandle shadowCountBuffer;
        Rhi::ResourceState shadowDrawState = Rhi::ResourceState::Undefined;
        Rhi::ResourceState shadowCountState = Rhi::ResourceState::Undefined;
        GraphResource shadowDrawResource;
        GraphResource shadowCountResource;
        std::array<Rhi::BufferHandle, Rhi::c_maxFramesInFlight> shadowCullBuffers{};     ///< 2 CullConstants a cascade.
        std::array<Rhi::BufferHandle, Rhi::c_maxFramesInFlight> shadowConstantBuffers{}; ///< ShadowConstants.
        /// The casters' FrameConstants (the camera's, with the cascade's view-projection), per frame slot and cascade.
        std::array<std::array<Rhi::BufferHandle, c_maxShadowCascades>, Rhi::c_maxFramesInFlight> shadowFrameBuffers{};
        std::uint32_t staticShadowDirty = ~0U; ///< A bit per cascade whose cached map must be drawn again.
        std::uint32_t staticShadowRedraws = 0; ///< staticShadowRedraws().
    };

    /// @brief One slot of the instance buffers, as the CPU keeps it.
    struct InstanceRecord
    {
        InstanceDesc desc{};                       ///< What's drawn; meaningless while free.
        Math::Mat4 previous{};                     ///< The transform the GPU has as "previous" after the next upload.
        std::uint32_t flags = MEOW2_INSTANCE_FREE; ///< MEOW2_INSTANCE_* as uploaded: mirrored, movable or free.
        bool dirty = false;                        ///< Uploaded at the next frame.
        bool settling = false; ///< Moved last upload: sent once more so its previous transform catches up.
    };

    /// @brief The view with that id. @pre It exists (asserted).
    [[nodiscard]] View& view(ViewId id);
    /// @brief The view with that id. @pre It exists (asserted).
    [[nodiscard]] const View& view(ViewId id) const;

    /// @brief Creates the buffers every view shares (not the meshes').
    /// @param device The device to create them on.
    /// @return The first error; what was made is destroyed by destroyBuffers().
    [[nodiscard]] Result<void> createBuffers(Rhi::IRhiDevice& device);
    /// @brief Creates a view's buffers (its textures come with its first frame).
    /// @return The first error; what was made is destroyed by destroyViewBuffers().
    [[nodiscard]] Result<void> createViewBuffers(Rhi::IRhiDevice& device, View& view);
    /// @brief Destroys a view's buffers and textures, and forgets their handles and states.
    void destroyViewBuffers(Rhi::IRhiDevice& device, View& view);

    /// @brief Writes one slot's record and marks it for upload; works out its flags (mirrored, movable).
    void storeInstance(std::uint32_t slot, const InstanceDesc& desc);
    /// @brief After a change of instances or meshes (at the next setView()): the blended list, the casters' bounds, the
    ///        static fingerprint (a changed static caster marks every view's cached shadow maps stale) and the meshlet
    ///        path's needs.
    void refreshDerived();
    /// @brief Adds the passes every view shares, once per frame: new textures copied, changed instances uploaded and
    ///        scattered into place.
    void addSharedPasses(RenderGraph& graph, std::uint32_t frameSlot);
    /// @brief Makes the per-frame-slot upload buffer big enough for `records` InstanceUpdates.
    /// @return False if it couldn't be made (the uploads wait for the next frame).
    bool ensureUpdateBuffer(Rhi::IRhiDevice& device, std::uint32_t frameSlot, std::uint32_t records);
    /// @brief Makes a view's TaskItem lists big enough for the instances' meshlets.
    /// @return False if they couldn't be made (the view draws through the indirect path).
    bool ensureTaskBuffer(Rhi::IRhiDevice& device, View& view);

    /// @brief Whether the sun's shadow can be drawn (enabled, its pipelines built, instances to cast).
    [[nodiscard]] bool shadowsReady() const;
    /// @brief Makes each cascade's two maps at the settings' resolution, if they aren't already.
    /// @return False if a texture couldn't be made (the frame then has no shadows).
    bool ensureShadowMaps(Rhi::IRhiDevice& device, View& view);
    /// @brief Fits the view's cascades to its camera and writes the shadow constants, the casters' frame constants and
    ///        cull records for this frame slot; sets `constants.shadowBuffer`.
    void writeShadows(View& view, std::uint32_t frameSlot, const Math::Vec3& sunDirection,
                      Shader::FrameConstants& constants);
    /// @brief Adds, per cascade, the static casters (if its cache is stale), the copy and the movable casters.
    void addShadowPasses(View& view, RenderGraph& graph, std::uint32_t frameSlot, const Shader::PushConstants& push);
    /// @brief The accesses every pass that reads instances needs: both instance buffers.
    [[nodiscard]] std::vector<GraphAccess> instanceReads() const;

    /// @brief Makes a view's G-buffer at a colour target's size, if it isn't already.
    /// @param device The device.
    /// @param view   The view.
    /// @param color  The scene's colour target; the G-buffer matches its size.
    /// @return False if a texture couldn't be made (the frame then draws forward).
    bool ensureGBuffer(Rhi::IRhiDevice& device, View& view, Rhi::TextureHandle color);

    /// @brief Destroys every buffer, the meshes' and the views' too, and forgets their handles.
    /// @param device The device they were made on.
    void destroyBuffers(Rhi::IRhiDevice& device);

    /// @brief Makes every buffer again on a new device, from the CPU copies; every instance is uploaded again.
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
    /// @param mesh   The mesh, with its meshlets.
    /// @param index  Its place in the mesh buffer.
    /// @return An error from creating its buffers.
    [[nodiscard]] Result<void> uploadMesh(Rhi::IRhiDevice& device, const MeshData& mesh, std::uint32_t index);

    /// @brief Adds the meshlet path's mesh pipelines for a variant.
    /// @param options The variant and states.
    /// @param out     [in,out] Gets the pipelines, and `hasMeshLists`.
    /// @return An error from building one.
    [[nodiscard]] Result<void> addMeshPipelines(const DrawPipelineOptions& options, DrawPipelines& out);

    /// @brief Records the draws of the three lists, inside beginRendering(): one mesh dispatch per list on the meshlet
    ///        path (`pipelines.hasMeshLists`, from the view's task arguments), one indirect draw call per list from
    ///        `draws` and `counts` on the other.
    void recordLists(const View& view, Rhi::IRhiCommandList& list, const DrawPipelines& pipelines,
                     const Shader::PushConstants& push, Rhi::BufferHandle draws, Rhi::BufferHandle counts) const;

    /// @brief Copies the meshlet counts read back for a frame slot into the view's stats, if that slot's frame used
    ///        the meshlet path. The GPU has finished the slot's last frame when it's being recorded again.
    void readMeshletStats(Rhi::IRhiDevice& device, View& view, std::uint32_t frameSlot);

    RenderDevice& m_device;
    PipelineLibrary& m_pipelines;
    GpuSceneDesc m_desc;
    std::optional<PipelineId> m_cullPipeline;
    std::optional<PipelineId> m_taskArgsPipeline; ///< csTaskArgs; only with mesh shaders.
    std::optional<PipelineId> m_scatterPipeline;  ///< csScatter.
    std::array<std::optional<PipelineId>, MEOW2_DRAW_LIST_COUNT> m_drawPipelines{}; ///< One per draw list.
    std::optional<PipelineId> m_blendPipeline;
    std::array<std::optional<PipelineId>, MEOW2_DRAW_LIST_COUNT>
        m_meshPipelines{};                      ///< Meshlet path; mesh shaders only.
    bool m_meshletsWanted = true;               ///< setMeshlets().
    std::uint32_t m_taskItemsNeeded = 1;        ///< TaskItems a list must have room for: every opaque slot's meshlets.
    MeshletStats m_meshletTotals{};             ///< Meshlets and triangles of every opaque instance.
    std::optional<DrawPipelines> m_override;    ///< Used instead of the pipelines above when set.
    std::vector<std::unique_ptr<View>> m_views; ///< By ViewId; null for a removed view. [0] is the main view.

    // Instances: the CPU's copy of every slot, and the GPU's two buffers, written only by csScatter.
    std::vector<InstanceRecord> m_instances; ///< One per slot up to the highest used; free ones are FREE.
    std::vector<std::uint32_t> m_dirty;      ///< Slots to upload at the next frame (each once).
    std::uint32_t m_liveInstances = 0;       ///< Slots in use.
    std::uint32_t m_lastUploads = 0;         ///< instanceUploads().
    bool m_derivedStale = true;              ///< refreshDerived() must run before the next view.
    Rhi::BufferHandle m_transformBuffer;     ///< InstanceTransform per slot; GPU only.
    Rhi::BufferHandle m_infoBuffer;          ///< InstanceInfo per slot; GPU only.
    Rhi::ResourceState m_transformState = Rhi::ResourceState::Undefined;
    Rhi::ResourceState m_infoState = Rhi::ResourceState::Undefined;
    GraphResource m_transformResource; ///< This frame's, imported by the first addPasses().
    GraphResource m_infoResource;
    bool m_sharedPassesAdded = false; ///< addSharedPasses() ran for the frame being built.
    std::array<Rhi::BufferHandle, Rhi::c_maxFramesInFlight> m_updateBuffers{}; ///< InstanceUpdates, per frame slot.
    std::array<std::uint32_t, Rhi::c_maxFramesInFlight> m_updateCapacity{};    ///< Records each has room for.

    Rhi::BufferHandle m_meshBuffer;
    std::vector<Rhi::BufferHandle> m_meshResources; ///< Vertex and index buffers.
    std::vector<MeshData> m_meshes;                 ///< CPU copies, for the next device.

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
    std::vector<std::uint32_t> m_blended; ///< Slots with blended materials, drawn sorted.
    SceneLighting m_lighting{};
    const SkyAtmosphere* m_sky = nullptr;            ///< Optional: lights the scene and is drawn behind it.
    bool m_skyPass = true;                           ///< Whether the sky's pass is added (setSkyPass()).
    bool m_deferred = false;                         ///< setDeferred().
    std::optional<DrawPipelines> m_gBufferPipelines; ///< GBUFFER = 1, into the G-buffer's formats.
    std::optional<PipelineId> m_lightingPipeline;    ///< Deferred.hlsl's psLighting.
    Rhi::BufferHandle m_materialBuffer;
    SunShadowSettings m_shadowSettings{};
    std::optional<DrawPipelines> m_shadowPipelines; ///< Depth only, psShadow; the indirect path.
    std::optional<PipelineId> m_copyDepthPipeline;  ///< Shadows.hlsl's psCopyDepth.
    std::uint64_t m_staticHash = 0;                 ///< Of the static instances, to see when they change.
    Math::Sphere m_casters{};                       ///< Around every instance.
    bool m_hasMovable = false;                      ///< Whether any instance is MEOW2_INSTANCE_MOVABLE.
    DeviceUser m_deviceUser;                        ///< Last: stops its callbacks before the members they use go away.
};
} // namespace Meow2::Renderer
