/// @file
/// @brief Meow2Renderer's screenshots: the window's own pixels, read back from the swapchain on the GPU and written
///        to a PNG file. Nothing else on the screen can end up in one.

#pragma once

#include <Meow2/Assets/Texture/Image.h>
#include <Meow2/Core/Log/Logger.h>
#include <Meow2/Graphics/Renderer/RenderDevice.h>
#include <Meow2/Graphics/Rhi/IRhiCommandList.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <vector>

namespace Meow2::RendererApp
{
/// @brief Turns a swapchain image read back from the GPU into an opaque RGBA8 image for a PNG.
///
/// Swapchains are BGRA on Windows: the channels are swapped back. Alpha is set to 255: a window has no transparency,
/// and what the swapchain's alpha holds is whatever the passes left there.
/// @param pixels Tightly packed rows, 4 bytes per pixel, as IRhiCommandList::copySwapchainToBuffer() writes them.
/// @param width  In pixels.
/// @param height In pixels.
/// @param format The swapchain's format: Bgra8Srgb is swizzled; RGBA formats are kept.
/// @return The image (sRGB values, as shown).
[[nodiscard]] Assets::Image swapchainToImage(std::span<const std::byte> pixels, std::uint32_t width,
                                             std::uint32_t height, Rhi::RhiFormat format);

/// @brief Takes a screenshot of a frame: request() asks for one, record() adds the copy at the end of the frame (after
///        the UI), finish() waits for it and writes the PNG.
///
/// One screenshot costs one wait for the GPU, a short hitch; nothing happens on frames without a request.
/// @par Thread safety
/// Not thread-safe; use on the main thread.
/// @par Lifetime
/// The RenderDevice and the Logger must outlive it.
class FrameScreenshot
{
public:
    /// @brief Ready for requests; no GPU memory until one comes.
    /// @param device The device.
    /// @param log    Receives where each screenshot went, or why it failed.
    FrameScreenshot(Renderer::RenderDevice& device, Log::Logger& log);

    /// @brief Frees a pending readback.
    ~FrameScreenshot();

    FrameScreenshot(const FrameScreenshot&) = delete;            ///< Not copyable: owns a GPU buffer.
    FrameScreenshot& operator=(const FrameScreenshot&) = delete; ///< Not copyable: owns a GPU buffer.

    /// @brief Takes a screenshot of the next frame drawn.
    /// @param path The PNG file; its folder is made if missing.
    void request(std::filesystem::path path)
    {
        m_requested = std::move(path);
    }

    /// @brief Whether the next frame should record a copy.
    /// @return True after request(), until record().
    [[nodiscard]] bool requested() const
    {
        return m_requested.has_value();
    }

    /// @brief Adds the copy of the swapchain's image to the frame, in a list of its own after the others (so after the
    ///        UI). Does nothing without a request.
    /// @param swapchain The acquired swapchain.
    /// @param lists     The frame's lists, about to be submitted; the copy's list is appended.
    void record(Rhi::IRhiSwapchain& swapchain, std::vector<Rhi::IRhiCommandList*>& lists);

    /// @brief After the frame was submitted: waits for the GPU and writes the PNG. Does nothing without a copy.
    void finish();

private:
    /// @brief Destroys the readback buffer (a backend switch, or the end).
    /// @param device The device it was made on.
    void release(Rhi::IRhiDevice& device);

    Renderer::RenderDevice& m_device;
    Log::Logger& m_log;
    std::optional<std::filesystem::path> m_requested; ///< Asked for, not recorded yet.
    std::optional<std::filesystem::path> m_recorded;  ///< Recorded this frame, to write in finish().
    Rhi::BufferHandle m_readback;
    std::uint32_t m_width = 0;
    std::uint32_t m_height = 0;
    Rhi::RhiFormat m_format = Rhi::RhiFormat::Unknown;
    Renderer::DeviceUser m_deviceUser; ///< Last: stops its callbacks before the members they use go away.
};
} // namespace Meow2::RendererApp
