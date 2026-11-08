/// @file
/// @brief glTF 2.0 import through cgltf: files through the VFS, conversion to left-handed space.

#include "Meow2/Assets/Gltf/GltfImporter.h"

#include "Assets/LibraryMemory.h"
#include "Meow2/Core/Memory/MemTag.h"

#include <cgltf.h>
#include <DirectXMath.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <format>
#include <limits>
#include <memory>
#include <span>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>

namespace Meow2::Assets
{
namespace
{
/// @brief Extensions a file may require that the importer can't honour: their data is unreadable without them.
constexpr std::array<std::string_view, 4> c_unsupportedRequired{"KHR_draco_mesh_compression", "EXT_meshopt_compression",
                                                                "KHR_texture_basisu", "EXT_texture_webp"};

void* cgltfAllocate(void* /*user*/, cgltf_size size)
{
    return libraryAllocate(size == 0 ? 1 : size);
}

void cgltfFree(void* /*user*/, void* memory)
{
    libraryFree(memory);
}

/// @brief cgltf's file reader: every buffer it loads comes through the engine's file system.
cgltf_result readThroughVfs(const cgltf_memory_options* memory, const cgltf_file_options* file, const char* path,
                            cgltf_size* size, void** data)
{
    const auto* files = static_cast<const FileSystem::FileSystem*>(file->user_data);
    Result<std::vector<std::byte>> bytes = files->readFile(path);
    if (!bytes)
    {
        return bytes.error().code == ErrorCode::NotFound ? cgltf_result_file_not_found : cgltf_result_io_error;
    }
    void* copy = memory->alloc_func(memory->user_data, std::max<std::size_t>(bytes->size(), 1));
    if (copy == nullptr)
    {
        return cgltf_result_out_of_memory;
    }
    std::memcpy(copy, bytes->data(), bytes->size());
    *size = bytes->size();
    *data = copy;
    return cgltf_result_success;
}

void releaseThroughVfs(const cgltf_memory_options* memory, const cgltf_file_options* /*file*/, void* data)
{
    memory->free_func(memory->user_data, data);
}

/// @brief Turns a cgltf result into an engine error.
Error toError(cgltf_result result, std::string_view path)
{
    switch (result)
    {
        case cgltf_result_file_not_found:
            return Error{ErrorCode::NotFound, std::format("{}: a file it needs wasn't found", path)};
        case cgltf_result_io_error:
            return Error{ErrorCode::IoError, std::format("{}: a file couldn't be read", path)};
        case cgltf_result_out_of_memory:
            return Error{ErrorCode::OutOfMemory, std::format("{}: out of memory", path)};
        case cgltf_result_legacy_gltf:
            return Error{ErrorCode::Unsupported, std::format("{}: glTF 1.0 isn't supported, only 2.0", path)};
        default:
            return Error{ErrorCode::ParseError, std::format("{}: not a valid glTF 2.0 file", path)};
    }
}

/// @brief Frees cgltf's data when it goes out of scope.
struct CgltfDataDeleter
{
    void operator()(cgltf_data* data) const
    {
        cgltf_free(data);
    }
};

/// @brief The folder part of a virtual path, with its trailing slash; empty for a bare file name.
std::string folderOf(std::string_view path)
{
    const std::size_t slash = path.find_last_of('/');
    return slash == std::string_view::npos ? std::string{} : std::string{path.substr(0, slash + 1)};
}

/// @brief Index of an element of a cgltf array, from a pointer into it.
template <class T>
std::int32_t indexIn(const T* element, const T* first)
{
    return element == nullptr ? -1 : static_cast<std::int32_t>(element - first);
}

/// @brief The bytes of an image, wherever the file keeps them: a buffer view (`.glb`), a data URI, or a file.
Result<std::vector<std::byte>> imageBytes(const cgltf_image& image, const cgltf_options& options,
                                          const FileSystem::FileSystem& files, const std::string& folder)
{
    if (image.buffer_view != nullptr)
    {
        const cgltf_buffer_view& view = *image.buffer_view;
        if (view.buffer == nullptr || view.buffer->data == nullptr)
        {
            return fail(ErrorCode::ParseError, "its buffer has no data");
        }
        const auto* first = static_cast<const std::byte*>(view.buffer->data) + view.offset;
        return std::vector<std::byte>(first, first + view.size);
    }
    if (image.uri == nullptr)
    {
        return fail(ErrorCode::ParseError, "it has neither a URI nor a buffer view");
    }
    const std::string_view uri = image.uri;
    if (uri.starts_with("data:"))
    {
        const std::size_t comma = uri.find(',');
        if (comma == std::string_view::npos || !uri.substr(0, comma).ends_with(";base64"))
        {
            return fail(ErrorCode::ParseError, "only base64 data URIs are supported");
        }
        const std::string_view base64 = uri.substr(comma + 1);
        const std::size_t padding = base64.ends_with("==") ? 2 : (base64.ends_with('=') ? 1 : 0);
        const std::size_t size = base64.size() / 4 * 3 - padding;
        void* decoded = nullptr;
        if (cgltf_load_buffer_base64(&options, size, base64.data(), &decoded) != cgltf_result_success)
        {
            return fail(ErrorCode::ParseError, "its data URI isn't valid base64");
        }
        const auto* first = static_cast<const std::byte*>(decoded);
        std::vector<std::byte> bytes(first, first + size);
        libraryFree(decoded);
        return bytes;
    }
    // A relative URI, percent-encoded (spaces in file names are %20).
    std::string decoded{uri};
    decoded.resize(cgltf_decode_uri(decoded.data()));
    return files.readFile(folder + decoded);
}

/// @brief Reads every element of an accessor as `components` floats each (normalised integers become 0..1 or -1..1).
std::vector<float> unpackFloats(const cgltf_accessor& accessor, std::size_t components)
{
    std::vector<float> floats(accessor.count * components);
    cgltf_accessor_unpack_floats(&accessor, floats.data(), floats.size());
    return floats;
}

std::vector<Math::Vec2> toVec2(const std::vector<float>& floats)
{
    std::vector<Math::Vec2> values(floats.size() / 2);
    for (std::size_t i = 0; i < values.size(); ++i)
    {
        values[i] = Math::Vec2{floats[i * 2], floats[i * 2 + 1]};
    }
    return values;
}

std::vector<Math::Vec3> toVec3(const std::vector<float>& floats)
{
    std::vector<Math::Vec3> values(floats.size() / 3);
    for (std::size_t i = 0; i < values.size(); ++i)
    {
        values[i] = Math::Vec3{floats[i * 3], floats[i * 3 + 1], floats[i * 3 + 2]};
    }
    return values;
}

std::vector<Math::Vec4> toVec4(const std::vector<float>& floats)
{
    std::vector<Math::Vec4> values(floats.size() / 4);
    for (std::size_t i = 0; i < values.size(); ++i)
    {
        values[i] = Math::Vec4{floats[i * 4], floats[i * 4 + 1], floats[i * 4 + 2], floats[i * 4 + 3]};
    }
    return values;
}

/// @brief Normals from the triangles, weighted by area, for primitives the file gives none.
///
/// Runs in glTF's right-handed space, before the mirror: counter-clockwise triangles, so (b - a) × (c - a) points out
/// of the front face.
std::vector<Math::Vec3> computeNormals(const std::vector<Math::Vec3>& positions,
                                       const std::vector<std::uint32_t>& indices)
{
    using namespace DirectX;
    std::vector<XMFLOAT3> sums(positions.size(), XMFLOAT3{0.0F, 0.0F, 0.0F});
    for (std::size_t i = 0; i + 2 < indices.size(); i += 3)
    {
        const XMVECTOR a = XMLoadFloat3(&positions[indices[i]]);
        const XMVECTOR b = XMLoadFloat3(&positions[indices[i + 1]]);
        const XMVECTOR c = XMLoadFloat3(&positions[indices[i + 2]]);
        const XMVECTOR face = XMVector3Cross(XMVectorSubtract(b, a), XMVectorSubtract(c, a));
        for (std::size_t corner = 0; corner < 3; ++corner)
        {
            XMFLOAT3& sum = sums[indices[i + corner]];
            XMStoreFloat3(&sum, XMVectorAdd(XMLoadFloat3(&sum), face));
        }
    }
    std::vector<Math::Vec3> normals(positions.size());
    for (std::size_t i = 0; i < sums.size(); ++i)
    {
        const XMVECTOR sum = XMLoadFloat3(&sums[i]);
        const bool degenerate = XMVectorGetX(XMVector3LengthSq(sum)) < 1e-20F;
        XMStoreFloat3(&normals[i], degenerate ? XMVectorSet(0.0F, 1.0F, 0.0F, 0.0F) : XMVector3Normalize(sum));
    }
    return normals;
}

/// @brief Tangents from the UVs (Lengyel's method), for primitives the file gives none.
///
/// Runs in glTF's right-handed space. The tangent follows +U; the sign makes cross(normal, tangent) × w point the way
/// glTF's normal maps expect +Y: towards decreasing V (V runs down the image). Not bit-identical to MikkTSpace, which
/// glTF recommends, but the same directions.
std::vector<Math::Vec4> computeTangents(const std::vector<Math::Vec3>& positions,
                                        const std::vector<Math::Vec3>& normals, const std::vector<Math::Vec2>& uvs,
                                        const std::vector<std::uint32_t>& indices)
{
    using namespace DirectX;
    std::vector<XMFLOAT3> alongU(positions.size(), XMFLOAT3{0.0F, 0.0F, 0.0F});
    std::vector<XMFLOAT3> alongV(positions.size(), XMFLOAT3{0.0F, 0.0F, 0.0F});
    for (std::size_t i = 0; i + 2 < indices.size(); i += 3)
    {
        const std::uint32_t ia = indices[i];
        const std::uint32_t ib = indices[i + 1];
        const std::uint32_t ic = indices[i + 2];
        const XMVECTOR edge1 = XMVectorSubtract(XMLoadFloat3(&positions[ib]), XMLoadFloat3(&positions[ia]));
        const XMVECTOR edge2 = XMVectorSubtract(XMLoadFloat3(&positions[ic]), XMLoadFloat3(&positions[ia]));
        const float du1 = uvs[ib].x - uvs[ia].x;
        const float dv1 = uvs[ib].y - uvs[ia].y;
        const float du2 = uvs[ic].x - uvs[ia].x;
        const float dv2 = uvs[ic].y - uvs[ia].y;
        const float determinant = du1 * dv2 - du2 * dv1;
        if (std::abs(determinant) < 1e-12F)
        {
            continue; // no UV area: the triangle says nothing about directions
        }
        const float r = 1.0F / determinant;
        const XMVECTOR u = XMVectorScale(XMVectorSubtract(XMVectorScale(edge1, dv2), XMVectorScale(edge2, dv1)), r);
        const XMVECTOR v = XMVectorScale(XMVectorSubtract(XMVectorScale(edge2, du1), XMVectorScale(edge1, du2)), r);
        for (const std::uint32_t corner : {ia, ib, ic})
        {
            XMStoreFloat3(&alongU[corner], XMVectorAdd(XMLoadFloat3(&alongU[corner]), u));
            XMStoreFloat3(&alongV[corner], XMVectorAdd(XMLoadFloat3(&alongV[corner]), v));
        }
    }
    std::vector<Math::Vec4> tangents(positions.size());
    for (std::size_t i = 0; i < positions.size(); ++i)
    {
        const XMVECTOR n = XMLoadFloat3(&normals[i]);
        // Gram-Schmidt: the part of +U perpendicular to the normal.
        XMVECTOR t = XMVectorSubtract(XMLoadFloat3(&alongU[i]),
                                      XMVectorScale(n, XMVectorGetX(XMVector3Dot(n, XMLoadFloat3(&alongU[i])))));
        if (XMVectorGetX(XMVector3LengthSq(t)) < 1e-20F)
        {
            // No usable UVs: any direction perpendicular to the normal.
            const XMVECTOR axis = std::abs(normals[i].x) < 0.9F ? XMVectorSet(1.0F, 0.0F, 0.0F, 0.0F)
                                                                : XMVectorSet(0.0F, 1.0F, 0.0F, 0.0F);
            t = XMVector3Cross(n, axis);
        }
        t = XMVector3Normalize(t);
        // +Y of the normal map points towards decreasing V.
        const float handedness =
            XMVectorGetX(XMVector3Dot(XMVector3Cross(n, t), XMVectorNegate(XMLoadFloat3(&alongV[i])))) < 0.0F ? -1.0F
                                                                                                              : 1.0F;
        XMFLOAT3 stored{};
        XMStoreFloat3(&stored, t);
        tangents[i] = Math::Vec4{stored.x, stored.y, stored.z, handedness};
    }
    return tangents;
}

/// @brief Imports one triangle primitive, converted to left-handed space.
ImportedPrimitive importPrimitive(const cgltf_primitive& primitive, const cgltf_data& data)
{
    ImportedPrimitive out;
    const cgltf_accessor* positions = nullptr;
    const cgltf_accessor* normals = nullptr;
    const cgltf_accessor* tangents = nullptr;
    const cgltf_accessor* uvs = nullptr;
    const cgltf_accessor* colors = nullptr;
    for (std::size_t i = 0; i < primitive.attributes_count; ++i)
    {
        const cgltf_attribute& attribute = primitive.attributes[i];
        switch (attribute.type)
        {
            case cgltf_attribute_type_position:
                positions = attribute.data;
                break;
            case cgltf_attribute_type_normal:
                normals = attribute.data;
                break;
            case cgltf_attribute_type_tangent:
                tangents = attribute.data;
                break;
            case cgltf_attribute_type_texcoord:
                uvs = attribute.index == 0 ? attribute.data : uvs;
                break;
            case cgltf_attribute_type_color:
                colors = attribute.index == 0 ? attribute.data : colors;
                break;
            default:
                break;
        }
    }
    if (positions == nullptr || positions->count == 0)
    {
        return out;
    }
    const std::size_t count = positions->count;
    out.positions = toVec3(unpackFloats(*positions, 3));
    if (normals != nullptr && normals->count == count)
    {
        out.normals = toVec3(unpackFloats(*normals, 3));
    }
    out.uvs = uvs != nullptr && uvs->count == count ? toVec2(unpackFloats(*uvs, 2)) : std::vector<Math::Vec2>(count);
    if (colors != nullptr && colors->count == count)
    {
        // RGB or RGBA; RGB colours are opaque.
        const std::size_t components = cgltf_num_components(colors->type);
        const std::vector<float> floats = unpackFloats(*colors, components);
        out.colors.resize(count);
        for (std::size_t i = 0; i < count; ++i)
        {
            const float* c = &floats[i * components];
            out.colors[i] = Math::Color{c[0], c[1], c[2], components == 4 ? c[3] : 1.0F};
        }
    }
    if (primitive.indices != nullptr)
    {
        out.indices.resize(primitive.indices->count);
        cgltf_accessor_unpack_indices(primitive.indices, out.indices.data(), sizeof(std::uint32_t), out.indices.size());
    }
    else
    {
        out.indices.resize(count);
        for (std::uint32_t i = 0; i < out.indices.size(); ++i)
        {
            out.indices[i] = i;
        }
    }
    out.indices.resize(out.indices.size() / 3 * 3);

    // Missing normals and tangents are made in glTF's space, before the conversion below, so they're converted exactly
    // like ones read from the file.
    if (out.normals.empty())
    {
        out.normals = computeNormals(out.positions, out.indices);
    }
    out.tangents = tangents != nullptr && tangents->count == count
                       ? toVec4(unpackFloats(*tangents, 4))
                       : computeTangents(out.positions, out.normals, out.uvs, out.indices);

    // Right-handed → left-handed: mirror X. The picture on screen stays the same, so front faces still look counter-
    // clockwise there (glTF's rule); the engine's rule is clockwise, so each triangle's winding is swapped. A mirror
    // flips cross products, so the bitangent sign flips too, keeping bitangent = cross(normal, tangent) × w.
    for (Math::Vec3& position : out.positions)
    {
        position.x = -position.x;
    }
    for (Math::Vec3& normal : out.normals)
    {
        normal.x = -normal.x;
    }
    for (Math::Vec4& tangent : out.tangents)
    {
        tangent.x = -tangent.x;
        tangent.w = tangent.w < 0.0F ? 1.0F : -1.0F;
    }
    for (std::size_t i = 0; i + 2 < out.indices.size(); i += 3)
    {
        std::swap(out.indices[i + 1], out.indices[i + 2]);
    }

    Math::Vec3 low{std::numeric_limits<float>::max(), std::numeric_limits<float>::max(),
                   std::numeric_limits<float>::max()};
    Math::Vec3 high{-low.x, -low.y, -low.z};
    for (const Math::Vec3& position : out.positions)
    {
        low = {std::min(low.x, position.x), std::min(low.y, position.y), std::min(low.z, position.z)};
        high = {std::max(high.x, position.x), std::max(high.y, position.y), std::max(high.z, position.z)};
    }
    out.bounds.center = {(low.x + high.x) * 0.5F, (low.y + high.y) * 0.5F, (low.z + high.z) * 0.5F};
    out.bounds.extents = {(high.x - low.x) * 0.5F, (high.y - low.y) * 0.5F, (high.z - low.z) * 0.5F};
    out.material = indexIn(primitive.material, data.materials);
    return out;
}

/// @brief A node's local transform in left-handed space: S × M × S, where S mirrors X.
std::optional<Math::Transform> localTransform(const cgltf_node& node)
{
    std::array<float, 16> m{};
    cgltf_node_transform_local(&node, m.data());
    // glTF stores column-major matrices for column vectors; read as rows, that's the row-vector matrix the engine uses.
    DirectX::XMFLOAT4X4 rows{m.data()};
    for (int row = 0; row < 4; ++row)
    {
        for (int column = 0; column < 4; ++column)
        {
            if ((row == 0) != (column == 0)) // exactly one X: negated by the mirror on one side
            {
                rows.m[row][column] = -rows.m[row][column];
            }
        }
    }
    return Math::fromMatrix(DirectX::XMLoadFloat4x4(&rows));
}

/// @brief Fills a material from cgltf's.
ImportedMaterial importMaterial(const cgltf_material& material, const cgltf_data& data,
                                std::vector<std::string>& warnings)
{
    ImportedMaterial out;
    out.name = material.name != nullptr ? material.name : "";
    const auto textureOf = [&data, &warnings, &out](const cgltf_texture_view& view, std::string_view slot)
    {
        if (view.texture == nullptr || view.texture->image == nullptr)
        {
            return ImportedTexture{};
        }
        if (view.texcoord != 0)
        {
            warnings.push_back(
                std::format("material '{}': {} uses UV set {}; only set 0 is imported", out.name, slot, view.texcoord));
        }
        ImportedTexture texture{.image = indexIn(view.texture->image, data.images),
                                .sampler = indexIn(view.texture->sampler, data.samplers)};
        if (view.has_transform)
        {
            // KHR_texture_transform. UVs aren't touched by the handedness conversion (only positions and normals
            // are), so the values apply as they are.
            const cgltf_texture_transform& transform = view.transform;
            texture.offset = {transform.offset[0], transform.offset[1]};
            texture.rotation = transform.rotation;
            texture.scale = {transform.scale[0], transform.scale[1]};
            if (transform.has_texcoord && transform.texcoord != 0)
            {
                warnings.push_back(std::format("material '{}': {}'s texture transform uses UV set {}; only set 0 is "
                                               "imported",
                                               out.name, slot, transform.texcoord));
            }
        }
        return texture;
    };
    if (material.has_pbr_metallic_roughness)
    {
        const cgltf_pbr_metallic_roughness& pbr = material.pbr_metallic_roughness;
        out.baseColorFactor = {pbr.base_color_factor[0], pbr.base_color_factor[1], pbr.base_color_factor[2],
                               pbr.base_color_factor[3]};
        out.baseColor = textureOf(pbr.base_color_texture, "base colour");
        out.metallicFactor = pbr.metallic_factor;
        out.roughnessFactor = pbr.roughness_factor;
        out.metallicRoughness = textureOf(pbr.metallic_roughness_texture, "metallic-roughness");
    }
    out.normal = textureOf(material.normal_texture, "normal");
    out.normalScale = material.normal_texture.scale;
    out.occlusion = textureOf(material.occlusion_texture, "occlusion");
    out.occlusionStrength = material.occlusion_texture.scale;
    out.emissive = textureOf(material.emissive_texture, "emissive");
    out.emissiveFactor = {material.emissive_factor[0], material.emissive_factor[1], material.emissive_factor[2], 1.0F};
    out.alphaMode = material.alpha_mode == cgltf_alpha_mode_mask    ? AlphaMode::Mask
                    : material.alpha_mode == cgltf_alpha_mode_blend ? AlphaMode::Blend
                                                                    : AlphaMode::Opaque;
    out.alphaCutoff = material.alpha_cutoff;
    out.doubleSided = material.double_sided != 0;
    return out;
}
} // namespace

Result<ImportedModel> importGltf(const FileSystem::FileSystem& files, std::string_view path, Log::Logger& log)
{
    const Memory::MemTagScope tag{Memory::MemTag::Assets};
    Result<std::vector<std::byte>> file = files.readFile(path);
    if (!file)
    {
        return Unexpected<Error>{file.error()};
    }

    cgltf_options options{};
    options.memory.alloc_func = cgltfAllocate;
    options.memory.free_func = cgltfFree;
    options.file.read = readThroughVfs;
    options.file.release = releaseThroughVfs;
    options.file.user_data = const_cast<FileSystem::FileSystem*>(&files); // only read: cgltf's user data isn't const
    cgltf_data* parsed = nullptr;
    // A .glb keeps pointing into `file` for its binary chunk, so `file` outlives `data`.
    if (const cgltf_result result = cgltf_parse(&options, file->data(), file->size(), &parsed);
        result != cgltf_result_success)
    {
        return Unexpected<Error>{toError(result, path)};
    }
    const std::unique_ptr<cgltf_data, CgltfDataDeleter> data{parsed};

    for (std::size_t i = 0; i < data->extensions_required_count; ++i)
    {
        const std::string_view extension = data->extensions_required[i];
        if (std::ranges::find(c_unsupportedRequired, extension) != c_unsupportedRequired.end())
        {
            return fail(ErrorCode::Unsupported, std::format("{}: requires {}, which isn't supported", path, extension));
        }
    }
    const std::string pathString{path};
    if (const cgltf_result result = cgltf_load_buffers(&options, data.get(), pathString.c_str());
        result != cgltf_result_success)
    {
        return Unexpected<Error>{toError(result, path)};
    }
    if (const cgltf_result result = cgltf_validate(data.get()); result != cgltf_result_success)
    {
        return Unexpected<Error>{toError(result, path)};
    }

    ImportedModel model;
    for (std::size_t i = 0; i < data->extensions_required_count; ++i)
    {
        model.warnings.push_back(std::format("requires {}, which is ignored", data->extensions_required[i]));
    }

    const std::string folder = folderOf(path);
    for (std::size_t i = 0; i < data->images_count; ++i)
    {
        const cgltf_image& image = data->images[i];
        const std::string name =
            image.name != nullptr
                ? image.name
                : (image.uri != nullptr && !std::string_view{image.uri}.starts_with("data:") ? image.uri
                                                                                             : std::format("#{}", i));
        Result<std::vector<std::byte>> bytes = imageBytes(image, options, files, folder);
        Result<Image> decoded = bytes ? decodeImage(*bytes) : Result<Image>{Unexpected<Error>{bytes.error()}};
        if (!decoded)
        {
            // A missing texture shouldn't stop the model: it's drawn without it.
            model.warnings.push_back(std::format("image '{}': {}", name, decoded.error().message));
            model.images.push_back({});
            continue;
        }
        model.images.push_back(std::move(*decoded));
    }

    for (std::size_t i = 0; i < data->samplers_count; ++i)
    {
        const cgltf_sampler& sampler = data->samplers[i];
        const auto wrap = [](cgltf_wrap_mode mode)
        {
            return mode == cgltf_wrap_mode_clamp_to_edge     ? ImportedWrap::Clamp
                   : mode == cgltf_wrap_mode_mirrored_repeat ? ImportedWrap::Mirror
                                                             : ImportedWrap::Repeat;
        };
        // Nearest only when the file asks for it on magnification; mipmaps unless minification says plain
        // nearest or linear. Undefined (0) means the default: linear with mipmaps.
        model.samplers.push_back({.linear = sampler.mag_filter != cgltf_filter_type_nearest,
                                  .mipmaps = sampler.min_filter != cgltf_filter_type_nearest &&
                                             sampler.min_filter != cgltf_filter_type_linear,
                                  .wrapU = wrap(sampler.wrap_s),
                                  .wrapV = wrap(sampler.wrap_t)});
    }

    for (std::size_t i = 0; i < data->materials_count; ++i)
    {
        model.materials.push_back(importMaterial(data->materials[i], *data, model.warnings));
    }

    std::vector<std::int32_t> meshIndex(data->meshes_count, -1); // file mesh → model mesh
    for (std::size_t i = 0; i < data->meshes_count; ++i)
    {
        const cgltf_mesh& mesh = data->meshes[i];
        ImportedMesh out;
        out.name = mesh.name != nullptr ? mesh.name : "";
        for (std::size_t p = 0; p < mesh.primitives_count; ++p)
        {
            const cgltf_primitive& primitive = mesh.primitives[p];
            if (primitive.type != cgltf_primitive_type_triangles)
            {
                model.warnings.push_back(
                    std::format("mesh '{}': a primitive isn't triangles (points, lines or strips), skipped", out.name));
                continue;
            }
            ImportedPrimitive imported = importPrimitive(primitive, *data);
            if (imported.indices.empty())
            {
                model.warnings.push_back(std::format("mesh '{}': a primitive has no triangles, skipped", out.name));
                continue;
            }
            out.primitives.push_back(std::move(imported));
        }
        if (!out.primitives.empty())
        {
            meshIndex[i] = static_cast<std::int32_t>(model.meshes.size());
            model.meshes.push_back(std::move(out));
        }
    }

    // The default scene, or the first, or every root node if the file has no scenes. Depth first, so parents come
    // before their children.
    std::vector<const cgltf_node*> roots;
    const cgltf_scene* scene = data->scene != nullptr ? data->scene : (data->scenes_count > 0 ? data->scenes : nullptr);
    if (scene != nullptr)
    {
        roots.assign(scene->nodes, scene->nodes + scene->nodes_count);
    }
    else
    {
        for (std::size_t i = 0; i < data->nodes_count; ++i)
        {
            if (data->nodes[i].parent == nullptr)
            {
                roots.push_back(&data->nodes[i]);
            }
        }
    }
    std::vector<std::pair<const cgltf_node*, std::int32_t>> stack; // node, parent
    for (auto root = roots.rbegin(); root != roots.rend(); ++root)
    {
        stack.emplace_back(*root, -1);
    }
    while (!stack.empty())
    {
        const auto [node, parent] = stack.back();
        stack.pop_back();
        ImportedNode out;
        out.name = node->name != nullptr ? node->name : "";
        out.parent = parent;
        if (node->mesh != nullptr)
        {
            out.mesh = meshIndex[static_cast<std::size_t>(node->mesh - data->meshes)];
        }
        if (const std::optional<Math::Transform> local = localTransform(*node))
        {
            out.local = *local;
        }
        else
        {
            model.warnings.push_back(std::format("node '{}': its matrix isn't a scale, rotation and translation; "
                                                 "using the identity",
                                                 out.name));
        }
        const auto index = static_cast<std::int32_t>(model.nodes.size());
        model.nodes.push_back(std::move(out));
        for (std::size_t c = node->children_count; c-- > 0;)
        {
            stack.emplace_back(node->children[c], index);
        }
    }

    for (const std::string& warning : model.warnings)
    {
        MEOW2_LOG_WARN(log, "{}: {}", path, warning);
    }
    return model;
}
} // namespace Meow2::Assets
