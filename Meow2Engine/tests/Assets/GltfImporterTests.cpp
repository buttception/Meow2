/// @file
/// @brief importGltf(): small glTF files written by the tests, so no sample assets are needed.

#include "Assets/TestFiles.h"

#include <Meow2/Assets/Gltf/GltfImporter.h>
#include <Meow2/Core/Log/LogSystem.h>
#include <Meow2/Core/Platform.h>
#include <Meow2/Platform/FileSystem/FileSystem.h>

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Meow2::Assets
{
namespace
{
namespace fs = std::filesystem;
using Testing::base64;
using Testing::bytesOf;

/// One triangle, counter-clockwise seen from +Z (glTF's front face), and its indices: 42 bytes.
std::vector<std::byte> triangleBuffer()
{
    const std::array<float, 9> positions{0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F};
    const std::array<std::uint16_t, 3> indices{0, 1, 2};
    std::vector<std::byte> buffer = bytesOf<float>(positions);
    const std::vector<std::byte> indexBytes = bytesOf<std::uint16_t>(indices);
    buffer.insert(buffer.end(), indexBytes.begin(), indexBytes.end());
    return buffer;
}

/// A glTF document around the triangle: a root node at (1, 2, 3) with a child turned 90° about Y that has the mesh.
/// `buffer` is the buffer object (with its URI, or none for a .glb); `extra` is added at the top level.
std::string triangleJson(std::string_view buffer, std::string_view extra = "")
{
    return std::format(R"({{
  "asset": {{"version": "2.0"}},
  "buffers": [{}],
  "bufferViews": [{{"buffer": 0, "byteOffset": 0, "byteLength": 36}},
                  {{"buffer": 0, "byteOffset": 36, "byteLength": 6}}],
  "accessors": [{{"bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3",
                  "min": [0, 0, 0], "max": [1, 1, 0]}},
                {{"bufferView": 1, "componentType": 5123, "count": 3, "type": "SCALAR"}}],
  "meshes": [{{"name": "Triangle", "primitives": [{{"attributes": {{"POSITION": 0}}, "indices": 1}}]}}],
  "nodes": [{{"name": "Root", "translation": [1, 2, 3], "children": [1]}},
            {{"name": "Child", "mesh": 0, "rotation": [0, 0.70710678, 0, 0.70710678]}}],
  "scenes": [{{"nodes": [0]}}],
  "scene": 0{}
}})",
                       buffer, extra);
}

/// A folder of test files, mounted as `test/`.
class GltfImporterTest : public ::testing::Test
{
protected:
    GltfImporterTest()
        : m_logSystem({.appName = "Test", .directory = {}, .console = true})
        , m_log(m_logSystem.createLogger("Assets"))
        , m_files(m_log)
    {
    }

    void SetUp() override
    {
        m_folder =
            fs::temp_directory_path() / std::format("meow2-gltf-{}-{}", c_configName,
                                                    ::testing::UnitTest::GetInstance()->current_test_info()->name());
        std::error_code error;
        fs::remove_all(m_folder, error);
        fs::create_directories(m_folder);
        ASSERT_TRUE(m_files.mountDirectory("test", m_folder).has_value());
    }

    void TearDown() override
    {
        std::error_code error;
        fs::remove_all(m_folder, error);
    }

    void write(std::string_view name, std::span<const std::byte> bytes) const
    {
        std::ofstream file{m_folder / name, std::ios::binary};
        file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }

    void write(std::string_view name, std::string_view text) const
    {
        std::ofstream file{m_folder / name, std::ios::binary};
        file << text;
    }

    Result<ImportedModel> import(std::string_view name)
    {
        return importGltf(m_files, std::string{"test/"} + std::string{name}, m_log);
    }

    Log::LogSystem m_logSystem;
    Log::Logger m_log;
    FileSystem::FileSystem m_files;
    fs::path m_folder;
};

void expectNear(const Math::Vec3& actual, const Math::Vec3& expected)
{
    EXPECT_NEAR(actual.x, expected.x, 1e-5F);
    EXPECT_NEAR(actual.y, expected.y, 1e-5F);
    EXPECT_NEAR(actual.z, expected.z, 1e-5F);
}

/// The data URI of a buffer.
std::string dataUri(std::span<const std::byte> bytes, std::string_view mime = "application/octet-stream")
{
    return std::format("data:{};base64,{}", mime, base64(bytes));
}
} // namespace

TEST_F(GltfImporterTest, MirrorsXAndSwapsTheWinding)
{
    const std::vector<std::byte> buffer = triangleBuffer();
    write("triangle.gltf", triangleJson(std::format(R"({{"byteLength": 42, "uri": "{}"}})", dataUri(buffer))));

    const Result<ImportedModel> model = import("triangle.gltf");

    ASSERT_TRUE(model) << model.error().message;
    ASSERT_EQ(model->meshes.size(), 1U);
    ASSERT_EQ(model->meshes[0].primitives.size(), 1U);
    const ImportedPrimitive& triangle = model->meshes[0].primitives[0];
    ASSERT_EQ(triangle.positions.size(), 3U);
    expectNear(triangle.positions[1], {-1.0F, 0.0F, 0.0F});
    expectNear(triangle.positions[2], {0.0F, 1.0F, 0.0F});
    EXPECT_EQ(triangle.indices, (std::vector<std::uint32_t>{0, 2, 1})) << "clockwise in the engine's space";
    EXPECT_EQ(triangle.material, -1);
    EXPECT_TRUE(model->warnings.empty());
}

TEST_F(GltfImporterTest, ComputesMissingNormalsTowardsTheFront)
{
    write("triangle.gltf",
          triangleJson(std::format(R"({{"byteLength": 42, "uri": "{}"}})", dataUri(triangleBuffer()))));

    const Result<ImportedModel> model = import("triangle.gltf");

    ASSERT_TRUE(model) << model.error().message;
    const ImportedPrimitive& triangle = model->meshes[0].primitives[0];
    ASSERT_EQ(triangle.normals.size(), 3U);
    for (const Math::Vec3& normal : triangle.normals)
    {
        expectNear(normal, {0.0F, 0.0F, 1.0F}); // glTF's front faced +Z; mirroring X keeps that
    }
    EXPECT_EQ(triangle.uvs.size(), 3U) << "zeros when the file has none";
    expectNear(triangle.bounds.center, {-0.5F, 0.5F, 0.0F});
    expectNear(triangle.bounds.extents, {0.5F, 0.5F, 0.0F});
}

TEST_F(GltfImporterTest, NodesKeepTheirHierarchyInLeftHandedSpace)
{
    write("triangle.gltf",
          triangleJson(std::format(R"({{"byteLength": 42, "uri": "{}"}})", dataUri(triangleBuffer()))));

    const Result<ImportedModel> model = import("triangle.gltf");

    ASSERT_TRUE(model) << model.error().message;
    ASSERT_EQ(model->nodes.size(), 2U);
    EXPECT_EQ(model->nodes[0].name, "Root");
    EXPECT_EQ(model->nodes[0].parent, -1);
    EXPECT_EQ(model->nodes[0].mesh, -1);
    expectNear(model->nodes[0].local.position, {-1.0F, 2.0F, 3.0F});
    EXPECT_EQ(model->nodes[1].parent, 0);
    EXPECT_EQ(model->nodes[1].mesh, 0);
    // In glTF, +90° about Y turns +X into -Z. Mirrored, it turns -X (the mirror of +X) into -Z (the mirror of -Z).
    expectNear(Math::transformPoint(model->nodes[1].local, {-1.0F, 0.0F, 0.0F}), {0.0F, 0.0F, -1.0F});
}

TEST_F(GltfImporterTest, ReadsBuffersNextToTheFile)
{
    write("tri data.bin", triangleBuffer());
    // A space in a URI is percent-encoded.
    write("triangle.gltf", triangleJson(R"({"byteLength": 42, "uri": "tri%20data.bin"})"));

    const Result<ImportedModel> model = import("triangle.gltf");

    ASSERT_TRUE(model) << model.error().message;
    EXPECT_EQ(model->meshes[0].primitives[0].positions.size(), 3U);
}

TEST_F(GltfImporterTest, MissingBufferIsNotFound)
{
    write("triangle.gltf", triangleJson(R"({"byteLength": 42, "uri": "missing.bin"})"));

    const Result<ImportedModel> model = import("triangle.gltf");

    ASSERT_FALSE(model);
    EXPECT_EQ(model.error().code, ErrorCode::NotFound);
}

TEST_F(GltfImporterTest, MissingFileIsNotFound)
{
    const Result<ImportedModel> model = import("nothing.gltf");
    ASSERT_FALSE(model);
    EXPECT_EQ(model.error().code, ErrorCode::NotFound);
}

TEST_F(GltfImporterTest, BrokenJsonIsAParseError)
{
    write("broken.gltf", R"({"asset": {"version": "2.0"}, "nodes": [)");
    const Result<ImportedModel> model = import("broken.gltf");
    ASSERT_FALSE(model);
    EXPECT_EQ(model.error().code, ErrorCode::ParseError);
}

TEST_F(GltfImporterTest, RequiredDracoIsUnsupported)
{
    write("draco.gltf", triangleJson(std::format(R"({{"byteLength": 42, "uri": "{}"}})", dataUri(triangleBuffer())),
                                     R"(, "extensionsUsed": ["KHR_draco_mesh_compression"],
                          "extensionsRequired": ["KHR_draco_mesh_compression"])"));

    const Result<ImportedModel> model = import("draco.gltf");

    ASSERT_FALSE(model);
    EXPECT_EQ(model.error().code, ErrorCode::Unsupported);
    EXPECT_NE(model.error().message.find("KHR_draco_mesh_compression"), std::string::npos);
}

TEST_F(GltfImporterTest, GlbWithAnEmbeddedTexture)
{
    // The binary chunk: the triangle (42 bytes, padded to 44), then a 1 × 1 orange PNG.
    std::vector<std::byte> binary = triangleBuffer();
    binary.resize(44);
    const std::array<std::uint8_t, 4> orange{255, 128, 0, 255};
    const std::vector<std::byte> png = Testing::encodePng(1, 1, 4, orange);
    binary.insert(binary.end(), png.begin(), png.end());
    const std::string json =
        triangleJson(std::format(R"({{"byteLength": {}}})", binary.size()), std::format(R"(,
  "images": [{{"bufferView": 2, "mimeType": "image/png"}}],
  "textures": [{{"source": 0}}],
  "materials": [{{"name": "Orange", "alphaMode": "MASK", "alphaCutoff": 0.25,
                 "pbrMetallicRoughness": {{"baseColorFactor": [0.5, 1, 1, 1], "baseColorTexture": {{"index": 0}},
                                          "metallicFactor": 0.1, "roughnessFactor": 0.7}}}}])",
                                                                                        png.size()));
    // A third buffer view for the image, and the triangle's primitive gets the material.
    std::string withImage = json;
    withImage.replace(withImage.find(R"({"buffer": 0, "byteOffset": 36, "byteLength": 6}])"),
                      std::string_view{R"({"buffer": 0, "byteOffset": 36, "byteLength": 6}])"}.size(),
                      std::format(R"({{"buffer": 0, "byteOffset": 36, "byteLength": 6}},
                  {{"buffer": 0, "byteOffset": 44, "byteLength": {}}}])",
                                  png.size()));
    withImage.replace(withImage.find(R"("indices": 1})"), std::string_view{R"("indices": 1})"}.size(),
                      R"("indices": 1, "material": 0})");
    write("triangle.glb", Testing::makeGlb(withImage, binary));

    const Result<ImportedModel> model = import("triangle.glb");

    ASSERT_TRUE(model) << model.error().message;
    ASSERT_EQ(model->images.size(), 1U);
    ASSERT_EQ(model->images[0].pixels.size(), 4U);
    EXPECT_EQ(std::to_integer<int>(model->images[0].pixels[1]), 128);
    ASSERT_EQ(model->materials.size(), 1U);
    const ImportedMaterial& material = model->materials[0];
    EXPECT_EQ(material.name, "Orange");
    EXPECT_EQ(material.baseColor.image, 0);
    EXPECT_FLOAT_EQ(material.baseColorFactor.r, 0.5F);
    EXPECT_EQ(material.alphaMode, AlphaMode::Mask);
    EXPECT_FLOAT_EQ(material.alphaCutoff, 0.25F);
    EXPECT_FLOAT_EQ(material.metallicFactor, 0.1F);
    EXPECT_FLOAT_EQ(material.roughnessFactor, 0.7F);
    EXPECT_EQ(model->meshes[0].primitives[0].material, 0);
}

TEST_F(GltfImporterTest, MissingImageIsAWarningNotAFailure)
{
    std::string json = triangleJson(std::format(R"({{"byteLength": 42, "uri": "{}"}})", dataUri(triangleBuffer())),
                                    R"(, "images": [{"uri": "missing.png"}])");
    write("triangle.gltf", json);

    const Result<ImportedModel> model = import("triangle.gltf");

    ASSERT_TRUE(model) << model.error().message;
    ASSERT_EQ(model->images.size(), 1U);
    EXPECT_TRUE(model->images[0].pixels.empty());
    ASSERT_EQ(model->warnings.size(), 1U);
    EXPECT_NE(model->warnings[0].find("missing.png"), std::string::npos) << model->warnings[0];
}

TEST_F(GltfImporterTest, PointsAreSkipped)
{
    std::string json = triangleJson(std::format(R"({{"byteLength": 42, "uri": "{}"}})", dataUri(triangleBuffer())));
    json.replace(json.find(R"("indices": 1})"), std::string_view{R"("indices": 1})"}.size(),
                 R"("indices": 1, "mode": 0})");
    write("points.gltf", json);

    const Result<ImportedModel> model = import("points.gltf");

    ASSERT_TRUE(model) << model.error().message;
    EXPECT_TRUE(model->meshes.empty());
    EXPECT_EQ(model->nodes[1].mesh, -1) << "its only primitive was points";
    EXPECT_FALSE(model->warnings.empty());
}
namespace
{
/// The triangle with normals (+Z), UVs (U along +X, V along +Y), optional tangents and vertex colours: 200 bytes.
std::vector<std::byte> attributeBuffer()
{
    std::vector<std::byte> buffer = triangleBuffer();
    buffer.resize(44); // indices padded to 4 bytes
    const auto append = [&buffer](std::span<const float> values)
    {
        const std::vector<std::byte> bytes = bytesOf<float>(values);
        buffer.insert(buffer.end(), bytes.begin(), bytes.end());
    };
    append(std::array<float, 9>{0, 0, 1, 0, 0, 1, 0, 0, 1});              // NORMAL at 44
    append(std::array<float, 12>{1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1});    // TANGENT at 80
    append(std::array<float, 12>{1, 0, 0, 1, 0, 1, 0, 1, 0, 0, 1, 0.5F}); // COLOR_0 at 128
    append(std::array<float, 6>{0, 0, 1, 0, 0, 1});                       // TEXCOORD_0 at 176
    return buffer;
}

/// A document around attributeBuffer(): `attributes` lists the primitive's attributes beyond POSITION; one material
/// with a normal map that has a sampler (nearest, mirrored across, clamped down).
std::string attributeJson(std::string_view bufferUri, std::string_view imageUri, std::string_view attributes)
{
    return std::format(R"({{
  "asset": {{"version": "2.0"}},
  "buffers": [{{"byteLength": 200, "uri": "{}"}}],
  "bufferViews": [{{"buffer": 0, "byteOffset": 0, "byteLength": 36}},
                  {{"buffer": 0, "byteOffset": 36, "byteLength": 6}},
                  {{"buffer": 0, "byteOffset": 44, "byteLength": 36}},
                  {{"buffer": 0, "byteOffset": 80, "byteLength": 48}},
                  {{"buffer": 0, "byteOffset": 128, "byteLength": 48}},
                  {{"buffer": 0, "byteOffset": 176, "byteLength": 24}}],
  "accessors": [{{"bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3",
                  "min": [0, 0, 0], "max": [1, 1, 0]}},
                {{"bufferView": 1, "componentType": 5123, "count": 3, "type": "SCALAR"}},
                {{"bufferView": 2, "componentType": 5126, "count": 3, "type": "VEC3"}},
                {{"bufferView": 3, "componentType": 5126, "count": 3, "type": "VEC4"}},
                {{"bufferView": 4, "componentType": 5126, "count": 3, "type": "VEC4"}},
                {{"bufferView": 5, "componentType": 5126, "count": 3, "type": "VEC2"}}],
  "images": [{{"uri": "{}"}}],
  "samplers": [{{"magFilter": 9728, "wrapS": 33648, "wrapT": 33071}}],
  "textures": [{{"source": 0, "sampler": 0}}],
  "materials": [{{"name": "Glass", "alphaMode": "BLEND", "doubleSided": true,
                 "normalTexture": {{"index": 0, "scale": 0.5}}}}],
  "meshes": [{{"primitives": [{{"attributes": {{"POSITION": 0{}}}, "indices": 1, "material": 0}}]}}],
  "nodes": [{{"mesh": 0}}],
  "scenes": [{{"nodes": [0]}}],
  "scene": 0
}})",
                       bufferUri, imageUri, attributes);
}

/// A 1 × 1 flat normal map as a data URI.
std::string flatNormalUri()
{
    const std::array<std::uint8_t, 4> flat{128, 128, 255, 255};
    return dataUri(Testing::encodePng(1, 1, 4, flat), "image/png");
}
} // namespace

TEST_F(GltfImporterTest, ReadsTangentsColoursAndSamplers)
{
    write("attributes.gltf", attributeJson(dataUri(attributeBuffer()), flatNormalUri(),
                                           R"(, "NORMAL": 2, "TANGENT": 3, "COLOR_0": 4, "TEXCOORD_0": 5)"));

    const Result<ImportedModel> model = import("attributes.gltf");

    ASSERT_TRUE(model) << model.error().message;
    const ImportedPrimitive& triangle = model->meshes[0].primitives[0];
    ASSERT_EQ(triangle.tangents.size(), 3U);
    // Mirrored with X, and the bitangent's sign flips with the handedness.
    EXPECT_FLOAT_EQ(triangle.tangents[0].x, -1.0F);
    EXPECT_FLOAT_EQ(triangle.tangents[0].w, -1.0F);
    ASSERT_EQ(triangle.colors.size(), 3U);
    EXPECT_FLOAT_EQ(triangle.colors[0].r, 1.0F);
    EXPECT_FLOAT_EQ(triangle.colors[1].g, 1.0F);
    EXPECT_FLOAT_EQ(triangle.colors[2].a, 0.5F);

    ASSERT_EQ(model->samplers.size(), 1U);
    const ImportedSampler& sampler = model->samplers[0];
    EXPECT_FALSE(sampler.linear);
    EXPECT_EQ(sampler.wrapU, ImportedWrap::Mirror);
    EXPECT_EQ(sampler.wrapV, ImportedWrap::Clamp);

    ASSERT_EQ(model->materials.size(), 1U);
    const ImportedMaterial& material = model->materials[0];
    EXPECT_EQ(material.normal.image, 0);
    EXPECT_EQ(material.normal.sampler, 0);
    EXPECT_FLOAT_EQ(material.normalScale, 0.5F);
    EXPECT_EQ(material.alphaMode, AlphaMode::Blend);
    EXPECT_TRUE(material.doubleSided);
}

TEST_F(GltfImporterTest, ReadsTextureTransforms)
{
    std::string json = attributeJson(dataUri(attributeBuffer()), flatNormalUri(), R"(, "TEXCOORD_0": 5)");
    const std::string_view normal = R"("normalTexture": {"index": 0, "scale": 0.5})";
    json.replace(json.find(normal), normal.size(),
                 R"("normalTexture": {"index": 0, "scale": 0.5, "extensions": {"KHR_texture_transform":)"
                 R"( {"offset": [0.25, 0.5], "rotation": 1.5, "scale": [2, 3]}}})");
    json.replace(json.find(R"("asset")"), 0, R"("extensionsUsed": ["KHR_texture_transform"], )");
    write("transform.gltf", json);

    const Result<ImportedModel> model = import("transform.gltf");

    ASSERT_TRUE(model) << model.error().message;
    const ImportedTexture& texture = model->materials[0].normal;
    EXPECT_FLOAT_EQ(texture.offset.x, 0.25F);
    EXPECT_FLOAT_EQ(texture.offset.y, 0.5F);
    EXPECT_FLOAT_EQ(texture.rotation, 1.5F);
    EXPECT_FLOAT_EQ(texture.scale.x, 2.0F);
    EXPECT_FLOAT_EQ(texture.scale.y, 3.0F);
    EXPECT_TRUE(model->warnings.empty()) << model->warnings.front();
    const ImportedTexture& none = model->materials[0].baseColor;
    EXPECT_FLOAT_EQ(none.scale.x, 1.0F) << "no transform: the identity";
    EXPECT_FLOAT_EQ(none.offset.x, 0.0F);
}

TEST_F(GltfImporterTest, GeneratesTangentsFromUvs)
{
    write("attributes.gltf",
          attributeJson(dataUri(attributeBuffer()), flatNormalUri(), R"(, "NORMAL": 2, "TEXCOORD_0": 5)"));

    const Result<ImportedModel> model = import("attributes.gltf");

    ASSERT_TRUE(model) << model.error().message;
    const ImportedPrimitive& triangle = model->meshes[0].primitives[0];
    ASSERT_EQ(triangle.tangents.size(), 3U);
    EXPECT_TRUE(triangle.colors.empty()) << "white when the file has none";
    for (std::size_t i = 0; i < 3; ++i)
    {
        const Math::Vec4& tangent = triangle.tangents[i];
        // U ran along glTF's +X, which is the engine's -X; the tangent is perpendicular to the normal (+Z).
        expectNear({tangent.x, tangent.y, tangent.z}, {-1.0F, 0.0F, 0.0F});
        EXPECT_NEAR(tangent.x * triangle.normals[i].x + tangent.y * triangle.normals[i].y +
                        tangent.z * triangle.normals[i].z,
                    0.0F, 1e-5F);
        // cross(+Z, -X) = -Y, towards decreasing V (V ran along +Y): so the sign is +1.
        EXPECT_FLOAT_EQ(tangent.w, 1.0F);
    }
}
} // namespace Meow2::Assets
