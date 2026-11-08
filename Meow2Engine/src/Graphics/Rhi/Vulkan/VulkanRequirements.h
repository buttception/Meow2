/// @file
/// @brief What the engine needs from a GPU, checked without touching Vulkan so it can be tested with no GPU.

#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Meow2::Rhi
{
/// @brief The Vulkan version the engine needs: major, minor.
inline constexpr std::array<std::uint32_t, 2> c_requiredVulkanVersion{1, 3};

/// @brief Device extensions every GPU must have. VK_KHR_swapchain is added when a device presents.
inline constexpr std::array<std::string_view, 1> c_requiredVulkanExtensions{"VK_EXT_mutable_descriptor_type"};

/// @brief What one GPU offers, in the terms the requirements care about. Plain data.
struct VulkanGpuFacts
{
    std::uint32_t apiMajor = 0;             ///< Vulkan major version.
    std::uint32_t apiMinor = 0;             ///< Vulkan minor version.
    std::vector<std::string> extensions;    ///< Device extensions.
    bool graphicsAndComputeQueue = false;   ///< One queue family does both.
    bool dynamicRendering = false;          ///< Rendering without render pass objects.
    bool synchronization2 = false;          ///< The newer barrier and submit calls.
    bool demoteToHelperInvocation = false;  ///< HLSL discard keeps derivatives working (core in 1.3, but optional).
    bool storageImageWithoutFormat = false; ///< Storage images written without a declared format (HLSL RWTexture2D).
    bool textureCompressionBc = false;      ///< BC1–BC7 block-compressed textures.
    bool timelineSemaphore = false;         ///< Semaphores with a counter, used to pace frames.
    bool bufferDeviceAddress = false;       ///< Buffers read through GPU pointers.
    bool bindlessDescriptors = false;       ///< Every descriptor-indexing feature the bindless heap needs.
    bool mutableDescriptorType = false;     ///< One heap holding every descriptor type (techstack.md §11).
    bool multiDrawIndirect = false;         ///< Several indirect draws in one call.
    bool drawIndirectCount = false;         ///< The GPU decides how many indirect draws run.
    bool shaderInt64 = false;               ///< 64-bit integers in shaders.
};

/// @brief Compares a GPU with the engine's requirements.
/// @param facts        What the GPU offers.
/// @param presentation Whether it must also show images in a window.
/// @return One readable line per missing thing; empty if the GPU qualifies.
[[nodiscard]] std::vector<std::string> missingVulkanRequirements(const VulkanGpuFacts& facts, bool presentation);
} // namespace Meow2::Rhi
