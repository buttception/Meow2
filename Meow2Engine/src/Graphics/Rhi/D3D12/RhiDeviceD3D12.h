/// @file
/// @brief The D3D12 device.

#pragma once

#include "Graphics/Rhi/D3D12/RhiGpuProfilerD3D12.h"
#include "Meow2/Graphics/Rhi/IRhiDevice.h"
#include "Meow2/Graphics/Rhi/RhiHandle.h"

#include <d3d12.h>
#include <D3D12MemAlloc.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <vector>

namespace Meow2::Rhi
{
class RhiDeviceD3D12;
class RhiSwapchainD3D12;

template <class T>
using ComPtr = Microsoft::WRL::ComPtr<T>; ///< Owning COM pointer, for objects the device keeps for its whole life.

/// @brief A readable description of an HRESULT, for error messages.
/// @param result The result.
/// @return Its name for the common D3D12 and DXGI codes, else its value in hex.
[[nodiscard]] std::string hresultName(HRESULT result);

/// @brief The DXGI format of an RHI format, as the resource is created.
/// @param format The format.
/// @return The format; R32_TYPELESS for depth, so it can be both a depth target and read by shaders.
[[nodiscard]] DXGI_FORMAT d3d12Format(RhiFormat format);

/// @brief Turns a UTF-8 name into the UTF-16 one D3D12's SetName() takes.
/// @param name The name.
/// @return The same text.
[[nodiscard]] std::wstring wideName(std::string_view name);

/// @brief Hands out slots of a descriptor heap and takes them back. Plain bookkeeping; the device locks around it.
/// @par Thread safety
/// Not thread-safe.
class DescriptorSlots
{
public:
    /// @brief Sets how many slots there are.
    /// @param first The first slot to hand out.
    /// @param count One past the last slot.
    void reset(std::uint32_t first, std::uint32_t count);

    /// @brief Takes a slot.
    /// @param[out] index The slot.
    /// @return False if the heap is full.
    [[nodiscard]] bool allocate(std::uint32_t& index);

    /// @brief Gives a slot back.
    /// @param index The slot.
    void release(std::uint32_t index);

private:
    std::uint32_t m_next = 0;
    std::uint32_t m_end = 0;
    std::vector<std::uint32_t> m_free;
};

/// @brief A descriptor heap and the size of one descriptor in it.
struct DescriptorHeapD3D12
{
    ComPtr<ID3D12DescriptorHeap> heap; ///< The heap.
    std::uint32_t increment = 0;       ///< Bytes from one descriptor to the next.

    /// @brief The CPU address of a slot, for writing a descriptor.
    /// @param index The slot.
    /// @return The handle.
    [[nodiscard]] D3D12_CPU_DESCRIPTOR_HANDLE cpu(std::uint32_t index) const
    {
        D3D12_CPU_DESCRIPTOR_HANDLE handle = heap->GetCPUDescriptorHandleForHeapStart();
        handle.ptr += static_cast<SIZE_T>(index) * increment;
        return handle;
    }
};

/// @brief A buffer's D3D12 objects. Plain data: the device releases them.
struct BufferD3D12
{
    D3D12MA::Allocation* allocation = nullptr; ///< Its memory; owns the resource.
    ID3D12Resource* resource = nullptr;        ///< The buffer.
    std::byte* mapped = nullptr;               ///< Mapped address; null for GPU-only memory. Upload and readback
                                               ///< buffers stay in their heap's state: they never get barriers.
    std::uint64_t size = 0;                    ///< Size in bytes, as asked for.
    Memory::MemTag tag = Memory::MemTag::Rhi;  ///< Which GpuMemoryStats counter it's in.
    std::uint64_t bytes = 0;                   ///< Memory it takes.
    std::uint32_t heapIndex = 0;               ///< Readable slot in the resource heap.
    std::uint32_t storageIndex = 0;            ///< Writable slot; 0 if shaders can't write it.
};

/// @brief A texture's D3D12 objects. Plain data: the device releases them.
struct TextureD3D12
{
    D3D12MA::Allocation* allocation = nullptr; ///< Its memory; owns the resource.
    ID3D12Resource* resource = nullptr;        ///< The texture.
    RhiFormat format = RhiFormat::Unknown;     ///< Its format.
    std::uint32_t width = 0;                   ///< Width in pixels.
    std::uint32_t height = 0;                  ///< Height in pixels.
    std::uint32_t mipLevels = 1;               ///< Mip levels.
    bool depth = false;                        ///< A depth format.
    bool renderTarget = false;                 ///< Can be drawn to: its first barrier discards the old contents.
    std::uint32_t heapIndex = 0;               ///< Readable slot in the resource heap.
    std::array<std::uint32_t, c_maxMipLevels> storageIndices{}; ///< Writable slot of each mip level; 0 if none.
    std::uint32_t targetIndex = 0;            ///< Slot in the RTV or DSV heap; 0 if it isn't a render target.
    Memory::MemTag tag = Memory::MemTag::Rhi; ///< Which GpuMemoryStats counter it's in.
    std::uint64_t bytes = 0;                  ///< Memory it takes.
};

/// @brief A sampler's slot. Plain data: D3D12 samplers are only descriptors.
struct SamplerD3D12
{
    std::uint32_t heapIndex = 0; ///< Slot in the sampler heap.
};

/// @brief A pipeline's D3D12 object. Plain data: the device releases it.
struct PipelineD3D12
{
    ID3D12PipelineState* state = nullptr; ///< The pipeline.
    bool compute = false;                 ///< A compute pipeline rather than a graphics one.
    D3D_PRIMITIVE_TOPOLOGY topology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST; ///< Set when a graphics pipeline is bound.
};

/// @brief Records into one ID3D12GraphicsCommandList7.
/// @par Thread safety
/// Not thread-safe.
class RhiCommandListD3D12 final : public IRhiCommandList
{
public:
    /// @brief A list with nothing to record into yet.
    /// @param device The device. Must outlive the list.
    explicit RhiCommandListD3D12(RhiDeviceD3D12& device);

    /// @brief Points it at a list that's recording.
    /// @param list The list.
    void setList(ID3D12GraphicsCommandList7* list)
    {
        m_list = list;
    }

    /// @brief The list.
    /// @return The pointer.
    [[nodiscard]] ID3D12GraphicsCommandList7* list() const
    {
        return m_list;
    }

    /// @brief Whether a beginRendering() or a marker is still open.
    /// @return True if the list isn't ready to submit.
    [[nodiscard]] bool isOpen() const
    {
        return m_rendering || m_openMarkers != 0;
    }

    void beginRendering(IRhiSwapchain& swapchain, std::optional<Math::Color> clear) override;
    void beginRendering(IRhiSwapchain& swapchain, std::optional<Math::Color> clear, TextureHandle depth,
                        std::optional<float> clearDepth) override;
    void beginRendering(const RenderTargets& targets) override;
    void endRendering() override;
    void setPipeline(PipelineHandle pipeline) override;
    void beginMarker(const char* name) override;
    void endMarker() override;
    void setPushConstants(const Shader::PushConstants& constants) override;
    void draw(std::uint32_t vertexCount, std::uint32_t instanceCount) override;
    void setScissor(const ScissorRect& rect) override;
    void drawIndirectCount(BufferHandle draws, BufferHandle count, std::uint32_t maxDraws, std::uint64_t drawOffset,
                           std::uint64_t countOffset) override;
    void dispatch(std::uint32_t x, std::uint32_t y, std::uint32_t z) override;
    void dispatchMesh(std::uint32_t x, std::uint32_t y, std::uint32_t z) override;
    void dispatchMeshIndirect(BufferHandle arguments, std::uint64_t offset) override;
    void fillBuffer(BufferHandle buffer, std::uint32_t value) override;
    void barrier(BufferHandle buffer, ResourceState from, ResourceState to) override;
    void barrier(TextureHandle texture, ResourceState from, ResourceState to) override;
    void barrier(IRhiSwapchain& swapchain) override;
    void copyBuffer(BufferHandle source, BufferHandle target, std::uint64_t size, std::uint64_t sourceOffset,
                    std::uint64_t targetOffset) override;
    void copyBufferToTexture(BufferHandle source, TextureHandle target, std::uint32_t mipLevel,
                             std::uint64_t sourceOffset) override;
    void copyTextureToBuffer(TextureHandle source, BufferHandle target) override;

private:
    /// @brief Binds the targets and sets the viewport and scissor to their size.
    /// @param color  The colour target's RTV.
    /// @param depth  The depth target's DSV, or null.
    /// @param width  Width in pixels.
    /// @param height Height in pixels.
    void startRendering(D3D12_CPU_DESCRIPTOR_HANDLE color, const D3D12_CPU_DESCRIPTOR_HANDLE* depth,
                        std::uint32_t width, std::uint32_t height);

    /// @brief Clears the depth target if asked to.
    /// @param depth      The depth texture.
    /// @param clearDepth The value, or empty to keep it.
    /// @return Its DSV.
    D3D12_CPU_DESCRIPTOR_HANDLE prepareDepth(TextureHandle depth, std::optional<float> clearDepth);

    /// @brief Describes a copy between one mip level of a texture and a tightly packed buffer.
    /// @param texture  The texture.
    /// @param buffer   The buffer.
    /// @param mipLevel The level.
    /// @param offset   Where the level's pixels start in the buffer, in bytes.
    /// @return The buffer side of the copy.
    [[nodiscard]] static D3D12_TEXTURE_COPY_LOCATION
    bufferFootprint(const TextureD3D12& texture, ID3D12Resource* buffer, std::uint32_t mipLevel, std::uint64_t offset);

    RhiDeviceD3D12& m_device;
    ID3D12GraphicsCommandList7* m_list = nullptr;
    bool m_rendering = false;
    std::uint32_t m_openMarkers = 0;
    GpuZoneStackD3D12 m_gpuZones;
};

/// @brief A factory, an adapter, a device with one direct queue, the frame loop, and the resources.
///
/// Frames are paced by one fence: every submit signals the next value, and a frame slot is free again once the value it
/// signalled has been reached. The same counter drives deferred destruction, as on Vulkan.
///
/// One root signature serves every pipeline: four 32-bit root constants at b0 (Shader::PushConstants), and direct
/// indexing of the resource and sampler heaps (`ResourceDescriptorHeap`, SM 6.6). Barriers use the enhanced barrier
/// API, which has the same stages, accesses and layouts as Vulkan's synchronization2, so ResourceState maps across
/// one to one and a texture can start from an undefined layout.
/// @par Thread safety
/// The frame functions are not thread-safe, except commandList() with distinct indices. Creating and destroying
/// resources, and looking them up, are, through one shared mutex that's never held across a D3D12 creation call.
class RhiDeviceD3D12 final : public IRhiDevice
{
public:
    /// @brief Use create().
    /// @param log Must outlive the device.
    explicit RhiDeviceD3D12(Log::Logger& log);

    /// @brief Connects to D3D12 and picks an adapter.
    /// @param desc Options.
    /// @param log  Must outlive the device.
    /// @return The device or an error.
    [[nodiscard]] static Result<std::unique_ptr<IRhiDevice>> create(const RhiDeviceDesc& desc, Log::Logger& log);

    ~RhiDeviceD3D12() override;

    RhiDeviceD3D12(const RhiDeviceD3D12&) = delete;            ///< Not copyable.
    RhiDeviceD3D12& operator=(const RhiDeviceD3D12&) = delete; ///< Not copyable.

    [[nodiscard]] RhiBackend backend() const override
    {
        return RhiBackend::D3D12;
    }
    [[nodiscard]] const RhiAdapterInfo& adapter() const override
    {
        return m_adapter;
    }
    [[nodiscard]] Result<std::unique_ptr<IRhiSwapchain>> createSwapchain(const RhiSwapchainDesc& desc) override;
    [[nodiscard]] Result<IRhiCommandList*> beginFrame() override;
    [[nodiscard]] bool acquire(IRhiSwapchain& swapchain) override;
    [[nodiscard]] Result<IRhiCommandList*> commandList(std::uint32_t threadIndex) override;
    [[nodiscard]] std::uint32_t frameSlot() const override
    {
        return m_frameIndex;
    }
    [[nodiscard]] Result<void> endFrame(std::span<IRhiCommandList* const> lists) override;
    void waitIdle() override;
    [[nodiscard]] std::uint64_t validationErrorCount() const override
    {
        return m_validationErrors.load();
    }
    [[nodiscard]] bool validationActive() const override
    {
        return m_messageCallback != 0;
    }

    [[nodiscard]] Result<BufferHandle> createBuffer(const BufferDesc& desc) override;
    void destroy(BufferHandle buffer) override;
    [[nodiscard]] std::span<std::byte> mappedMemory(BufferHandle buffer) override;
    [[nodiscard]] std::uint32_t bindlessIndex(BufferHandle buffer) override;
    [[nodiscard]] std::uint32_t bindlessStorageIndex(BufferHandle buffer) override;
    [[nodiscard]] Result<TextureHandle> createTexture(const TextureDesc& desc) override;
    void destroy(TextureHandle texture) override;
    [[nodiscard]] std::uint32_t bindlessIndex(TextureHandle texture) override;
    [[nodiscard]] TextureInfo textureInfo(TextureHandle texture) override;
    [[nodiscard]] std::uint32_t bindlessStorageIndex(TextureHandle texture, std::uint32_t mipLevel) override;
    [[nodiscard]] Result<SamplerHandle> createSampler(const SamplerDesc& desc) override;
    void destroy(SamplerHandle sampler) override;
    [[nodiscard]] std::uint32_t bindlessIndex(SamplerHandle sampler) override;
    [[nodiscard]] Result<PipelineHandle> createGraphicsPipeline(const GraphicsPipelineDesc& desc) override;
    [[nodiscard]] Result<PipelineHandle> createComputePipeline(const ComputePipelineDesc& desc) override;
    void destroy(PipelineHandle pipeline) override;
    [[nodiscard]] std::vector<std::byte> pipelineCacheData() override;
    [[nodiscard]] GpuMemoryStats memoryStats() override;

    /// @brief The device.
    /// @return The pointer, owned by the device.
    [[nodiscard]] ID3D12Device10* device() const
    {
        return m_device.Get();
    }

    /// @brief The factory, for swapchains.
    /// @return The pointer, owned by the device.
    [[nodiscard]] IDXGIFactory6* factory() const
    {
        return m_factory.Get();
    }

    /// @brief The direct queue, which swapchains present from.
    /// @return The pointer, owned by the device.
    [[nodiscard]] ID3D12CommandQueue* queue() const
    {
        return m_queue.Get();
    }

    /// @brief The root signature every pipeline shares.
    /// @return The pointer, owned by the device.
    [[nodiscard]] ID3D12RootSignature* rootSignature() const
    {
        return m_rootSignature.Get();
    }

    /// @brief The command signature of drawIndirectCount(): a root constant, then a draw (Shader::IndirectDraw).
    /// @return The pointer, owned by the device.
    [[nodiscard]] ID3D12CommandSignature* drawSignature() const
    {
        return m_drawSignature.Get();
    }

    /// @brief The command signature of dispatchMeshIndirect(): one DispatchMesh's group counts.
    /// @return The pointer, owned by the device; null without mesh shaders.
    [[nodiscard]] ID3D12CommandSignature* meshSignature() const
    {
        return m_meshSignature.Get();
    }

    /// @brief The RTV heap, which swapchains also take slots from.
    /// @return The heap.
    [[nodiscard]] const DescriptorHeapD3D12& rtvHeap() const
    {
        return m_rtvHeap;
    }

    /// @brief The DSV heap.
    /// @return The heap.
    [[nodiscard]] const DescriptorHeapD3D12& dsvHeap() const
    {
        return m_dsvHeap;
    }

    /// @brief Takes an RTV slot, for a swapchain image.
    /// @param[out] index The slot.
    /// @return False if the heap is full.
    [[nodiscard]] bool allocateRtv(std::uint32_t& index);

    /// @brief Gives an RTV slot back. Only once the GPU can no longer use it.
    /// @param index The slot.
    void releaseRtv(std::uint32_t index);

    /// @brief The log.
    /// @return The logger.
    [[nodiscard]] Log::Logger& log()
    {
        return m_log;
    }

    /// @brief Whether a frame is being recorded.
    /// @return True between beginFrame() and endFrame().
    [[nodiscard]] bool inFrame() const
    {
        return m_inFrame;
    }

    /// @brief Whether swapchains may turn vsync off without waiting (DXGI_FEATURE_PRESENT_ALLOW_TEARING).
    /// @return True if the display stack allows tearing.
    [[nodiscard]] bool tearingSupported() const
    {
        return m_tearing;
    }

    /// @brief Counts a swapchain in, so the destructor can check they're all gone.
    void swapchainCreated()
    {
        ++m_swapchainCount;
    }

    /// @brief Counts a swapchain out.
    void swapchainDestroyed()
    {
        --m_swapchainCount;
    }

    /// @brief Forwards a debug layer message to the log. Called by the message callback, on any thread.
    /// @param isError Whether it's an error rather than a warning.
    /// @param message The text.
    void onValidationMessage(bool isError, const char* message);

    /// @brief Logs what DRED knows about a lost device, the first time it's noticed, and returns the error to report.
    /// @return An error naming the reason.
    [[nodiscard]] Error reportDeviceLoss();

    /// @brief Looks up a buffer for the command list.
    /// @param handle The handle.
    /// @return A copy of its record; asserts on a stale handle.
    [[nodiscard]] BufferD3D12 buffer(BufferHandle handle);

    /// @brief Looks up a texture for the command list.
    /// @param handle The handle.
    /// @return A copy of its record; asserts on a stale handle.
    [[nodiscard]] TextureD3D12 texture(TextureHandle handle);

    /// @brief Looks up a pipeline for the command list.
    /// @param handle The handle.
    /// @return A copy of its record; asserts on a stale handle.
    [[nodiscard]] PipelineD3D12 pipeline(PipelineHandle handle);

    /// @brief Where GPU zones go.
    /// @return The profiler.
    [[nodiscard]] GpuProfilerD3D12& gpuProfiler()
    {
        return m_gpuProfiler;
    }

private:
    /// @brief One command list and the allocator only it records with: D3D12 allows one open list per allocator, and
    ///        a job may hold several lists open at once.
    struct RecordedList
    {
        ComPtr<ID3D12CommandAllocator> allocator; ///< Reset when the frame slot is reused.
        ComPtr<ID3D12GraphicsCommandList7> list;  ///< Closed until it's handed out.
    };

    /// @brief What one job thread records with in one frame slot.
    struct ThreadCommands
    {
        std::vector<RecordedList> recorded;    ///< Kept across frames.
        std::deque<RhiCommandListD3D12> lists; ///< One per recorded list. A deque, so pointers stay valid.
        std::size_t used = 0;                  ///< How many are in use this frame.
    };

    /// @brief One slot of the frames in flight.
    struct Frame
    {
        RecordedList first;                  ///< The frame's own list, from beginFrame().
        RecordedList last;                   ///< Runs after every other list: present barriers.
        std::uint64_t submittedValue = 0;    ///< Fence value that means the GPU is done with this slot.
        std::vector<ThreadCommands> threads; ///< One per job thread.
    };

    /// @brief Something to destroy once the GPU has reached a fence value.
    struct PendingDestroy
    {
        std::uint64_t value = 0;       ///< The value.
        std::function<void()> destroy; ///< Releases the objects and gives back heap slots.
    };

    /// @brief Turns on the debug layer and DRED, and creates the factory.
    /// @param desc Options.
    /// @return An error if DXGI is missing.
    [[nodiscard]] Result<void> createFactory(const RhiDeviceDesc& desc);

    /// @brief Chooses the fastest adapter that meets the requirements, and creates the device on it.
    /// @return An error naming each adapter and what it's missing.
    [[nodiscard]] Result<void> pickAdapter();

    /// @brief Creates the queue, the allocator, the fence and the frame slots, and connects the debug layer.
    /// @param desc Options.
    /// @return An error if D3D12 refuses.
    [[nodiscard]] Result<void> createQueue(const RhiDeviceDesc& desc);

    /// @brief Creates the heaps, the root and command signatures, the pipeline library and the default sampler.
    /// @param desc Options.
    /// @return An error if D3D12 refuses.
    [[nodiscard]] Result<void> createBindless(const RhiDeviceDesc& desc);

    /// @brief Creates a list and its allocator.
    /// @param[out] recorded The new pair, closed.
    /// @return An error if D3D12 refuses.
    [[nodiscard]] Result<void> createList(RecordedList& recorded);

    /// @brief Opens a list for recording and binds what every list needs: the heaps, the root signature.
    /// @param list The list; its allocator was reset with the frame slot.
    /// @return An error if D3D12 refuses.
    [[nodiscard]] Result<void> beginList(const RecordedList& list);

    /// @brief Waits until the fence reaches a value.
    /// @param value The value.
    /// @return An error if the GPU hung or the device was lost.
    [[nodiscard]] Result<void> waitForFence(std::uint64_t value);

    /// @brief Records a swapchain image's layout change.
    /// @param list      Where to record it.
    /// @param swapchain The swapchain.
    /// @param toPresent True for render target → present, false for present → render target.
    static void recordSwapchainBarrier(ID3D12GraphicsCommandList7* list, const RhiSwapchainD3D12& swapchain,
                                       bool toPresent);

    /// @brief Queues objects for release after the frames that could use them.
    /// @param destroyLater Releases them.
    void deferDestroy(std::function<void()> destroyLater);

    /// @brief Releases everything the GPU has finished with.
    void collectGarbage();

    /// @brief Writes a sampler into its heap slot.
    /// @param desc  The settings.
    /// @param index The slot.
    void writeSampler(const SamplerDesc& desc, std::uint32_t index);

    /// @brief Creates a pipeline through the pipeline library, so a later run finds the driver's compiled code.
    /// @param name   A key for the library, from the description and the bytecode.
    /// @param create Creates the pipeline when the library doesn't have it.
    /// @param load   Loads it from the library.
    /// @return The pipeline, or an error.
    [[nodiscard]] Result<ID3D12PipelineState*>
    libraryPipeline(const std::wstring& name, const std::function<HRESULT(ID3D12PipelineState**)>& create,
                    const std::function<HRESULT(ID3D12PipelineState**)>& load);

    Log::Logger& m_log;
    ComPtr<IDXGIFactory6> m_factory;
    ComPtr<IDXGIAdapter1> m_dxgiAdapter;
    ComPtr<ID3D12Device10> m_device;
    ComPtr<ID3D12CommandQueue> m_queue;
    ComPtr<D3D12MA::Allocator> m_allocator;
    ComPtr<ID3D12Fence> m_fence;
    HANDLE m_fenceEvent = nullptr;
    ComPtr<ID3D12InfoQueue1> m_infoQueue;
    DWORD m_messageCallback = 0;
    bool m_debugLayer = false;
    bool m_tearing = false;
    std::atomic<bool> m_lost{false};

    std::array<Frame, c_maxFramesInFlight> m_frames{};
    std::uint32_t m_frameIndex = 0;
    std::atomic<std::uint64_t> m_fenceValue{0};
    std::atomic<bool> m_inFrame{false};
    RhiCommandListD3D12 m_commandList;
    std::vector<RhiSwapchainD3D12*> m_acquired;
    std::uint32_t m_swapchainCount = 0;
    RhiAdapterInfo m_adapter;
    std::atomic<std::uint64_t> m_validationErrors{0};

    DescriptorHeapD3D12 m_resourceHeap;
    DescriptorHeapD3D12 m_samplerHeap;
    DescriptorHeapD3D12 m_rtvHeap;
    DescriptorHeapD3D12 m_dsvHeap;
    ComPtr<ID3D12RootSignature> m_rootSignature;
    ComPtr<ID3D12CommandSignature> m_drawSignature;
    ComPtr<ID3D12CommandSignature> m_meshSignature; ///< Only with mesh shaders.
    /// @brief Identifies the GPU and driver a saved pipeline library came from, ahead of its bytes. Plain data.
    struct LibraryHeader
    {
        std::uint32_t magic = 0;         ///< c_libraryMagic.
        std::uint32_t vendorId = 0;      ///< DXGI_ADAPTER_DESC1::VendorId.
        std::uint32_t deviceId = 0;      ///< DXGI_ADAPTER_DESC1::DeviceId.
        std::uint32_t padding = 0;       ///< Zero.
        std::uint64_t driverVersion = 0; ///< The user-mode driver's version.
    };

    /// @brief The header this GPU and driver write.
    /// @return The header.
    [[nodiscard]] LibraryHeader libraryHeader() const;

    std::vector<std::byte> m_libraryData; ///< The library reads from this memory for its whole life.
    ComPtr<ID3D12PipelineLibrary1> m_pipelineLibrary;
    std::mutex m_libraryMutex; ///< Guards the pipeline library.

    std::array<std::atomic<std::uint64_t>, Memory::c_memTagCount> m_bytesByTag{};
    std::uint32_t m_recordingThreads = 1;
    GpuProfilerD3D12 m_gpuProfiler;

    std::shared_mutex m_resourceMutex; ///< Guards everything below. Shared for lookups: recording jobs only read.
    HandlePool<BufferD3D12, BufferTag> m_buffers;
    HandlePool<TextureD3D12, TextureTag> m_textures;
    HandlePool<SamplerD3D12, SamplerTag> m_samplers;
    HandlePool<PipelineD3D12, PipelineTag> m_pipelines;
    DescriptorSlots m_resourceSlots;
    DescriptorSlots m_samplerSlots;
    DescriptorSlots m_rtvSlots;
    DescriptorSlots m_dsvSlots;
    std::vector<PendingDestroy> m_pendingDestroys;
};
} // namespace Meow2::Rhi
