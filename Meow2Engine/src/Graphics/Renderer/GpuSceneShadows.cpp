/// @file
/// @brief GpuScene's sun shadow (GpuScene.h): cascaded shadow maps, each a cached map of the static casters (drawn
///        again only when it went stale) and a map drawn every frame from it plus the movable casters.

#include <Meow2/Graphics/Renderer/GpuScene.h>
#include <Meow2/Math/Bounds.h>

#include <DirectXMath.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <format>
#include <memory>
#include <utility>
#include <vector>

namespace Meow2::Renderer
{
namespace
{
/// @brief Fraction of each cascade, at its far end, blended into the next: hides the change of resolution.
constexpr float c_cascadeBlend = 0.1F;

/// @brief Constant depth bias against shadow acne, in the maps' depth units; the normal offset does most of the
/// work.
constexpr float c_depthBias = 2e-5F;
} // namespace

void GpuScene::setShadows(const SunShadowSettings& settings)
{
    const CascadeSettings& old = m_shadowSettings.cascades;
    const bool layoutChanged = settings.cascades.count != old.count || settings.cascades.resolution != old.resolution ||
                               settings.cascades.splitLambda != old.splitLambda ||
                               settings.cascades.padding != old.padding;
    m_shadowSettings = settings;
    if (layoutChanged)
    {
        for (const std::unique_ptr<View>& view : m_views)
        {
            if (view)
            {
                view->staticShadowDirty = ~0U;
            }
        }
    }
}

bool GpuScene::shadowsReady() const
{
    return m_shadowSettings.enabled && m_shadowPipelines && m_copyDepthPipeline && m_liveInstances > 0;
}

bool GpuScene::ensureShadowMaps(Rhi::IRhiDevice& device, View& view)
{
    const std::uint32_t resolution = m_shadowSettings.cascades.resolution;
    const std::uint32_t count = m_shadowSettings.cascades.count;
    if (view.shadowMapResolution == resolution && view.shadowMapCount >= count)
    {
        return true;
    }
    for (std::size_t i = 0; i < c_maxShadowCascades; ++i)
    {
        device.destroy(view.staticMaps[i]); // deferred until the GPU is done with them
        device.destroy(view.shadowMaps[i]);
        view.staticMaps[i] = {};
        view.shadowMaps[i] = {};
        view.staticStates[i] = Rhi::ResourceState::Undefined;
        view.shadowStates[i] = Rhi::ResourceState::Undefined;
    }
    view.shadowMapResolution = 0;
    view.shadowMapCount = 0;
    for (std::uint32_t i = 0; i < count; ++i)
    {
        for (Rhi::TextureHandle* map : {&view.staticMaps[i], &view.shadowMaps[i]})
        {
            Result<Rhi::TextureHandle> made =
                device.createTexture({.width = resolution,
                                      .height = resolution,
                                      .format = Rhi::RhiFormat::D32Float,
                                      .renderTarget = true,
                                      .tag = Memory::MemTag::Renderer,
                                      .name = map == &view.staticMaps[i] ? std::format("Shadow static {}", i)
                                                                         : std::format("Shadow map {}", i)});
            if (!made)
            {
                return false; // this frame has no shadows; the next tries again
            }
            *map = *made;
        }
    }
    view.shadowMapResolution = resolution;
    view.shadowMapCount = count;
    view.staticShadowDirty = ~0U;
    return true;
}

void GpuScene::writeShadows(View& view, std::uint32_t frameSlot, const Math::Vec3& sunDirection,
                            Shader::FrameConstants& constants)
{
    using namespace DirectX;
    view.frameShadows = false;
    constants.shadowBuffer = 0;
    Rhi::IRhiDevice& device = m_device.device();
    if (!shadowsReady() || !ensureShadowMaps(device, view))
    {
        return;
    }

    // How far shadows reach: to the far side of the casters from the camera, in powers of two (so it changes only
    // when the camera moves a lot, since every change moves every cascade), at most maxDistance.
    const XMVECTOR camera =
        XMVectorSet(constants.cameraPosition.x, constants.cameraPosition.y, constants.cameraPosition.z, 0.0F);
    const float reach =
        XMVectorGetX(XMVector3Length(XMVectorSubtract(Math::load(m_casters.center), camera))) + m_casters.radius;
    CascadeSettings settings = m_shadowSettings.cascades;
    settings.distance = std::min(m_shadowSettings.maxDistance, std::exp2(std::ceil(std::log2(std::max(reach, 1e-3F)))));
    const CascadeSettings& current = view.cascades.settings();
    if (settings.distance != current.distance || settings.count != current.count ||
        settings.resolution != current.resolution || settings.splitLambda != current.splitLambda ||
        settings.padding != current.padding)
    {
        view.cascades.setSettings(settings);
    }
    const CascadeCamera cascadeCamera{.view = constants.view,
                                      .tanHalfX = 1.0F / constants.projection._11,
                                      .tanHalfY = 1.0F / constants.projection._22,
                                      .nearZ = std::max(constants.projection._43, 1e-4F)};
    view.staticShadowDirty |= view.cascades.update(cascadeCamera, sunDirection, m_casters);

    // What lighting reads.
    Shader::ShadowConstants shadows{};
    const std::span<const ShadowCascade> cascades = view.cascades.cascades();
    for (std::size_t i = 0; i < cascades.size(); ++i)
    {
        Shader::ShadowCascadeData& data = shadows.cascades[i];
        data.viewProjection = cascades[i].viewProjection;
        data.splitNear = cascades[i].splitNear;
        data.splitFar = cascades[i].splitFar;
        data.texelSize = cascades[i].texelSize;
        data.depthTexture = device.bindlessIndex(view.shadowMaps[i]);
    }
    shadows.count = static_cast<std::uint32_t>(cascades.size());
    shadows.resolution = view.shadowMapResolution;
    shadows.blend = c_cascadeBlend;
    shadows.depthBias = c_depthBias;
    std::memcpy(device.mappedMemory(view.shadowConstantBuffers[frameSlot]).data(), &shadows, sizeof(shadows));

    // What the casters' passes read: the camera's frame constants with the cascade's view-projection (the vertex
    // shader projects with it; psShadow reads the materials), and a cull record per cascade and caster set.
    std::array<Shader::CullConstants, 2 * c_maxShadowCascades> culls{};
    for (std::size_t i = 0; i < cascades.size(); ++i)
    {
        Shader::FrameConstants caster = constants;
        caster.viewProjection = cascades[i].viewProjection;
        // The vertex shader projects (world - camera): translate back by the camera, then by the cascade.
        XMStoreFloat4x4(&caster.viewProjectionRelative,
                        XMMatrixMultiply(XMMatrixTranslation(constants.cameraPosition.x, constants.cameraPosition.y,
                                                             constants.cameraPosition.z),
                                         XMLoadFloat4x4(&cascades[i].viewProjection)));
        caster.debugView = 0;
        caster.shadowBuffer = 0;
        std::memcpy(device.mappedMemory(view.shadowFrameBuffers[frameSlot][i]).data(), &caster, sizeof(caster));

        const Math::Frustum box = Math::frustumFromMatrix(XMLoadFloat4x4(&cascades[i].viewProjection));
        for (std::uint32_t set = 0; set < 2; ++set)
        {
            Shader::CullConstants& cull = culls[i * 2 + set];
            cull.plane0 = box.planes[0];
            cull.plane1 = box.planes[1];
            cull.plane2 = box.planes[2];
            cull.plane3 = box.planes[3];
            cull.plane4 = box.planes[4];
            cull.plane5 = box.planes[5];
            cull.instanceBuffer = device.bindlessIndex(m_transformBuffer);
            cull.instanceInfoBuffer = device.bindlessIndex(m_infoBuffer);
            cull.meshBuffer = device.bindlessIndex(m_meshBuffer);
            cull.drawBuffer = device.bindlessStorageIndex(view.shadowDrawBuffer);
            cull.countBuffer = device.bindlessStorageIndex(view.shadowCountBuffer);
            cull.instanceCount = static_cast<std::uint32_t>(m_instances.size());
            cull.materialBuffer = constants.materialBuffer;
            cull.materialCount = constants.materialCount;
            cull.drawCapacity = m_desc.instanceCapacity;
            cull.origin = {0.0F, 0.0F, 0.0F, 0.0F}; // no cone culling: a parallel light has no apex
            cull.meshlets = 0;                      // casters draw through the indirect path
            cull.mobility = set == 0 ? 1U : 2U;     // static ones, then movable ones
        }
    }
    std::memcpy(device.mappedMemory(view.shadowCullBuffers[frameSlot]).data(), culls.data(), sizeof(culls));
    constants.shadowBuffer = device.bindlessIndex(view.shadowConstantBuffers[frameSlot]);
    view.frameShadows = true;
}

void GpuScene::addShadowPasses(View& view, RenderGraph& graph, std::uint32_t frameSlot,
                               const Shader::PushConstants& push)
{
    using Rhi::ResourceState;
    Rhi::IRhiDevice& device = m_device.device();
    view.shadowDrawResource = graph.import(view.shadowDrawBuffer, view.shadowDrawState, "Shadow draws");
    view.shadowCountResource = graph.import(view.shadowCountBuffer, view.shadowCountState, "Shadow draw counts");
    const std::uint32_t cullIndex = device.bindlessIndex(view.shadowCullBuffers[frameSlot]);
    DrawPipelines casters = *m_shadowPipelines;
    casters.hasMeshLists = false;
    View* const v = &view; // for the passes' lambdas: views live behind unique_ptrs, so the address is stable
    const auto slotCount = static_cast<std::uint32_t>(m_instances.size());

    // One set of casters into a map: clear the counts, cull against the cascade's box, draw depth only. `skipped`
    // keeps the passes in the graph without running them, so the graph has the same shape every frame.
    const auto drawCasters = [&, v](std::uint32_t record, GraphResource target, Rhi::TextureHandle texture, bool clear,
                                    const Shader::PushConstants& castPush, std::string name, bool skipped)
    {
        graph.addPass({.name = std::format("Clear {} count", name),
                       .accesses = {{view.shadowCountResource, ResourceState::CopyTarget}},
                       .record = [v](Rhi::IRhiCommandList& list) { list.fillBuffer(v->shadowCountBuffer, 0); },
                       .small = true,
                       .skipped = skipped});
        std::vector<GraphAccess> cullAccesses = instanceReads();
        cullAccesses.push_back({view.shadowCountResource, ResourceState::ShaderWrite});
        cullAccesses.push_back({view.shadowDrawResource, ResourceState::ShaderWrite});
        std::vector<GraphAccess> drawAccesses = instanceReads();
        drawAccesses.push_back({view.shadowCountResource, ResourceState::IndirectArgs});
        drawAccesses.push_back({view.shadowDrawResource, ResourceState::IndirectArgs});
        drawAccesses.push_back({target, ResourceState::DepthTarget});
        graph.addPass({.name = std::format("Cull {}", name),
                       .accesses = std::move(cullAccesses),
                       .record =
                           [this, cullIndex, record, slotCount](Rhi::IRhiCommandList& list)
                       {
                           Shader::PushConstants cullPush{};
                           cullPush.userData = cullIndex;
                           cullPush.instance = record; // which cascade and caster set
                           list.setPipeline(m_pipelines.get(*m_cullPipeline));
                           list.setPushConstants(cullPush);
                           list.dispatch((slotCount + 63) / 64); // [numthreads(64, 1, 1)] in csCull
                       },
                       .small = true,
                       .skipped = skipped});
        graph.addPass({.name = std::move(name),
                       .accesses = std::move(drawAccesses),
                       .record =
                           [this, v, casters, castPush, texture, clear](Rhi::IRhiCommandList& list)
                       {
                           // Cleared to 0, the far end of reversed Z: nothing in the way of the sun.
                           list.beginRendering(Rhi::RenderTargets{
                               .depth = texture, .clearDepth = clear ? std::optional{0.0F} : std::nullopt});
                           recordLists(*v, list, casters, castPush, v->shadowDrawBuffer, v->shadowCountBuffer);
                           list.endRendering();
                       },
                       .skipped = skipped});
    };

    for (std::uint32_t i = 0; i < view.cascades.settings().count; ++i)
    {
        view.staticResources[i] =
            graph.import(view.staticMaps[i], view.staticStates[i], std::format("Shadow static {}", i));
        view.shadowResources[i] =
            graph.import(view.shadowMaps[i], view.shadowStates[i], std::format("Shadow map {}", i));
        Shader::PushConstants castPush = push; // instances and meshes as for the camera
        castPush.frameBuffer = device.bindlessIndex(view.shadowFrameBuffers[frameSlot][i]);

        // The static casters, run only when this cascade's cache is stale; skipped (but still in the graph)
        // otherwise.
        const bool stale = (view.staticShadowDirty & (1U << i)) != 0;
        drawCasters(i * 2, view.staticResources[i], view.staticMaps[i], true, castPush,
                    std::format("Static shadow {}", i), !stale);
        if (stale)
        {
            view.staticShadowDirty &= ~(1U << i);
            ++view.staticShadowRedraws;
        }

        // The cached map, copied texel for texel into this frame's map ...
        Shader::PushConstants copyPush{};
        copyPush.userData = device.bindlessIndex(view.staticMaps[i]);
        graph.addPass({.name = std::format("Shadow map {}", i),
                       .accesses = {{view.staticResources[i], ResourceState::ShaderRead},
                                    {view.shadowResources[i], ResourceState::DepthTarget}},
                       .record =
                           [this, copyPush, texture = view.shadowMaps[i]](Rhi::IRhiCommandList& list)
                       {
                           list.beginRendering(Rhi::RenderTargets{.depth = texture});
                           list.setPipeline(m_pipelines.get(*m_copyDepthPipeline));
                           list.setPushConstants(copyPush);
                           list.draw(3);
                           list.endRendering();
                       },
                       .small = true});

        // ... then the movable casters over it, every frame.
        if (m_hasMovable)
        {
            drawCasters(i * 2 + 1, view.shadowResources[i], view.shadowMaps[i], false, castPush,
                        std::format("Movable shadow {}", i), false);
        }
    }
}
} // namespace Meow2::Renderer
