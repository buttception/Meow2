/// @file
/// @brief The D3D12 device: debug layer, DRED, adapter selection, the queue and the frame loop.

#include "Graphics/Rhi/D3D12/RhiDeviceD3D12.h"

#include "Graphics/Rhi/D3D12/D3D12Requirements.h"
#include "Graphics/Rhi/D3D12/RhiSwapchainD3D12.h"
#include "Meow2/Core/Assert.h"
#include "Meow2/Core/Memory/MemTag.h"

#include <algorithm>
#include <format>
#include <string>
#include <utility>

namespace Meow2::Rhi
{
namespace
{
constexpr DWORD c_frameWaitMilliseconds = 10'000; // a GPU this late has hung

/// @brief Turns UTF-16 from D3D12 (adapter and object names) into UTF-8.
/// @param text The text; may be null.
/// @return The same text.
std::string narrowName(const wchar_t* text)
{
    if (text == nullptr || *text == L'\0')
    {
        return {};
    }
    const int size = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
    std::string result(static_cast<std::size_t>(std::max(size, 1) - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text, -1, result.data(), size, nullptr, nullptr);
    return result;
}

// Debug layer warnings that are expected, so they'd only bury the ones that matter.
constexpr std::array<D3D12_MESSAGE_ID, 3> c_ignoredWarnings{
    // The pipeline library misses the first time each pipeline is created: that's how it fills up.
    D3D12_MESSAGE_ID_LOADPIPELINE_NAMENOTFOUND,
    // Clearing to a colour the texture wasn't created with. Clear colours vary per pass, and Vulkan has no such hint.
    D3D12_MESSAGE_ID_CLEARRENDERTARGETVIEW_MISMATCHINGCLEARVALUE,
    // CheckFeatureSupport() has a newer version. D3D12MA calls the old one, which still works.
    D3D12_MESSAGE_ID_DEPRECATED_API};

/// @brief Receives debug layer messages, on whichever thread made the call.
void CALLBACK onDebugMessage(D3D12_MESSAGE_CATEGORY /*category*/, D3D12_MESSAGE_SEVERITY severity, D3D12_MESSAGE_ID id,
                             LPCSTR description, void* context)
{
    if (severity == D3D12_MESSAGE_SEVERITY_INFO || severity == D3D12_MESSAGE_SEVERITY_MESSAGE ||
        (severity == D3D12_MESSAGE_SEVERITY_WARNING &&
         std::ranges::find(c_ignoredWarnings, id) != c_ignoredWarnings.end()))
    {
        return;
    }
    static_cast<RhiDeviceD3D12*>(context)->onValidationMessage(severity != D3D12_MESSAGE_SEVERITY_WARNING, description);
}

/// @brief Collects what an adapter offers for the requirements check.
/// @param device A device created on it.
/// @return The facts.
D3D12GpuFacts readGpuFacts(ID3D12Device* device)
{
    D3D12GpuFacts facts;
    const std::array<D3D_FEATURE_LEVEL, 4> levels{D3D_FEATURE_LEVEL_12_0, D3D_FEATURE_LEVEL_12_1,
                                                  D3D_FEATURE_LEVEL_12_2, D3D_FEATURE_LEVEL_11_0};
    D3D12_FEATURE_DATA_FEATURE_LEVELS featureLevels{.NumFeatureLevels = static_cast<UINT>(levels.size()),
                                                    .pFeatureLevelsRequested = levels.data()};
    if (SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_FEATURE_LEVELS, &featureLevels, sizeof(featureLevels))))
    {
        facts.featureLevel = static_cast<std::uint32_t>(featureLevels.MaxSupportedFeatureLevel);
    }
    // Asking for the newest model the headers know; the runtime lowers it to what the driver supports.
    D3D12_FEATURE_DATA_SHADER_MODEL shaderModel{.HighestShaderModel = D3D_HIGHEST_SHADER_MODEL};
    if (SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &shaderModel, sizeof(shaderModel))))
    {
        facts.shaderModel = static_cast<std::uint32_t>(shaderModel.HighestShaderModel);
    }
    D3D12_FEATURE_DATA_D3D12_OPTIONS options{};
    if (SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS, &options, sizeof(options))))
    {
        facts.resourceBindingTier = static_cast<std::uint32_t>(options.ResourceBindingTier);
    }
    D3D12_FEATURE_DATA_D3D12_OPTIONS12 options12{};
    if (SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS12, &options12, sizeof(options12))))
    {
        facts.enhancedBarriers = options12.EnhancedBarriersSupported != FALSE;
    }
    D3D12_FEATURE_DATA_D3D12_OPTIONS13 options13{};
    if (SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS13, &options13, sizeof(options13))))
    {
        facts.unrestrictedCopyPitch = options13.UnrestrictedBufferTextureCopyPitchSupported != FALSE;
    }
    ComPtr<ID3D12Device10> device10;
    facts.device10 = SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&device10)));
    return facts;
}

/// @brief The name of a DRED breadcrumb, for the operations the engine records.
/// @param op The operation.
/// @return Its name.
std::string_view breadcrumbName(D3D12_AUTO_BREADCRUMB_OP op)
{
    switch (op)
    {
        case D3D12_AUTO_BREADCRUMB_OP_SETMARKER:
            return "SetMarker";
        case D3D12_AUTO_BREADCRUMB_OP_BEGINEVENT:
            return "BeginEvent";
        case D3D12_AUTO_BREADCRUMB_OP_ENDEVENT:
            return "EndEvent";
        case D3D12_AUTO_BREADCRUMB_OP_DRAWINSTANCED:
            return "DrawInstanced";
        case D3D12_AUTO_BREADCRUMB_OP_DISPATCH:
            return "Dispatch";
        case D3D12_AUTO_BREADCRUMB_OP_COPYBUFFERREGION:
            return "CopyBufferRegion";
        case D3D12_AUTO_BREADCRUMB_OP_COPYTEXTUREREGION:
            return "CopyTextureRegion";
        case D3D12_AUTO_BREADCRUMB_OP_CLEARRENDERTARGETVIEW:
            return "ClearRenderTargetView";
        case D3D12_AUTO_BREADCRUMB_OP_CLEARDEPTHSTENCILVIEW:
            return "ClearDepthStencilView";
        case D3D12_AUTO_BREADCRUMB_OP_RESOURCEBARRIER:
            return "ResourceBarrier";
        case D3D12_AUTO_BREADCRUMB_OP_EXECUTEINDIRECT:
            return "ExecuteIndirect";
        case D3D12_AUTO_BREADCRUMB_OP_WRITEBUFFERIMMEDIATE:
            return "WriteBufferImmediate";
        case D3D12_AUTO_BREADCRUMB_OP_BARRIER:
            return "Barrier";
        default:
            return "another operation";
    }
}
} // namespace

std::string hresultName(HRESULT result)
{
    switch (result)
    {
        case E_INVALIDARG:
            return "E_INVALIDARG";
        case E_OUTOFMEMORY:
            return "E_OUTOFMEMORY";
        case E_NOINTERFACE:
            return "E_NOINTERFACE";
        case E_FAIL:
            return "E_FAIL";
        case DXGI_ERROR_DEVICE_REMOVED:
            return "DXGI_ERROR_DEVICE_REMOVED";
        case DXGI_ERROR_DEVICE_HUNG:
            return "DXGI_ERROR_DEVICE_HUNG";
        case DXGI_ERROR_DEVICE_RESET:
            return "DXGI_ERROR_DEVICE_RESET";
        case DXGI_ERROR_DRIVER_INTERNAL_ERROR:
            return "DXGI_ERROR_DRIVER_INTERNAL_ERROR";
        case DXGI_ERROR_INVALID_CALL:
            return "DXGI_ERROR_INVALID_CALL";
        case DXGI_ERROR_UNSUPPORTED:
            return "DXGI_ERROR_UNSUPPORTED";
        case D3D12_ERROR_ADAPTER_NOT_FOUND:
            return "D3D12_ERROR_ADAPTER_NOT_FOUND";
        case D3D12_ERROR_DRIVER_VERSION_MISMATCH:
            return "D3D12_ERROR_DRIVER_VERSION_MISMATCH";
        default:
            return std::format("HRESULT 0x{:08X}", static_cast<std::uint32_t>(result));
    }
}

std::wstring wideName(std::string_view name)
{
    if (name.empty())
    {
        return {};
    }
    const int size = MultiByteToWideChar(CP_UTF8, 0, name.data(), static_cast<int>(name.size()), nullptr, 0);
    std::wstring result(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, name.data(), static_cast<int>(name.size()), result.data(), size);
    return result;
}

void DescriptorSlots::reset(std::uint32_t first, std::uint32_t count)
{
    m_next = first;
    m_end = count;
    m_free.clear();
}

bool DescriptorSlots::allocate(std::uint32_t& index)
{
    if (!m_free.empty())
    {
        index = m_free.back();
        m_free.pop_back();
        return true;
    }
    if (m_next == m_end)
    {
        return false;
    }
    index = m_next++;
    return true;
}

void DescriptorSlots::release(std::uint32_t index)
{
    m_free.push_back(index);
}

RhiDeviceD3D12::RhiDeviceD3D12(Log::Logger& log)
    : m_log(log)
    , m_commandList(*this)
{
}

Result<std::unique_ptr<IRhiDevice>> RhiDeviceD3D12::create(const RhiDeviceDesc& desc, Log::Logger& log)
{
    const Memory::MemTagScope tag{Memory::MemTag::Rhi};
    auto device = std::make_unique<RhiDeviceD3D12>(log);
    if (Result<void> factory = device->createFactory(desc); !factory)
    {
        return Unexpected<Error>{factory.error()};
    }
    if (Result<void> picked = device->pickAdapter(); !picked)
    {
        return Unexpected<Error>{picked.error()};
    }
    if (Result<void> queue = device->createQueue(desc); !queue)
    {
        return Unexpected<Error>{queue.error()};
    }
    if (Result<void> bindless = device->createBindless(desc); !bindless)
    {
        return Unexpected<Error>{bindless.error()};
    }
    return std::unique_ptr<IRhiDevice>{std::move(device)};
}

Result<void> RhiDeviceD3D12::createFactory(const RhiDeviceDesc& desc)
{
    if (desc.validation)
    {
        ComPtr<ID3D12Debug1> debug;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug))))
        {
            debug->EnableDebugLayer();
            m_debugLayer = true;
        }
        else
        {
            MEOW2_LOG_WARN(m_log,
                           "Validation was asked for, but the D3D12 debug layer (d3d12SDKLayers.dll) is missing");
        }
    }
    // DRED: breadcrumbs say which command the GPU was running when it crashed, and page faults name the resource it
    // touched. Always on, also in Release: it costs little, and a report from a player's crash is the useful one.
    ComPtr<ID3D12DeviceRemovedExtendedDataSettings1> dred;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&dred))))
    {
        dred->SetAutoBreadcrumbsEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
        dred->SetPageFaultEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
    }

    // The DXGI debug flag needs the Graphics Tools; without them, carry on without DXGI's messages.
    HRESULT created = E_FAIL;
    if (m_debugLayer)
    {
        created = CreateDXGIFactory2(DXGI_CREATE_FACTORY_DEBUG, IID_PPV_ARGS(&m_factory));
    }
    if (FAILED(created))
    {
        created = CreateDXGIFactory2(0, IID_PPV_ARGS(&m_factory));
    }
    if (FAILED(created))
    {
        return fail(ErrorCode::Unsupported, std::format("CreateDXGIFactory2: {}", hresultName(created)));
    }
    BOOL tearing = FALSE;
    m_tearing =
        SUCCEEDED(m_factory->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &tearing, sizeof(tearing))) &&
        tearing != FALSE;
    return {};
}

Result<void> RhiDeviceD3D12::pickAdapter()
{
    std::string rejected;
    bool any = false;
    ComPtr<IDXGIAdapter1> adapter;
    // Fastest first, so the first one that qualifies wins: a dedicated GPU over the one in the CPU.
    for (UINT i = 0; m_factory->EnumAdapterByGpuPreference(i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
                                                           IID_PPV_ARGS(&adapter)) != DXGI_ERROR_NOT_FOUND;
         ++i)
    {
        DXGI_ADAPTER_DESC1 info{};
        adapter->GetDesc1(&info);
        if ((info.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0)
        {
            continue; // WARP: far too slow to be useful
        }
        any = true;
        const std::string name = narrowName(info.Description);
        ComPtr<ID3D12Device> candidate;
        if (FAILED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&candidate))))
        {
            rejected += std::format("\n{} is missing:\n  - feature level 12_0", name);
            continue;
        }
        const D3D12GpuFacts facts = readGpuFacts(candidate.Get());
        const std::vector<std::string> missing = missingD3D12Requirements(facts);
        if (!missing.empty())
        {
            rejected += std::format("\n{} is missing:", name);
            for (const std::string& item : missing)
            {
                rejected += std::format("\n  - {}", item);
            }
            continue;
        }
        if (FAILED(candidate.As(&m_device)))
        {
            continue;
        }
        m_dxgiAdapter = adapter;
        D3D12_FEATURE_DATA_ARCHITECTURE architecture{};
        m_device->CheckFeatureSupport(D3D12_FEATURE_ARCHITECTURE, &architecture, sizeof(architecture));
        m_adapter = {.name = name,
                     .apiVersion = std::format("feature level {}_{}, shader model {}.{}", facts.featureLevel >> 12U,
                                               (facts.featureLevel >> 8U) & 0xFU, facts.shaderModel >> 4U,
                                               facts.shaderModel & 0xFU),
                     .discrete = architecture.UMA == FALSE,
                     .software = false};
        // Mesh shaders are optional (the meshlet path); the shader model they need (6.5) is below the required 6.6.
        D3D12_FEATURE_DATA_D3D12_OPTIONS7 options7{};
        m_adapter.meshShaders =
            SUCCEEDED(m_device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS7, &options7, sizeof(options7))) &&
            options7.MeshShaderTier >= D3D12_MESH_SHADER_TIER_1;
        MEOW2_LOG_INFO(m_log, "GPU: {} (D3D12, {})", m_adapter.name, m_adapter.apiVersion);
        MEOW2_LOG_INFO(m_log, "Mesh shaders: {}",
                       m_adapter.meshShaders ? "yes" : "no (scenes draw through the indirect path)");
        return {};
    }
    return fail(ErrorCode::Unsupported, !any ? std::string{"No D3D12 GPU was found"}
                                             : std::format("No GPU meets the requirements:{}", rejected));
}

Result<void> RhiDeviceD3D12::createQueue(const RhiDeviceDesc& desc)
{
    MEOW2_ASSERT(desc.recordingThreads > 0, "At least one thread records");
    m_recordingThreads = desc.recordingThreads;
    m_device->SetName(L"Meow2 device");

    if (m_debugLayer && SUCCEEDED(m_device.As(&m_infoQueue)))
    {
        // The debug layer would otherwise break into the debugger on errors; the log and the test count are enough.
        m_infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, FALSE);
        m_infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, FALSE);
        m_infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, FALSE);
        if (FAILED(m_infoQueue->RegisterMessageCallback(onDebugMessage, D3D12_MESSAGE_CALLBACK_FLAG_NONE, this,
                                                        &m_messageCallback)))
        {
            m_messageCallback = 0;
            MEOW2_LOG_WARN(m_log, "The D3D12 debug layer is on, but its messages can't be read");
        }
    }

    const D3D12_COMMAND_QUEUE_DESC queueDesc{.Type = D3D12_COMMAND_LIST_TYPE_DIRECT};
    if (const HRESULT result = m_device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_queue)); FAILED(result))
    {
        return fail(ErrorCode::Unknown, std::format("CreateCommandQueue: {}", hresultName(result)));
    }
    m_queue->SetName(L"Graphics queue");

    const D3D12MA::ALLOCATOR_DESC allocatorDesc{.Flags = D3D12MA::ALLOCATOR_FLAG_NONE,
                                                .pDevice = m_device.Get(),
                                                .PreferredBlockSize = 0,
                                                .pAllocationCallbacks = nullptr,
                                                .pAdapter = m_dxgiAdapter.Get()};
    if (const HRESULT result = D3D12MA::CreateAllocator(&allocatorDesc, &m_allocator); FAILED(result))
    {
        return fail(ErrorCode::OutOfMemory, std::format("D3D12MA: {}", hresultName(result)));
    }

    if (const HRESULT result = m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence)); FAILED(result))
    {
        return fail(ErrorCode::Unknown, std::format("CreateFence: {}", hresultName(result)));
    }
    m_fence->SetName(L"Frame fence");
    m_fenceEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (m_fenceEvent == nullptr)
    {
        return fail(ErrorCode::Unknown, "CreateEvent failed");
    }

    for (Frame& frame : m_frames)
    {
        for (RecordedList* list : {&frame.first, &frame.last})
        {
            if (Result<void> created = createList(*list); !created)
            {
                return created;
            }
        }
        frame.threads = std::vector<ThreadCommands>(desc.recordingThreads);
    }
    m_gpuProfiler.connect(m_device.Get(), m_queue.Get());
    return {};
}

Result<void> RhiDeviceD3D12::createList(RecordedList& recorded)
{
    if (const HRESULT result =
            m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&recorded.allocator));
        FAILED(result))
    {
        return fail(ErrorCode::Unknown, std::format("CreateCommandAllocator: {}", hresultName(result)));
    }
    // Created closed, so every list goes through the same Reset() in beginList().
    if (const HRESULT result = m_device->CreateCommandList1(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                            D3D12_COMMAND_LIST_FLAG_NONE, IID_PPV_ARGS(&recorded.list));
        FAILED(result))
    {
        return fail(ErrorCode::Unknown, std::format("CreateCommandList1: {}", hresultName(result)));
    }
    return {};
}

Result<void> RhiDeviceD3D12::beginList(const RecordedList& recorded)
{
    ID3D12GraphicsCommandList7* list = recorded.list.Get();
    if (const HRESULT result = list->Reset(recorded.allocator.Get(), nullptr); FAILED(result))
    {
        return fail(ErrorCode::Unknown, std::format("Command list Reset: {}", hresultName(result)));
    }
    // Bound once per list and never again: nothing else in the engine is ever bound (techstack.md §11).
    const std::array<ID3D12DescriptorHeap*, 2> heaps{m_resourceHeap.heap.Get(), m_samplerHeap.heap.Get()};
    list->SetDescriptorHeaps(static_cast<UINT>(heaps.size()), heaps.data());
    list->SetGraphicsRootSignature(m_rootSignature.Get());
    list->SetComputeRootSignature(m_rootSignature.Get());
    list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    return {};
}

RhiDeviceD3D12::~RhiDeviceD3D12()
{
    MEOW2_ASSERT(m_swapchainCount == 0, "Destroy every swapchain before the device");
    if (m_queue != nullptr && m_fence != nullptr && !m_lost)
    {
        waitIdle();
    }
    collectGarbage();
    m_gpuProfiler.disconnect();
    if (m_infoQueue != nullptr && m_messageCallback != 0)
    {
        m_infoQueue->UnregisterMessageCallback(m_messageCallback);
    }
    if (m_fenceEvent != nullptr)
    {
        CloseHandle(m_fenceEvent);
    }
}

Result<std::unique_ptr<IRhiSwapchain>> RhiDeviceD3D12::createSwapchain(const RhiSwapchainDesc& desc)
{
    return RhiSwapchainD3D12::create(*this, desc);
}

Result<void> RhiDeviceD3D12::waitForFence(std::uint64_t value)
{
    const std::uint64_t reached = m_fence->GetCompletedValue();
    if (reached == UINT64_MAX)
    {
        return Unexpected<Error>{reportDeviceLoss()}; // a removed device's fences read all ones
    }
    if (reached >= value)
    {
        return {};
    }
    m_fence->SetEventOnCompletion(value, m_fenceEvent);
    if (WaitForSingleObject(m_fenceEvent, c_frameWaitMilliseconds) != WAIT_OBJECT_0)
    {
        if (FAILED(m_device->GetDeviceRemovedReason()))
        {
            return Unexpected<Error>{reportDeviceLoss()};
        }
        return fail(ErrorCode::Timeout, "Waiting for the GPU: it hasn't finished a frame in 10 seconds");
    }
    if (m_fence->GetCompletedValue() == UINT64_MAX)
    {
        return Unexpected<Error>{reportDeviceLoss()};
    }
    return {};
}

Result<IRhiCommandList*> RhiDeviceD3D12::beginFrame()
{
    MEOW2_ASSERT(!m_inFrame, "endFrame() wasn't called");
    if (m_lost || FAILED(m_device->GetDeviceRemovedReason()))
    {
        return Unexpected<Error>{reportDeviceLoss()};
    }
    Frame& frame = m_frames[m_frameIndex];
    if (Result<void> waited = waitForFence(frame.submittedValue); !waited)
    {
        return Unexpected<Error>{waited.error()};
    }
    collectGarbage();
    // Every list of this slot finished on the GPU, so their memory can be recorded over.
    for (RecordedList* list : {&frame.first, &frame.last})
    {
        list->allocator->Reset();
    }
    for (ThreadCommands& thread : frame.threads)
    {
        for (std::size_t i = 0; i < thread.used; ++i)
        {
            thread.recorded[i].allocator->Reset();
        }
        thread.used = 0;
    }
    for (const RecordedList* list : {&frame.first, &frame.last})
    {
        if (Result<void> begun = beginList(*list); !begun)
        {
            return Unexpected<Error>{begun.error()};
        }
    }
    m_gpuProfiler.collect();
    m_commandList.setList(frame.first.list.Get());
    m_inFrame = true;
    return &m_commandList;
}

Result<IRhiCommandList*> RhiDeviceD3D12::commandList(std::uint32_t threadIndex)
{
    MEOW2_ASSERT(m_inFrame, "commandList() outside a frame");
    MEOW2_ASSERT(threadIndex < m_recordingThreads, "RhiDeviceDesc::recordingThreads is too small");
    const Memory::MemTagScope tag{Memory::MemTag::Rhi};
    ThreadCommands& thread = m_frames[m_frameIndex].threads[threadIndex];
    if (thread.used == thread.recorded.size())
    {
        RecordedList recorded;
        if (Result<void> created = createList(recorded); !created)
        {
            return Unexpected<Error>{created.error()};
        }
        thread.recorded.push_back(std::move(recorded));
        thread.lists.emplace_back(*this);
    }
    if (Result<void> begun = beginList(thread.recorded[thread.used]); !begun)
    {
        return Unexpected<Error>{begun.error()};
    }
    RhiCommandListD3D12& list = thread.lists[thread.used];
    list.setList(thread.recorded[thread.used].list.Get());
    ++thread.used;
    return &list;
}

void RhiDeviceD3D12::recordSwapchainBarrier(ID3D12GraphicsCommandList7* list, const RhiSwapchainD3D12& swapchain,
                                            bool toPresent)
{
    D3D12_TEXTURE_BARRIER barrier{.pResource = swapchain.image(),
                                  .Subresources = {.IndexOrFirstMipLevel = 0xFFFFFFFF}, // every subresource
                                  .Flags = D3D12_TEXTURE_BARRIER_FLAG_NONE};
    if (toPresent)
    {
        barrier.SyncBefore = D3D12_BARRIER_SYNC_RENDER_TARGET;
        barrier.SyncAfter = D3D12_BARRIER_SYNC_NONE;
        barrier.AccessBefore = D3D12_BARRIER_ACCESS_RENDER_TARGET;
        barrier.AccessAfter = D3D12_BARRIER_ACCESS_NO_ACCESS;
        barrier.LayoutBefore = D3D12_BARRIER_LAYOUT_RENDER_TARGET;
        barrier.LayoutAfter = D3D12_BARRIER_LAYOUT_PRESENT;
    }
    else
    {
        barrier.SyncBefore = D3D12_BARRIER_SYNC_NONE;
        barrier.SyncAfter = D3D12_BARRIER_SYNC_RENDER_TARGET;
        barrier.AccessBefore = D3D12_BARRIER_ACCESS_NO_ACCESS;
        barrier.AccessAfter = D3D12_BARRIER_ACCESS_RENDER_TARGET;
        barrier.LayoutBefore = D3D12_BARRIER_LAYOUT_PRESENT;
        barrier.LayoutAfter = D3D12_BARRIER_LAYOUT_RENDER_TARGET;
    }
    const D3D12_BARRIER_GROUP group{.Type = D3D12_BARRIER_TYPE_TEXTURE, .NumBarriers = 1, .pTextureBarriers = &barrier};
    list->Barrier(1, &group);
}

bool RhiDeviceD3D12::acquire(IRhiSwapchain& swapchain)
{
    MEOW2_ASSERT(m_inFrame, "acquire() outside a frame");
    auto& target = static_cast<RhiSwapchainD3D12&>(swapchain);
    if (!target.acquire())
    {
        return false;
    }
    recordSwapchainBarrier(m_frames[m_frameIndex].first.list.Get(), target, false);
    m_acquired.push_back(&target);
    return true;
}

Result<void> RhiDeviceD3D12::endFrame(std::span<IRhiCommandList* const> lists)
{
    MEOW2_ASSERT(m_inFrame, "beginFrame() wasn't called");
    m_inFrame = false;
    Frame& frame = m_frames[m_frameIndex];
    for (const RhiSwapchainD3D12* swapchain : m_acquired)
    {
        recordSwapchainBarrier(frame.last.list.Get(), *swapchain, true);
    }
    MEOW2_ASSERT(!m_commandList.isOpen(), "The frame's list has rendering or a marker still open");
    std::vector<ID3D12CommandList*> submitted{frame.first.list.Get()};
    for (IRhiCommandList* list : lists)
    {
        const auto* recorded = static_cast<const RhiCommandListD3D12*>(list);
        MEOW2_ASSERT(!recorded->isOpen(), "A list has rendering or a marker still open");
        submitted.push_back(recorded->list());
    }
    submitted.push_back(frame.last.list.Get());
    [[maybe_unused]] std::size_t recordedLists = 0;
    for (const ThreadCommands& thread : frame.threads)
    {
        recordedLists += thread.used;
    }
    MEOW2_ASSERT(recordedLists == lists.size(), "endFrame() must get every list from commandList()");

    const auto releaseSwapchains = [this]
    {
        for (RhiSwapchainD3D12* swapchain : m_acquired)
        {
            swapchain->released();
        }
        m_acquired.clear();
    };
    for (ID3D12CommandList* list : submitted)
    {
        if (const HRESULT result = static_cast<ID3D12GraphicsCommandList*>(list)->Close(); FAILED(result))
        {
            releaseSwapchains();
            m_frameIndex = (m_frameIndex + 1) % c_maxFramesInFlight;
            return fail(ErrorCode::Unknown, std::format("Closing a command list: {}", hresultName(result)));
        }
    }
    m_queue->ExecuteCommandLists(static_cast<UINT>(submitted.size()), submitted.data());
    frame.submittedValue = ++m_fenceValue;
    m_queue->Signal(m_fence.Get(), frame.submittedValue);
    m_frameIndex = (m_frameIndex + 1) % c_maxFramesInFlight;

    Result<void> outcome;
    for (RhiSwapchainD3D12* swapchain : m_acquired)
    {
        const HRESULT presented = swapchain->present();
        if (presented == DXGI_ERROR_DEVICE_REMOVED || presented == DXGI_ERROR_DEVICE_RESET)
        {
            outcome = Unexpected<Error>{reportDeviceLoss()};
        }
        else if (FAILED(presented))
        {
            MEOW2_LOG_WARN(m_log, "Present: {}", hresultName(presented));
        }
    }
    releaseSwapchains();
    return outcome;
}

void RhiDeviceD3D12::waitIdle()
{
    const std::uint64_t value = ++m_fenceValue;
    m_queue->Signal(m_fence.Get(), value);
    if (Result<void> waited = waitForFence(value); !waited)
    {
        MEOW2_LOG_ERROR(m_log, "waitIdle: {}", waited.error());
    }
    collectGarbage();
}

bool RhiDeviceD3D12::allocateRtv(std::uint32_t& index)
{
    const std::scoped_lock lock{m_resourceMutex};
    return m_rtvSlots.allocate(index);
}

void RhiDeviceD3D12::releaseRtv(std::uint32_t index)
{
    const std::scoped_lock lock{m_resourceMutex};
    m_rtvSlots.release(index);
}

void RhiDeviceD3D12::onValidationMessage(bool isError, const char* message)
{
    if (isError)
    {
        ++m_validationErrors;
        MEOW2_LOG_ERROR(m_log, "D3D12: {}", message);
    }
    else
    {
        MEOW2_LOG_WARN(m_log, "D3D12: {}", message);
    }
}

Error RhiDeviceD3D12::reportDeviceLoss()
{
    const HRESULT reason = m_device->GetDeviceRemovedReason();
    Error error{.code = ErrorCode::Unknown,
                .message = std::format("The GPU device was lost ({})", hresultName(reason))};
    if (m_lost.exchange(true))
    {
        return error; // reported once; every later frame just fails
    }
    MEOW2_LOG_ERROR(m_log, "{}. DRED report follows.", error.message);
    ComPtr<ID3D12DeviceRemovedExtendedData1> dred;
    if (FAILED(m_device.As(&dred)))
    {
        MEOW2_LOG_ERROR(m_log, "DRED: not available");
        return error;
    }
    D3D12_DRED_AUTO_BREADCRUMBS_OUTPUT1 breadcrumbs{};
    if (SUCCEEDED(dred->GetAutoBreadcrumbsOutput1(&breadcrumbs)))
    {
        bool unfinished = false;
        for (const D3D12_AUTO_BREADCRUMB_NODE1* node = breadcrumbs.pHeadAutoBreadcrumbNode; node != nullptr;
             node = node->pNext)
        {
            const std::uint32_t done = node->pLastBreadcrumbValue != nullptr ? *node->pLastBreadcrumbValue : 0;
            if (done >= node->BreadcrumbCount)
            {
                continue; // this list finished: not where the GPU stopped
            }
            unfinished = true;
            const std::string_view next =
                node->pCommandHistory != nullptr ? breadcrumbName(node->pCommandHistory[done]) : "unknown";
            MEOW2_LOG_ERROR(m_log, "DRED: list '{}' on '{}' finished {} of {} operations; the next was {}",
                            narrowName(node->pCommandListDebugNameW), narrowName(node->pCommandQueueDebugNameW), done,
                            node->BreadcrumbCount, next);
            for (UINT i = 0; i < node->BreadcrumbContextsCount; ++i)
            {
                const D3D12_DRED_BREADCRUMB_CONTEXT& context = node->pBreadcrumbContexts[i];
                MEOW2_LOG_ERROR(m_log, "DRED:   marker at operation {}: {}", context.BreadcrumbIndex,
                                narrowName(context.pContextString));
            }
        }
        if (!unfinished)
        {
            MEOW2_LOG_ERROR(m_log, "DRED: every command list had finished: the loss came from outside the GPU's work "
                                   "(a driver reset, or the device was removed on purpose)");
        }
    }
    D3D12_DRED_PAGE_FAULT_OUTPUT1 pageFault{};
    if (SUCCEEDED(dred->GetPageFaultAllocationOutput1(&pageFault)) && pageFault.PageFaultVA != 0)
    {
        MEOW2_LOG_ERROR(m_log, "DRED: page fault at GPU address 0x{:016X}", pageFault.PageFaultVA);
        for (const D3D12_DRED_ALLOCATION_NODE1* node = pageFault.pHeadExistingAllocationNode; node != nullptr;
             node = node->pNext)
        {
            MEOW2_LOG_ERROR(m_log, "DRED:   live resource there: {}", narrowName(node->ObjectNameW));
        }
        for (const D3D12_DRED_ALLOCATION_NODE1* node = pageFault.pHeadRecentFreedAllocationNode; node != nullptr;
             node = node->pNext)
        {
            MEOW2_LOG_ERROR(m_log, "DRED:   recently freed there: {}", narrowName(node->ObjectNameW));
        }
    }
    return error;
}
} // namespace Meow2::Rhi
