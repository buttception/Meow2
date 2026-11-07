/// @file
/// @brief What the D3D12 backend needs from a GPU, and the check that names what's missing.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace Meow2::Rhi
{
/// @brief The lowest shader model the engine's shaders use: 6.6, for `ResourceDescriptorHeap` (techstack.md §11).
inline constexpr std::uint32_t c_requiredShaderModel = 0x66;

/// @brief The lowest feature level: 12_0, encoded as D3D_FEATURE_LEVEL is (0xc000).
inline constexpr std::uint32_t c_requiredFeatureLevel = 0xc000;

/// @brief What one adapter offers, in the terms the requirements care about. Plain data.
struct D3D12GpuFacts
{
    std::uint32_t featureLevel = 0;        ///< Highest D3D_FEATURE_LEVEL, e.g. 0xc100 for 12_1.
    std::uint32_t shaderModel = 0;         ///< Highest D3D_SHADER_MODEL, e.g. 0x66 for 6.6.
    std::uint32_t resourceBindingTier = 0; ///< D3D12_RESOURCE_BINDING_TIER, 1..3.
    bool enhancedBarriers = false;         ///< The barrier API that matches Vulkan's synchronization2.
    bool unrestrictedCopyPitch = false;    ///< Buffer ↔ texture copies with tightly packed rows, as on Vulkan.
    bool device10 = false;                 ///< ID3D12Device10: creating resources in an initial layout.
};

/// @brief Compares an adapter with the engine's requirements.
/// @param facts What the adapter offers.
/// @return One readable line per missing thing; empty if the adapter qualifies.
[[nodiscard]] std::vector<std::string> missingD3D12Requirements(const D3D12GpuFacts& facts);
} // namespace Meow2::Rhi
