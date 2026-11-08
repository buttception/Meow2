/// @file
/// @brief glTF 2.0 import: a `.gltf` or `.glb` file to meshes, materials, images and a node tree on the CPU, in the
///        engine's left-handed coordinates.

#pragma once

#include <Meow2/Assets/Texture/Image.h>
#include <Meow2/Core/Log/Logger.h>
#include <Meow2/Core/Result.h>
#include <Meow2/Math/Bounds.h>
#include <Meow2/Math/Color.h>
#include <Meow2/Math/Transform.h>
#include <Meow2/Math/Types.h>
#include <Meow2/Platform/FileSystem/FileSystem.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Meow2::Assets
{
/// @brief How a material treats alpha (glTF `alphaMode`).
enum class AlphaMode : std::uint8_t
{
    Opaque, ///< Alpha is ignored.
    Mask,   ///< Pixels below ImportedMaterial::alphaCutoff are discarded.
    Blend   ///< Blended over what's behind.
};

/// @brief What a sampler does outside 0..1 (glTF `wrapS`, `wrapT`).
enum class ImportedWrap : std::uint8_t
{
    Repeat, ///< Wraps around.
    Clamp,  ///< Keeps the edge texel.
    Mirror  ///< Wraps around, flipping every other repeat.
};

/// @brief How a texture is sampled (glTF `sampler`). Plain data.
struct ImportedSampler
{
    bool linear = true;                        ///< Linear filtering; false for nearest (pixel art).
    bool mipmaps = true;                       ///< Whether minification uses mipmaps.
    ImportedWrap wrapU = ImportedWrap::Repeat; ///< Across.
    ImportedWrap wrapV = ImportedWrap::Repeat; ///< Down.
};

/// @brief One texture slot of a material: an image and how to sample it. Plain data.
struct ImportedTexture
{
    std::int32_t image = -1;   ///< Index into ImportedModel::images, or -1 for none.
    std::int32_t sampler = -1; ///< Index into ImportedModel::samplers, or -1 for the default (linear, repeat).
};

/// @brief A material's metallic-roughness parameters (glTF core). Plain data.
///
/// Factors multiply the textures.
struct ImportedMaterial
{
    std::string name{};                                  ///< From the file; may be empty.
    Math::Color baseColorFactor{1.0F, 1.0F, 1.0F, 1.0F}; ///< Linear RGBA.
    ImportedTexture baseColor{};                         ///< sRGB colour and alpha.
    float metallicFactor = 1.0F;                         ///< 0..1.
    float roughnessFactor = 1.0F;                        ///< 0..1.
    ImportedTexture metallicRoughness{};                 ///< Linear: roughness in G, metallic in B.
    ImportedTexture normal{};                            ///< Linear tangent-space normal, +Y up (OpenGL style).
    float normalScale = 1.0F;                            ///< Scales the normal map's X and Y.
    ImportedTexture occlusion{};                         ///< Linear: occlusion in R.
    float occlusionStrength = 1.0F;                      ///< 0..1.
    Math::Color emissiveFactor{0.0F, 0.0F, 0.0F, 1.0F};  ///< Linear RGB; alpha unused.
    ImportedTexture emissive{};                          ///< sRGB emissive colour.
    AlphaMode alphaMode = AlphaMode::Opaque;             ///< How alpha is used.
    float alphaCutoff = 0.5F;                            ///< For AlphaMode::Mask.
    bool doubleSided = false;                            ///< Draw back faces too.
};

/// @brief One drawable part of a mesh: triangles with one material. Plain data.
///
/// Converted to the engine's left-handed space: X negated and the triangle winding swapped, so front faces are
/// clockwise as the engine expects.
struct ImportedPrimitive
{
    std::vector<Math::Vec3> positions{}; ///< Model-space positions.
    std::vector<Math::Vec3> normals{};   ///< Unit normals; computed from the triangles when the file has none.
    std::vector<Math::Vec2> uvs{};       ///< First texture coordinate set; zeros when the file has none.
    /// @brief Tangents: xyz along +U, w the bitangent's sign (bitangent = cross(normal, tangent) × w). From the file,
    ///        or computed from the UVs when it has none.
    std::vector<Math::Vec4> tangents{};
    std::vector<Math::Color> colors{};    ///< Linear vertex colours (COLOR_0); empty when the file has none (white).
    std::vector<std::uint32_t> indices{}; ///< Three per triangle, clockwise.
    std::int32_t material = -1;           ///< Index into ImportedModel::materials, or -1 for the default material.
    Math::Aabb bounds{};                  ///< Box around the positions.
};

/// @brief A glTF mesh: its primitives share a node and a transform. Plain data.
struct ImportedMesh
{
    std::string name{};                          ///< From the file; may be empty.
    std::vector<ImportedPrimitive> primitives{}; ///< At least one.
};

/// @brief A node of the scene tree. Plain data.
struct ImportedNode
{
    std::string name{};       ///< From the file; may be empty.
    Math::Transform local{};  ///< Relative to the parent, left-handed.
    std::int32_t parent = -1; ///< Index into ImportedModel::nodes, always lower than this node's; -1 for a root.
    std::int32_t mesh = -1;   ///< Index into ImportedModel::meshes, or -1.
};

/// @brief Everything imported from one glTF file. Plain data.
struct ImportedModel
{
    std::vector<ImportedMesh> meshes{};        ///< Every mesh with at least one triangle primitive.
    std::vector<ImportedMaterial> materials{}; ///< Every material.
    std::vector<Image> images{};               ///< Decoded images, RGBA8.
    std::vector<ImportedSampler> samplers{};   ///< The file's samplers.
    std::vector<ImportedNode> nodes{};         ///< The default scene's nodes, parents before children.
    std::vector<std::string> warnings{};       ///< What was skipped (points and lines, unknown extensions, …).
};

/// @brief Imports a glTF 2.0 file, `.gltf` with its buffers and images or a single `.glb`.
///
/// Reads every file through the virtual file system, so paths are virtual (`samples/Box/glTF/Box.gltf`) and buffers
/// and images are found next to the file. Data URIs work too. Only the default scene (or the first one) is imported.
///
/// glTF is right-handed with +Y up and +Z forward, the engine left-handed with the same axes: X is negated and every
/// triangle's winding swapped (techstack.md §10).
/// @param files The file system to read through.
/// @param path  Virtual path of the `.gltf` or `.glb` file.
/// @param log   Receives the warnings too.
/// @return The model; ErrorCode::NotFound for a missing file, ParseError for a broken one, Unsupported for a file that
///         requires an extension the engine doesn't have (Draco, Basis Universal, meshopt).
/// @note Thread-safe; memory is counted under MemTag::Assets.
[[nodiscard]] Result<ImportedModel> importGltf(const FileSystem::FileSystem& files, std::string_view path,
                                               Log::Logger& log);
} // namespace Meow2::Assets
