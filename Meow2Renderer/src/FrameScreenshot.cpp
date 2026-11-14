/// @file
/// @brief FrameScreenshot (FrameScreenshot.h).

#include "FrameScreenshot.h"

#include <Meow2/Graphics/Rhi/IRhiSwapchain.h>

#include <fstream>
#include <system_error>

namespace Meow2::RendererApp
{
Assets::Image swapchainToImage(std::span<const std::byte> pixels, std::uint32_t width, std::uint32_t height,
                               Rhi::RhiFormat format)
{
    Assets::Image image{.width = width, .height = height, .pixels = {}};
    const std::size_t bytes = std::size_t{width} * height * 4;
    image.pixels.assign(pixels.begin(), pixels.begin() + static_cast<std::ptrdiff_t>(std::min(bytes, pixels.size())));
    image.pixels.resize(bytes);
    const bool bgra = format == Rhi::RhiFormat::Bgra8Srgb;
    for (std::size_t i = 0; i + 3 < image.pixels.size(); i += 4)
    {
        if (bgra)
        {
            std::swap(image.pixels[i], image.pixels[i + 2]);
        }
        image.pixels[i + 3] = std::byte{255};
    }
    return image;
}

FrameScreenshot::FrameScreenshot(Renderer::RenderDevice& device, Log::Logger& log)
    : m_device(device)
    , m_log(log)
{
    // The buffer is made per screenshot, so a new device has nothing to restore.
    m_deviceUser = device.addUser(
        "FrameScreenshot", [this](Rhi::IRhiDevice& old) { release(old); },
        [](Rhi::IRhiDevice&) -> Result<void> { return {}; });
}

FrameScreenshot::~FrameScreenshot()
{
    if (m_device.hasDevice())
    {
        release(m_device.device());
    }
}

void FrameScreenshot::release(Rhi::IRhiDevice& device)
{
    if (m_readback.isSet())
    {
        device.destroy(m_readback);
        m_readback = {};
    }
    m_recorded.reset();
}

void FrameScreenshot::record(Rhi::IRhiSwapchain& swapchain, std::vector<Rhi::IRhiCommandList*>& lists)
{
    if (!m_requested)
    {
        return;
    }
    Rhi::IRhiDevice& device = m_device.device();
    const std::uint32_t width = swapchain.width();
    const std::uint32_t height = swapchain.height();
    if (m_readback.isSet())
    {
        device.destroy(m_readback);
        m_readback = {};
    }
    Result<Rhi::BufferHandle> buffer = device.createBuffer({.size = std::uint64_t{width} * height * 4,
                                                            .memory = Rhi::RhiMemory::Readback,
                                                            .tag = Memory::MemTag::Renderer,
                                                            .name = "Screenshot"});
    // A list of its own, after every other: it copies what the UI left on the image. Thread index 0 is the main thread.
    Result<Rhi::IRhiCommandList*> list = buffer ? device.commandList(0) : Result<Rhi::IRhiCommandList*>{nullptr};
    if (!buffer || !list || *list == nullptr)
    {
        MEOW2_LOG_ERROR(m_log, "Screenshot not taken: {}", !buffer ? buffer.error() : list.error());
        if (buffer)
        {
            device.destroy(*buffer);
        }
        m_requested.reset();
        return;
    }
    m_readback = *buffer;
    (*list)->copySwapchainToBuffer(swapchain, m_readback);
    (*list)->barrier(m_readback, Rhi::ResourceState::CopyTarget, Rhi::ResourceState::HostRead);
    lists.push_back(*list);
    m_width = width;
    m_height = height;
    m_format = swapchain.format();
    m_recorded = std::move(m_requested);
    m_requested.reset();
}

void FrameScreenshot::finish()
{
    if (!m_recorded || !m_readback.isSet())
    {
        return;
    }
    Rhi::IRhiDevice& device = m_device.device();
    device.waitIdle(); // the copy has run: one hitch, for one screenshot
    const Assets::Image image = swapchainToImage(device.mappedMemory(m_readback), m_width, m_height, m_format);
    device.destroy(m_readback);
    m_readback = {};
    const std::filesystem::path path = *m_recorded;
    m_recorded.reset();

    const Result<std::vector<std::byte>> png = Assets::encodePng(image);
    if (!png)
    {
        MEOW2_LOG_ERROR(m_log, "Screenshot not written: {}", png.error());
        return;
    }
    std::error_code error;
    if (path.has_parent_path())
    {
        std::filesystem::create_directories(path.parent_path(), error);
    }
    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char*>(png->data()), static_cast<std::streamsize>(png->size()));
    if (!file)
    {
        MEOW2_LOG_ERROR(m_log, "Screenshot not written to {}", path.string());
        return;
    }
    MEOW2_LOG_INFO(m_log, "Screenshot: {} ({}x{})", path.string(), m_width, m_height);
}
} // namespace Meow2::RendererApp
