/// @file
/// @brief UploadRing, ReadbackQueue and OffscreenTarget.

#include "Meow2/Graphics/Renderer/FrameResources.h"

#include "Meow2/Core/Assert.h"

#include <cstring>
#include <format>
#include <utility>

namespace Meow2::Renderer
{
UploadRing::UploadRing(Rhi::IRhiDevice& device)
    : m_device(device)
{
}

Result<std::unique_ptr<UploadRing>> UploadRing::create(Rhi::IRhiDevice& device, std::uint64_t bytesPerFrame)
{
    auto ring = std::make_unique<UploadRing>(device);
    ring->m_capacity = bytesPerFrame;
    for (std::size_t i = 0; i < ring->m_buffers.size(); ++i)
    {
        Result<Rhi::BufferHandle> buffer = device.createBuffer({.size = bytesPerFrame,
                                                                .memory = Rhi::RhiMemory::Upload,
                                                                .tag = Memory::MemTag::Renderer,
                                                                .name = std::format("Upload ring {}", i)});
        if (!buffer)
        {
            return Unexpected<Error>{buffer.error()};
        }
        ring->m_buffers[i] = *buffer;
    }
    return ring;
}

UploadRing::~UploadRing()
{
    for (const Rhi::BufferHandle buffer : m_buffers)
    {
        m_device.destroy(buffer);
    }
}

void UploadRing::beginFrame(std::uint32_t frameSlot)
{
    MEOW2_ASSERT(frameSlot < m_buffers.size(), "Frame slot out of range");
    m_slot = frameSlot;
    m_used = 0;
}

Result<UploadAllocation> UploadRing::allocate(std::uint64_t size, std::uint64_t alignment)
{
    MEOW2_ASSERT(size > 0 && (alignment & (alignment - 1)) == 0, "A size, and a power-of-two alignment");
    std::uint64_t used = m_used.load();
    std::uint64_t offset = 0;
    do
    {
        offset = (used + alignment - 1) & ~(alignment - 1);
        if (offset + size > m_capacity)
        {
            return fail(ErrorCode::OutOfMemory, std::format("The upload ring is full: {} bytes per frame", m_capacity));
        }
    } while (!m_used.compare_exchange_weak(used, offset + size));

    const Rhi::BufferHandle buffer = m_buffers[m_slot];
    return UploadAllocation{.buffer = buffer,
                            .heapIndex = m_device.bindlessIndex(buffer),
                            .offset = offset,
                            .memory = m_device.mappedMemory(buffer).subspan(offset, size)};
}

ReadbackQueue::ReadbackQueue(Rhi::IRhiDevice& device)
    : m_device(device)
{
}

ReadbackQueue::~ReadbackQueue()
{
    for (const std::vector<Pending>& slot : m_pending)
    {
        for (const Pending& pending : slot)
        {
            m_device.destroy(pending.buffer);
        }
    }
}

void ReadbackQueue::beginFrame(std::uint32_t frameSlot)
{
    MEOW2_ASSERT(frameSlot < m_pending.size(), "Frame slot out of range");
    m_slot = frameSlot;
    for (const Pending& pending : m_pending[frameSlot])
    {
        pending.callback(m_device.mappedMemory(pending.buffer).first(pending.size));
        m_device.destroy(pending.buffer);
    }
    m_pending[frameSlot].clear();
}

Result<void> ReadbackQueue::request(Rhi::IRhiCommandList& list, Rhi::BufferHandle source, std::uint64_t size,
                                    Callback callback)
{
    Result<Rhi::BufferHandle> staging =
        m_device.createBuffer({.size = size, .memory = Rhi::RhiMemory::Readback, .name = "Readback"});
    if (!staging)
    {
        return Unexpected<Error>{staging.error()};
    }
    list.copyBuffer(source, *staging, size);
    list.barrier(*staging, Rhi::ResourceState::CopyTarget, Rhi::ResourceState::HostRead);
    m_pending[m_slot].push_back({.buffer = *staging, .size = size, .callback = std::move(callback)});
    return {};
}

Result<void> ReadbackQueue::request(Rhi::IRhiCommandList& list, Rhi::TextureHandle source, std::uint64_t size,
                                    Callback callback)
{
    Result<Rhi::BufferHandle> staging =
        m_device.createBuffer({.size = size, .memory = Rhi::RhiMemory::Readback, .name = "Readback"});
    if (!staging)
    {
        return Unexpected<Error>{staging.error()};
    }
    list.copyTextureToBuffer(source, *staging);
    list.barrier(*staging, Rhi::ResourceState::CopyTarget, Rhi::ResourceState::HostRead);
    m_pending[m_slot].push_back({.buffer = *staging, .size = size, .callback = std::move(callback)});
    return {};
}

OffscreenTarget::OffscreenTarget(Rhi::IRhiDevice& device, std::uint32_t width, std::uint32_t height)
    : m_device(device)
    , m_width(width)
    , m_height(height)
{
}

Result<std::unique_ptr<OffscreenTarget>> OffscreenTarget::create(Rhi::IRhiDevice& device, std::uint32_t width,
                                                                 std::uint32_t height)
{
    auto target = std::make_unique<OffscreenTarget>(device, width, height);
    Result<Rhi::TextureHandle> texture = device.createTexture({.width = width,
                                                               .height = height,
                                                               .format = format(),
                                                               .renderTarget = true,
                                                               .tag = Memory::MemTag::Renderer,
                                                               .name = "Offscreen target"});
    if (!texture)
    {
        return Unexpected<Error>{texture.error()};
    }
    target->m_texture = *texture;
    Result<Rhi::BufferHandle> readback = device.createBuffer({.size = std::uint64_t{width} * height * 4,
                                                              .memory = Rhi::RhiMemory::Readback,
                                                              .tag = Memory::MemTag::Renderer,
                                                              .name = "Offscreen readback"});
    if (!readback)
    {
        return Unexpected<Error>{readback.error()};
    }
    target->m_readback = *readback;
    return target;
}

OffscreenTarget::~OffscreenTarget()
{
    m_device.destroy(m_texture);
    m_device.destroy(m_readback);
}

void OffscreenTarget::begin(Rhi::IRhiCommandList& list, const Math::Color& clear)
{
    list.barrier(m_texture, m_used ? Rhi::ResourceState::CopySource : Rhi::ResourceState::Undefined,
                 Rhi::ResourceState::ColorTarget);
    m_used = true;
    list.beginRendering(Rhi::RenderTargets{.color = m_texture, .clearColor = clear});
}

void OffscreenTarget::end(Rhi::IRhiCommandList& list)
{
    list.endRendering();
    list.barrier(m_texture, Rhi::ResourceState::ColorTarget, Rhi::ResourceState::CopySource);
    list.copyTextureToBuffer(m_texture, m_readback);
    list.barrier(m_readback, Rhi::ResourceState::CopyTarget, Rhi::ResourceState::HostRead);
}

std::span<const std::byte> OffscreenTarget::pixels() const
{
    return m_device.mappedMemory(m_readback);
}
} // namespace Meow2::Renderer
