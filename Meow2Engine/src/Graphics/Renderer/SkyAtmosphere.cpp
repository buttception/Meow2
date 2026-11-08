/// @file
/// @brief SkyAtmosphere: Hillaire's sky LUTs, image-based lighting from them, and the sky behind the scene.

#include "Meow2/Graphics/Renderer/SkyAtmosphere.h"

#include "Meow2/Core/Assert.h"
#include "Meow2/Graphics/Shader/BindlessLayout.h"

#include <DirectXMath.h>

#include <algorithm>
#include <cstring>
#include <utility>

namespace Meow2::Renderer
{
namespace
{
// The sizes shaders/Atmosphere.hlsli uses; the shaders read them from there, the textures are made with these.
constexpr std::uint32_t c_transmittanceWidth = 256;
constexpr std::uint32_t c_transmittanceHeight = 64;
constexpr std::uint32_t c_multiScatteringSize = 32;
constexpr std::uint32_t c_skyViewWidth = 192;
constexpr std::uint32_t c_skyViewHeight = 108;
constexpr std::uint32_t c_environmentSize = 128;
constexpr std::uint32_t c_environmentLevels = 8;     // 128 down to 1
constexpr std::uint32_t c_prefilteredLevels = 6;     // 128 down to 4
constexpr std::uint32_t c_irradianceSourceLevel = 3; // the environment's 16 × 16 level, one texel per thread
constexpr std::uint32_t c_group = 8;                 // [numthreads(8, 8, 1)] of the 2D passes
constexpr std::uint32_t c_harmonicCount = 9;

/// @brief Thread groups to cover a size.
constexpr std::uint32_t groups(std::uint32_t size)
{
    return (size + c_group - 1) / c_group;
}
} // namespace

SkyAtmosphere::SkyAtmosphere(RenderDevice& device, PipelineLibrary& pipelines, const SkyAtmosphereDesc& desc)
    : m_device(device)
    , m_pipelines(pipelines)
    , m_desc(desc)
{
}

Result<std::unique_ptr<SkyAtmosphere>> SkyAtmosphere::create(RenderDevice& device, PipelineLibrary& pipelines,
                                                             const SkyAtmosphereDesc& desc)
{
    auto sky = std::make_unique<SkyAtmosphere>(device, pipelines, desc);
    if (Result<void> made = sky->createResources(device.device()); !made)
    {
        sky->release(device.device());
        return Unexpected<Error>{made.error()};
    }
    const auto compute = [&](std::optional<PipelineId>& out, const char* entry, const char* name) -> Result<void>
    {
        Result<PipelineId> pipeline =
            pipelines.add(ComputePipelineRequest{.shader = desc.shader, .options = {}, .entry = entry, .name = name});
        if (!pipeline)
        {
            return Unexpected<Error>{pipeline.error()};
        }
        out = *pipeline;
        return {};
    };
    for (Result<void> made : {compute(sky->m_transmittancePipeline, "csTransmittance", "Sky transmittance"),
                              compute(sky->m_multiScatteringPipeline, "csMultiScattering", "Sky multiple scattering"),
                              compute(sky->m_skyViewPipeline, "csSkyView", "Sky view"),
                              compute(sky->m_environmentPipeline, "csEnvironment", "Sky environment"),
                              compute(sky->m_prefilterPipeline, "csPrefilter", "Sky prefilter"),
                              compute(sky->m_irradiancePipeline, "csIrradiance", "Sky irradiance")})
    {
        if (!made)
        {
            return Unexpected<Error>{made.error()};
        }
    }
    // Depth is tested, not written: the sky's triangle sits at depth 0 (infinitely far, reversed Z), so it only
    // shows where the cleared depth is still 0.
    Result<PipelineId> draw = pipelines.add(GraphicsPipelineRequest{.shader = desc.shader,
                                                                    .options = {},
                                                                    .vertexEntry = "vsSky",
                                                                    .pixelEntry = "psSky",
                                                                    .colorFormat = desc.colorFormat,
                                                                    .depthFormat = Rhi::RhiFormat::D32Float,
                                                                    .depthWrite = false,
                                                                    .cull = Rhi::CullMode::None,
                                                                    .blend = Rhi::BlendMode::None,
                                                                    .name = "Sky"});
    if (!draw)
    {
        return Unexpected<Error>{draw.error()};
    }
    sky->m_skyPipeline = *draw;

    // Registered after the pipeline library, so its pipelines are back before the sky restores.
    SkyAtmosphere* const self = sky.get();
    sky->m_deviceUser = device.addUser(
        "SkyAtmosphere", [self](Rhi::IRhiDevice& old) { self->release(old); },
        [self](Rhi::IRhiDevice& fresh) -> Result<void>
        {
            if (Result<void> made = self->createResources(fresh); !made)
            {
                self->release(fresh); // a failed restore isn't released
                return made;
            }
            return {};
        });
    return sky;
}

SkyAtmosphere::~SkyAtmosphere()
{
    if (m_device.hasDevice())
    {
        release(m_device.device());
    }
}

Result<void> SkyAtmosphere::createResources(Rhi::IRhiDevice& device)
{
    const auto texture = [&device](Rhi::TextureHandle& out, std::uint32_t width, std::uint32_t height,
                                   std::uint32_t levels, const char* name) -> Result<void>
    {
        Result<Rhi::TextureHandle> made = device.createTexture({.width = width,
                                                                .height = height,
                                                                .format = Rhi::RhiFormat::Rgba16Float,
                                                                .mipLevels = levels,
                                                                .renderTarget = false,
                                                                .storage = true,
                                                                .tag = Memory::MemTag::Renderer,
                                                                .name = name});
        if (!made)
        {
            return Unexpected<Error>{made.error()};
        }
        out = *made;
        return {};
    };
    for (Result<void> made :
         {texture(m_transmittance, c_transmittanceWidth, c_transmittanceHeight, 1, "Sky transmittance LUT"),
          texture(m_multiScattering, c_multiScatteringSize, c_multiScatteringSize, 1, "Sky multiple-scattering LUT"),
          texture(m_skyView, c_skyViewWidth, c_skyViewHeight, 1, "Sky-view LUT"),
          texture(m_environment, c_environmentSize, c_environmentSize, c_environmentLevels, "Sky environment"),
          texture(m_prefiltered, c_environmentSize, c_environmentSize, c_prefilteredLevels, "Sky prefiltered")})
    {
        if (!made)
        {
            return made;
        }
    }
    // GpuOnly: shaders can only write GPU memory on D3D12.
    Result<Rhi::BufferHandle> irradiance = device.createBuffer({.size = c_harmonicCount * 16,
                                                                .memory = Rhi::RhiMemory::GpuOnly,
                                                                .tag = Memory::MemTag::Renderer,
                                                                .name = "Sky irradiance"});
    if (!irradiance)
    {
        return Unexpected<Error>{irradiance.error()};
    }
    m_irradiance = *irradiance;
    for (Rhi::BufferHandle& constants : m_constants)
    {
        Result<Rhi::BufferHandle> buffer = device.createBuffer({.size = sizeof(Shader::SkyConstants),
                                                                .memory = Rhi::RhiMemory::Upload,
                                                                .tag = Memory::MemTag::Renderer,
                                                                .name = "Sky constants"});
        if (!buffer)
        {
            return Unexpected<Error>{buffer.error()};
        }
        constants = *buffer;
    }
    // Clamped: the LUTs' edges are the parameter range's ends, and wrapping would bleed one into the other.
    Result<Rhi::SamplerHandle> sampler = device.createSampler({.filter = Rhi::SamplerFilter::Linear,
                                                               .addressU = Rhi::SamplerAddress::Clamp,
                                                               .addressV = Rhi::SamplerAddress::Clamp,
                                                               .maxAnisotropy = 1.0F,
                                                               .name = "Sky sampler"});
    if (!sampler)
    {
        return Unexpected<Error>{sampler.error()};
    }
    m_sampler = *sampler;
    m_lutsBuilt = false;
    m_everDrawn = false;
    m_recorded = false;
    return {};
}

void SkyAtmosphere::release(Rhi::IRhiDevice& device)
{
    // Unset handles are ignored, so this also cleans up after a creation that stopped halfway.
    for (Rhi::TextureHandle* texture :
         {&m_transmittance, &m_multiScattering, &m_skyView, &m_environment, &m_prefiltered})
    {
        device.destroy(*texture);
        *texture = {};
    }
    device.destroy(m_irradiance);
    m_irradiance = {};
    for (Rhi::BufferHandle& constants : m_constants)
    {
        device.destroy(constants);
        constants = {};
    }
    device.destroy(m_sampler);
    m_sampler = {};
    m_lutsBuilt = false;
    m_everDrawn = false;
    m_recorded = false;
}

void SkyAtmosphere::setView(std::uint32_t frameSlot, const SkySettings& settings, const Math::Mat4& viewProjection)
{
    using namespace DirectX;
    m_settings = settings;
    Rhi::IRhiDevice& device = m_device.device();
    Shader::SkyConstants constants{};
    XMMATRIX inverse = XMMatrixInverse(nullptr, XMLoadFloat4x4(&viewProjection));
    XMStoreFloat4x4(&constants.inverseViewProjection, inverse);
    Math::Vec3 sun;
    XMStoreFloat3(&sun, XMVector3Normalize(Math::load(settings.sunDirection)));
    constants.sunDirection = {sun.x, sun.y, sun.z, settings.sunAngularRadius};
    constants.sunIlluminance = {settings.sunIlluminance.r, settings.sunIlluminance.g, settings.sunIlluminance.b,
                                std::max(settings.cameraAltitudeKm, 0.05F)};
    constants.groundAlbedo = {settings.groundAlbedo.r, settings.groundAlbedo.g, settings.groundAlbedo.b, 0.0F};
    constants.transmittanceLut = device.bindlessIndex(m_transmittance);
    constants.multiScatteringLut = device.bindlessIndex(m_multiScattering);
    constants.skyViewLut = device.bindlessIndex(m_skyView);
    constants.environment = device.bindlessIndex(m_environment);
    constants.transmittanceStorage = device.bindlessStorageIndex(m_transmittance);
    constants.multiScatteringStorage = device.bindlessStorageIndex(m_multiScattering);
    constants.skyViewStorage = device.bindlessStorageIndex(m_skyView);
    constants.irradianceStorage = device.bindlessStorageIndex(m_irradiance);
    constants.linearSampler = device.bindlessIndex(m_sampler);
    constants.environmentLevels = c_environmentLevels;
    std::memcpy(device.mappedMemory(m_constants[frameSlot]).data(), &constants, sizeof(constants));
}

void SkyAtmosphere::addPasses(RenderGraph& graph, std::uint32_t frameSlot)
{
    using Rhi::ResourceState;
    Rhi::IRhiDevice& device = m_device.device();
    // Everything this pass needs, looked up now: the record function runs on a job.
    struct Work
    {
        std::uint32_t constants = 0;
        bool buildLuts = false;
        ResourceState perFrameState = ResourceState::Undefined;
        std::array<std::uint32_t, c_environmentLevels> environmentLevels{};
        std::array<std::uint32_t, c_prefilteredLevels> prefilteredLevels{};
    };
    Work work;
    work.constants = device.bindlessIndex(m_constants[frameSlot]);
    work.buildLuts = !m_lutsBuilt;
    // After the first frame the per-frame textures are left readable; before it they hold nothing.
    work.perFrameState = m_everDrawn ? ResourceState::ShaderRead : ResourceState::Undefined;
    for (std::uint32_t level = 0; level < c_environmentLevels; ++level)
    {
        work.environmentLevels[level] = device.bindlessStorageIndex(m_environment, level);
    }
    for (std::uint32_t level = 0; level < c_prefilteredLevels; ++level)
    {
        work.prefilteredLevels[level] = device.bindlessStorageIndex(m_prefiltered, level);
    }
    m_recorded = true;

    graph.addPass({.name = "Sky atmosphere",
                   .accesses = {},
                   .record =
                       [this, work](Rhi::IRhiCommandList& list)
                   {
                       const auto push = [&list, &work](std::uint32_t storage, std::uint32_t level, std::uint32_t size)
                       {
                           Shader::PushConstants constants{};
                           constants.frameBuffer = size;
                           constants.instanceBuffer = level;
                           constants.instance = storage;
                           constants.userData = work.constants;
                           list.setPushConstants(constants);
                       };
                       if (work.buildLuts)
                       {
                           // The multiple-scattering LUT reads the transmittance LUT, so it must be finished first.
                           list.barrier(m_transmittance, ResourceState::Undefined, ResourceState::ShaderWrite);
                           list.setPipeline(m_pipelines.get(*m_transmittancePipeline));
                           push(0, 0, 0);
                           list.dispatch(groups(c_transmittanceWidth), groups(c_transmittanceHeight));
                           list.barrier(m_transmittance, ResourceState::ShaderWrite, ResourceState::ShaderRead);
                           list.barrier(m_multiScattering, ResourceState::Undefined, ResourceState::ShaderWrite);
                           list.setPipeline(m_pipelines.get(*m_multiScatteringPipeline));
                           list.dispatch(c_multiScatteringSize, c_multiScatteringSize); // a group per texel
                           list.barrier(m_multiScattering, ResourceState::ShaderWrite, ResourceState::ShaderRead);
                       }
                       // The sky for this sun and altitude.
                       list.barrier(m_skyView, work.perFrameState, ResourceState::ShaderWrite);
                       list.setPipeline(m_pipelines.get(*m_skyViewPipeline));
                       push(0, 0, 0);
                       list.dispatch(groups(c_skyViewWidth), groups(c_skyViewHeight));
                       list.barrier(m_skyView, ResourceState::ShaderWrite, ResourceState::ShaderRead);
                       // The environment, every level straight from the sky-view LUT (no level reads another).
                       list.barrier(m_environment, work.perFrameState, ResourceState::ShaderWrite);
                       list.setPipeline(m_pipelines.get(*m_environmentPipeline));
                       for (std::uint32_t level = 0; level < c_environmentLevels; ++level)
                       {
                           const std::uint32_t size = std::max(c_environmentSize >> level, 1U);
                           push(work.environmentLevels[level], level, size);
                           list.dispatch(groups(size), groups(size));
                       }
                       list.barrier(m_environment, ResourceState::ShaderWrite, ResourceState::ShaderRead);
                       // Prefiltered for roughness, and the diffuse light, both read the finished environment.
                       list.barrier(m_prefiltered, work.perFrameState, ResourceState::ShaderWrite);
                       list.setPipeline(m_pipelines.get(*m_prefilterPipeline));
                       for (std::uint32_t level = 0; level < c_prefilteredLevels; ++level)
                       {
                           const std::uint32_t size = std::max(c_environmentSize >> level, 1U);
                           push(work.prefilteredLevels[level], level, size);
                           list.dispatch(groups(size), groups(size));
                       }
                       list.barrier(m_prefiltered, ResourceState::ShaderWrite, ResourceState::ShaderRead);
                       list.barrier(m_irradiance, work.perFrameState, ResourceState::ShaderWrite);
                       list.setPipeline(m_pipelines.get(*m_irradiancePipeline));
                       push(c_irradianceSourceLevel, 0, 0);
                       list.dispatch(1);
                       list.barrier(m_irradiance, ResourceState::ShaderWrite, ResourceState::ShaderRead);
                   },
                   .small = false});
}

void SkyAtmosphere::endFrame()
{
    if (m_recorded)
    {
        m_lutsBuilt = true;
        m_everDrawn = true;
        m_recorded = false;
    }
}

void SkyAtmosphere::drawSky(Rhi::IRhiCommandList& list, std::uint32_t frameSlot, std::uint32_t frameConstants) const
{
    Shader::PushConstants push{};
    push.frameBuffer = frameConstants;
    push.userData = m_device.device().bindlessIndex(m_constants[frameSlot]);
    list.setPipeline(m_pipelines.get(*m_skyPipeline));
    list.setPushConstants(push);
    list.draw(3);
}

SkyShadingInputs SkyAtmosphere::shadingInputs() const
{
    Rhi::IRhiDevice& device = m_device.device();
    return {.environmentMap = device.bindlessIndex(m_prefiltered),
            .environmentLevels = c_prefilteredLevels,
            .irradianceBuffer = device.bindlessIndex(m_irradiance),
            .transmittanceLut = device.bindlessIndex(m_transmittance),
            .environmentSampler = device.bindlessIndex(m_sampler)};
}
} // namespace Meow2::Renderer
