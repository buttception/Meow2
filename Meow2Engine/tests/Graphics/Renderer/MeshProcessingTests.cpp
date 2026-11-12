/// @file
/// @brief Mesh optimisation and meshlets (MeshProcessing.h), on the CPU.

#include <Meow2/Graphics/Renderer/MeshProcessing.h>
#include <Meow2/Math/Packing.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <utility>
#include <vector>

namespace Meow2::Renderer
{
namespace
{
/// @brief A 2 × 2 grid of `cells` × `cells` quads facing -Z, each quad with its own four vertices (so merging
///        identical ones has work to do), its triangles in a scrambled order (so the cache order has work to do).
MeshData scrambledGrid(std::uint32_t cells)
{
    std::vector<Math::Vec3> positions;
    std::vector<Math::Vec3> normals;
    std::vector<Math::Vec2> uvs;
    std::vector<std::uint32_t> indices;
    const auto point = [cells](std::uint32_t x, std::uint32_t y)
    {
        return Math::Vec3{static_cast<float>(x) / static_cast<float>(cells) * 2.0F - 1.0F,
                          static_cast<float>(y) / static_cast<float>(cells) * 2.0F - 1.0F, 0.0F};
    };
    for (std::uint32_t y = 0; y < cells; ++y)
    {
        for (std::uint32_t x = 0; x < cells; ++x)
        {
            const auto base = static_cast<std::uint32_t>(positions.size());
            for (const auto& [dx, dy] : {std::pair{0U, 0U}, std::pair{0U, 1U}, std::pair{1U, 1U}, std::pair{1U, 0U}})
            {
                positions.push_back(point(x + dx, y + dy));
                normals.push_back({0.0F, 0.0F, -1.0F});
                uvs.push_back({0.0F, 0.0F}); // the same everywhere, so corners shared by quads are identical
            }
            indices.insert(indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
        }
    }
    // A fixed scramble of the triangles: every 7th one, wrapping around (7 doesn't divide the count).
    const std::size_t triangles = indices.size() / 3;
    std::vector<std::uint32_t> scrambled;
    scrambled.reserve(indices.size());
    for (std::size_t i = 0; i < triangles; ++i)
    {
        const std::size_t t = (i * 7) % triangles;
        scrambled.insert(scrambled.end(), {indices[t * 3], indices[t * 3 + 1], indices[t * 3 + 2]});
    }
    return makeMesh({.positions = positions, .normals = normals, .uvs = uvs, .indices = scrambled},
                    {.center = {0.0F, 0.0F, 0.0F}, .extents = {1.0F, 1.0F, 0.01F}});
}

/// @brief A triangle by its vertices' contents, starting at the smallest so the winding is kept but the starting
///        corner doesn't matter.
using TriangleKey = std::array<std::array<std::uint32_t, 6>, 3>;

std::array<std::uint32_t, 6> contents(const Shader::PackedVertex& vertex)
{
    std::array<std::uint32_t, 6> words{};
    std::memcpy(words.data(), &vertex, sizeof(vertex));
    return words;
}

std::vector<TriangleKey> triangleKeys(const MeshData& mesh)
{
    std::vector<TriangleKey> keys;
    for (std::size_t t = 0; t + 2 < mesh.indices.size(); t += 3)
    {
        TriangleKey key{contents(mesh.vertices[mesh.indices[t]]), contents(mesh.vertices[mesh.indices[t + 1]]),
                        contents(mesh.vertices[mesh.indices[t + 2]])};
        std::rotate(key.begin(), std::min_element(key.begin(), key.end()), key.end());
        keys.push_back(key);
    }
    std::sort(keys.begin(), keys.end());
    return keys;
}

/// @brief A position as the shaders decode it.
Math::Vec3 positionOf(const MeshData& mesh, std::uint32_t vertex)
{
    const Shader::PackedVertex& packed = mesh.vertices[vertex];
    const Math::QuantizedPosition quantized{{static_cast<std::uint16_t>(packed.xy & 0xFFFFU),
                                             static_cast<std::uint16_t>(packed.xy >> 16U),
                                             static_cast<std::uint16_t>(packed.z & 0xFFFFU)}};
    return Math::dequantizePosition(quantized, mesh.bounds);
}
} // namespace

TEST(MeshProcessing, OptimizeKeepsTriangles)
{
    MeshData mesh = scrambledGrid(16);
    const std::vector<TriangleKey> before = triangleKeys(mesh);
    const std::size_t vertices = mesh.vertices.size();

    optimizeMesh(mesh);

    EXPECT_EQ(triangleKeys(mesh), before) << "the same triangles, each wound the same way";
    EXPECT_EQ(mesh.vertices.size(), std::size_t{17 * 17}) << "corners shared by quads became one vertex";
    EXPECT_LT(mesh.vertices.size(), vertices);
}

TEST(MeshProcessing, OptimizeImprovesVertexCache)
{
    MeshData mesh = scrambledGrid(32);
    optimizeMesh(mesh); // merges the duplicates, so the comparison below is only about order

    // Scrambled again: the triangles of the optimised mesh, every 7th one.
    MeshData scrambled = mesh;
    const std::size_t triangles = mesh.indices.size() / 3;
    for (std::size_t i = 0; i < triangles; ++i)
    {
        const std::size_t t = (i * 7) % triangles;
        std::copy_n(mesh.indices.begin() + static_cast<std::ptrdiff_t>(t * 3), 3,
                    scrambled.indices.begin() + static_cast<std::ptrdiff_t>(i * 3));
    }
    const float worse = vertexCacheMissRatio(scrambled);
    const float better = vertexCacheMissRatio(mesh);
    EXPECT_LT(better, worse * 0.75F);
    EXPECT_LT(better, 1.0F) << "well under one new vertex per triangle on a grid";
}

TEST(MeshProcessing, MeshletsCoverEveryTriangleOnce)
{
    MeshData mesh = scrambledGrid(32);
    optimizeMesh(mesh);
    buildMeshlets(mesh);
    ASSERT_FALSE(mesh.meshlets.empty());

    // Every triangle of the meshlets, back in mesh vertex indices, sorted by corner so it can be compared.
    std::vector<std::array<std::uint32_t, 3>> fromMeshlets;
    for (const Shader::Meshlet& meshlet : mesh.meshlets)
    {
        for (std::uint32_t t = 0; t < meshlet.triangleCount; ++t)
        {
            const std::uint32_t packed = mesh.meshletTriangles[meshlet.firstTriangle + t];
            std::array<std::uint32_t, 3> triangle{};
            for (std::uint32_t c = 0; c < 3; ++c)
            {
                const std::uint32_t local = (packed >> (c * 8U)) & 0xFFU;
                ASSERT_LT(local, meshlet.vertexCount);
                triangle[c] = mesh.meshletVertices[meshlet.firstVertex + local];
            }
            std::rotate(triangle.begin(), std::min_element(triangle.begin(), triangle.end()), triangle.end());
            fromMeshlets.push_back(triangle);
        }
    }
    std::vector<std::array<std::uint32_t, 3>> fromIndices;
    for (std::size_t t = 0; t + 2 < mesh.indices.size(); t += 3)
    {
        std::array<std::uint32_t, 3> triangle{mesh.indices[t], mesh.indices[t + 1], mesh.indices[t + 2]};
        std::rotate(triangle.begin(), std::min_element(triangle.begin(), triangle.end()), triangle.end());
        fromIndices.push_back(triangle);
    }
    std::sort(fromMeshlets.begin(), fromMeshlets.end());
    std::sort(fromIndices.begin(), fromIndices.end());
    EXPECT_EQ(fromMeshlets, fromIndices) << "each triangle once, wound the same way";
}

TEST(MeshProcessing, MeshletsRespectLimits)
{
    MeshData mesh = scrambledGrid(48);
    optimizeMesh(mesh);
    buildMeshlets(mesh);
    ASSERT_FALSE(mesh.meshlets.empty());
    for (const Shader::Meshlet& meshlet : mesh.meshlets)
    {
        EXPECT_GT(meshlet.vertexCount, 0U);
        EXPECT_LE(meshlet.vertexCount, MEOW2_MESHLET_MAX_VERTICES);
        EXPECT_GT(meshlet.triangleCount, 0U);
        EXPECT_LE(meshlet.triangleCount, MEOW2_MESHLET_MAX_TRIANGLES);
        // The sphere holds every vertex (with a little slack for rounding).
        for (std::uint32_t v = 0; v < meshlet.vertexCount; ++v)
        {
            const Math::Vec3 p = positionOf(mesh, mesh.meshletVertices[meshlet.firstVertex + v]);
            const float dx = p.x - meshlet.sphere.x;
            const float dy = p.y - meshlet.sphere.y;
            const float dz = p.z - meshlet.sphere.z;
            EXPECT_LE(std::sqrt(dx * dx + dy * dy + dz * dz), meshlet.sphere.w * 1.001F + 1e-5F);
        }
        // A flat grid facing -Z: every normal is -Z, so the cone of directions it can be culled from is a whole
        // half-space: cutoff sin(0) = 0.
        EXPECT_NEAR(meshlet.cone.z, -1.0F, 1e-3F);
        EXPECT_NEAR(meshlet.cone.w, 0.0F, 1e-3F);
    }
    // Most meshlets are full: 48 × 48 × 2 triangles in not many more than the fewest that could hold them.
    const std::size_t fewest =
        (mesh.indices.size() / 3 + MEOW2_MESHLET_MAX_TRIANGLES - 1) / MEOW2_MESHLET_MAX_TRIANGLES;
    EXPECT_LT(mesh.meshlets.size(), fewest * 2);
}

TEST(MeshProcessing, EmptyMeshHasNoMeshlets)
{
    MeshData mesh;
    optimizeMesh(mesh);
    buildMeshlets(mesh);
    EXPECT_TRUE(mesh.meshlets.empty());
    EXPECT_EQ(vertexCacheMissRatio(mesh), 0.0F);
}
} // namespace Meow2::Renderer
