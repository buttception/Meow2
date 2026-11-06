/// @file
/// @brief Tests for shader `.meta` parsing and variant keys.

#include <Meow2/Assets/Shader/ShaderMeta.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <format>
#include <set>
#include <string>
#include <vector>

namespace Meow2::Assets
{
namespace
{
constexpr std::string_view c_meta = R"toml(
id = "01900000-0000-8000-8100-000000000001"

[[entry]]
name = "vsMain"
stage = "vertex"

[[entry]]
name = "psMain"
stage = "pixel"

[defines]
USE_FOG = 1
TONEMAP = "ACES"
WITH_DEBUG = false

[options]
SKINNED = [0, 1]
QUALITY = ["LOW", "MID", "HIGH"]
)toml";

ShaderMeta parsed()
{
    Result<ShaderMeta> meta = parseShaderMeta(c_meta, "Test.hlsl.meta");
    EXPECT_TRUE(meta.has_value()) << meta.error().message;
    return *meta;
}
} // namespace

TEST(ShaderMeta, ParsesEverything)
{
    const ShaderMeta meta = parsed();
    ASSERT_EQ(meta.entries.size(), 2U);
    EXPECT_EQ(meta.entries[0], (ShaderEntry{"vsMain", Shader::ShaderStage::Vertex}));
    EXPECT_EQ(meta.entries[1], (ShaderEntry{"psMain", Shader::ShaderStage::Pixel}));
    const std::vector<Shader::ShaderDefine> defines{{"TONEMAP", "ACES"}, {"USE_FOG", "1"}, {"WITH_DEBUG", "0"}};
    EXPECT_EQ(meta.defines, defines) << "sorted by name, values as text";
    ASSERT_EQ(meta.options.size(), 2U);
    EXPECT_EQ(meta.options[0], (ShaderOption{"QUALITY", {"LOW", "MID", "HIGH"}})) << "sorted by name";
    EXPECT_EQ(meta.options[1], (ShaderOption{"SKINNED", {"0", "1"}}));
    EXPECT_EQ(variantCount(meta), 6U);
}

TEST(ShaderMeta, MinimalFile)
{
    const Result<ShaderMeta> meta =
        parseShaderMeta("[[entry]]\nname = \"csMain\"\nstage = \"compute\"\n", "Test.hlsl.meta");
    ASSERT_TRUE(meta.has_value()) << meta.error().message;
    EXPECT_EQ(variantCount(*meta), 1U) << "no options: one variant";
    const Result<ShaderVariantKey> key = variantKey(*meta, {});
    ASSERT_TRUE(key.has_value());
    EXPECT_EQ(key->index, 0U);
    EXPECT_TRUE(variantDefines(*meta, *key).empty());
}

TEST(ShaderMeta, RejectsBadFiles)
{
    const std::string entry = "[[entry]]\nname = \"m\"\nstage = \"pixel\"\n";
    const std::vector<std::string> broken{
        "not = = toml",
        "id = \"no entries\"\n",
        "[[entry]]\nname = \"m\"\n",
        "[[entry]]\nname = \"m\"\nstage = \"geometry\"\n",
        entry + "[options]\nA = []\n",
        entry + "[options]\nA = 1\n",
        entry + "[options]\nA = [1, 1]\n",
        entry + "[options]\nA = [1.5]\n",
        entry + "[defines]\nA = [1]\n",
        entry + "[defines]\nA = 1\n[options]\nA = [0, 1]\n",
    };
    for (const std::string& text : broken)
    {
        const Result<ShaderMeta> meta = parseShaderMeta(text, "Bad.meta");
        ASSERT_FALSE(meta.has_value()) << text;
        EXPECT_EQ(meta.error().code, ErrorCode::ParseError);
        EXPECT_NE(meta.error().message.find("Bad.meta"), std::string::npos) << meta.error().message;
    }

    // Each axis multiplies: 13 two-value axes are 8192 variants, over the limit.
    std::string tooMany = entry + "[options]\n";
    for (int i = 0; i < 13; ++i)
    {
        tooMany += std::format("O{} = [0, 1]\n", i);
    }
    EXPECT_FALSE(parseShaderMeta(tooMany, "Bad.meta").has_value());
}

TEST(ShaderMeta, EveryVariantHasItsOwnKeyAndDefines)
{
    const ShaderMeta meta = parsed();
    std::set<std::string> seen;
    for (std::uint32_t i = 0; i < variantCount(meta); ++i)
    {
        const std::vector<Shader::ShaderDefine> defines = variantDefines(meta, {i});
        std::vector<ShaderOptionChoice> choices;
        std::string text;
        for (const Shader::ShaderDefine& define : defines)
        {
            text += std::format("{}={};", define.name, define.value);
            if (define.name == "QUALITY" || define.name == "SKINNED")
            {
                choices.push_back({define.name, define.value});
            }
        }
        EXPECT_TRUE(seen.insert(text).second) << "variant " << i << " repeats " << text;
        const Result<ShaderVariantKey> key = variantKey(meta, choices);
        ASSERT_TRUE(key.has_value());
        EXPECT_EQ(key->index, i) << "defines → key round trip";
    }
    EXPECT_EQ(seen.size(), 6U);
}

TEST(ShaderMeta, ChoicesAndDefaults)
{
    const ShaderMeta meta = parsed();
    const std::vector<ShaderOptionChoice> none;
    EXPECT_EQ(variantKey(meta, none)->index, 0U) << "every option at its first value";

    const std::vector<ShaderOptionChoice> skinned{{"SKINNED", "1"}};
    const Result<ShaderVariantKey> key = variantKey(meta, skinned);
    ASSERT_TRUE(key.has_value());
    const std::vector<Shader::ShaderDefine> defines = variantDefines(meta, *key);
    EXPECT_NE(std::find(defines.begin(), defines.end(), Shader::ShaderDefine{"SKINNED", "1"}), defines.end());
    EXPECT_NE(std::find(defines.begin(), defines.end(), Shader::ShaderDefine{"QUALITY", "LOW"}), defines.end())
        << "unnamed options take their first value";
    EXPECT_NE(std::find(defines.begin(), defines.end(), Shader::ShaderDefine{"USE_FOG", "1"}), defines.end())
        << "defines are always there";

    const std::vector<ShaderOptionChoice> typoName{{"SKINED", "1"}};
    EXPECT_EQ(variantKey(meta, typoName).error().code, ErrorCode::InvalidArgument);
    const std::vector<ShaderOptionChoice> typoValue{{"QUALITY", "ULTRA"}};
    EXPECT_EQ(variantKey(meta, typoValue).error().code, ErrorCode::InvalidArgument);
}

TEST(ShaderMeta, FileOrderDoesNotChangeKeys)
{
    const std::string a = "[[entry]]\nname = \"m\"\nstage = \"pixel\"\n[options]\nA = [0, 1]\nB = [0, 1]\n";
    const std::string b = "[[entry]]\nname = \"m\"\nstage = \"pixel\"\n[options]\nB = [0, 1]\nA = [0, 1]\n";
    const std::vector<ShaderOptionChoice> choice{{"B", "1"}};
    EXPECT_EQ(variantKey(*parseShaderMeta(a, "a"), choice)->index, variantKey(*parseShaderMeta(b, "b"), choice)->index);
}
} // namespace Meow2::Assets
