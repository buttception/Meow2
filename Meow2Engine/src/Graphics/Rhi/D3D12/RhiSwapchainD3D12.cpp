/// @file
/// @brief The D3D12 swapchain: flip model, three images, sRGB views.

#include "Graphics/Rhi/D3D12/RhiSwapchainD3D12.h"

#include "Graphics/Rhi/D3D12/RhiDeviceD3D12.h"
#include "Meow2/Core/Assert.h"

#include <format>
#include <utility>

namespace Meow2::Rhi
{
namespace
{
// One more than the frames in flight: the image a new frame gets was last used c_imageCount frames ago, which
// beginFrame() has already waited for.
constexpr std::uint32_t c_imageCount = c_maxFramesInFlight + 1;
constexpr DXGI_FORMAT c_imageFormat = DXGI_FORMAT_B8G8R8A8_UNORM;
constexpr DXGI_FORMAT c_viewFormat = DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
} // namespace

RhiSwapchainD3D12::RhiSwapchainD3D12(RhiDeviceD3D12& device, const RhiSwapchainDesc& desc)
    : m_device(device)
    , m_window(static_cast<HWND>(desc.window.window))
    , m_name(desc.name)
    , m_vsync(desc.vsync)
{
    m_device.swapchainCreated();
}

Result<std::unique_ptr<IRhiSwapchain>> RhiSwapchainD3D12::create(RhiDeviceD3D12& device, const RhiSwapchainDesc& desc)
{
    if (desc.window.type != Platform::NativeWindowType::Win32 || desc.window.window == nullptr)
    {
        return fail(ErrorCode::Unsupported, "D3D12 needs a Win32 window (the offscreen driver has none)");
    }
    auto swapchain = std::make_unique<RhiSwapchainD3D12>(device, desc);
    if (Result<void> built = swapchain->build(desc.width, desc.height); !built)
    {
        return Unexpected<Error>{built.error()};
    }
    return std::unique_ptr<IRhiSwapchain>{std::move(swapchain)};
}

RhiSwapchainD3D12::~RhiSwapchainD3D12()
{
    MEOW2_ASSERT(!m_device.inFrame(), "A swapchain can't be destroyed while a frame is recording");
    m_device.waitIdle();
    releaseImages();
    m_swapchain.Reset();
    m_device.swapchainDestroyed();
}

void RhiSwapchainD3D12::releaseImages()
{
    for (const std::uint32_t view : m_views)
    {
        m_device.releaseRtv(view);
    }
    m_views.clear();
    m_images.clear();
}

Result<void> RhiSwapchainD3D12::resize(std::uint32_t width, std::uint32_t height)
{
    MEOW2_ASSERT(!m_device.inFrame(), "A swapchain can't be resized while a frame is recording");
    m_device.waitIdle();
    return build(width, height);
}

Result<void> RhiSwapchainD3D12::build(std::uint32_t width, std::uint32_t height)
{
    releaseImages();
    if (width == 0 || height == 0)
    {
        // A minimised window has no images until it's restored. Resizing DXGI to 0 would mean "the window's size".
        m_width = 0;
        m_height = 0;
        return {};
    }
    const UINT flags = m_device.tearingSupported() ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0U;
    if (m_swapchain == nullptr)
    {
        const DXGI_SWAP_CHAIN_DESC1 desc{.Width = width,
                                         .Height = height,
                                         .Format = c_imageFormat,
                                         .SampleDesc = {.Count = 1, .Quality = 0},
                                         .BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT,
                                         .BufferCount = c_imageCount,
                                         .Scaling = DXGI_SCALING_STRETCH,
                                         .SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD,
                                         .AlphaMode = DXGI_ALPHA_MODE_IGNORE,
                                         .Flags = flags};
        Microsoft::WRL::ComPtr<IDXGISwapChain1> swapchain;
        if (const HRESULT result = m_device.factory()->CreateSwapChainForHwnd(m_device.queue(), m_window, &desc,
                                                                              nullptr, nullptr, &swapchain);
            FAILED(result))
        {
            return fail(ErrorCode::Unsupported, std::format("CreateSwapChainForHwnd: {}", hresultName(result)));
        }
        // Alt+Enter is SDL's to handle (borderless fullscreen), not DXGI's exclusive mode.
        m_device.factory()->MakeWindowAssociation(m_window, DXGI_MWA_NO_ALT_ENTER);
        if (const HRESULT result = swapchain.As(&m_swapchain); FAILED(result))
        {
            return fail(ErrorCode::Unsupported, std::format("IDXGISwapChain3: {}", hresultName(result)));
        }
    }
    else if (const HRESULT result = m_swapchain->ResizeBuffers(c_imageCount, width, height, c_imageFormat, flags);
             FAILED(result))
    {
        return fail(ErrorCode::Unknown, std::format("ResizeBuffers: {}", hresultName(result)));
    }

    for (std::uint32_t i = 0; i < c_imageCount; ++i)
    {
        Microsoft::WRL::ComPtr<ID3D12Resource> image;
        if (const HRESULT result = m_swapchain->GetBuffer(i, IID_PPV_ARGS(&image)); FAILED(result))
        {
            return fail(ErrorCode::Unknown, std::format("Swapchain image: {}", hresultName(result)));
        }
        image->SetName(wideName(std::format("{} image {}", m_name, i)).c_str());
        std::uint32_t view = 0;
        if (!m_device.allocateRtv(view))
        {
            return fail(ErrorCode::OutOfMemory, "The render target view heap is full");
        }
        const D3D12_RENDER_TARGET_VIEW_DESC viewDesc{.Format = c_viewFormat,
                                                     .ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D,
                                                     .Texture2D = {.MipSlice = 0, .PlaneSlice = 0}};
        m_device.device()->CreateRenderTargetView(image.Get(), &viewDesc, m_device.rtvHeap().cpu(view));
        m_images.push_back(std::move(image));
        m_views.push_back(view);
    }
    m_width = width;
    m_height = height;
    return {};
}

bool RhiSwapchainD3D12::acquire()
{
    MEOW2_ASSERT(!m_acquired, "acquire() was already called this frame");
    if (m_images.empty())
    {
        return false;
    }
    m_imageIndex = m_swapchain->GetCurrentBackBufferIndex();
    m_acquired = true;
    return true;
}

HRESULT RhiSwapchainD3D12::present()
{
    // Without vsync, tearing (where allowed) shows the frame at once instead of waiting for the next refresh.
    const UINT flags = !m_vsync && m_device.tearingSupported() ? DXGI_PRESENT_ALLOW_TEARING : 0U;
    return m_swapchain->Present(m_vsync ? 1U : 0U, flags);
}

D3D12_CPU_DESCRIPTOR_HANDLE RhiSwapchainD3D12::view() const
{
    return m_device.rtvHeap().cpu(m_views[m_imageIndex]);
}
} // namespace Meow2::Rhi
