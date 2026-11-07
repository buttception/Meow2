/// @file
/// @brief D3D12 tests that need no GPU: the requirements check.

#include "Graphics/Rhi/D3D12/D3D12Requirements.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

namespace Meow2::Rhi
{
namespace
{
/// @brief An adapter that has everything.
D3D12GpuFacts capableAdapter()
{
    return {.featureLevel = 0xc100,
            .shaderModel = 0x66,
            .resourceBindingTier = 3,
            .enhancedBarriers = true,
            .unrestrictedCopyPitch = true,
            .device10 = true};
}

/// @brief Whether one of the lines mentions a word.
bool mentions(const std::vector<std::string>& lines, const std::string& word)
{
    return std::ranges::any_of(lines, [&](const std::string& line) { return line.find(word) != std::string::npos; });
}
} // namespace

TEST(D3D12Requirements, CapableAdapterPasses)
{
    EXPECT_TRUE(missingD3D12Requirements(capableAdapter()).empty());
}

TEST(D3D12Requirements, OldShaderModelIsNamedWithItsVersion)
{
    D3D12GpuFacts facts = capableAdapter();
    facts.shaderModel = 0x65;
    const std::vector<std::string> missing = missingD3D12Requirements(facts);
    ASSERT_EQ(missing.size(), 1U);
    EXPECT_TRUE(mentions(missing, "6.6"));
    EXPECT_TRUE(mentions(missing, "has 6.5"));
    EXPECT_TRUE(mentions(missing, "D3D12 folder")) << "the usual cause: the Agility runtime isn't next to the exe";
}

TEST(D3D12Requirements, EachMissingFeatureIsNamed)
{
    const D3D12GpuFacts nothing{};
    const std::vector<std::string> missing = missingD3D12Requirements(nothing);
    EXPECT_EQ(missing.size(), 6U);
    for (const char* word :
         {"feature level", "shader model", "binding tier", "enhanced barriers", "copy pitch", "ID3D12Device10"})
    {
        EXPECT_TRUE(mentions(missing, word)) << word;
    }
}
} // namespace Meow2::Rhi
