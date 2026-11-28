/// @file
/// @brief CollisionMeshes (CollisionMeshes.h).

#include <Meow2/Physics/CollisionMeshes.h>

#include <utility>

namespace Meow2::Physics
{
void CollisionMeshes::add(AssetId mesh, CollisionMesh data)
{
    m_meshes.insert_or_assign(mesh, std::move(data));
}

const CollisionMesh* CollisionMeshes::find(AssetId mesh) const
{
    const auto found = m_meshes.find(mesh);
    return found != m_meshes.end() ? &found->second : nullptr;
}

void CollisionMeshes::clear()
{
    m_meshes.clear();
}
} // namespace Meow2::Physics
