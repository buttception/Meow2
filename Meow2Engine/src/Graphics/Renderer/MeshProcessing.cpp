/// @file
/// @brief Mesh optimisation and meshlets, through meshoptimizer.

#include "Meow2/Graphics/Renderer/MeshProcessing.h"

#include "Meow2/Core/Assert.h"
#include "Meow2/Core/Memory/MemTag.h"
#include "Meow2/Math/Packing.h"

#include <meshoptimizer.h>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace Meow2::Renderer
{
namespace
{
/// @brief How far overdraw ordering may worsen vertex reuse: 5 %, meshoptimizer's suggestion.
constexpr float c_overdrawThreshold = 1.05F;

/// @brief How much the clusterizer favours tight normal cones over full clusters; meshoptimizer suggests 0.25 when
///        the cones are used for culling.
constexpr float c_coneWeight = 0.25F;

/// @brief Post-transform cache size for vertexCacheMissRatio(): a common figure for desktop GPUs.
constexpr unsigned c_cacheSize = 16;

/// @brief The mesh's positions as floats, three per vertex, decoded from the packed vertices exactly as the shaders
///        decode them (GpuDriven.hlsl's unpackPosition()).
/// @param mesh The mesh.
/// @return x, y, z of each vertex.
std::vector<float> positions(const MeshData& mesh)
{
    std::vector<float> out;
    out.reserve(mesh.vertices.size() * 3);
    for (const Shader::PackedVertex& vertex : mesh.vertices)
    {
        const Math::QuantizedPosition quantized{{static_cast<std::uint16_t>(vertex.xy & 0xFFFFU),
                                                 static_cast<std::uint16_t>(vertex.xy >> 16U),
                                                 static_cast<std::uint16_t>(vertex.z & 0xFFFFU)}};
        const Math::Vec3 position = Math::dequantizePosition(quantized, mesh.bounds);
        out.push_back(position.x);
        out.push_back(position.y);
        out.push_back(position.z);
    }
    return out;
}
} // namespace

void optimizeMesh(MeshData& mesh)
{
    MEOW2_ASSERT(mesh.indices.size() % 3 == 0, "Three indices per triangle");
    // meshoptimizer allocates through operator new, which the engine's tracking counts under the current tag.
    const Memory::MemTagScope tag{Memory::MemTag::Renderer};
    mesh.meshlets.clear();
    mesh.meshletVertices.clear();
    mesh.meshletTriangles.clear();
    if (mesh.indices.empty() || mesh.vertices.empty())
    {
        return;
    }
    const std::size_t indexCount = mesh.indices.size();

    // Identical vertices become one (glTF files often repeat them), so the cache can see the reuse.
    std::vector<unsigned> remap(mesh.vertices.size());
    const std::size_t unique =
        meshopt_generateVertexRemap(remap.data(), mesh.indices.data(), indexCount, mesh.vertices.data(),
                                    mesh.vertices.size(), sizeof(Shader::PackedVertex));
    std::vector<Shader::PackedVertex> vertices(unique);
    meshopt_remapVertexBuffer(vertices.data(), mesh.vertices.data(), mesh.vertices.size(), sizeof(Shader::PackedVertex),
                              remap.data());
    meshopt_remapIndexBuffer(mesh.indices.data(), mesh.indices.data(), indexCount, remap.data());
    mesh.vertices = std::move(vertices);

    meshopt_optimizeVertexCache(mesh.indices.data(), mesh.indices.data(), indexCount, mesh.vertices.size());
    const std::vector<float> points = positions(mesh);
    meshopt_optimizeOverdraw(mesh.indices.data(), mesh.indices.data(), indexCount, points.data(), mesh.vertices.size(),
                             sizeof(float) * 3, c_overdrawThreshold);
    // Last: it renumbers the vertices by first use, which the two above don't care about.
    meshopt_optimizeVertexFetch(mesh.vertices.data(), mesh.indices.data(), indexCount, mesh.vertices.data(),
                                mesh.vertices.size(), sizeof(Shader::PackedVertex));
}

void buildMeshlets(MeshData& mesh)
{
    MEOW2_ASSERT(mesh.indices.size() % 3 == 0, "Three indices per triangle");
    const Memory::MemTagScope tag{Memory::MemTag::Renderer};
    mesh.meshlets.clear();
    mesh.meshletVertices.clear();
    mesh.meshletTriangles.clear();
    if (mesh.indices.empty() || mesh.vertices.empty())
    {
        return;
    }
    const std::size_t indexCount = mesh.indices.size();
    const std::vector<float> points = positions(mesh);

    const std::size_t bound =
        meshopt_buildMeshletsBound(indexCount, MEOW2_MESHLET_MAX_VERTICES, MEOW2_MESHLET_MAX_TRIANGLES);
    std::vector<meshopt_Meshlet> clusters(bound);
    std::vector<unsigned> clusterVertices(indexCount); // at most one per index
    std::vector<unsigned char> clusterTriangles(indexCount);
    const std::size_t count =
        meshopt_buildMeshlets(clusters.data(), clusterVertices.data(), clusterTriangles.data(), mesh.indices.data(),
                              indexCount, points.data(), mesh.vertices.size(), sizeof(float) * 3,
                              MEOW2_MESHLET_MAX_VERTICES, MEOW2_MESHLET_MAX_TRIANGLES, c_coneWeight);

    mesh.meshlets.reserve(count);
    mesh.meshletTriangles.reserve(indexCount / 3);
    for (std::size_t i = 0; i < count; ++i)
    {
        const meshopt_Meshlet& cluster = clusters[i];
        const meshopt_Bounds bounds = meshopt_computeMeshletBounds(
            &clusterVertices[cluster.vertex_offset], &clusterTriangles[cluster.triangle_offset], cluster.triangle_count,
            points.data(), mesh.vertices.size(), sizeof(float) * 3);
        Shader::Meshlet meshlet{};
        meshlet.sphere = {bounds.center[0], bounds.center[1], bounds.center[2], bounds.radius};
        meshlet.cone = {bounds.cone_axis[0], bounds.cone_axis[1], bounds.cone_axis[2], bounds.cone_cutoff};
        meshlet.firstVertex = static_cast<std::uint32_t>(mesh.meshletVertices.size());
        meshlet.firstTriangle = static_cast<std::uint32_t>(mesh.meshletTriangles.size());
        meshlet.vertexCount = cluster.vertex_count;
        meshlet.triangleCount = cluster.triangle_count;
        mesh.meshletVertices.insert(mesh.meshletVertices.end(), clusterVertices.begin() + cluster.vertex_offset,
                                    clusterVertices.begin() + cluster.vertex_offset + cluster.vertex_count);
        // Three bytes per triangle in meshoptimizer; one uint here, so the mesh shader reads each with one load.
        for (std::uint32_t t = 0; t < cluster.triangle_count; ++t)
        {
            const unsigned char* corners = &clusterTriangles[cluster.triangle_offset + t * 3];
            mesh.meshletTriangles.push_back(std::uint32_t{corners[0]} | (std::uint32_t{corners[1]} << 8U) |
                                            (std::uint32_t{corners[2]} << 16U));
        }
        mesh.meshlets.push_back(meshlet);
    }
}

float vertexCacheMissRatio(const MeshData& mesh)
{
    if (mesh.indices.size() < 3)
    {
        return 0.0F;
    }
    const Memory::MemTagScope tag{Memory::MemTag::Renderer};
    return meshopt_analyzeVertexCache(mesh.indices.data(), mesh.indices.size(), mesh.vertices.size(), c_cacheSize, 0, 0)
        .acmr;
}
} // namespace Meow2::Renderer
