#pragma once

/// @file
/// @brief What a shader's `.meta` file declares: entry points, macros, and the option axes its permutations come from.
///
/// All of a shader's compile settings are in one place, next to the source:
/// @code{.toml}
/// id = "01900000-0000-8000-8100-000000000001"
///
/// [[entry]]
/// name = "vsMain"
/// stage = "vertex"
///
/// [[entry]]
/// name = "psMain"
/// stage = "pixel"
///
/// [defines]        # always set
/// USE_FOG = 1
///
/// [options]        # one permutation per combination: here 2 × 3 = 6 variants
/// SKINNED = [0, 1]
/// QUALITY = ["LOW", "MID", "HIGH"]
/// @endcode
/// Code picks a variant by naming values; options it leaves out take their first value:
/// @code
/// const std::vector<Meow2::Assets::ShaderOptionChoice> choices{{"SKINNED", "1"}};
/// Meow2::Result<Meow2::Assets::ShaderVariantKey> key = Meow2::Assets::variantKey(meta, choices);
/// @endcode
/// A misspelt option or value is an error, not a silently different shader.
/// @see techstack.md §12, docs/wiki/Shaders.md

#include <Meow2/Core/Result.h>
#include <Meow2/Graphics/Shader/ShaderCompiler.h>

#include <compare>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Meow2::Assets
{
/// @brief More variants than this in one shader log a warning: each option multiplies the count.
inline constexpr std::uint64_t c_shaderVariantWarning = 256;

/// @brief More variants than this are rejected: a typo'd axis shouldn't start a cook that never ends.
inline constexpr std::uint64_t c_shaderVariantLimit = 4096;

/// @brief One entry point. Plain data.
struct ShaderEntry
{
    std::string name;                                       ///< The HLSL function.
    Shader::ShaderStage stage = Shader::ShaderStage::Pixel; ///< Its stage.

    /// @brief Entries are equal when name and stage are.
    friend bool operator==(const ShaderEntry&, const ShaderEntry&) = default;
};

/// @brief One permutation axis: a macro and the values it can take. Plain data.
struct ShaderOption
{
    std::string name;                ///< The macro.
    std::vector<std::string> values; ///< Its values as text; the first is the default.

    /// @brief Options are equal when name and values are.
    friend bool operator==(const ShaderOption&, const ShaderOption&) = default;
};

/// @brief The settings of a shader asset. Plain data.
struct ShaderMeta
{
    std::vector<ShaderEntry> entries;          ///< Its entry points, at least one.
    std::vector<Shader::ShaderDefine> defines; ///< Macros set in every variant, sorted by name.
    std::vector<ShaderOption> options; ///< Permutation axes, sorted by name, so the file's order doesn't matter.
};

/// @brief Picks one value of one option. Plain data.
struct ShaderOptionChoice
{
    std::string name;  ///< The option.
    std::string value; ///< One of its declared values.
};

/// @brief Names one variant of a shader: its position among all combinations of the options. Plain data.
struct ShaderVariantKey
{
    std::uint32_t index = 0; ///< 0 .. variantCount() - 1; 0 is the variant with every option at its first value.

    /// @brief Keys are ordered by index.
    friend auto operator<=>(const ShaderVariantKey&, const ShaderVariantKey&) = default;
};

/// @brief Parses a shader's `.meta` text.
/// @param text The file's text.
/// @param path Its path, for error messages.
/// @return The settings; ErrorCode::ParseError naming the problem otherwise (bad TOML, no entry point, an unknown
///         stage, an empty option, a repeated value, a macro set both ways, more than c_shaderVariantLimit variants).
[[nodiscard]] Result<ShaderMeta> parseShaderMeta(std::string_view text, std::string_view path);

/// @brief How many variants a shader has.
/// @param meta The shader's settings.
/// @return The product of the options' value counts; 1 with no options.
[[nodiscard]] std::uint64_t variantCount(const ShaderMeta& meta);

/// @brief The variant for some option values. Options not named take their first value.
/// @param meta    The shader's settings.
/// @param choices The chosen values.
/// @return The key; ErrorCode::InvalidArgument for an option or value the shader doesn't declare.
[[nodiscard]] Result<ShaderVariantKey> variantKey(const ShaderMeta& meta, std::span<const ShaderOptionChoice> choices);

/// @brief The macros of a variant: the shader's defines plus one per option.
/// @param meta The shader's settings.
/// @param key  The variant.
/// @return The macros, for ShaderCompileDesc::defines.
/// @pre `key.index < variantCount(meta)` (asserted).
[[nodiscard]] std::vector<Shader::ShaderDefine> variantDefines(const ShaderMeta& meta, ShaderVariantKey key);
} // namespace Meow2::Assets
