/// @file
/// @brief Mesh optimisation and meshlets (meshoptimizer): what a mesh goes through between import and the GPU.

#pragma once

#include <Meow2/Graphics/Renderer/GpuScene.h>

namespace Meow2::Renderer
{
/// @brief Reorders a mesh for the GPU, keeping every triangle: merges identical vertices, orders triangles so
///        vertices are reused while the GPU's post-transform cache still holds them, then so near triangles come
///        first where that costs little reuse (less overdraw), and finally orders vertices by first use (fewer cache
///        misses fetching them).
///
/// Done once, at import: the result draws the same, faster. Any meshlets are cleared, since they name the old order;
/// buildMeshlets() makes them again.
/// @param mesh [in,out] The mesh. Indices are three per triangle.
/// @pre `mesh.indices.size()` is a multiple of 3 (asserted).
/// @par Thread safety
/// Safe on separate meshes from several threads.
void optimizeMesh(MeshData& mesh);

/// @brief Splits a mesh into meshlets: clusters of at most MEOW2_MESHLET_MAX_VERTICES vertices and
///        MEOW2_MESHLET_MAX_TRIANGLES triangles, each with a bounding sphere and a cone around its triangles' normals,
///        for the meshlet path's culling (GpuScene).
///
/// Every triangle lands in exactly one meshlet. Clusters are grown from neighbouring triangles, trading a little
/// size for tighter cones, so more of them can be culled when they face away.
/// @param mesh [in,out] The mesh; its `meshlets`, `meshletVertices` and `meshletTriangles` are replaced.
/// @pre `mesh.indices.size()` is a multiple of 3 (asserted).
/// @par Thread safety
/// Safe on separate meshes from several threads.
void buildMeshlets(MeshData& mesh);

/// @brief The average number of vertices the GPU transforms per triangle, with a post-transform cache of 16 entries:
///        0.5 is ideal for a large grid, 3 means no reuse at all. For tests and statistics.
/// @param mesh The mesh.
/// @return The ratio (ACMR); 0 for a mesh without triangles.
[[nodiscard]] float vertexCacheMissRatio(const MeshData& mesh);
} // namespace Meow2::Renderer
