#pragma once

/// @file
/// @brief CollisionMeshes: the triangles of meshes on the CPU, for mesh Colliders.

#include <Meow2/Core/Id/Uuid.h>
#include <Meow2/Math/Types.h>

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace Meow2::Physics
{
/// @brief A mesh's triangles for collision: positions in the mesh's own space and three indices per triangle,
///        clockwise seen from the front (the renderer's winding). Plain data.
struct CollisionMesh
{
    std::vector<Math::Vec3> positions{};  ///< Mesh-space positions.
    std::vector<std::uint32_t> indices{}; ///< Three per triangle.
};

/// @brief The collision triangles of the meshes a scene uses, by mesh AssetId.
///
/// The renderer keeps meshes packed for the GPU (quantised positions, meshlets), which physics can't use; whoever
/// imports a model also puts its meshes' plain triangles here (all of a mesh's primitives as one), for
/// PhysicsScene's mesh Colliders.
/// @par Thread safety
/// Not thread-safe; read-only while a PhysicsScene builds.
class CollisionMeshes
{
public:
    /// @brief Adds a mesh, or replaces one with the same id.
    /// @param mesh The mesh's id.
    /// @param data Its triangles.
    void add(AssetId mesh, CollisionMesh data);

    /// @brief A mesh's triangles.
    /// @param mesh The mesh's id.
    /// @return Them, or null for a mesh it doesn't have.
    [[nodiscard]] const CollisionMesh* find(AssetId mesh) const;

    /// @brief How many meshes it has.
    /// @return The count.
    [[nodiscard]] std::size_t size() const
    {
        return m_meshes.size();
    }

    /// @brief Forgets every mesh.
    void clear();

private:
    std::unordered_map<AssetId, CollisionMesh> m_meshes;
};
} // namespace Meow2::Physics
