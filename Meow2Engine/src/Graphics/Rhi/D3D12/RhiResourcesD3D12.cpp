/// @file
/// @brief Resources on D3D12: buffers, textures, samplers, pipelines, the heaps, the signatures and deferred
///        destruction.

#include "Graphics/Rhi/D3D12/RhiDeviceD3D12.h"
#include "Meow2/Core/Assert.h"
#include "Meow2/Core/Memory/MemTag.h"
#include "Meow2/Graphics/Shader/BindlessLayout.h"

#include <d3dx12_pipeline_state_stream.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <format>
#include <iterator>
#include <mutex>
#include <string>
#include <utility>

namespace Meow2::Rhi
{
namespace
{
// The same sizes as the Vulkan heaps (see RhiResourcesVulkan.cpp). D3D12 allows at most 2048 samplers in a
// shader-visible heap.
constexpr std::uint32_t c_resourceHeapSize = 1U << 16U;
constexpr std::uint32_t c_samplerHeapSize = 256;
constexpr std::uint32_t c_rtvHeapSize = 1024;
constexpr std::uint32_t c_dsvHeapSize = 256;
constexpr std::uint32_t c_libraryMagic = 0x4C503252; // "R2PL": a Meow2 pipeline library

/// @brief The shader-readable format of a texture: depth is created typeless and read as one float.
/// @param format The format.
/// @return The view format.
DXGI_FORMAT viewFormat(RhiFormat format)
{
    return format == RhiFormat::D32Float ? DXGI_FORMAT_R32_FLOAT : d3d12Format(format);
}

/// @brief Rounds a buffer's size up to whole 32-bit words: raw views count in words.
/// @param size Bytes.
/// @return Bytes, a multiple of 4.
std::uint64_t wordAligned(std::uint64_t size)
{
    return (size + 3U) & ~std::uint64_t{3};
}

/// @brief A key for the pipeline library: FNV-1a over a pipeline's description and bytecode.
class PipelineKey
{
public:
    /// @brief Adds bytes.
    /// @param bytes The bytes.
    void add(std::span<const std::byte> bytes)
    {
        for (const std::byte value : bytes)
        {
            m_hash = (m_hash ^ static_cast<std::uint64_t>(value)) * 0x100000001B3ULL;
        }
    }

    /// @brief Adds a value's bytes.
    /// @param value The value; trivially copyable.
    template <class T>
    void addValue(const T& value)
    {
        add(std::as_bytes(std::span{&value, 1}));
    }

    /// @brief The key as text.
    /// @param kind "g" or "c", so graphics and compute pipelines never collide.
    /// @return The name for the library.
    [[nodiscard]] std::wstring name(const wchar_t* kind) const
    {
        return std::format(L"{}{:016X}", kind, m_hash);
    }

private:
    std::uint64_t m_hash = 0xCBF29CE484222325ULL;
};
} // namespace

DXGI_FORMAT d3d12Format(RhiFormat format)
{
    switch (format)
    {
        case RhiFormat::Unknown:
            return DXGI_FORMAT_UNKNOWN;
        case RhiFormat::Bgra8Srgb:
            return DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
        case RhiFormat::Rgba8Srgb:
            return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
        case RhiFormat::Rgba8Unorm:
            return DXGI_FORMAT_R8G8B8A8_UNORM;
        case RhiFormat::Rgba16Float:
            return DXGI_FORMAT_R16G16B16A16_FLOAT;
        case RhiFormat::R32Float:
            return DXGI_FORMAT_R32_FLOAT;
        case RhiFormat::D32Float:
            return DXGI_FORMAT_R32_TYPELESS;
        case RhiFormat::Bc7Unorm:
            return DXGI_FORMAT_BC7_UNORM;
        case RhiFormat::Bc7Srgb:
            return DXGI_FORMAT_BC7_UNORM_SRGB;
        case RhiFormat::Bc5Unorm:
            return DXGI_FORMAT_BC5_UNORM;
    }
    return DXGI_FORMAT_UNKNOWN;
}

Result<void> RhiDeviceD3D12::createBindless(const RhiDeviceDesc& desc)
{
    const auto makeHeap = [this](DescriptorHeapD3D12& out, D3D12_DESCRIPTOR_HEAP_TYPE type, std::uint32_t size,
                                 bool shaderVisible, const wchar_t* name) -> Result<void>
    {
        const D3D12_DESCRIPTOR_HEAP_DESC heapDesc{.Type = type,
                                                  .NumDescriptors = size,
                                                  .Flags = shaderVisible ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE
                                                                         : D3D12_DESCRIPTOR_HEAP_FLAG_NONE};
        if (const HRESULT result = m_device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&out.heap)); FAILED(result))
        {
            return fail(ErrorCode::OutOfMemory, std::format("Descriptor heap: {}", hresultName(result)));
        }
        out.heap->SetName(name);
        out.increment = m_device->GetDescriptorHandleIncrementSize(type);
        return {};
    };
    for (Result<void> made :
         {makeHeap(m_resourceHeap, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, c_resourceHeapSize, true, L"Bindless heap"),
          makeHeap(m_samplerHeap, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, c_samplerHeapSize, true, L"Sampler heap"),
          makeHeap(m_rtvHeap, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, c_rtvHeapSize, false, L"RTV heap"),
          makeHeap(m_dsvHeap, D3D12_DESCRIPTOR_HEAP_TYPE_DSV, c_dsvHeapSize, false, L"DSV heap")})
    {
        if (!made)
        {
            return made;
        }
    }

    // One root signature: Shader::PushConstants as root constants at b0, and the heaps indexed directly by shaders.
    const D3D12_ROOT_PARAMETER1 constants{
        .ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS,
        .Constants = {.ShaderRegister = 0,
                      .RegisterSpace = 0,
                      .Num32BitValues = sizeof(Shader::PushConstants) / sizeof(std::uint32_t)},
        .ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL};
    const D3D12_VERSIONED_ROOT_SIGNATURE_DESC rootDesc{
        .Version = D3D_ROOT_SIGNATURE_VERSION_1_1,
        .Desc_1_1 = {.NumParameters = 1,
                     .pParameters = &constants,
                     .NumStaticSamplers = 0,
                     .pStaticSamplers = nullptr,
                     .Flags = D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED |
                              D3D12_ROOT_SIGNATURE_FLAG_SAMPLER_HEAP_DIRECTLY_INDEXED}};
    ComPtr<ID3DBlob> blob;
    ComPtr<ID3DBlob> errors;
    if (const HRESULT result = D3D12SerializeVersionedRootSignature(&rootDesc, &blob, &errors); FAILED(result))
    {
        return fail(ErrorCode::Unknown,
                    std::format("Root signature: {} {}", hresultName(result),
                                errors != nullptr ? static_cast<const char*>(errors->GetBufferPointer()) : ""));
    }
    if (const HRESULT result = m_device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(),
                                                             IID_PPV_ARGS(&m_rootSignature));
        FAILED(result))
    {
        return fail(ErrorCode::Unknown, std::format("CreateRootSignature: {}", hresultName(result)));
    }
    m_rootSignature->SetName(L"Bindless root signature");

    // drawIndirectCount(): each Shader::IndirectDraw sets PushConstants::instance, then draws. D3D12's SV_InstanceID
    // starts at 0 for every draw, so this is how the vertex shader learns which instance it's drawing.
    const std::array<D3D12_INDIRECT_ARGUMENT_DESC, 2> arguments{
        D3D12_INDIRECT_ARGUMENT_DESC{
            .Type = D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT,
            .Constant = {.RootParameterIndex = 0,
                         .DestOffsetIn32BitValues = offsetof(Shader::PushConstants, instance) / sizeof(std::uint32_t),
                         .Num32BitValuesToSet = 1}},
        D3D12_INDIRECT_ARGUMENT_DESC{.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW}};
    const D3D12_COMMAND_SIGNATURE_DESC signatureDesc{.ByteStride = sizeof(Shader::IndirectDraw),
                                                     .NumArgumentDescs = static_cast<UINT>(arguments.size()),
                                                     .pArgumentDescs = arguments.data(),
                                                     .NodeMask = 0};
    if (const HRESULT result =
            m_device->CreateCommandSignature(&signatureDesc, m_rootSignature.Get(), IID_PPV_ARGS(&m_drawSignature));
        FAILED(result))
    {
        return fail(ErrorCode::Unknown, std::format("CreateCommandSignature: {}", hresultName(result)));
    }
    m_drawSignature->SetName(L"Indirect draw signature");

    // dispatchMeshIndirect(): x, y and z of one DispatchMesh. No root constant: the meshlet path's shaders find their
    // work in buffers.
    if (m_adapter.meshShaders)
    {
        const D3D12_INDIRECT_ARGUMENT_DESC meshArgument{.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH_MESH};
        const D3D12_COMMAND_SIGNATURE_DESC meshDesc{.ByteStride = sizeof(D3D12_DISPATCH_MESH_ARGUMENTS),
                                                    .NumArgumentDescs = 1,
                                                    .pArgumentDescs = &meshArgument,
                                                    .NodeMask = 0};
        // No root signature: the signature changes no root arguments.
        if (const HRESULT result = m_device->CreateCommandSignature(&meshDesc, nullptr, IID_PPV_ARGS(&m_meshSignature));
            FAILED(result))
        {
            return fail(ErrorCode::Unknown, std::format("CreateCommandSignature (mesh): {}", hresultName(result)));
        }
        m_meshSignature->SetName(L"Indirect mesh dispatch signature");
    }

    // RenderDoc doesn't support pipeline libraries: it hands back a stand-in whose saved data the next normal run
    // can't read. So there's no pipeline cache while it's in the process, and the one on disk is left alone.
    if (GetModuleHandleW(L"renderdoc.dll") != nullptr)
    {
        MEOW2_LOG_INFO(m_log, "No pipeline cache while RenderDoc is attached");
    }
    else
    {
        // Saved data starts with LibraryHeader. Data from another GPU or driver is dropped before D3D12 sees it: the
        // debug layer reports a foreign blob as an error, and some drivers have crashed on one. The library reads the
        // bytes in place, so they're copied into memory that lives as long as it.
        const LibraryHeader expected = libraryHeader();
        LibraryHeader saved{};
        if (desc.pipelineCache.size() > sizeof(saved))
        {
            std::memcpy(&saved, desc.pipelineCache.data(), sizeof(saved));
        }
        if (std::memcmp(&saved, &expected, sizeof(saved)) == 0)
        {
            m_libraryData.assign(desc.pipelineCache.begin() + sizeof(saved), desc.pipelineCache.end());
        }
        else if (!desc.pipelineCache.empty())
        {
            MEOW2_LOG_INFO(m_log, "Ignoring the saved pipeline cache: it's from another GPU or driver");
        }
        HRESULT library = E_FAIL;
        if (!m_libraryData.empty())
        {
            library = m_device->CreatePipelineLibrary(m_libraryData.data(), m_libraryData.size(),
                                                      IID_PPV_ARGS(&m_pipelineLibrary));
            if (FAILED(library))
            {
                MEOW2_LOG_INFO(m_log, "Ignoring the saved pipeline cache: it's from another GPU or driver ({})",
                               hresultName(library));
            }
        }
        if (FAILED(library))
        {
            m_libraryData.clear();
            if (const HRESULT result = m_device->CreatePipelineLibrary(nullptr, 0, IID_PPV_ARGS(&m_pipelineLibrary));
                FAILED(result))
            {
                MEOW2_LOG_WARN(m_log, "No pipeline cache: {}", hresultName(result));
            }
        }
    }

    // Slot 0 of each heap is never handed out, so a zeroed index or view is caught (c_invalidBindlessIndex).
    m_resourceSlots.reset(c_invalidBindlessIndex + 1, c_resourceHeapSize);
    m_samplerSlots.reset(c_defaultSamplerIndex, c_samplerHeapSize);
    m_rtvSlots.reset(1, c_rtvHeapSize);
    m_dsvSlots.reset(1, c_dsvHeapSize);
    std::uint32_t defaultSampler = 0;
    if (!m_samplerSlots.allocate(defaultSampler))
    {
        return fail(ErrorCode::OutOfMemory, "The sampler heap is full");
    }
    MEOW2_ASSERT(defaultSampler == c_defaultSamplerIndex, "The first sampler takes the default slot");
    writeSampler({.name = "Default sampler"}, defaultSampler);
    return {};
}

void RhiDeviceD3D12::deferDestroy(std::function<void()> destroyLater)
{
    // Work recorded so far is covered by the current value; a frame still recording ends with the next one.
    const std::uint64_t value = m_fenceValue.load() + (m_inFrame.load() ? 1 : 0);
    const std::scoped_lock lock{m_resourceMutex};
    m_pendingDestroys.push_back({.value = value, .destroy = std::move(destroyLater)});
}

void RhiDeviceD3D12::collectGarbage()
{
    // A lost device never reaches the values, but its objects can still be released.
    const std::uint64_t reached = m_fence != nullptr ? m_fence->GetCompletedValue() : UINT64_MAX;
    std::vector<PendingDestroy> ready;
    {
        const std::scoped_lock lock{m_resourceMutex};
        const auto firstLater = std::ranges::stable_partition(m_pendingDestroys, [reached](const PendingDestroy& item)
                                                              { return item.value <= reached; });
        ready.assign(std::make_move_iterator(m_pendingDestroys.begin()), std::make_move_iterator(firstLater.begin()));
        m_pendingDestroys.erase(m_pendingDestroys.begin(), firstLater.begin());
    }
    // Without the lock: the callbacks take it to give back heap slots.
    for (const PendingDestroy& item : ready)
    {
        item.destroy();
    }
}

Result<BufferHandle> RhiDeviceD3D12::createBuffer(const BufferDesc& desc)
{
    MEOW2_ASSERT(desc.size > 0, "A buffer needs a size");
    const Memory::MemTagScope tag{Memory::MemTag::Rhi};
    const bool writable = desc.memory == RhiMemory::GpuOnly; // upload and readback heaps can't have UAVs
    const D3D12_RESOURCE_DESC1 resourceDesc{.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER,
                                            .Alignment = 0,
                                            .Width = wordAligned(desc.size),
                                            .Height = 1,
                                            .DepthOrArraySize = 1,
                                            .MipLevels = 1,
                                            .Format = DXGI_FORMAT_UNKNOWN,
                                            .SampleDesc = {.Count = 1, .Quality = 0},
                                            .Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR,
                                            .Flags = writable ? D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS
                                                              : D3D12_RESOURCE_FLAG_NONE};
    D3D12MA::ALLOCATION_DESC allocation{};
    allocation.HeapType = desc.memory == RhiMemory::Upload     ? D3D12_HEAP_TYPE_UPLOAD
                          : desc.memory == RhiMemory::Readback ? D3D12_HEAP_TYPE_READBACK
                                                               : D3D12_HEAP_TYPE_DEFAULT;

    BufferD3D12 record;
    if (const HRESULT result = m_allocator->CreateResource3(&allocation, &resourceDesc, D3D12_BARRIER_LAYOUT_UNDEFINED,
                                                            nullptr, 0, nullptr, &record.allocation, IID_NULL, nullptr);
        FAILED(result))
    {
        return fail(ErrorCode::OutOfMemory, std::format("Buffer '{}': {}", desc.name, hresultName(result)));
    }
    record.resource = record.allocation->GetResource();
    record.resource->SetName(wideName(desc.name).c_str());
    if (desc.memory != RhiMemory::GpuOnly)
    {
        // Mapped once for the buffer's whole life; D3D12 allows that. An empty read range: nothing is read back here.
        const D3D12_RANGE noRead{.Begin = 0, .End = 0};
        void* mapped = nullptr;
        record.resource->Map(0, desc.memory == RhiMemory::Readback ? nullptr : &noRead, &mapped);
        record.mapped = static_cast<std::byte*>(mapped);
    }
    record.size = desc.size;
    record.tag = desc.tag;
    record.bytes = record.allocation->GetSize();
    m_bytesByTag[static_cast<std::size_t>(record.tag)] += record.bytes;

    bool slots = false;
    {
        const std::scoped_lock lock{m_resourceMutex};
        slots =
            m_resourceSlots.allocate(record.heapIndex) && (!writable || m_resourceSlots.allocate(record.storageIndex));
    }
    if (!slots)
    {
        record.allocation->Release();
        return fail(ErrorCode::OutOfMemory, "The bindless heap is full");
    }
    const auto words = static_cast<UINT>(resourceDesc.Width / sizeof(std::uint32_t));
    const D3D12_SHADER_RESOURCE_VIEW_DESC srv{
        .Format = DXGI_FORMAT_R32_TYPELESS,
        .ViewDimension = D3D12_SRV_DIMENSION_BUFFER,
        .Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,
        .Buffer = {.FirstElement = 0, .NumElements = words, .Flags = D3D12_BUFFER_SRV_FLAG_RAW}};
    m_device->CreateShaderResourceView(record.resource, &srv, m_resourceHeap.cpu(record.heapIndex));
    if (writable)
    {
        const D3D12_UNORDERED_ACCESS_VIEW_DESC uav{
            .Format = DXGI_FORMAT_R32_TYPELESS,
            .ViewDimension = D3D12_UAV_DIMENSION_BUFFER,
            .Buffer = {.FirstElement = 0, .NumElements = words, .Flags = D3D12_BUFFER_UAV_FLAG_RAW}};
        m_device->CreateUnorderedAccessView(record.resource, nullptr, &uav, m_resourceHeap.cpu(record.storageIndex));
    }

    const std::scoped_lock lock{m_resourceMutex};
    return m_buffers.create(record);
}

void RhiDeviceD3D12::destroy(BufferHandle buffer)
{
    BufferD3D12 record;
    {
        const std::scoped_lock lock{m_resourceMutex};
        const BufferD3D12* found = m_buffers.get(buffer);
        if (found == nullptr)
        {
            return;
        }
        record = *found;
        m_buffers.destroy(buffer);
    }
    deferDestroy(
        [this, record]
        {
            record.allocation->Release(); // releases the resource too
            m_bytesByTag[static_cast<std::size_t>(record.tag)] -= record.bytes;
            const std::scoped_lock lock{m_resourceMutex};
            m_resourceSlots.release(record.heapIndex);
            if (record.storageIndex != c_invalidBindlessIndex)
            {
                m_resourceSlots.release(record.storageIndex);
            }
        });
}

std::span<std::byte> RhiDeviceD3D12::mappedMemory(BufferHandle buffer)
{
    const std::shared_lock lock{m_resourceMutex};
    const BufferD3D12* found = m_buffers.get(buffer);
    if (found == nullptr || found->mapped == nullptr)
    {
        return {};
    }
    return {found->mapped, static_cast<std::size_t>(found->size)};
}

std::uint32_t RhiDeviceD3D12::bindlessIndex(BufferHandle buffer)
{
    const std::shared_lock lock{m_resourceMutex};
    const BufferD3D12* found = m_buffers.get(buffer);
    return found != nullptr ? found->heapIndex : c_invalidBindlessIndex;
}

std::uint32_t RhiDeviceD3D12::bindlessStorageIndex(BufferHandle buffer)
{
    const std::shared_lock lock{m_resourceMutex};
    const BufferD3D12* found = m_buffers.get(buffer);
    return found != nullptr ? found->storageIndex : c_invalidBindlessIndex;
}

BufferD3D12 RhiDeviceD3D12::buffer(BufferHandle handle)
{
    const std::shared_lock lock{m_resourceMutex};
    const BufferD3D12* found = m_buffers.get(handle);
    MEOW2_ASSERT(found != nullptr, "Stale buffer handle");
    return found != nullptr ? *found : BufferD3D12{};
}

Result<TextureHandle> RhiDeviceD3D12::createTexture(const TextureDesc& desc)
{
    MEOW2_ASSERT(desc.width > 0 && desc.height > 0 && desc.mipLevels > 0, "A texture needs a size");
    MEOW2_ASSERT(desc.mipLevels <= c_maxMipLevels, "More mip levels than c_maxMipLevels");
    const Memory::MemTagScope tag{Memory::MemTag::Rhi};
    const bool depth = desc.format == RhiFormat::D32Float;
    D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE;
    if (desc.renderTarget)
    {
        flags |= depth ? D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL : D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    }
    if (desc.storage)
    {
        flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    }
    const D3D12_RESOURCE_DESC1 resourceDesc{.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D,
                                            .Alignment = 0,
                                            .Width = desc.width,
                                            .Height = desc.height,
                                            .DepthOrArraySize = 1,
                                            .MipLevels = static_cast<UINT16>(desc.mipLevels),
                                            .Format = d3d12Format(desc.format),
                                            .SampleDesc = {.Count = 1, .Quality = 0},
                                            .Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN,
                                            .Flags = flags};
    // Depth is always cleared to 0 (reversed Z), so the driver can make that clear free.
    const D3D12_CLEAR_VALUE depthClear{.Format = DXGI_FORMAT_D32_FLOAT, .DepthStencil = {.Depth = 0.0F, .Stencil = 0}};
    D3D12MA::ALLOCATION_DESC allocation{};
    allocation.HeapType = D3D12_HEAP_TYPE_DEFAULT;

    TextureD3D12 record;
    record.format = desc.format;
    record.width = desc.width;
    record.height = desc.height;
    record.mipLevels = desc.mipLevels;
    record.depth = depth;
    record.renderTarget = desc.renderTarget;
    if (const HRESULT result = m_allocator->CreateResource3(&allocation, &resourceDesc, D3D12_BARRIER_LAYOUT_UNDEFINED,
                                                            depth && desc.renderTarget ? &depthClear : nullptr, 0,
                                                            nullptr, &record.allocation, IID_NULL, nullptr);
        FAILED(result))
    {
        return fail(ErrorCode::OutOfMemory, std::format("Texture '{}': {}", desc.name, hresultName(result)));
    }
    record.resource = record.allocation->GetResource();
    record.resource->SetName(wideName(desc.name).c_str());
    record.tag = desc.tag;
    record.bytes = record.allocation->GetSize();
    m_bytesByTag[static_cast<std::size_t>(record.tag)] += record.bytes;

    bool slots = false;
    const std::uint32_t storageLevels = desc.storage ? desc.mipLevels : 0;
    {
        const std::scoped_lock lock{m_resourceMutex};
        slots = m_resourceSlots.allocate(record.heapIndex) &&
                (!desc.renderTarget || (depth ? m_dsvSlots : m_rtvSlots).allocate(record.targetIndex));
        for (std::uint32_t level = 0; slots && level < storageLevels; ++level)
        {
            slots = m_resourceSlots.allocate(record.storageIndices[level]);
        }
        if (!slots)
        {
            // Give back what was taken before the heap ran out.
            for (const std::uint32_t index : record.storageIndices)
            {
                if (index != c_invalidBindlessIndex)
                {
                    m_resourceSlots.release(index);
                }
            }
            if (record.heapIndex != c_invalidBindlessIndex)
            {
                m_resourceSlots.release(record.heapIndex);
            }
            if (record.targetIndex != 0)
            {
                (depth ? m_dsvSlots : m_rtvSlots).release(record.targetIndex);
            }
        }
    }
    if (!slots)
    {
        record.allocation->Release();
        return fail(ErrorCode::OutOfMemory, "A descriptor heap is full");
    }
    const D3D12_SHADER_RESOURCE_VIEW_DESC srv{
        .Format = viewFormat(desc.format),
        .ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D,
        .Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,
        .Texture2D = {.MostDetailedMip = 0, .MipLevels = desc.mipLevels, .PlaneSlice = 0, .ResourceMinLODClamp = 0.0F}};
    m_device->CreateShaderResourceView(record.resource, &srv, m_resourceHeap.cpu(record.heapIndex));
    for (std::uint32_t level = 0; level < storageLevels; ++level)
    {
        const D3D12_UNORDERED_ACCESS_VIEW_DESC uav{.Format = viewFormat(desc.format),
                                                   .ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D,
                                                   .Texture2D = {.MipSlice = level, .PlaneSlice = 0}};
        m_device->CreateUnorderedAccessView(record.resource, nullptr, &uav,
                                            m_resourceHeap.cpu(record.storageIndices[level]));
    }
    if (desc.renderTarget && depth)
    {
        const D3D12_DEPTH_STENCIL_VIEW_DESC dsv{.Format = DXGI_FORMAT_D32_FLOAT,
                                                .ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D,
                                                .Flags = D3D12_DSV_FLAG_NONE,
                                                .Texture2D = {.MipSlice = 0}};
        m_device->CreateDepthStencilView(record.resource, &dsv, m_dsvHeap.cpu(record.targetIndex));
    }
    else if (desc.renderTarget)
    {
        const D3D12_RENDER_TARGET_VIEW_DESC rtv{.Format = d3d12Format(desc.format),
                                                .ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D,
                                                .Texture2D = {.MipSlice = 0, .PlaneSlice = 0}};
        m_device->CreateRenderTargetView(record.resource, &rtv, m_rtvHeap.cpu(record.targetIndex));
    }

    const std::scoped_lock lock{m_resourceMutex};
    return m_textures.create(record);
}

void RhiDeviceD3D12::destroy(TextureHandle texture)
{
    TextureD3D12 record;
    {
        const std::scoped_lock lock{m_resourceMutex};
        const TextureD3D12* found = m_textures.get(texture);
        if (found == nullptr)
        {
            return;
        }
        record = *found;
        m_textures.destroy(texture);
    }
    deferDestroy(
        [this, record]
        {
            record.allocation->Release();
            m_bytesByTag[static_cast<std::size_t>(record.tag)] -= record.bytes;
            const std::scoped_lock lock{m_resourceMutex};
            m_resourceSlots.release(record.heapIndex);
            for (const std::uint32_t index : record.storageIndices)
            {
                if (index != c_invalidBindlessIndex)
                {
                    m_resourceSlots.release(index);
                }
            }
            if (record.targetIndex != 0)
            {
                (record.depth ? m_dsvSlots : m_rtvSlots).release(record.targetIndex);
            }
        });
}

std::uint32_t RhiDeviceD3D12::bindlessIndex(TextureHandle texture)
{
    const std::shared_lock lock{m_resourceMutex};
    const TextureD3D12* found = m_textures.get(texture);
    return found != nullptr ? found->heapIndex : c_invalidBindlessIndex;
}

std::uint32_t RhiDeviceD3D12::bindlessStorageIndex(TextureHandle texture, std::uint32_t mipLevel)
{
    const std::shared_lock lock{m_resourceMutex};
    const TextureD3D12* found = m_textures.get(texture);
    return found != nullptr && mipLevel < c_maxMipLevels ? found->storageIndices[mipLevel] : c_invalidBindlessIndex;
}

TextureInfo RhiDeviceD3D12::textureInfo(TextureHandle texture)
{
    const TextureD3D12 record = this->texture(texture);
    return {.width = record.width, .height = record.height, .mipLevels = record.mipLevels, .format = record.format};
}

TextureD3D12 RhiDeviceD3D12::texture(TextureHandle handle)
{
    const std::shared_lock lock{m_resourceMutex};
    const TextureD3D12* found = m_textures.get(handle);
    MEOW2_ASSERT(found != nullptr, "Stale texture handle");
    return found != nullptr ? *found : TextureD3D12{};
}

void RhiDeviceD3D12::writeSampler(const SamplerDesc& desc, std::uint32_t index)
{
    const bool linear = desc.filter == SamplerFilter::Linear;
    const auto addressMode = [](SamplerAddress address)
    {
        return address == SamplerAddress::Repeat   ? D3D12_TEXTURE_ADDRESS_MODE_WRAP
               : address == SamplerAddress::Mirror ? D3D12_TEXTURE_ADDRESS_MODE_MIRROR
                                                   : D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    };
    const float anisotropy = std::clamp(desc.maxAnisotropy, 1.0F, static_cast<float>(D3D12_MAX_MAXANISOTROPY));
    const D3D12_FILTER filter = anisotropy > 1.0F ? D3D12_FILTER_ANISOTROPIC
                                : linear          ? D3D12_FILTER_MIN_MAG_MIP_LINEAR
                                                  : D3D12_FILTER_MIN_MAG_MIP_POINT;
    const D3D12_SAMPLER_DESC samplerDesc{.Filter = filter,
                                         .AddressU = addressMode(desc.addressU),
                                         .AddressV = addressMode(desc.addressV),
                                         .AddressW = addressMode(desc.addressU),
                                         .MipLODBias = 0.0F,
                                         .MaxAnisotropy = static_cast<UINT>(anisotropy),
                                         .ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER,
                                         .BorderColor = {0.0F, 0.0F, 0.0F, 0.0F},
                                         .MinLOD = 0.0F,
                                         .MaxLOD = D3D12_FLOAT32_MAX};
    m_device->CreateSampler(&samplerDesc, m_samplerHeap.cpu(index));
}

Result<SamplerHandle> RhiDeviceD3D12::createSampler(const SamplerDesc& desc)
{
    SamplerD3D12 record;
    {
        const std::scoped_lock lock{m_resourceMutex};
        if (!m_samplerSlots.allocate(record.heapIndex))
        {
            return fail(ErrorCode::OutOfMemory, "The sampler heap is full");
        }
    }
    writeSampler(desc, record.heapIndex);
    const std::scoped_lock lock{m_resourceMutex};
    return m_samplers.create(record);
}

void RhiDeviceD3D12::destroy(SamplerHandle sampler)
{
    SamplerD3D12 record;
    {
        const std::scoped_lock lock{m_resourceMutex};
        const SamplerD3D12* found = m_samplers.get(sampler);
        if (found == nullptr)
        {
            return;
        }
        record = *found;
        m_samplers.destroy(sampler);
    }
    deferDestroy(
        [this, record]
        {
            const std::scoped_lock lock{m_resourceMutex};
            m_samplerSlots.release(record.heapIndex);
        });
}

std::uint32_t RhiDeviceD3D12::bindlessIndex(SamplerHandle sampler)
{
    const std::shared_lock lock{m_resourceMutex};
    const SamplerD3D12* found = m_samplers.get(sampler);
    return found != nullptr ? found->heapIndex : c_defaultSamplerIndex;
}

Result<ID3D12PipelineState*>
RhiDeviceD3D12::libraryPipeline(const std::wstring& name, const std::function<HRESULT(ID3D12PipelineState**)>& create,
                                const std::function<HRESULT(ID3D12PipelineState**)>& load)
{
    ID3D12PipelineState* state = nullptr;
    if (m_pipelineLibrary != nullptr)
    {
        const std::scoped_lock lock{m_libraryMutex};
        if (SUCCEEDED(load(&state)))
        {
            return state;
        }
    }
    // Not in the library: the driver compiles it now, without the lock, so other threads keep going.
    if (const HRESULT result = create(&state); FAILED(result))
    {
        return fail(ErrorCode::Unknown, hresultName(result));
    }
    if (m_pipelineLibrary != nullptr)
    {
        const std::scoped_lock lock{m_libraryMutex};
        m_pipelineLibrary->StorePipeline(name.c_str(), state); // E_INVALIDARG if another thread stored it first
    }
    return state;
}

Result<PipelineHandle> RhiDeviceD3D12::createGraphicsPipeline(const GraphicsPipelineDesc& desc)
{
    const Memory::MemTagScope tag{Memory::MemTag::Rhi};
    const bool hasDepth = desc.depthFormat != RhiFormat::Unknown;
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pipelineDesc{};
    pipelineDesc.pRootSignature = m_rootSignature.Get();
    pipelineDesc.VS = {.pShaderBytecode = desc.vertexCode.data(), .BytecodeLength = desc.vertexCode.size()};
    pipelineDesc.PS = {.pShaderBytecode = desc.pixelCode.data(), .BytecodeLength = desc.pixelCode.size()};
    const bool additive = desc.blend == BlendMode::Additive;
    pipelineDesc.BlendState.RenderTarget[0] = {.BlendEnable = desc.blend != BlendMode::None ? TRUE : FALSE,
                                               .LogicOpEnable = FALSE,
                                               .SrcBlend = additive ? D3D12_BLEND_ONE : D3D12_BLEND_SRC_ALPHA,
                                               .DestBlend = additive ? D3D12_BLEND_ONE : D3D12_BLEND_INV_SRC_ALPHA,
                                               .BlendOp = D3D12_BLEND_OP_ADD,
                                               .SrcBlendAlpha = D3D12_BLEND_ONE,
                                               .DestBlendAlpha = additive ? D3D12_BLEND_ONE : D3D12_BLEND_INV_SRC_ALPHA,
                                               .BlendOpAlpha = D3D12_BLEND_OP_ADD,
                                               .LogicOp = D3D12_LOGIC_OP_NOOP,
                                               .RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL};
    pipelineDesc.SampleMask = UINT_MAX;
    // Clockwise front faces, as on Vulkan (CullMode).
    pipelineDesc.RasterizerState = {.FillMode = desc.fill == FillMode::Wireframe ? D3D12_FILL_MODE_WIREFRAME
                                                                                 : D3D12_FILL_MODE_SOLID,
                                    .CullMode = desc.cull == CullMode::Back    ? D3D12_CULL_MODE_BACK
                                                : desc.cull == CullMode::Front ? D3D12_CULL_MODE_FRONT
                                                                               : D3D12_CULL_MODE_NONE,
                                    .FrontCounterClockwise = FALSE,
                                    .DepthClipEnable = TRUE};
    pipelineDesc.DepthStencilState = {.DepthEnable = hasDepth ? TRUE : FALSE,
                                      .DepthWriteMask = hasDepth && desc.depthWrite ? D3D12_DEPTH_WRITE_MASK_ALL
                                                                                    : D3D12_DEPTH_WRITE_MASK_ZERO,
                                      .DepthFunc = desc.depthTest ? D3D12_COMPARISON_FUNC_GREATER_EQUAL // reversed Z
                                                                  : D3D12_COMPARISON_FUNC_ALWAYS};
    const bool lines = desc.topology == PrimitiveTopology::Lines && desc.meshCode.empty();
    pipelineDesc.PrimitiveTopologyType =
        lines ? D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE : D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    // The first colour target, then the extra ones up to the first Unknown (a G-buffer); each blends the same way.
    pipelineDesc.NumRenderTargets = 1;
    pipelineDesc.RTVFormats[0] = d3d12Format(desc.colorFormat);
    for (const RhiFormat extra : desc.extraColorFormats)
    {
        if (extra == RhiFormat::Unknown)
        {
            break;
        }
        pipelineDesc.BlendState.RenderTarget[pipelineDesc.NumRenderTargets] = pipelineDesc.BlendState.RenderTarget[0];
        pipelineDesc.RTVFormats[pipelineDesc.NumRenderTargets++] = d3d12Format(extra);
    }
    pipelineDesc.DSVFormat = hasDepth ? DXGI_FORMAT_D32_FLOAT : DXGI_FORMAT_UNKNOWN;
    pipelineDesc.SampleDesc = {.Count = 1, .Quality = 0};

    const bool meshPipeline = !desc.meshCode.empty();
    if (meshPipeline && !m_adapter.meshShaders)
    {
        return fail(ErrorCode::Unsupported, std::format("Pipeline '{}': this GPU has no mesh shaders", desc.name));
    }

    PipelineKey key;
    key.add(desc.vertexCode);
    key.add(desc.taskCode);
    key.add(desc.meshCode);
    key.add(desc.pixelCode);
    key.addValue(pipelineDesc.NumRenderTargets);
    for (UINT i = 0; i < pipelineDesc.NumRenderTargets; ++i)
    {
        key.addValue(pipelineDesc.RTVFormats[i]);
    }
    key.addValue(pipelineDesc.DSVFormat);
    key.addValue(pipelineDesc.RasterizerState.CullMode);
    key.addValue(pipelineDesc.DepthStencilState.DepthWriteMask);
    key.addValue(pipelineDesc.BlendState.RenderTarget[0].BlendEnable);
    // Every state that differs between our pipelines must be in the key, or the library hands back another pipeline
    // with the same shaders (a wireframe view would load the solid one).
    key.addValue(pipelineDesc.RasterizerState.FillMode);
    key.addValue(pipelineDesc.PrimitiveTopologyType);
    key.addValue(pipelineDesc.DepthStencilState.DepthEnable);
    key.addValue(pipelineDesc.DepthStencilState.DepthFunc);
    key.addValue(pipelineDesc.BlendState.RenderTarget[0].SrcBlend);
    key.addValue(pipelineDesc.BlendState.RenderTarget[0].DestBlend);
    key.addValue(pipelineDesc.BlendState.RenderTarget[0].DestBlendAlpha);
    const std::wstring name = key.name(meshPipeline ? L"m" : L"g");
    Result<ID3D12PipelineState*> state = Unexpected<Error>{Error{}};
    if (meshPipeline)
    {
        // Mesh pipelines have no D3D12_GRAPHICS_PIPELINE_STATE_DESC: they're described as a stream of subobjects, the
        // same states as above with task and mesh shaders in place of the vertex shader.
        D3DX12_MESH_SHADER_PIPELINE_STATE_DESC meshDesc{};
        meshDesc.pRootSignature = m_rootSignature.Get();
        meshDesc.AS = {.pShaderBytecode = desc.taskCode.data(), .BytecodeLength = desc.taskCode.size()};
        meshDesc.MS = {.pShaderBytecode = desc.meshCode.data(), .BytecodeLength = desc.meshCode.size()};
        meshDesc.PS = pipelineDesc.PS;
        meshDesc.BlendState = pipelineDesc.BlendState;
        meshDesc.SampleMask = pipelineDesc.SampleMask;
        meshDesc.RasterizerState = pipelineDesc.RasterizerState;
        meshDesc.DepthStencilState = pipelineDesc.DepthStencilState;
        meshDesc.PrimitiveTopologyType = pipelineDesc.PrimitiveTopologyType;
        meshDesc.NumRenderTargets = pipelineDesc.NumRenderTargets;
        std::ranges::copy(pipelineDesc.RTVFormats, std::begin(meshDesc.RTVFormats));
        meshDesc.DSVFormat = pipelineDesc.DSVFormat;
        meshDesc.SampleDesc = pipelineDesc.SampleDesc;
        CD3DX12_PIPELINE_MESH_STATE_STREAM stream{meshDesc};
        const D3D12_PIPELINE_STATE_STREAM_DESC streamDesc{.SizeInBytes = sizeof(stream),
                                                          .pPipelineStateSubobjectStream = &stream};
        state = libraryPipeline(
            name, [&](ID3D12PipelineState** out)
            { return m_device->CreatePipelineState(&streamDesc, IID_PPV_ARGS(out)); }, [&](ID3D12PipelineState** out)
            { return m_pipelineLibrary->LoadPipeline(name.c_str(), &streamDesc, IID_PPV_ARGS(out)); });
    }
    else
    {
        state = libraryPipeline(
            name, [&](ID3D12PipelineState** out)
            { return m_device->CreateGraphicsPipelineState(&pipelineDesc, IID_PPV_ARGS(out)); },
            [&](ID3D12PipelineState** out)
            { return m_pipelineLibrary->LoadGraphicsPipeline(name.c_str(), &pipelineDesc, IID_PPV_ARGS(out)); });
    }
    if (!state)
    {
        return fail(ErrorCode::InvalidArgument, std::format("Pipeline '{}': {}", desc.name, state.error().message));
    }
    (*state)->SetName(wideName(desc.name).c_str());
    const std::scoped_lock lock{m_resourceMutex};
    return m_pipelines.create(
        {.state = *state,
         .compute = false,
         .topology = lines ? D3D_PRIMITIVE_TOPOLOGY_LINELIST : D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST});
}

Result<PipelineHandle> RhiDeviceD3D12::createComputePipeline(const ComputePipelineDesc& desc)
{
    const Memory::MemTagScope tag{Memory::MemTag::Rhi};
    const D3D12_COMPUTE_PIPELINE_STATE_DESC pipelineDesc{
        .pRootSignature = m_rootSignature.Get(),
        .CS = {.pShaderBytecode = desc.code.data(), .BytecodeLength = desc.code.size()}};
    PipelineKey key;
    key.add(desc.code);
    const std::wstring name = key.name(L"c");
    Result<ID3D12PipelineState*> state = libraryPipeline(
        name, [&](ID3D12PipelineState** out)
        { return m_device->CreateComputePipelineState(&pipelineDesc, IID_PPV_ARGS(out)); },
        [&](ID3D12PipelineState** out)
        { return m_pipelineLibrary->LoadComputePipeline(name.c_str(), &pipelineDesc, IID_PPV_ARGS(out)); });
    if (!state)
    {
        return fail(ErrorCode::InvalidArgument, std::format("Pipeline '{}': {}", desc.name, state.error().message));
    }
    (*state)->SetName(wideName(desc.name).c_str());
    const std::scoped_lock lock{m_resourceMutex};
    return m_pipelines.create({.state = *state, .compute = true});
}

void RhiDeviceD3D12::destroy(PipelineHandle pipeline)
{
    PipelineD3D12 record;
    {
        const std::scoped_lock lock{m_resourceMutex};
        const PipelineD3D12* found = m_pipelines.get(pipeline);
        if (found == nullptr)
        {
            return;
        }
        record = *found;
        m_pipelines.destroy(pipeline);
    }
    deferDestroy([record] { record.state->Release(); });
}

PipelineD3D12 RhiDeviceD3D12::pipeline(PipelineHandle handle)
{
    const std::shared_lock lock{m_resourceMutex};
    const PipelineD3D12* found = m_pipelines.get(handle);
    MEOW2_ASSERT(found != nullptr, "Stale pipeline handle");
    return found != nullptr ? *found : PipelineD3D12{};
}

GpuMemoryStats RhiDeviceD3D12::memoryStats()
{
    GpuMemoryStats stats;
    D3D12MA::Budget local{};
    D3D12MA::Budget shared{};
    m_allocator->GetBudget(&local, &shared);
    stats.usedBytes = local.UsageBytes + shared.UsageBytes;
    stats.budgetBytes = local.BudgetBytes + shared.BudgetBytes;
    for (std::size_t tag = 0; tag < stats.bytesByTag.size(); ++tag)
    {
        stats.bytesByTag[tag] = m_bytesByTag[tag].load();
    }
    return stats;
}

RhiDeviceD3D12::LibraryHeader RhiDeviceD3D12::libraryHeader() const
{
    LibraryHeader header{.magic = c_libraryMagic};
    DXGI_ADAPTER_DESC1 adapter{};
    m_dxgiAdapter->GetDesc1(&adapter);
    header.vendorId = adapter.VendorId;
    header.deviceId = adapter.DeviceId;
    LARGE_INTEGER driver{};
    if (SUCCEEDED(m_dxgiAdapter->CheckInterfaceSupport(__uuidof(IDXGIDevice), &driver)))
    {
        header.driverVersion = static_cast<std::uint64_t>(driver.QuadPart);
    }
    return header;
}

std::vector<std::byte> RhiDeviceD3D12::pipelineCacheData()
{
    if (m_pipelineLibrary == nullptr)
    {
        return {};
    }
    const std::scoped_lock lock{m_libraryMutex};
    const LibraryHeader header = libraryHeader();
    std::vector<std::byte> data(sizeof(header) + m_pipelineLibrary->GetSerializedSize());
    std::memcpy(data.data(), &header, sizeof(header));
    if (FAILED(m_pipelineLibrary->Serialize(data.data() + sizeof(header), data.size() - sizeof(header))))
    {
        return {};
    }
    return data;
}
} // namespace Meow2::Rhi
