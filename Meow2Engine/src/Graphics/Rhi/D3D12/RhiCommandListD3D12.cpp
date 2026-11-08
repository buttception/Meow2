/// @file
/// @brief Recording GPU work on D3D12: rendering, draws, dispatches, enhanced barriers and copies.

#include "Graphics/Rhi/D3D12/RhiDeviceD3D12.h"
#include "Graphics/Rhi/D3D12/RhiSwapchainD3D12.h"
#include "Meow2/Core/Assert.h"
#include "Meow2/Graphics/Shader/BindlessLayout.h"

#include <cstring>
#include <vector>

namespace Meow2::Rhi
{
namespace
{
/// @brief What a resource state means to D3D12's enhanced barriers. Plain data.
struct D3D12State
{
    D3D12_BARRIER_SYNC sync = D3D12_BARRIER_SYNC_NONE;            ///< Which work uses the resource in this state.
    D3D12_BARRIER_ACCESS access = D3D12_BARRIER_ACCESS_NO_ACCESS; ///< How it's used.
    D3D12_BARRIER_LAYOUT layout = D3D12_BARRIER_LAYOUT_UNDEFINED; ///< The layout, for textures.
};

/// @brief Translates a state: the same stages and accesses as on Vulkan (vulkanState() in RhiCommandListVulkan.cpp).
/// @param state The state.
/// @return Its D3D12 sync, access and layout.
D3D12State d3d12State(ResourceState state)
{
    switch (state)
    {
        case ResourceState::Undefined:
        case ResourceState::HostRead: // the fence wait before the CPU reads is the synchronisation
            return {};
        case ResourceState::ColorTarget:
            return {D3D12_BARRIER_SYNC_RENDER_TARGET, D3D12_BARRIER_ACCESS_RENDER_TARGET,
                    D3D12_BARRIER_LAYOUT_RENDER_TARGET};
        case ResourceState::DepthTarget:
            return {D3D12_BARRIER_SYNC_DEPTH_STENCIL, D3D12_BARRIER_ACCESS_DEPTH_STENCIL_WRITE,
                    D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_WRITE};
        case ResourceState::ShaderRead:
            return {D3D12_BARRIER_SYNC_ALL_SHADING, D3D12_BARRIER_ACCESS_SHADER_RESOURCE,
                    D3D12_BARRIER_LAYOUT_SHADER_RESOURCE};
        case ResourceState::ShaderWrite:
            return {D3D12_BARRIER_SYNC_ALL_SHADING, D3D12_BARRIER_ACCESS_UNORDERED_ACCESS,
                    D3D12_BARRIER_LAYOUT_UNORDERED_ACCESS};
        case ResourceState::CopySource:
            return {D3D12_BARRIER_SYNC_COPY, D3D12_BARRIER_ACCESS_COPY_SOURCE, D3D12_BARRIER_LAYOUT_COPY_SOURCE};
        case ResourceState::CopyTarget:
            return {D3D12_BARRIER_SYNC_COPY, D3D12_BARRIER_ACCESS_COPY_DEST, D3D12_BARRIER_LAYOUT_COPY_DEST};
        case ResourceState::IndirectArgs:
            return {D3D12_BARRIER_SYNC_EXECUTE_INDIRECT, D3D12_BARRIER_ACCESS_INDIRECT_ARGUMENT,
                    D3D12_BARRIER_LAYOUT_UNDEFINED};
    }
    return {};
}

/// @brief Bytes per pixel of the formats textures use.
/// @param format The format.
/// @return The size.
std::uint32_t pixelSize(RhiFormat format)
{
    return format == RhiFormat::Rgba16Float ? 8U : 4U;
}
} // namespace

RhiCommandListD3D12::RhiCommandListD3D12(RhiDeviceD3D12& device)
    : m_device(device)
{
}

void RhiCommandListD3D12::startRendering(D3D12_CPU_DESCRIPTOR_HANDLE color, const D3D12_CPU_DESCRIPTOR_HANDLE* depth,
                                         std::uint32_t width, std::uint32_t height)
{
    MEOW2_ASSERT(!m_rendering, "endRendering() wasn't called");
    m_list->OMSetRenderTargets(1, &color, FALSE, depth);
    // D3D12's Y points down in the viewport already, so no flip: the same matrices as Vulkan's flipped viewport.
    const D3D12_VIEWPORT viewport{.TopLeftX = 0.0F,
                                  .TopLeftY = 0.0F,
                                  .Width = static_cast<float>(width),
                                  .Height = static_cast<float>(height),
                                  .MinDepth = 0.0F,
                                  .MaxDepth = 1.0F};
    const D3D12_RECT scissor{
        .left = 0, .top = 0, .right = static_cast<LONG>(width), .bottom = static_cast<LONG>(height)};
    m_list->RSSetViewports(1, &viewport);
    m_list->RSSetScissorRects(1, &scissor);
    m_rendering = true;
}

D3D12_CPU_DESCRIPTOR_HANDLE RhiCommandListD3D12::prepareDepth(TextureHandle depth, std::optional<float> clearDepth)
{
    const D3D12_CPU_DESCRIPTOR_HANDLE view = m_device.dsvHeap().cpu(m_device.texture(depth).targetIndex);
    if (clearDepth)
    {
        m_list->ClearDepthStencilView(view, D3D12_CLEAR_FLAG_DEPTH, *clearDepth, 0, 0, nullptr);
    }
    return view;
}

void RhiCommandListD3D12::beginRendering(IRhiSwapchain& swapchain, std::optional<Math::Color> clear)
{
    beginRendering(swapchain, clear, TextureHandle{}, std::nullopt);
}

void RhiCommandListD3D12::beginRendering(IRhiSwapchain& swapchain, std::optional<Math::Color> clear,
                                         TextureHandle depth, std::optional<float> clearDepth)
{
    auto& target = static_cast<RhiSwapchainD3D12&>(swapchain);
    MEOW2_ASSERT(target.isAcquired(), "acquire() must return true before drawing to a swapchain");
    const D3D12_CPU_DESCRIPTOR_HANDLE color = target.view();
    if (clear)
    {
        const std::array<float, 4> values{clear->r, clear->g, clear->b, clear->a};
        m_list->ClearRenderTargetView(color, values.data(), 0, nullptr);
    }
    if (depth.isSet())
    {
        const D3D12_CPU_DESCRIPTOR_HANDLE depthView = prepareDepth(depth, clearDepth);
        startRendering(color, &depthView, target.width(), target.height());
    }
    else
    {
        startRendering(color, nullptr, target.width(), target.height());
    }
}

void RhiCommandListD3D12::beginRendering(const RenderTargets& targets)
{
    const TextureD3D12 texture = m_device.texture(targets.color);
    const D3D12_CPU_DESCRIPTOR_HANDLE color = m_device.rtvHeap().cpu(texture.targetIndex);
    if (targets.clearColor)
    {
        const Math::Color& clear = *targets.clearColor;
        const std::array<float, 4> values{clear.r, clear.g, clear.b, clear.a};
        m_list->ClearRenderTargetView(color, values.data(), 0, nullptr);
    }
    if (targets.depth.isSet())
    {
        const D3D12_CPU_DESCRIPTOR_HANDLE depthView = prepareDepth(targets.depth, targets.clearDepth);
        startRendering(color, &depthView, texture.width, texture.height);
    }
    else
    {
        startRendering(color, nullptr, texture.width, texture.height);
    }
}

void RhiCommandListD3D12::endRendering()
{
    MEOW2_ASSERT(m_rendering, "beginRendering() wasn't called");
    m_rendering = false; // D3D12 has no render pass to end: targets stay bound until the next beginRendering()
}

void RhiCommandListD3D12::setPipeline(PipelineHandle pipeline)
{
    m_list->SetPipelineState(m_device.pipeline(pipeline).state);
}

void RhiCommandListD3D12::beginMarker(const char* name)
{
    // Metadata 1 is PIX's plain-ANSI-string event, which PIX, RenderDoc and Nsight all read, with no PIX library.
    constexpr UINT c_ansiString = 1;
    m_list->BeginEvent(c_ansiString, name, static_cast<UINT>(std::strlen(name) + 1));
    m_gpuZones.push(m_device.gpuProfiler(), m_list, name);
    ++m_openMarkers;
}

void RhiCommandListD3D12::endMarker()
{
    MEOW2_ASSERT(m_openMarkers > 0, "endMarker() without beginMarker()");
    m_gpuZones.pop();
    m_list->EndEvent();
    --m_openMarkers;
}

void RhiCommandListD3D12::setPushConstants(const Shader::PushConstants& constants)
{
    // Both root signatures are the same one, so both get the values, whichever kind of pipeline comes next.
    constexpr UINT c_values = sizeof(constants) / sizeof(std::uint32_t);
    m_list->SetGraphicsRoot32BitConstants(0, c_values, &constants, 0);
    m_list->SetComputeRoot32BitConstants(0, c_values, &constants, 0);
}

void RhiCommandListD3D12::draw(std::uint32_t vertexCount, std::uint32_t instanceCount)
{
    MEOW2_ASSERT(m_rendering, "draw() outside beginRendering()");
    m_list->DrawInstanced(vertexCount, instanceCount, 0, 0);
}

void RhiCommandListD3D12::setScissor(const ScissorRect& rect)
{
    MEOW2_ASSERT(m_rendering, "setScissor() outside beginRendering()");
    const D3D12_RECT scissor{.left = rect.x,
                             .top = rect.y,
                             .right = rect.x + static_cast<LONG>(rect.width),
                             .bottom = rect.y + static_cast<LONG>(rect.height)};
    m_list->RSSetScissorRects(1, &scissor);
}

void RhiCommandListD3D12::drawIndirectCount(BufferHandle draws, BufferHandle count, std::uint32_t maxDraws,
                                            std::uint64_t drawOffset, std::uint64_t countOffset)
{
    MEOW2_ASSERT(m_rendering, "drawIndirectCount() outside beginRendering()");
    // Each draw also writes PushConstants::instance (see the command signature), which leaves that root constant
    // undefined afterwards: set push constants again before the next draw.
    m_list->ExecuteIndirect(m_device.drawSignature(), maxDraws, m_device.buffer(draws).resource, drawOffset,
                            m_device.buffer(count).resource, countOffset);
}

void RhiCommandListD3D12::fillBuffer(BufferHandle buffer, std::uint32_t value)
{
    MEOW2_ASSERT(!m_rendering, "fillBuffer() inside beginRendering()");
    // D3D12 has no fill command for buffers. WriteBufferImmediate writes 32-bit words straight from the command list,
    // which suits the small buffers this is for (draw counts); a large clear would want a compute shader.
    const BufferD3D12 target = m_device.buffer(buffer);
    const D3D12_GPU_VIRTUAL_ADDRESS start = target.resource->GetGPUVirtualAddress();
    std::vector<D3D12_WRITEBUFFERIMMEDIATE_PARAMETER> words(static_cast<std::size_t>((target.size + 3U) / 4U));
    for (std::size_t i = 0; i < words.size(); ++i)
    {
        words[i] = {.Dest = start + i * sizeof(std::uint32_t), .Value = value};
    }
    m_list->WriteBufferImmediate(static_cast<UINT>(words.size()), words.data(), nullptr);
}

void RhiCommandListD3D12::dispatch(std::uint32_t x, std::uint32_t y, std::uint32_t z)
{
    MEOW2_ASSERT(!m_rendering, "dispatch() inside beginRendering()");
    m_list->Dispatch(x, y, z);
}

void RhiCommandListD3D12::barrier(BufferHandle buffer, ResourceState from, ResourceState to)
{
    MEOW2_ASSERT(!m_rendering, "Barriers go between passes");
    const BufferD3D12 record = m_device.buffer(buffer);
    if (record.mapped != nullptr)
    {
        // Upload and readback buffers live in a heap whose state is fixed (D3D12 forbids barriers on them). The CPU
        // sees GPU writes after the fence wait, and the GPU sees CPU writes at the next submit.
        return;
    }
    const D3D12State source = d3d12State(from);
    const D3D12State target = d3d12State(to);
    const D3D12_BUFFER_BARRIER barrier{.SyncBefore = source.sync,
                                       .SyncAfter = target.sync,
                                       .AccessBefore = source.access,
                                       .AccessAfter = target.access,
                                       .pResource = record.resource,
                                       .Offset = 0,
                                       .Size = UINT64_MAX};
    const D3D12_BARRIER_GROUP group{.Type = D3D12_BARRIER_TYPE_BUFFER, .NumBarriers = 1, .pBufferBarriers = &barrier};
    m_list->Barrier(1, &group);
}

void RhiCommandListD3D12::barrier(TextureHandle texture, ResourceState from, ResourceState to)
{
    MEOW2_ASSERT(!m_rendering, "Barriers go between passes");
    const TextureD3D12 image = m_device.texture(texture);
    const D3D12State source = d3d12State(from);
    const D3D12State target = d3d12State(to);
    // Coming from undefined, a render target or depth buffer must be told its old contents can go, so the driver
    // can initialise its compression data.
    const bool discard = from == ResourceState::Undefined && image.renderTarget;
    const D3D12_TEXTURE_BARRIER barrier{.SyncBefore = source.sync,
                                        .SyncAfter = target.sync,
                                        .AccessBefore = source.access,
                                        .AccessAfter = target.access,
                                        .LayoutBefore = source.layout,
                                        .LayoutAfter = target.layout,
                                        .pResource = image.resource,
                                        .Subresources = {.IndexOrFirstMipLevel = 0xFFFFFFFF}, // every subresource
                                        .Flags = discard ? D3D12_TEXTURE_BARRIER_FLAG_DISCARD
                                                         : D3D12_TEXTURE_BARRIER_FLAG_NONE};
    const D3D12_BARRIER_GROUP group{.Type = D3D12_BARRIER_TYPE_TEXTURE, .NumBarriers = 1, .pTextureBarriers = &barrier};
    m_list->Barrier(1, &group);
}

void RhiCommandListD3D12::barrier(IRhiSwapchain& swapchain)
{
    MEOW2_ASSERT(!m_rendering, "Barriers go between passes");
    auto& target = static_cast<RhiSwapchainD3D12&>(swapchain);
    MEOW2_ASSERT(target.isAcquired(), "acquire() must return true before drawing to a swapchain");
    const D3D12_TEXTURE_BARRIER barrier{.SyncBefore = D3D12_BARRIER_SYNC_RENDER_TARGET,
                                        .SyncAfter = D3D12_BARRIER_SYNC_RENDER_TARGET,
                                        .AccessBefore = D3D12_BARRIER_ACCESS_RENDER_TARGET,
                                        .AccessAfter = D3D12_BARRIER_ACCESS_RENDER_TARGET,
                                        .LayoutBefore = D3D12_BARRIER_LAYOUT_RENDER_TARGET,
                                        .LayoutAfter = D3D12_BARRIER_LAYOUT_RENDER_TARGET,
                                        .pResource = target.image(),
                                        .Subresources = {.IndexOrFirstMipLevel = 0xFFFFFFFF},
                                        .Flags = D3D12_TEXTURE_BARRIER_FLAG_NONE};
    const D3D12_BARRIER_GROUP group{.Type = D3D12_BARRIER_TYPE_TEXTURE, .NumBarriers = 1, .pTextureBarriers = &barrier};
    m_list->Barrier(1, &group);
}

void RhiCommandListD3D12::copyBuffer(BufferHandle source, BufferHandle target, std::uint64_t size,
                                     std::uint64_t sourceOffset, std::uint64_t targetOffset)
{
    m_list->CopyBufferRegion(m_device.buffer(target).resource, targetOffset, m_device.buffer(source).resource,
                             sourceOffset, size);
}

D3D12_TEXTURE_COPY_LOCATION RhiCommandListD3D12::bufferFootprint(const TextureD3D12& texture, ID3D12Resource* buffer,
                                                                 std::uint32_t mipLevel, std::uint64_t offset)
{
    // Rows tightly packed, as on Vulkan. Needs the unrestricted copy pitch, which the requirements check asks for;
    // otherwise rows would have to be 256-byte aligned (and level offsets 512-byte aligned).
    const std::uint32_t width = std::max(texture.width >> mipLevel, 1U);
    const std::uint32_t height = std::max(texture.height >> mipLevel, 1U);
    return {
        .pResource = buffer,
        .Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT,
        .PlacedFootprint = {.Offset = offset,
                            .Footprint = {.Format = texture.depth ? DXGI_FORMAT_R32_FLOAT : d3d12Format(texture.format),
                                          .Width = width,
                                          .Height = height,
                                          .Depth = 1,
                                          .RowPitch = width * pixelSize(texture.format)}}};
}

void RhiCommandListD3D12::copyBufferToTexture(BufferHandle source, TextureHandle target, std::uint32_t mipLevel,
                                              std::uint64_t sourceOffset)
{
    const TextureD3D12 texture = m_device.texture(target);
    const D3D12_TEXTURE_COPY_LOCATION from =
        bufferFootprint(texture, m_device.buffer(source).resource, mipLevel, sourceOffset);
    const D3D12_TEXTURE_COPY_LOCATION to{
        .pResource = texture.resource, .Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX, .SubresourceIndex = mipLevel};
    m_list->CopyTextureRegion(&to, 0, 0, 0, &from, nullptr);
}

void RhiCommandListD3D12::copyTextureToBuffer(TextureHandle source, BufferHandle target)
{
    const TextureD3D12 texture = m_device.texture(source);
    const D3D12_TEXTURE_COPY_LOCATION from{
        .pResource = texture.resource, .Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX, .SubresourceIndex = 0};
    const D3D12_TEXTURE_COPY_LOCATION to = bufferFootprint(texture, m_device.buffer(target).resource, 0, 0);
    m_list->CopyTextureRegion(&to, 0, 0, 0, &from, nullptr);
}
} // namespace Meow2::Rhi
