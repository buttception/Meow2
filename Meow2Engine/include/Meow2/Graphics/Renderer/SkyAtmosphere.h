/// @file
/// @brief SkyAtmosphere: a physically based sky (Hillaire 2020) and the image-based lighting taken from it.

#pragma once

#include <Meow2/Core/Result.h>
#include <Meow2/Graphics/Renderer/PipelineLibrary.h>
#include <Meow2/Graphics/Renderer/RenderDevice.h>
#include <Meow2/Graphics/Renderer/RenderGraph.h>
#include <Meow2/Graphics/Rhi/IRhiDevice.h>
#include <Meow2/Math/Color.h>
#include <Meow2/Math/Types.h>

#include <array>
#include <cstdint>
#include <memory>
#include <optional>

namespace Meow2::Renderer
{
/// @brief What to create a SkyAtmosphere with. Plain data.
struct SkyAtmosphereDesc
{
    AssetId shader{};                                     ///< `shaders/Sky.hlsl`.
    Rhi::RhiFormat colorFormat = Rhi::RhiFormat::Unknown; ///< Format of the target the sky is drawn into.
};

/// @brief The sun and the viewer, for one frame. Plain data.
struct SkySettings
{
    Math::Vec3 sunDirection{0.4F, 0.8F, 0.6F};          ///< Towards the sun, world space (+Y up); normalised here.
    Math::Color sunIlluminance{3.0F, 3.0F, 3.0F, 1.0F}; ///< Linear RGB above the atmosphere, the scene's units.
    float sunAngularRadius = 0.004675F;                 ///< Radians; the real sun's is about 0.27°.
    Math::Color groundAlbedo{0.3F, 0.3F, 0.3F, 1.0F};   ///< The planet's colour below the horizon, linear.
    float cameraAltitudeKm = 0.05F;                     ///< Above the ground; kept at least 50 m up (float precision).
};

/// @brief Heap indices the scene's shading reads to light with the sky (Shader::FrameConstants). Plain data.
struct SkyShadingInputs
{
    std::uint32_t environmentMap = 0;     ///< The prefiltered octahedral environment map.
    std::uint32_t environmentLevels = 0;  ///< Its mip levels.
    std::uint32_t irradianceBuffer = 0;   ///< 9 float4 spherical-harmonic coefficients of the diffuse light.
    std::uint32_t transmittanceLut = 0;   ///< For the sun's colour at the camera.
    std::uint32_t environmentSampler = 0; ///< Linear, clamped, mipmapped.
};

/// @brief A sky made of Sébastien Hillaire's atmosphere LUTs, and the lighting the scene gets from it.
///
/// Every frame, one compute pass (addPasses()):
/// 1. builds the transmittance and multiple-scattering LUTs, the first time only (they depend on the atmosphere);
/// 2. renders the sky-view LUT for this sun and camera altitude;
/// 3. captures the sky into an octahedral environment map, prefilters it for GGX roughness, and projects it onto
///    spherical harmonics for diffuse light.
///
/// drawSky() then draws the sky behind the scene, from inside the scene's own rendering pass. It's a RenderDevice
/// user: everything is made again after a backend switch, and the LUTs rebuilt.
/// @par Thread safety
/// Not thread-safe. The pass it adds runs on a job and only reads what addPasses() captured.
/// @see docs/wiki/Rendering.md
class SkyAtmosphere
{
public:
    /// @brief Creates the textures, buffers and pipelines.
    /// @param device    The device. Must outlive the sky.
    /// @param pipelines Builds the pipelines; uses the same device. Must outlive the sky.
    /// @param desc      Shader and target format.
    /// @return The sky, or an error from creating them.
    [[nodiscard]] static Result<std::unique_ptr<SkyAtmosphere>> create(RenderDevice& device, PipelineLibrary& pipelines,
                                                                       const SkyAtmosphereDesc& desc);

    /// @brief Use create().
    /// @param device    The device.
    /// @param pipelines Builds the pipelines.
    /// @param desc      Shader and target format.
    SkyAtmosphere(RenderDevice& device, PipelineLibrary& pipelines, const SkyAtmosphereDesc& desc);

    /// @brief Destroys the GPU objects.
    ~SkyAtmosphere();

    SkyAtmosphere(const SkyAtmosphere&) = delete;            ///< Not copyable: owns GPU objects.
    SkyAtmosphere& operator=(const SkyAtmosphere&) = delete; ///< Not copyable: owns GPU objects.

    /// @brief Sets this frame's sun and camera.
    /// @param frameSlot      IRhiDevice::frameSlot().
    /// @param settings       The sun and the camera's altitude.
    /// @param viewProjection The camera's view × projection (camera-relative world, reversed Z), for the sky's view
    ///                       directions.
    void setView(std::uint32_t frameSlot, const SkySettings& settings, const Math::Mat4& viewProjection);

    /// @brief Adds the compute pass that updates the LUTs and the lighting. Before the scene's passes, which read them.
    /// @param graph     The frame's graph.
    /// @param frameSlot IRhiDevice::frameSlot().
    void addPasses(RenderGraph& graph, std::uint32_t frameSlot);

    /// @brief Records that the frame's passes ran. Call after the graph has executed.
    void endFrame();

    /// @brief Draws the sky where nothing else was drawn. Inside a rendering pass with a D32Float depth target, after
    ///        the opaque geometry.
    /// @param list           The pass's command list.
    /// @param frameSlot      IRhiDevice::frameSlot().
    /// @param frameConstants Heap index of the frame's Shader::FrameConstants (for the exposure).
    void drawSky(Rhi::IRhiCommandList& list, std::uint32_t frameSlot, std::uint32_t frameConstants) const;

    /// @brief The heap indices the scene's shading reads.
    /// @return The indices on the current device.
    [[nodiscard]] SkyShadingInputs shadingInputs() const;

    /// @brief The settings from the last setView().
    /// @return The settings.
    [[nodiscard]] const SkySettings& settings() const
    {
        return m_settings;
    }

    /// @brief The transmittance LUT, Rgba16Float, for tests.
    /// @return The texture; changes when the backend does.
    [[nodiscard]] Rhi::TextureHandle transmittanceLut() const
    {
        return m_transmittance;
    }

    /// @brief The sky-view LUT, Rgba16Float, for tests.
    /// @return The texture; changes when the backend does.
    [[nodiscard]] Rhi::TextureHandle skyViewLut() const
    {
        return m_skyView;
    }

private:
    /// @brief Creates every texture, buffer and the sampler.
    /// @param device The device.
    /// @return The first error; what was made is destroyed by release().
    [[nodiscard]] Result<void> createResources(Rhi::IRhiDevice& device);

    /// @brief Destroys every GPU object and forgets the LUTs.
    /// @param device The device they were made on.
    void release(Rhi::IRhiDevice& device);

    RenderDevice& m_device;
    PipelineLibrary& m_pipelines;
    SkyAtmosphereDesc m_desc;
    SkySettings m_settings{};
    std::optional<PipelineId> m_transmittancePipeline;
    std::optional<PipelineId> m_multiScatteringPipeline;
    std::optional<PipelineId> m_skyViewPipeline;
    std::optional<PipelineId> m_environmentPipeline;
    std::optional<PipelineId> m_prefilterPipeline;
    std::optional<PipelineId> m_irradiancePipeline;
    std::optional<PipelineId> m_skyPipeline;
    Rhi::TextureHandle m_transmittance;
    Rhi::TextureHandle m_multiScattering;
    Rhi::TextureHandle m_skyView;
    Rhi::TextureHandle m_environment;
    Rhi::TextureHandle m_prefiltered;
    Rhi::BufferHandle m_irradiance;
    Rhi::SamplerHandle m_sampler;
    std::array<Rhi::BufferHandle, Rhi::c_maxFramesInFlight> m_constants{};
    bool m_lutsBuilt = false; ///< The transmittance and multiple-scattering LUTs hold their final contents.
    bool m_everDrawn = false; ///< The per-frame textures have been written once (they start Undefined).
    bool m_recorded = false;  ///< A pass was added since the last endFrame().
    DeviceUser m_deviceUser;  ///< Last: stops its callbacks before the members they use go away.
};
} // namespace Meow2::Renderer
