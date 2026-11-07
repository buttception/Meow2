/// @file
/// @brief Plain data shared by the RHI interfaces: backends, descriptions and GPU information.

#pragma once

#include <Meow2/Platform/Window/Window.h>

#include <cstdint>
#include <string>

namespace Meow2::Rhi
{
/// @brief Frames the CPU may record ahead of the GPU. More adds latency, fewer makes the CPU wait for the GPU.
inline constexpr std::uint32_t c_maxFramesInFlight = 2;

/// @brief The graphics APIs behind the RHI, chosen at runtime (techstack.md §11).
enum class RhiBackend : std::uint8_t
{
    Vulkan, ///< Vulkan 1.3; every platform.
    D3D12   ///< Direct3D 12; Windows only.
};

/// @brief Pixel formats a swapchain can use.
enum class RhiFormat : std::uint8_t
{
    Unknown,   ///< Not set.
    Bgra8Srgb, ///< 8-bit BGRA, sRGB-encoded on write.
    Rgba8Srgb  ///< 8-bit RGBA, sRGB-encoded on write.
};

/// @brief What to create a device with. Plain data.
struct RhiDeviceDesc
{
    RhiBackend backend = RhiBackend::Vulkan; ///< API to use.
    std::string appName = "Meow2";           ///< Reported to the driver and to tools such as RenderDoc.
    bool validation = false;                 ///< Enable the API's validation layer, if it's installed.
    bool presentation = true;                ///< False for a device that never opens a swapchain (headless).
};

/// @brief What to create a swapchain with. Plain data.
struct RhiSwapchainDesc
{
    Platform::NativeWindowHandle window{}; ///< The window to draw to. Not kept after creation.
    std::uint32_t width = 0;               ///< Drawable width in pixels; 0 while minimised.
    std::uint32_t height = 0;              ///< Drawable height in pixels; 0 while minimised.
    bool vsync = true;                     ///< Wait for the display; false shows frames as soon as they're ready.
    std::string name = "Swapchain";        ///< Shown in debugging tools.
};

/// @brief The GPU a device runs on. Plain data.
struct RhiAdapterInfo
{
    std::string name;       ///< Product name.
    std::string apiVersion; ///< API version the driver supports, e.g. `1.3.280`.
    bool discrete = false;  ///< A dedicated GPU rather than one built into the CPU.
    bool software = false;  ///< Runs on the CPU, e.g. Mesa's lavapipe.
};
} // namespace Meow2::Rhi
