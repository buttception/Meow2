/// @file
/// @brief The D3D12 requirements check.

#include "Graphics/Rhi/D3D12/D3D12Requirements.h"

#include <format>

namespace Meow2::Rhi
{
std::vector<std::string> missingD3D12Requirements(const D3D12GpuFacts& facts)
{
    std::vector<std::string> missing;
    if (facts.featureLevel < c_requiredFeatureLevel)
    {
        missing.emplace_back("feature level 12_0");
    }
    if (facts.shaderModel < c_requiredShaderModel)
    {
        // Usually the Agility SDK's D3D12/ folder is missing next to the exe, or the driver is old.
        missing.push_back(std::format("shader model 6.6 (has {}.{}): update the GPU driver, and check that the "
                                      "D3D12 folder is next to the program",
                                      facts.shaderModel >> 4U, facts.shaderModel & 0xFU));
    }
    if (facts.resourceBindingTier < 3)
    {
        missing.emplace_back("resource binding tier 3 (bindless descriptor heaps)");
    }
    if (!facts.enhancedBarriers)
    {
        missing.emplace_back("enhanced barriers: update the GPU driver");
    }
    if (!facts.unrestrictedCopyPitch)
    {
        missing.emplace_back("unrestricted buffer-texture copy pitch: update the GPU driver");
    }
    if (!facts.device10)
    {
        missing.emplace_back("ID3D12Device10: check that the D3D12 folder is next to the program");
    }
    return missing;
}
} // namespace Meow2::Rhi
