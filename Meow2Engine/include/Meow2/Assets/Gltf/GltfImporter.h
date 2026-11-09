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
    std::int32_t image = -1;       ///< Index into ImportedModel::images, or -1 for none.
    std::int32_t sampler = -1;     ///< Index into ImportedModel::samplers, or -1 for the default (linear, repeat).
    Math::Vec2 offset{0.0F, 0.0F}; ///< KHR_texture_transform: added to the UV last.
    float rotation = 0.0F;         ///< KHR_texture_transform: radians, counter-clockwise in UV space.
    Math::Vec2 scale{1.0F, 1.0F};  ///< KHR_texture_transform: multiplies the UV first.
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
    std::int32_t camera = -1; ///< Index into ImportedModel::cameras, or -1.
};

/// @brief A camera from the file. Like in glTF, it looks along its node's −Z; a viewer turns it to the engine's +Z.
///        Plain data.
struct ImportedCamera
{
    std::string name{};       ///< From the file; may be empty.
    bool perspective = true;  ///< Perspective, or orthographic.
    float verticalFov = 0.8F; ///< Perspective: radians.
    float halfHeight = 1.0F;  ///< Orthographic: half the visible height (glTF's `ymag`).
    float nearZ = 0.1F;       ///< Near plane.
    float farZ = 0.0F;        ///< Far plane; 0 for none (perspective without `zfar`). The renderer's depth is infinite.
};

/// @brief What an animation channel moves.
enum class AnimationPath : std::uint8_t
{
    Translation, ///< The node's position; values are (x, y, z, 0).
    Rotation,    ///< The node's rotation; values are quaternions (x, y, z, w).
    Scale        ///< The node's scale; values are (x, y, z, 0).
};

/// @brief How an animation channel interpolates between keyframes.
enum class AnimationInterpolation : std::uint8_t
{
    Step,       ///< Holds each keyframe until the next.
    Linear,     ///< Straight lines; spherical (slerp) for rotations.
    CubicSpline ///< Hermite curves; values hold three entries per keyframe: in-tangent, value, out-tangent.
};

/// @brief One animated property of one node. Values are converted to left-handed space, like node transforms. Plain
///        data.
struct ImportedChannel
{
    std::int32_t node = -1;                                                ///< Index into ImportedModel::nodes.
    AnimationPath path = AnimationPath::Translation;                       ///< What it moves.
    AnimationInterpolation interpolation = AnimationInterpolation::Linear; ///< How.
    std::vector<float> times{};                                            ///< Keyframe times in seconds, increasing.
    std::vector<Math::Vec4> values{}; ///< One per keyframe (three for CubicSpline), see AnimationPath.
};

/// @brief A glTF animation: channels that move nodes over time. Morph-target weights aren't imported. Plain data.
struct ImportedAnimation
{
    std::string name{};                      ///< From the file; may be empty.
    std::vector<ImportedChannel> channels{}; ///< At least one.
    float duration = 0.0F;                   ///< The last keyframe's time, in seconds.
};

/// @brief Everything imported from one glTF file. Plain data.
struct ImportedModel
{
    std::vector<ImportedMesh> meshes{};          ///< Every mesh with at least one triangle primitive.
    std::vector<ImportedMaterial> materials{};   ///< Every material.
    std::vector<Image> images{};                 ///< Decoded images, RGBA8.
    std::vector<ImportedSampler> samplers{};     ///< The file's samplers.
    std::vector<ImportedNode> nodes{};           ///< The default scene's nodes, parents before children.
    std::vector<ImportedCamera> cameras{};       ///< Cameras used by those nodes.
    std::vector<ImportedAnimation> animations{}; ///< Animations of those nodes (translation, rotation, scale).
    std::vector<std::string> warnings{};         ///< What was skipped (points and lines, unknown extensions, …).
};

/// @brief A node's local transform at a time in an animation: its rest transform with the animated parts replaced.
/// @param animation The animation.
/// @param node      Index into ImportedModel::nodes.
/// @param rest      The node's local transform without the animation (ImportedNode::local).
/// @param seconds   The time; clamped to the keyframes (before the first: the first; after the last: the last).
/// @return The transform.
[[nodiscard]] Math::Transform animatedTransform(const ImportedAnimation& animation, std::int32_t node,
                                                const Math::Transform& rest, float seconds);

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
