#pragma once

/// @file
/// @brief ShaderAsset: a cooked shader (`.m2shader`), the compiled object every app loads. Needs no compiler.
///
/// A `.m2shader` holds the bytecode of every variant, entry point and target, plus the shader's `.meta` settings, so a
/// variant can be chosen by name with no source around:
/// @code
/// Meow2::Result<Meow2::Assets::AssetHandle<Meow2::Assets::ShaderAsset>> shader =
///     assets.load<Meow2::Assets::ShaderAsset>(id);
/// const std::vector<Meow2::Assets::ShaderOptionChoice> choices{{"SKINNED", "1"}};
/// std::shared_ptr<const Meow2::Assets::ShaderAsset> loaded = shader->get();
/// Meow2::Result<Meow2::Assets::ShaderVariantKey> key = Meow2::Assets::variantKey(loaded->meta(), choices);
/// Meow2::Result<std::span<const std::byte>> code =
///     loaded->bytecode(*key, "psMain", Meow2::Shader::ShaderTarget::Spirv);
/// @endcode
/// @see techstack.md §12, docs/wiki/Shaders.md

#include <Meow2/Assets/AssetManager.h>
#include <Meow2/Assets/Shader/ShaderMeta.h>
#include <Meow2/Core/Result.h>
#include <Meow2/Graphics/Shader/ShaderCompiler.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Meow2::Assets
{
/// @brief The `.m2shader` format version. Bump it when the layout changes: old files then count as out of date.
inline constexpr std::uint32_t c_shaderFileVersion = 1;

/// @brief One compiled combination in a `.m2shader`. Plain data.
struct CookedShaderBlob
{
    std::uint32_t variant = 0;                                 ///< ShaderVariantKey::index.
    std::uint32_t entry = 0;                                   ///< Index into ShaderMeta::entries.
    Shader::ShaderTarget target = Shader::ShaderTarget::Spirv; ///< What it's compiled for.
    std::vector<std::byte> bytecode;                           ///< The DXIL container or SPIR-V module.
};

/// @brief The contents of a `.m2shader`. Plain data.
struct CookedShader
{
    std::uint64_t cacheKey = 0;            ///< Hash of everything the bytecode came from.
    std::string metaText;                  ///< The `.meta` file's text.
    std::vector<std::string> dependencies; ///< Virtual paths of every included file.
    std::vector<CookedShaderBlob> blobs;   ///< All compiled combinations.
};

/// @brief Writes a cooked shader to bytes. The layout is little-endian (every target platform is).
/// @param shader The contents.
/// @return The file's bytes.
[[nodiscard]] std::vector<std::byte> serializeShader(const CookedShader& shader);

/// @brief Reads a cooked shader from bytes, with every length checked.
/// @param bytes The file's bytes.
/// @return The contents; ErrorCode::ParseError for a corrupt file; ErrorCode::Unsupported for another format version.
[[nodiscard]] Result<CookedShader> deserializeShader(std::span<const std::byte> bytes);

/// @brief Where an asset's cooked shader lives: `cooked/shaders/<id>.m2shader`, in the `cooked` mount.
/// @param id The asset.
/// @return The virtual path.
[[nodiscard]] std::string cookedShaderPath(AssetId id);

/// @brief A loaded shader: its settings and all its bytecode.
/// @par Thread safety
/// Immutable after loading: any thread may read it.
class ShaderAsset
{
public:
    /// @brief Loads a shader from the `cooked` mount (AssetManager::load() calls this).
    /// @param assets The manager, for its file system.
    /// @param id     The asset.
    /// @return The shader; ErrorCode::NotFound if it hasn't been cooked; errors of deserializeShader() and
    ///         parseShaderMeta() otherwise.
    [[nodiscard]] static Result<ShaderAsset> loadAsset(AssetManager& assets, AssetId id);

    /// @brief Builds a shader from a cooked file's contents.
    /// @param cooked The contents.
    /// @return The shader, or the error from parsing its `.meta` text.
    [[nodiscard]] static Result<ShaderAsset> fromCooked(CookedShader cooked);

    /// @brief The shader's settings, for variantKey().
    /// @return The settings.
    [[nodiscard]] const ShaderMeta& meta() const
    {
        return m_meta;
    }

    /// @brief The bytecode of one variant, entry point and target.
    /// @param key    The variant.
    /// @param entry  The entry point's name.
    /// @param target The target.
    /// @return The bytecode, valid as long as this asset; ErrorCode::NotFound if that combination wasn't cooked, e.g.
    ///         DXIL in a Linux cook.
    [[nodiscard]] Result<std::span<const std::byte>> bytecode(ShaderVariantKey key, std::string_view entry,
                                                              Shader::ShaderTarget target) const;

private:
    ShaderAsset(CookedShader cooked, ShaderMeta meta);

    CookedShader m_cooked;
    ShaderMeta m_meta;
};
} // namespace Meow2::Assets
